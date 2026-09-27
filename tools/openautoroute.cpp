// Command-line router: read ENC cells, route between two points, write a GPX that OpenCPN can import.
//
//   openautoroute --enc DIR --from LAT,LON --to LAT,LON [--draft M] [--clearance M] [--cell-m M] [--name NAME] [--start-name A] [--end-name B] [--picture FILE.ppm] [-o route.gpx]
//   openautoroute --enc DIR --eval route.gpx [--draft M] ...   (score an existing route, e.g. from another planner)
//
// DIR is searched recursively for `.000` base cells (e.g. an ENC_ROOT folder). Only cells that overlap the route's
// bounding box are kept in memory.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "openautoroute/chart_grid.hpp"
#include "openautoroute/gpx.hpp"
#include "openautoroute/pathfinder.hpp"
#include "openautoroute/planner.hpp"
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

// Vertices of a GPX route or track (<rtept> or <trkpt>), in file order. A tolerant scan, not a full XML parser: enough for the files chart
// apps export, and anything without lat/lon attributes is skipped. Standalone <wpt> marks (which chart apps export beside the route) are
// only used when the file has no route or track points, so they never become the start of the route that gets scored.
std::vector<LatLon> readGpxPoints(const std::string& path) {
    std::ifstream in(path);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto scan = [&](std::initializer_list<const char*> tags) {
        std::vector<LatLon> pts;
        for (size_t pos = text.find('<'); pos != std::string::npos; pos = text.find('<', pos + 1)) {
            bool match = false;
            for (const char* t : tags) match = match || text.compare(pos, std::strlen(t), t) == 0;
            if (!match) continue;
            const size_t end = text.find('>', pos);
            if (end == std::string::npos) break;
            const std::string tag = text.substr(pos, end - pos);
            const size_t la = tag.find("lat=\""), lo = tag.find("lon=\"");
            if (la == std::string::npos || lo == std::string::npos) continue;
            pts.push_back({std::atof(tag.c_str() + la + 5), std::atof(tag.c_str() + lo + 5)});
        }
        return pts;
    };
    std::vector<LatLon> pts = scan({"<rtept", "<trkpt"});
    return pts.empty() ? scan({"<wpt"}) : pts;
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s --enc DIR (--from LAT,LON --to LAT,LON | --eval ROUTE.gpx) [--draft M=1.5] [--clearance M=1.0]\n"
                 "          [--cell-m M=30] [--margin-m M=500] [--margin-weight W=10] [--no-tss] [--length-m L=12] [--air-draft-m H] [--under-sail] [--lane-use F] [--lane-margin-m M=1500] [--lane-margin-weight W=12] [--caution F] [--simplify T=0.05] [--min-leg-m M=460] [--summary] [--map LAT,LON,CELLS] [-o route.gpx]\n", argv0);
}

}  // namespace

// A picture of the router's own view: land and blocked water dark, open water pale, lanes and zones tinted, caution and dear areas warm,
// with the route in red and its waypoints as dots (start green, end blue). Written as a binary PPM (no library needed) and reduced so the longest
// side is at most maxPx; a .json beside it gives the latitude and longitude of the top-left corner and of each pixel, so a reader can turn a
// spot in the picture back into a position.
void writePicture(const std::string& path, const CostGrid& grid, const std::vector<LatLon>& route, int maxPx = 1500) {
    const int scale = std::max(1, static_cast<int>(std::ceil(std::max(grid.cols(), grid.rows()) / static_cast<double>(maxPx))));
    const int w = (grid.cols() + scale - 1) / scale, h = (grid.rows() + scale - 1) / scale;
    std::vector<unsigned char> img(static_cast<size_t>(w) * h * 3);
    auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        unsigned char* p = &img[(static_cast<size_t>(y) * w + x) * 3];
        p[0] = r; p[1] = g; p[2] = b;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int cells = 0, blocked = 0, lane = 0, zone = 0, caution = 0, dear = 0, channel = 0;
            for (int r = y * scale; r < std::min(grid.rows(), (y + 1) * scale); ++r) {
                for (int c = x * scale; c < std::min(grid.cols(), (x + 1) * scale); ++c) {
                    const Cell cell{c, r};
                    ++cells;
                    if (grid.blocked(cell)) { ++blocked; continue; }
                    lane += !std::isnan(grid.laneDirection(cell));
                    zone += grid.isZone(cell);
                    caution += grid.isCaution(cell);
                    channel += grid.isNarrowChannel(cell);
                    dear += grid.cost(cell) >= 8.0f;
                }
            }
            if (blocked * 2 > cells) put(x, y, 70, 70, 70);                          // land and blocked water
            else if (zone * 4 > cells) put(x, y, 150, 100, 200);                     // separation zone
            else if (lane * 3 > cells) put(x, y, 205, 190, 240);                     // traffic lane
            else if (caution * 3 > cells) put(x, y, 250, 220, 150);                  // precautionary area
            else if (dear * 3 > cells) put(x, y, 240, 190, 190);                    // expensive (restricted, military...)
            else if (channel * 3 > cells) put(x, y, 170, 215, 190);                  // narrow charted channel
            else put(x, y, 215, 232, 246);                                           // open water
        }
    }
    auto px = [&](LatLon p, int& x, int& y) {
        const Cell c = grid.cellAt(p);
        x = c.col / scale;
        y = c.row / scale;
    };
    auto dot = [&](int cx, int cy, int rad, unsigned char r, unsigned char g, unsigned char b) {
        for (int dy = -rad; dy <= rad; ++dy) for (int dx = -rad; dx <= rad; ++dx) put(cx + dx, cy + dy, r, g, b);
    };
    for (size_t i = 1; i < route.size(); ++i) {  // route legs, thick red
        int x0, y0, x1, y1;
        px(route[i - 1], x0, y0);
        px(route[i], x1, y1);
        const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) + 1;
        for (int k = 0; k <= steps; ++k) dot(x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps, 1, 220, 30, 30);
    }
    for (size_t i = 0; i < route.size(); ++i) {  // waypoints: black dots, start green, end blue
        int x, y;
        px(route[i], x, y);
        if (i == 0) dot(x, y, 4, 20, 160, 40);
        else if (i + 1 == route.size()) dot(x, y, 4, 30, 60, 220);
        else dot(x, y, 2, 0, 0, 0);
    }
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    out.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    const LatLon nw = grid.centre({0, 0});
    std::ofstream meta(path + ".json");
    meta.precision(9);
    meta << "{\"width\": " << w << ", \"height\": " << h << ", \"north_lat\": " << nw.lat + 0.5 * grid.cellSizeDeg()
         << ", \"west_lon\": " << nw.lon - 0.5 * grid.cellSizeLonDeg() << ", \"lat_per_px\": " << grid.cellSizeDeg() * scale
         << ", \"lon_per_px\": " << grid.cellSizeLonDeg() * scale << ", \"metres_per_px\": " << grid.cellSizeM() * scale << "}\n";
}

