// Command-line router: read ENC cells, route between two points, write a GPX that OpenCPN can import.
//
//   openautoroute --enc DIR --from LAT,LON --to LAT,LON [--draft M] [--clearance M] [--cell-m M] [-o route.gpx]
//
// DIR is searched recursively for `.000` base cells (e.g. an ENC_ROOT folder). Only cells that overlap the route's
// bounding box are kept in memory.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
                 "usage: %s --enc DIR --from LAT,LON --to LAT,LON [--draft M=1.5] [--clearance M=1.0]\n"
                 "          [--cell-m M=30] [--margin-m M=300] [--margin-weight W=6] [-o route.gpx]\n", argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string encDir, outPath = "route.gpx";
    LatLon from{}, to{};
    bool haveFrom = false, haveTo = false;
    double draft = 1.5, clearance = 1.0, cellM = 30.0, marginM = 300.0, marginWeight = 6.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasVal = i + 1 < argc;
        if (a == "--enc" && hasVal) encDir = argv[++i];
        else if (a == "--from" && hasVal) haveFrom = parseLatLon(argv[++i], from);
        else if (a == "--to" && hasVal) haveTo = parseLatLon(argv[++i], to);
        else if (a == "--draft" && hasVal) draft = std::atof(argv[++i]);
        else if (a == "--clearance" && hasVal) clearance = std::atof(argv[++i]);
        else if (a == "--cell-m" && hasVal) cellM = std::atof(argv[++i]);
        else if (a == "--margin-m" && hasVal) marginM = std::atof(argv[++i]);
        else if (a == "--margin-weight" && hasVal) marginWeight = std::atof(argv[++i]);
        else if ((a == "-o" || a == "--out") && hasVal) outPath = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    if (encDir.empty() || !haveFrom || !haveTo || cellM <= 0.0 || draft < 0.0 || clearance < 0.0) {
        usage(argv[0]);
        return 2;
    }

    // Grid covers the route's bounding box plus a margin so the router can swing wide around headlands.
    const double marginDeg = std::max(0.03, 0.25 * std::max(std::fabs(from.lat - to.lat), std::fabs(from.lon - to.lon)));
    Bounds box;
    box.add({std::min(from.lat, to.lat) - marginDeg, std::min(from.lon, to.lon) - marginDeg});
    box.add({std::max(from.lat, to.lat) + marginDeg, std::max(from.lon, to.lon) + marginDeg});
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
        stampChart(d, minDepth, grid);
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

    Cell s = grid.cellAt(from), g = grid.cellAt(to);
    Cell s2, g2;
    if (!grid.inBounds(s) || !grid.inBounds(g) || !snapToOpen(grid, s, 40, s2) || !snapToOpen(grid, g, 40, g2)) {
        std::fprintf(stderr, "start or end is not near charted water deep enough for %.1f m draft + %.1f m clearance\n",
                     draft, clearance);
        return 1;
    }
    if (!(s == s2)) std::printf("start moved to nearest safe water (%.0f m)\n", haversineM(from, grid.centre(s2)));
    if (!(g == g2)) std::printf("end moved to nearest safe water (%.0f m)\n", haversineM(to, grid.centre(g2)));

    std::vector<LatLon> route = findRoute(grid, grid.centre(s2), grid.centre(g2));
    if (route.empty()) {
        std::fprintf(stderr, "no route found: the charts show no continuous water at least %.1f m deep between the points\n",
                     minDepth);
        return 1;
    }
    double nm = 0;
    for (size_t i = 1; i < route.size(); ++i) nm += haversineM(route[i - 1], route[i]) / 1852.0;
    // Clearance report: sample each leg about every cell and look up the distance to blocked water. Samples within
    // 1 km of either end are skipped, since a start in a marina or a berth is at the shore by definition.
    std::vector<double> clearances;
    double travelled = 0.0;
    for (size_t i = 1; i < route.size(); ++i) {
        const double leg = haversineM(route[i - 1], route[i]);
        const int steps = std::max(1, static_cast<int>(leg / grid.cellSizeM()));
        for (int k = 0; k <= steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            const double along = travelled + t * leg;
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
    std::ofstream(outPath) << routeToGpx(route, "open-autoroute");
    std::printf("%zu charts, %zu waypoints, %.1f nm (straight line %.1f nm), wrote %s\n", static_cast<size_t>(used),
                route.size(), nm, haversineM(from, to) / 1852.0, outPath.c_str());
    return 0;
}
