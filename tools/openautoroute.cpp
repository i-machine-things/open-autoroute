// Command-line router: read ENC cells, route between two points, write a GPX that OpenCPN can import.
//
//   openautoroute --enc DIR --from LAT,LON --to LAT,LON [--draft M] [--clearance M] [--cell-m M] [-o route.gpx]
//   openautoroute --enc DIR --eval route.gpx [--draft M] ...   (score an existing route, e.g. from another planner)
//
// DIR is searched recursively for `.000` base cells (e.g. an ENC_ROOT folder). Only cells that overlap the route's
// bounding box are kept in memory.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "openautoroute/chart_grid.hpp"
#include "openautoroute/gpx.hpp"
#include "openautoroute/pathfinder.hpp"
#include "openautoroute/s57.hpp"
#include "openautoroute/vessel.hpp"

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

struct Snap {
    Cell cell;
    double distCells;
};

// For each water body, the open cell nearest to `c` within `radius` cells. A start point that lands on a blocked pixel (a
// marina berth, a coarse-raster shoreline) still routes, and the caller can choose a body both endpoints share instead of
// whichever tiny pocket happens to be closest.
std::map<int, Snap> nearestPerBody(const CostGrid& g, const WaterBodies& bodies, Cell c, int radius) {
    std::map<int, Snap> best;
    for (int dr = -radius; dr <= radius; ++dr) {
        for (int dc = -radius; dc <= radius; ++dc) {
            const Cell n{c.col + dc, c.row + dr};
            if (!g.inBounds(n) || g.blocked(n)) continue;
            const double d = std::sqrt(static_cast<double>(dc * dc + dr * dr));
            const int b = bodies.label[static_cast<size_t>(n.row) * g.cols() + n.col];
            auto it = best.find(b);
            if (it == best.end() || d < it->second.distCells) best[b] = {n, d};
        }
    }
    return best;
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --enc DIR (--from LAT,LON --to LAT,LON | --eval ROUTE.gpx) [--draft M=1.5] [--clearance M=1.0]\n"
                 "          [--cell-m M=30] [--margin-m M=500] [--margin-weight W=10] [--no-tss] [--length-m L=12] [--under-sail] [--lane-use F] [--lane-margin-m M=1500] [--lane-margin-weight W=12] [--caution F] [--simplify T=0.05] [--min-leg-m M=460] [--summary] [--map LAT,LON,CELLS] [-o route.gpx]\n", argv0);
}

}  // namespace

