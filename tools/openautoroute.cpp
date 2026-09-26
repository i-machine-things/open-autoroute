// Command-line router: read ENC cells, route between two points, write a GPX that OpenCPN can import.
//
//   openautoroute --enc DIR --from LAT,LON --to LAT,LON [--draft M] [--clearance M] [--cell-m M] [-o route.gpx]
//   openautoroute --enc DIR --eval route.gpx [--draft M] ...   (score an existing route, e.g. from another planner)
//
// DIR is searched recursively for `.000` base cells (e.g. an ENC_ROOT folder). Only cells that overlap the route's
// bounding box are kept in memory.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "openautoroute/chart_grid.hpp"
#include "openautoroute/gpx.hpp"
#include "openautoroute/pathfinder.hpp"
#include "openautoroute/s57.hpp"

namespace fs = std::filesystem;
using namespace oar;

namespace {

bool parseLatLon(const char* s, LatLon& out) {
    char* end = nullptr;
    out.lat = std::strtod(s, &end);
    if (*end != ',') return false;
    out.lon = std::strtod(end + 1, &end);
    return *end == '\0' && std::fabs(out.lat) <= 90.0 && std::fabs(out.lon) <= 180.0;
}

// Vertices of a GPX route/track (<rtept>, <trkpt> or <wpt>), in file order. A tolerant scan, not a full XML parser:
// enough for the files chart apps export, and anything without lat/lon attributes is skipped.
std::vector<LatLon> readGpxPoints(const std::string& path) {
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<LatLon> pts;
    for (size_t pos = text.find('<'); pos != std::string::npos; pos = text.find('<', pos + 1)) {
        if (text.compare(pos, 6, "<rtept") != 0 && text.compare(pos, 6, "<trkpt") != 0 && text.compare(pos, 4, "<wpt") != 0) continue;
        const size_t end = text.find('>', pos);
        if (end == std::string::npos) break;
        const std::string tag = text.substr(pos, end - pos);
        const size_t la = tag.find("lat=\""), lo = tag.find("lon=\"");
        if (la == std::string::npos || lo == std::string::npos) continue;
        pts.push_back({std::atof(tag.c_str() + la + 5), std::atof(tag.c_str() + lo + 5)});
    }
    return pts;
}

struct Bounds {
    double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
    void add(LatLon p) {
        minLat = std::min(minLat, p.lat); maxLat = std::max(maxLat, p.lat);
        minLon = std::min(minLon, p.lon); maxLon = std::max(maxLon, p.lon);
    }
    bool overlaps(const Bounds& o) const {
        return minLat <= o.maxLat && maxLat >= o.minLat && minLon <= o.maxLon && maxLon >= o.minLon;
    }
};

// Nearest open cell to `c` within `radius` cells, so a start point that lands on a blocked pixel (a marina berth, a
// coarse-raster shoreline) still routes. Returns false when nothing open is nearby.
bool snapToOpen(const CostGrid& g, Cell c, int radius, Cell& out) {
    double best = 1e18;
    for (int dr = -radius; dr <= radius; ++dr) {
        for (int dc = -radius; dc <= radius; ++dc) {
            const Cell n{c.col + dc, c.row + dr};
            if (!g.inBounds(n) || g.blocked(n)) continue;
            const double d = dc * dc + dr * dr;
            if (d < best) { best = d; out = n; }
        }
    }
    return best < 1e18;
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --enc DIR (--from LAT,LON --to LAT,LON | --eval ROUTE.gpx) [--draft M=1.5] [--clearance M=1.0]\n"
                 "          [--cell-m M=30] [--margin-m M=500] [--margin-weight W=10] [--no-tss] [-o route.gpx]\n", argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string encDir, evalPath, outPath = "route.gpx";
    LatLon from{}, to{};
    bool haveFrom = false, haveTo = false, applyTss = true;
    double draft = 1.5, clearance = 1.0, cellM = 30.0, marginM = 500.0, marginWeight = 10.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasVal = i + 1 < argc;
        if (a == "--enc" && hasVal) encDir = argv[++i];
        else if (a == "--from" && hasVal) haveFrom = parseLatLon(argv[++i], from);
        else if (a == "--to" && hasVal) haveTo = parseLatLon(argv[++i], to);
        else if (a == "--eval" && hasVal) evalPath = argv[++i];
        else if (a == "--draft" && hasVal) draft = std::atof(argv[++i]);
        else if (a == "--clearance" && hasVal) clearance = std::atof(argv[++i]);
        else if (a == "--cell-m" && hasVal) cellM = std::atof(argv[++i]);
        else if (a == "--no-tss") applyTss = false;
        else if (a == "--margin-m" && hasVal) marginM = std::atof(argv[++i]);
        else if (a == "--margin-weight" && hasVal) marginWeight = std::atof(argv[++i]);
        else if ((a == "-o" || a == "--out") && hasVal) outPath = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    std::vector<LatLon> given;  // the route being scored in --eval mode
    if (!evalPath.empty()) {
        given = readGpxPoints(evalPath);
        if (given.size() < 2) {
            std::fprintf(stderr, "no route points found in %s\n", evalPath.c_str());
            return 2;
        }
        from = given.front();
        to = given.back();
        haveFrom = haveTo = true;
    }
    if (encDir.empty() || !haveFrom || !haveTo || cellM <= 0.0 || draft < 0.0 || clearance < 0.0) {
        usage(argv[0]);
        return 2;
    }

    // Grid covers the route's bounding box plus a margin so the router can swing wide around headlands.
    Bounds ends;
    ends.add(from);
    ends.add(to);
    for (LatLon p : given) ends.add(p);
    const double marginDeg = std::max(0.03, 0.25 * std::max(ends.maxLat - ends.minLat, ends.maxLon - ends.minLon));
    Bounds box;
    box.add({ends.minLat - marginDeg, ends.minLon - marginDeg});
    box.add({ends.maxLat + marginDeg, ends.maxLon + marginDeg});
    const double latStep = cellM / 111320.0;
    const double lonStep = latStep / std::cos(deg2rad((box.minLat + box.maxLat) / 2));
    const double cols = std::ceil((box.maxLon - box.minLon) / lonStep), rows = std::ceil((box.maxLat - box.minLat) / latStep);
    if (cols * rows > 25e6) {
        std::fprintf(stderr, "grid would be %.0f x %.0f cells; raise --cell-m or route a shorter distance\n", cols, rows);
        return 2;
    }
    CostGrid grid(static_cast<int>(cols), static_cast<int>(rows), {box.maxLat, box.minLon}, latStep, lonStep);
    grid.fill(kBlocked);  // no chart data means not known to be safe

    // Load overlapping cells; stamp coarse-to-fine by the usage band in the cell name (US5xxxxx = harbour scale).
    std::vector<std::pair<std::string, fs::path>> cells;
    for (const auto& e : fs::recursive_directory_iterator(encDir)) {
        if (e.is_regular_file() && e.path().extension() == ".000") cells.emplace_back(e.path().stem().string(), e.path());
    }
    std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
        const char ba = a.first.size() > 2 ? a.first[2] : '0', bb = b.first.size() > 2 ? b.first[2] : '0';
        return ba != bb ? ba < bb : a.first < b.first;
    });
    const double minDepth = draft + clearance;
    int used = 0;
    for (const auto& [name, path] : cells) {
        ChartData d;
        std::string err;
        if (!loadS57(path.string(), d, err)) {
            std::fprintf(stderr, "skip %s: %s\n", name.c_str(), err.c_str());
            continue;
        }
        Bounds b;
        for (const auto& f : d.features) for (const auto& r : f.parts) for (const auto& p : r.points) b.add(p);
        if (!b.overlaps(box)) continue;
        stampChart(d, minDepth, grid, applyTss);
        std::printf("  chart %s\n", name.c_str());
        ++used;
    }
    if (used == 0) {
        std::fprintf(stderr, "no ENC cells under %s overlap the route area\n", encDir.c_str());
        return 1;
    }

    // Keep off the shore: penalise cells near blocked water (see CostGrid::applyShoreMargin). Also measure how close the
    // finished route gets, using the distances from before the penalty changes any costs.
    const std::vector<float> shoreDist = grid.distanceToBlockedM();
    grid.applyShoreMargin(marginM, marginWeight);

    std::vector<LatLon> route;
    if (!evalPath.empty()) {
        route = given;  // score as-is: no snapping, no rerouting
    } else {
        Cell s = grid.cellAt(from), g = grid.cellAt(to);
        Cell s2, g2;
        if (!grid.inBounds(s) || !grid.inBounds(g) || !snapToOpen(grid, s, 40, s2) || !snapToOpen(grid, g, 40, g2)) {
            std::fprintf(stderr, "start or end is not near charted water deep enough for %.1f m draft + %.1f m clearance\n",
                         draft, clearance);
            return 1;
        }
        if (!(s == s2)) std::printf("start moved to nearest safe water (%.0f m)\n", haversineM(from, grid.centre(s2)));
        if (!(g == g2)) std::printf("end moved to nearest safe water (%.0f m)\n", haversineM(to, grid.centre(g2)));

        route = findRoute(grid, grid.centre(s2), grid.centre(g2));
        if (route.empty()) {
            std::fprintf(stderr, "no route found: the charts show no continuous water at least %.1f m deep between the points\n",
                         minDepth);
            return 1;
        }
    }
    double nm = 0;
    for (size_t i = 1; i < route.size(); ++i) nm += haversineM(route[i - 1], route[i]) / 1852.0;
    // Clearance report: sample each leg about every cell and look up the distance to blocked water. Samples within
    // 1 km of either end are skipped, since a start in a marina or a berth is at the shore by definition.
    std::vector<double> clearances;
    double travelled = 0.0, unsafeM = 0.0;
    struct Spot { LatLon at; double alongM; };
    std::vector<Spot> unsafeSpots;  // first sample of each separate unsafe stretch
    bool inUnsafe = false;
    // Rule 10: maximal runs of samples inside a traffic lane, with how the route's heading relates to the lane's flow.
    struct LaneRun { LatLon at; double alongM = 0, lengthM = 0, thetaSum = 0, thetaMin = 180, thetaMax = 0; float lane = 0; int n = 0; };
    std::vector<LaneRun> laneRuns;
    bool inLane = false;
    for (size_t i = 1; i < route.size(); ++i) {
        const double leg = haversineM(route[i - 1], route[i]);
        const int steps = std::max(1, static_cast<int>(leg / grid.cellSizeM()));
        for (int k = 0; k <= steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            const double along = travelled + t * leg;
            const LatLon here{route[i - 1].lat + t * (route[i].lat - route[i - 1].lat),
                              route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)};
            const Cell hc = grid.cellAt(here);
            const bool unsafe = !grid.inBounds(hc) || grid.blocked(hc);
            if (unsafe) {
                unsafeM += leg / steps;
                if (!inUnsafe) unsafeSpots.push_back({here, along});
            }
            inUnsafe = unsafe;
            const float lane = grid.inBounds(hc) ? grid.laneDirection(hc) : std::nanf("");
            if (std::isnan(lane)) {
                inLane = false;
            } else {
                const double theta = angleDiffDeg(bearingDeg(route[i - 1], route[i]), lane);
                if (!inLane || laneRuns.back().lane != lane) {
                    laneRuns.push_back({here, along, 0, 0, 180, 0, lane, 0});
                    inLane = true;
                }
                LaneRun& run = laneRuns.back();
                run.lengthM += leg / steps;
                run.thetaSum += theta;
                run.thetaMin = std::min(run.thetaMin, theta);
                run.thetaMax = std::max(run.thetaMax, theta);
                ++run.n;
            }
            if (along < 1000.0 || along > nm * 1852.0 - 1000.0) continue;
            const Cell c = grid.cellAt({route[i - 1].lat + t * (route[i].lat - route[i - 1].lat),
                                        route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)});
            if (grid.inBounds(c)) clearances.push_back(shoreDist[static_cast<size_t>(c.row) * grid.cols() + c.col]);
        }
        travelled += leg;
    }
    if (!clearances.empty()) {
        std::sort(clearances.begin(), clearances.end());
        std::printf("clearance from land, shoal or uncharted water (excluding 1 km at each end): closest %.0f m, "
                    "median %.0f m\n", clearances.front(), clearances[clearances.size() / 2]);
    }
    if (!evalPath.empty()) {
        std::printf("unsafe by the chart rules: %.0f m of %.0f m (%.1f%%) in %zu stretch(es)\n", unsafeM, nm * 1852.0,
                    100.0 * unsafeM / (nm * 1852.0), unsafeSpots.size());
        for (size_t i = 0; i < unsafeSpots.size() && i < 10; ++i) {
            std::printf("  at %.5f,%.5f (%.1f nm along)\n", unsafeSpots[i].at.lat, unsafeSpots[i].at.lon,
                        unsafeSpots[i].alongM / 1852.0);
        }
        // Rule 10 report. A run within 25 degrees of the flow is normal lane use, within 25 degrees of the opposite is
        // wrong-way travel, and anything between is a crossing, judged by how far it is from square to the flow.
        int withFlow = 0, crossings = 0, wrongWay = 0;
        double wrongWayM = 0, worstOff = 0;
        for (const LaneRun& r : laneRuns) {
            const double mean = r.thetaSum / r.n;
            if (mean <= 25.0) ++withFlow;
            else if (mean >= 155.0) { ++wrongWay; wrongWayM += r.lengthM; }
            else { ++crossings; worstOff = std::max(worstOff, std::fabs(mean - 90.0)); }
        }
        std::printf("traffic lanes (Rule 10): %zu lane transit(s): %d with the flow, %d crossing, %d wrong-way (%.0f m)\n",
                    laneRuns.size(), withFlow, crossings, wrongWay, wrongWayM);
        for (size_t i = 0; i < laneRuns.size() && i < 12; ++i) {
            const LaneRun& r = laneRuns[i];
            const double mean = r.thetaSum / r.n;
            const char* kind = mean <= 25.0 ? "with flow" : mean >= 155.0 ? "WRONG WAY" : "crossing";
            std::printf("  %-9s lane %3.0f deg, %4.0f m in lane, heading %.0f deg off the flow", kind, r.lane, r.lengthM, mean);
            if (mean > 25.0 && mean < 155.0) std::printf(" (%.0f deg from square)", std::fabs(mean - 90.0));
            std::printf(" at %.4f,%.4f\n", r.at.lat, r.at.lon);
        }
        if (crossings > 0) std::printf("  worst crossing is %.0f deg from square\n", worstOff);
        std::printf("%zu charts, %zu waypoints, %.1f nm (straight line %.1f nm)\n", static_cast<size_t>(used), route.size(),
                    nm, haversineM(from, to) / 1852.0);
        return 0;
    }
    std::ofstream(outPath) << routeToGpx(route, "open-autoroute");
    std::printf("%zu charts, %zu waypoints, %.1f nm (straight line %.1f nm), wrote %s\n", static_cast<size_t>(used),
                route.size(), nm, haversineM(from, to) / 1852.0, outPath.c_str());
    return 0;
}