int main(int argc, char** argv) {
    std::string encDir, evalPath, outPath = "route.gpx", routeName, startName = "START", endName = "END", picturePath;
    LatLon from{}, to{}, mapAt{};
    int mapRadius = 0;
    bool trace = false, timing = false;
    bool haveFrom = false, haveTo = false, applyTss = true, underSail = false;
    double draft = 1.5, clearance = 1.0, cellM = 30.0, marginM = 500.0, marginWeight = 10.0, lengthM = 12.0, laneUse = -1.0, simplify = 0.05, laneMarginM = 1500.0, laneMarginWeight = 12.0, caution = -1.0, minLegM = 460.0;
    bool summary = false, useMarks = true, useChannels = true, useHazards = true, useCatalogue = true;
    double airDraftArg = -1.0;
    std::vector<std::string> skipClasses;  // dev switches (--no-marks, --no-boundaries) exist only to compare runs
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool hasVal = i + 1 < argc;
        if (a == "--version") {
#ifndef OAR_VERSION
#define OAR_VERSION "dev"
#endif
            std::printf("open-autoroute %s (a planning aid, not for navigation; use at your own risk)\n", OAR_VERSION);
            return 0;
        }
        else if (a == "--enc" && hasVal) encDir = argv[++i];
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
        else if (a == "--air-draft-m" && hasVal) airDraftArg = std::atof(argv[++i]);
        else if (a == "--skip-class" && hasVal) {  // developer switch: ignore some hazard classes, e.g. --skip-class RESARE,BRIDGE
            std::string list = argv[++i];
            for (size_t p = 0; p <= list.size();) {
                const size_t c = list.find(',', p);
                skipClasses.push_back(list.substr(p, c == std::string::npos ? std::string::npos : c - p));
                if (c == std::string::npos) break;
                p = c + 1;
            }
        }
        else if (a == "--no-hazards") useHazards = false;  // developer switch: skip the chart-object hazard rules
        else if (a == "--no-boundaries") useChannels = false;  // developer switch: ignore charted channel limits
        else if (a == "--timing") timing = true;  // developer: print when each phase starts
        else if (a == "--no-catalogue") useCatalogue = false;  // developer switch: open every cell instead of using CATALOG.031 (to compare speed)
        else if (a == "--no-marks") useMarks = false;  // ignore red/green lateral marks (for comparison)
        else if (a == "--summary") summary = true;  // one machine-readable line at the end, for benchmark scripts
        else if (a == "--simplify" && hasVal) simplify = std::atof(argv[++i]);
        else if (a == "--trace") trace = true;  // developer: with --map, also list the raw path cells in the window with their lane and cost
        else if (a == "--no-tss") applyTss = false;
        else if (a == "--under-sail" || a == "--sail") underSail = true;  // engine off, sails doing the work
        else if (a == "--length-m" && hasVal) lengthM = std::atof(argv[++i]);
        else if (a == "--lane-use" && hasVal) laneUse = std::atof(argv[++i]);
        else if (a == "--margin-m" && hasVal) marginM = std::atof(argv[++i]);
        else if (a == "--margin-weight" && hasVal) marginWeight = std::atof(argv[++i]);
        else if (a == "--picture" && hasVal) picturePath = argv[++i];  // draw the route over the router's view of the water (PPM) plus a .json of the corners
        else if (a == "--name" && hasVal) routeName = argv[++i];
        else if (a == "--start-name" && hasVal) startName = argv[++i];
        else if (a == "--end-name" && hasVal) endName = argv[++i];
        else if ((a == "-o" || a == "--out") && hasVal) outPath = argv[++i];
        else { usage(argv[0]); return 2; }
    }
    PlanRequest req;
    if (!encDir.empty()) req.encDirs.push_back(encDir);
    req.from = from;
    req.to = to;
    req.draftM = draft;
    req.clearanceM = clearance;
    req.cellM = cellM;
    req.shoreMarginM = marginM;
    req.shoreMarginWeight = marginWeight;
    req.lengthM = lengthM;
    req.laneUse = laneUse;
    req.simplifyTolerance = simplify;
    req.laneMarginM = laneMarginM;
    req.laneMarginWeight = laneMarginWeight;
    req.caution = caution;
    req.minLegM = minLegM;
    req.airDraftM = airDraftArg;
    req.underSail = underSail;
    req.applyTss = applyTss;
    req.useMarks = useMarks;
    req.useChannels = useChannels;
    req.useHazards = useHazards;
    req.useCatalogue = useCatalogue;
    req.skipClasses = skipClasses;
    req.routeName = routeName;
    req.startName = startName;
    req.endName = endName;
    if (!evalPath.empty()) {
        req.evalRoute = readGpxPoints(evalPath);
        if (req.evalRoute.size() < 2) {
            std::fprintf(stderr, "no route points found in %s\n", evalPath.c_str());
            return 2;
        }
        haveFrom = haveTo = true;
    }
    if (encDir.empty() || !haveFrom || !haveTo || !std::isfinite(cellM) || cellM <= 0.0 || !std::isfinite(draft) || draft < 0.0 ||
        !std::isfinite(clearance) || clearance < 0.0) {
        usage(argv[0]);
        return 2;
    }

    PlanHooks hooks;
    hooks.out = [](const std::string& s) { std::fputs(s.c_str(), stdout); };
    hooks.err = [](const std::string& s) { std::fputs(s.c_str(), stderr); };
    if (timing) {
        const auto t0 = std::chrono::steady_clock::now();
        auto lastPhase = std::make_shared<std::string>();
        hooks.progress = [t0, lastPhase](const PlanProgress& p) {
            if (*lastPhase != p.phase) {
                *lastPhase = p.phase;
                std::fprintf(stderr, "[%6.1f s] %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), p.phase);
            }
            return true;
        };
    }
    if (mapRadius > 0) {
        // Debug picture of the grid around --map, with the raw A* path and the smoothed route drawn over it. Also used when no
        // route is found, which is exactly when the grid is what needs looking at.
        hooks.debugGrid = [&](const CostGrid& grid, const std::vector<Cell>& rawPath, const std::vector<LatLon>& route) {
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
            if (trace) {
                std::printf("raw path cells in the window (col,row: lane flow, zone, caution, cost), in travel order:\n");
                Cell prev{-1, -1};
                for (const Cell& c : rawPath) {
                    if (std::abs(c.col - mc.col) > mapRadius || std::abs(c.row - mc.row) > mapRadius) continue;
                    const float lane = grid.laneDirection(c);
                    std::printf("  (%d,%d)", c.col, c.row);
                    if (prev.col >= 0) std::printf(" step %+d,%+d", c.col - prev.col, c.row - prev.row);
                    std::printf(" lane=%s%.0f zone=%d caution=%d cost=%.2f\n", std::isnan(lane) ? "-" : "", std::isnan(lane) ? 0.0f : lane,
                                grid.isZone(c) ? 1 : 0, grid.isCaution(c) ? 1 : 0, grid.cost(c));
                    prev = c;
                }
            }
        };
    }
    const PlanResult result = planRoute(req, hooks);
    if (result.status == 2) return 2;
    if (result.status != 0) {
        if (summary) std::printf("%s\n", result.summaryLine.c_str());
        return 1;
    }
    if (evalPath.empty()) {
        if (result.grid && !picturePath.empty()) writePicture(picturePath, *result.grid, result.route);
        std::ofstream gpxOut(outPath);
        gpxOut << routeToGpx(result.route, result.routeName, startName, endName, result.description);
        gpxOut.close();
        if (!gpxOut) {   // a missing folder, a read-only path or a full disk must not look like success
            std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
            if (summary) std::printf("SUMMARY found=0 reason=write_failed\n");
            return 1;
        }
        std::printf("%zu charts, %zu waypoints, %.1f nm (straight line %.1f nm), wrote %s\n", static_cast<size_t>(result.chartsUsed),
                    result.route.size(), result.nm, result.straightNm, outPath.c_str());
    }
    if (summary) std::printf("%s\n", result.summaryLine.c_str());
    return 0;
}