int main(int argc, char** argv) {
    std::string encDir, evalPath, outPath = "route.gpx";
    LatLon from{}, to{}, mapAt{};
    int mapRadius = 0;
    bool haveFrom = false, haveTo = false, applyTss = true, underSail = false;
    double draft = 1.5, clearance = 1.0, cellM = 30.0, marginM = 500.0, marginWeight = 10.0, lengthM = 12.0, laneUse = -1.0, simplify = 0.05, laneMarginM = 1500.0, laneMarginWeight = 12.0, caution = -1.0, minLegM = 460.0;
    bool summary = false, useMarks = true, useChannels = true;  // dev switches (--no-marks, --no-boundaries) exist only to compare runs
    const auto startedAt = std::chrono::steady_clock::now();
    double snapStartM = 0.0, snapEndM = 0.0;
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
        else if (a == "--map" && hasVal) {  // debug: ASCII picture of the grid around a point, route overlaid
            const std::string v = argv[++i];
            const size_t c2 = v.rfind(',');
            if (c2 == std::string::npos || !parseLatLon(v.substr(0, c2).c_str(), mapAt)) { usage(argv[0]); return 2; }
            mapRadius = std::atoi(v.c_str() + c2 + 1);
        }
        else if (a == "--lane-margin-weight" && hasVal) laneMarginWeight = std::atof(argv[++i]);
        else if (a == "--caution" && hasVal) caution = std::atof(argv[++i]);
        else if (a == "--lane-margin-m" && hasVal) laneMarginM = std::atof(argv[++i]);
        else if (a == "--min-leg-m" && hasVal) minLegM = std::atof(argv[++i]);
        else if (a == "--no-boundaries") useChannels = false;  // developer switch: ignore charted channel limits
        else if (a == "--no-marks") useMarks = false;  // ignore red/green lateral marks (for comparison)
        else if (a == "--summary") summary = true;  // one machine-readable line at the end, for benchmark scripts
        else if (a == "--simplify" && hasVal) simplify = std::atof(argv[++i]);
        else if (a == "--no-tss") applyTss = false;
        else if (a == "--under-sail" || a == "--sail") underSail = true;  // engine off, sails doing the work
        else if (a == "--length-m" && hasVal) lengthM = std::atof(argv[++i]);
        else if (a == "--lane-use" && hasVal) laneUse = std::atof(argv[++i]);
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
    // Vessel type decides how lanes are used (COLREGs Rule 10(j), see vessel.hpp). --lane-use overrides the factor.
    const Vessel vessel{lengthM, underSail};
    if (laneUse < 0.0) laneUse = defaultLaneUseFactor(vessel);
    const bool avoidsLanes = laneUse > 1.0;
    // Precautionary areas are where lanes converge and traffic is heaviest. A small craft should go round or cross the lanes
    // square elsewhere rather than through one (a square lane crossing costs far less than passing through the area);
    // ships expect to pass through them and only take care.
    const double cautionFactor = caution > 0.0 ? caution : (avoidsLanes ? 12.0 : 1.5);

    int used = 0;
    std::vector<LateralMark> marks;
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
        stampChart(d, minDepth, grid, applyTss, cautionFactor);
        if (useMarks) collectLateralMarks(d, marks);
        std::printf("  chart %s\n", name.c_str());
        ++used;
    }
    grid.assignZoneDirections();  // separation zones take their direction from the lanes beside them
    // Buoyed channels: pair opposite red/green marks into gates so the route stays between them (see applyChannelGates).
    const std::vector<Gate> gates = useMarks ? applyChannelGates(grid, marks, 500.0) : std::vector<Gate>{};
    if (useMarks) std::printf("%zu channel gates from %zu lateral marks\n", gates.size(), marks.size());
    // Narrow charted channels (fairway or dredged area under 600 m wide, the dashed limits): stay between the limits, like keeping
    // between the lines on a road. Automatic: it acts only within 1.5 km of a charted narrow channel, only on moves running along or
    // cutting across its bends (not on a clean crossing of it), and never on traffic separation.
    if (useChannels) {
        grid.markNarrowChannels(600.0);
        grid.applyChannelPreference(1500.0, 6.0);
    }
    grid.setLaneUseFactor(laneUse);
    // Leeway: a vessel that avoids lanes also keeps a wide berth from lanes, zones and the ends of lane parts, so it prefers
    // open water to a narrow strip between a shore and a lane.
    if (avoidsLanes) grid.applyLaneMargin(laneMarginM, laneMarginWeight);
    if (applyTss) {
        std::printf("vessel %.1f m%s: %s (lane-use factor %.2f)\n", lengthM, underSail ? ", under sail" : "",
                    avoidsLanes ? "stays out of traffic lanes, crosses square-on" : "uses traffic lanes", laneUse);
    }
    // On any failure, still emit a summary line (found=0 and why) so a benchmark run records it and carries on.
    const auto fail = [&](const char* reason) {
        if (summary) std::printf("SUMMARY found=0 reason=%s\n", reason);
        return 1;
    };
    if (used == 0) {
        std::fprintf(stderr, "no ENC cells under %s overlap the route area\n", encDir.c_str());
        return fail("no_charts");
    }

    // Keep off the shore: penalise cells near blocked water (see CostGrid::applyShoreMargin). The clearance report uses the
    // distances from before the penalty changes any costs.
    const std::vector<float> shoreDist = grid.distanceToBlockedM();
    grid.applyShoreMargin(marginM, marginWeight);

    std::vector<LatLon> route;
    std::vector<Cell> rawPath;  // unsmoothed A* cells, drawn by --map
    // Debug picture of the grid around --map, with the raw A* path and the smoothed route drawn over it. Also used when no
    // route is found, which is exactly when the grid is what needs looking at.
    const auto drawMap = [&]() {
        // '#' blocked, '.' open, 'n/e/s/w' lane by flow direction (lower case), '*' route (upper case where over a lane)
        std::vector<std::string> canvas;
        const Cell mc = grid.cellAt(mapAt);
        for (int r = mc.row - mapRadius; r <= mc.row + mapRadius; ++r) {
            std::string line;
            for (int c = mc.col - mapRadius; c <= mc.col + mapRadius; ++c) {
                const Cell cell{c, r};
                char ch = ' ';
                if (grid.inBounds(cell)) {
                    const float lane = grid.laneDirection(cell);
                    if (grid.blocked(cell)) ch = '#';
                    else if (!std::isnan(lane)) ch = "nesw"[static_cast<int>(std::floor((lane + 45.0f) / 90.0f)) & 3];
                    else ch = '.';
                }
                line += ch;
            }
            canvas.push_back(line);
        }
        for (const Cell& c : rawPath) {  // the raw A* cells first ('o'), so the smoothed route draws over them
            const int y = c.row - (mc.row - mapRadius), x = c.col - (mc.col - mapRadius);
            if (y >= 0 && y < static_cast<int>(canvas.size()) && x >= 0 && x < static_cast<int>(canvas[y].size())) {
                char& ch = canvas[y][x];
                ch = (ch == 'n' || ch == 'e' || ch == 's' || ch == 'w') ? static_cast<char>(ch - 32) : 'o';
            }
        }
        for (size_t i = 1; i < route.size(); ++i) {
            const double leg = haversineM(route[i - 1], route[i]);
            const int steps = std::max(1, static_cast<int>(leg / (grid.cellSizeM() / 2)));
            for (int k = 0; k <= steps; ++k) {
                const double t = static_cast<double>(k) / steps;
                const Cell c = grid.cellAt({route[i - 1].lat + t * (route[i].lat - route[i - 1].lat),
                                            route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)});
                const int y = c.row - (mc.row - mapRadius), x = c.col - (mc.col - mapRadius);
                if (y >= 0 && y < static_cast<int>(canvas.size()) && x >= 0 && x < static_cast<int>(canvas[y].size())) {
                    char& ch = canvas[y][x];
                    if (ch == 'n' || ch == 'e' || ch == 's' || ch == 'w') ch = static_cast<char>(ch - 32);  // upper case: route in a lane
                    else if (ch != 'N' && ch != 'E' && ch != 'S' && ch != 'W') ch = '*';  // (a raw-path 'o' is drawn over by '*')
                }
            }
        }
        std::printf("map: %d cells (%.0f m each) around %.4f,%.4f, north up. # blocked, . open, n/e/s/w lane flow, "
                    "* smoothed route, o raw A* path, N/E/S/W path in a lane\n", 2 * mapRadius + 1, grid.cellSizeM(), mapAt.lat, mapAt.lon);
        for (const auto& line : canvas) std::printf("%s\n", line.c_str());
    };
    if (!evalPath.empty()) {
        route = given;  // score as-is: no snapping, no rerouting
    } else {
        Cell s = grid.cellAt(from), g = grid.cellAt(to);
        if (!grid.inBounds(s) || !grid.inBounds(g)) {
            std::fprintf(stderr, "start or end is outside the charted area\n");
            return fail("endpoint_outside_grid");
        }
        const WaterBodies bodies = findWaterBodies(grid);
        const auto sc = nearestPerBody(grid, bodies, s, 40), gc = nearestPerBody(grid, bodies, g, 40);
        if (sc.empty() || gc.empty()) {
            std::fprintf(stderr, "%s is not near charted water deep enough for %.1f m draft + %.1f m clearance\n",
                         sc.empty() ? "start" : "end", draft, clearance);
            if (mapRadius > 0) drawMap();
            return fail("endpoint_not_in_safe_water");
        }
        // Prefer a water body both endpoints can reach, closest overall, and never a tiny pocket when a real one is available.
        int chosen = -1;
        double bestScore = 1e30;
        for (const auto& [body, ss] : sc) {
            const auto it = gc.find(body);
            if (it == gc.end()) continue;
            const double score = ss.distCells + it->second.distCells + (bodies.size[body] < 500 ? 1e6 : 0.0);
            if (score < bestScore) { bestScore = score; chosen = body; }
        }
        if (chosen < 0) {
            auto biggest = [&](const std::map<int, Snap>& m) {
                size_t n = 0;
                for (const auto& kv : m) n = std::max(n, bodies.size[kv.first]);
                return n;
            };
            std::fprintf(stderr, "start and end are in different bodies of water: nearest to the start is %zu cells, to the end %zu\n",
                         biggest(sc), biggest(gc));
            if (mapRadius > 0) drawMap();
            return fail("disconnected_water");
        }
        const Cell s2 = sc.at(chosen).cell, g2 = gc.at(chosen).cell;
        snapStartM = haversineM(from, grid.centre(s2));
        snapEndM = haversineM(to, grid.centre(g2));
        if (!(s == s2)) std::printf("start moved to nearest safe water (%.0f m)\n", snapStartM);
        if (!(g == g2)) std::printf("end moved to nearest safe water (%.0f m)\n", snapEndM);

        route = findRoute(grid, grid.centre(s2), grid.centre(g2), simplify, &rawPath, minLegM);
        if (route.empty()) {
            std::fprintf(stderr, "no route found: the charts show no continuous water at least %.1f m deep between the points\n",
                         minDepth);
            if (mapRadius > 0) drawMap();
            return fail("no_route");
        }
    }
    double nm = 0;
    for (size_t i = 1; i < route.size(); ++i) nm += haversineM(route[i - 1], route[i]) / 1852.0;
    // Clearance report: sample each leg about every cell and look up the distance to blocked water. Samples within
    // 1 km of either end are skipped, since a start in a marina or a berth is at the shore by definition.
    if (mapRadius > 0) drawMap();
    std::vector<double> clearances;
    double travelled = 0.0, unsafeM = 0.0, cautionM = 0.0;
    int cautionStretches = 0;
    bool inCaution = false;
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
            const bool caut = grid.inBounds(hc) && grid.isCaution(hc);
            if (caut) {
                cautionM += leg / steps;
                if (!inCaution) ++cautionStretches;
            }
            inCaution = caut;
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
    // Rule 10 counts. A run within 25 degrees of the flow is normal lane use, within 25 degrees of the opposite is wrong-way
    // travel, and anything between is a crossing, judged by how far it is from square to the flow.
    int withFlow = 0, crossings = 0, wrongWay = 0;
    double wrongWayM = 0, worstOff = 0;
    for (const LaneRun& r : laneRuns) {
        const double mean = r.thetaSum / r.n;
        if (mean <= 25.0) ++withFlow;
        else if (mean >= 155.0) { ++wrongWay; wrongWayM += r.lengthM; }
        else { ++crossings; worstOff = std::max(worstOff, std::fabs(mean - 90.0)); }
    }
    const auto printSummary = [&]() {
        if (!summary) return;
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
        std::printf("SUMMARY found=1 nm=%.2f straight_nm=%.2f waypoints=%zu charts=%d closest_m=%.0f median_m=%.0f blocked_m=%.0f "
                    "caution_m=%.0f lane_runs=%zu with_flow=%d crossings=%d wrong_way_m=%.0f worst_off_deg=%.0f snap_start_m=%.0f snap_end_m=%.0f "
                    "seconds=%.1f\n", nm, haversineM(from, to) / 1852.0, route.size(), used,
                    clearances.empty() ? -1.0 : clearances.front(), clearances.empty() ? -1.0 : clearances[clearances.size() / 2],
                    unsafeM, cautionM, laneRuns.size(), withFlow, crossings, wrongWayM, worstOff, snapStartM, snapEndM, secs);
    };
    if (!evalPath.empty()) {
        std::printf("unsafe by the chart rules: %.0f m of %.0f m (%.1f%%) in %zu stretch(es)\n", unsafeM, nm * 1852.0,
                    100.0 * unsafeM / (nm * 1852.0), unsafeSpots.size());
        for (size_t i = 0; i < unsafeSpots.size() && i < 10; ++i) {
            std::printf("  at %.5f,%.5f (%.1f nm along)\n", unsafeSpots[i].at.lat, unsafeSpots[i].at.lon,
                        unsafeSpots[i].alongM / 1852.0);
        }
        std::printf("precautionary areas: %.0f m in %d stretch(es)\n", cautionM, cautionStretches);
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
        printSummary();
        return 0;
    }
    std::ofstream(outPath) << routeToGpx(route, "open-autoroute");
    std::printf("%zu charts, %zu waypoints, %.1f nm (straight line %.1f nm), wrote %s\n", static_cast<size_t>(used),
                route.size(), nm, haversineM(from, to) / 1852.0, outPath.c_str());
    printSummary();
    return 0;
}
