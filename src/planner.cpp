#include "openautoroute/planner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
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

namespace oar {
namespace {

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


// printf into a message for the caller's hook (nothing happens if the caller has no hook).
void emitTo(const std::function<void(const std::string&)>& hook, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void emitTo(const std::function<void(const std::string&)>& hook, const char* fmt, ...) {
    if (!hook) return;
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string s(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0) std::vsnprintf(&s[0], s.size() + 1, fmt, ap2);
    va_end(ap2);
    hook(s);
}

}  // namespace

PlanResult planRoute(const PlanRequest& req, const PlanHooks& hooks) {
    PlanResult result;
    const std::string& encDir = req.encDir;
    const bool evaluating = req.evalRoute.size() >= 2;
    const std::vector<LatLon>& given = req.evalRoute;
    LatLon from = req.from, to = req.to;
    if (evaluating) {
        from = given.front();
        to = given.back();
    }
    const double draft = req.draftM, clearance = req.clearanceM, cellM = req.cellM, marginM = req.shoreMarginM, marginWeight = req.shoreMarginWeight;
    const double lengthM = req.lengthM, simplify = req.simplifyTolerance, laneMarginM = req.laneMarginM, laneMarginWeight = req.laneMarginWeight;
    const double caution = req.caution, minLegM = req.minLegM, airDraftArg = req.airDraftM;
    double laneUse = req.laneUse;
    const bool applyTss = req.applyTss, underSail = req.underSail, useMarks = req.useMarks, useChannels = req.useChannels, useHazards = req.useHazards;
    const std::vector<std::string>& skipClasses = req.skipClasses;
    std::string routeName = req.routeName;
    const auto startedAt = std::chrono::steady_clock::now();
    double snapStartM = 0.0, snapEndM = 0.0;
    if (encDir.empty() || cellM <= 0.0 || draft < 0.0 || clearance < 0.0) {
        emitTo(hooks.err, "the request needs an ENC folder, a positive cell size and non-negative draft and clearance\n");
        result.status = 2;
        result.failReason = "bad_request";
        return result;
    }
    bool cancelled = false;
    // Progress: `fraction` is through the phase, mapped onto [lo, hi] of the whole plan. Returning false from the caller cancels.
    const auto report = [&](const char* phase, double fraction, double lo, double hi) {
        if (!hooks.progress) return true;
        PlanProgress p;
        p.phase = phase;
        p.fraction = fraction;
        p.overall = lo + (hi - lo) * fraction;
        if (!hooks.progress(p)) cancelled = true;
        return !cancelled;
    };

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
        emitTo(hooks.err, "grid would be %.0f x %.0f cells; raise the cell size or route a shorter distance\n", cols, rows);
        result.status = 2;
        result.failReason = "grid_too_big";
        return result;
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
    Vessel vessel{lengthM, underSail};
    vessel.airDraftM = airDraftArg;
    const double airDraft = airDraftEstimateM(vessel);
    if (laneUse < 0.0) laneUse = defaultLaneUseFactor(vessel);
    const bool avoidsLanes = laneUse > 1.0;
    // Precautionary areas are where lanes converge and traffic is heaviest. A small craft should go round or cross the lanes
    // square elsewhere rather than through one (a square lane crossing costs far less than passing through the area);
    // ships expect to pass through them and only take care.
    const double cautionFactor = caution > 0.0 ? caution : (avoidsLanes ? 12.0 : 1.5);

    int used = 0;
    std::vector<LateralMark> marks;
    // On any failure, still produce a summary line (found=0 and why) so a benchmark run records it and carries on.
    const auto failed = [&](const char* reason) {
        result.status = 1;
        result.failReason = reason;
        result.summaryLine = std::string("SUMMARY found=0 reason=") + reason;
        result.chartsUsed = used;
        result.cancelled = cancelled;
        result.grid = std::make_shared<CostGrid>(std::move(grid));
        return std::move(result);
    };

    AreaLayer areaLayer;  // restricted and dangerous areas costed rather than blocked, to report which ones the route crosses
    size_t cellIndex = 0;
    for (const auto& [name, path] : cells) {
        if (!report("Reading charts", static_cast<double>(cellIndex++) / std::max<size_t>(1, cells.size()), 0.0, 0.40)) return failed("cancelled");
        ChartData d;
        std::string err;
        if (!loadS57(path.string(), d, err)) {
            emitTo(hooks.err, "skip %s: %s\n", name.c_str(), err.c_str());
            continue;
        }
        Bounds b;
        for (const auto& f : d.features) for (const auto& r : f.parts) for (const auto& p : r.points) b.add(p);
        if (!b.overlaps(box)) continue;
        StampOptions so;
        so.minDepthM = minDepth;
        so.applyTss = applyTss;
        so.cautionFactor = cautionFactor;
        so.airDraftM = airDraft;
        so.vesselLengthM = lengthM;
        so.hazardObjects = useHazards;
        so.skipClasses = skipClasses;
        so.areas = &areaLayer;
        stampChart(d, so, grid);
        if (useMarks) collectLateralMarks(d, marks);
        emitTo(hooks.out, "  chart %s\n", name.c_str());
        ++used;
    }
    if (!report("Building the grid", 0.0, 0.40, 0.50)) return failed("cancelled");
    grid.openLockCorridors();  // the line of a lock chamber is a maintained channel, whatever thin walls and bank depths say at this resolution
    grid.assignZoneDirections();  // separation zones take their direction from the lanes beside them
    // Buoyed channels: pair opposite red/green marks into gates so the route stays between them (see applyChannelGates).
    const std::vector<Gate> gates = useMarks ? applyChannelGates(grid, marks, 500.0) : std::vector<Gate>{};
    if (useMarks) emitTo(hooks.out, "%zu channel gates from %zu lateral marks\n", gates.size(), marks.size());
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
        emitTo(hooks.out, "vessel %.1f m%s: %s (lane-use factor %.2f)\n", lengthM, underSail ? ", under sail" : "",
                    avoidsLanes ? "stays out of traffic lanes, crosses square-on" : "uses traffic lanes", laneUse);
    }
    emitTo(hooks.out, "air draft %.1f m%s: bridges and overhead cables need %.1f m clearance\n", airDraft, airDraftArg < 0 ? " (estimated from the length; set --air-draft-m)" : "", airDraft + 1.0);
    if (used == 0) {
        emitTo(hooks.err, "no ENC cells under %s overlap the route area\n", encDir.c_str());
        return failed("no_charts");
    }

    // Keep off the shore: penalise cells near blocked water (see CostGrid::applyShoreMargin). The clearance report uses the
    // distances from before the penalty changes any costs.
    const std::vector<float> shoreDist = grid.distanceToBlockedM();
    grid.applyShoreMargin(marginM, marginWeight);

    std::vector<LatLon> route;
    std::vector<Cell> rawPath;  // unsmoothed A* cells, drawn by --map
    if (evaluating) {
        route = given;  // score as-is: no snapping, no rerouting
    } else {
        Cell s = grid.cellAt(from), g = grid.cellAt(to);
        if (!grid.inBounds(s) || !grid.inBounds(g)) {
            emitTo(hooks.err, "start or end is outside the charted area\n");
            return failed("endpoint_outside_grid");
        }
        const WaterBodies bodies = findWaterBodies(grid);
        const auto sc = nearestPerBody(grid, bodies, s, 40), gc = nearestPerBody(grid, bodies, g, 40);
        if (sc.empty() || gc.empty()) {
            emitTo(hooks.err, "%s is not near charted water deep enough for %.1f m draft + %.1f m clearance\n",
                         sc.empty() ? "start" : "end", draft, clearance);
            if (hooks.debugGrid) hooks.debugGrid(grid, rawPath, route);
            return failed("endpoint_not_in_safe_water");
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
            emitTo(hooks.err, "start and end are in different bodies of water: nearest to the start is %zu cells, to the end %zu\n",
                         biggest(sc), biggest(gc));
            if (hooks.debugGrid) hooks.debugGrid(grid, rawPath, route);
            return failed("disconnected_water");
        }
        const Cell s2 = sc.at(chosen).cell, g2 = gc.at(chosen).cell;
        snapStartM = haversineM(from, grid.centre(s2));
        snapEndM = haversineM(to, grid.centre(g2));
        if (!(s == s2)) emitTo(hooks.out, "start moved to nearest safe water (%.0f m)\n", snapStartM);
        if (!(g == g2)) emitTo(hooks.out, "end moved to nearest safe water (%.0f m)\n", snapEndM);

        std::function<bool(double)> searchProgress = [&](double f) { return report("Searching", f, 0.50, 0.95); };
        route = findRoute(grid, grid.centre(s2), grid.centre(g2), simplify, &rawPath, minLegM, &searchProgress);
        if (route.empty() && cancelled) return failed("cancelled");
        if (route.empty()) {
            emitTo(hooks.err, "no route found: the charts show no continuous water at least %.1f m deep between the points\n",
                         minDepth);
            emitTo(hooks.err, "  search ran inside one body of %zu cells; start cell %d,%d (blocked=%d cost=%g) goal cell %d,%d (blocked=%d cost=%g)\n",
                         bodies.size[chosen], s2.col, s2.row, grid.blocked(s2) ? 1 : 0, grid.cost(s2), g2.col, g2.row, grid.blocked(g2) ? 1 : 0, grid.cost(g2));
            if (hooks.debugGrid) hooks.debugGrid(grid, rawPath, route);
            return failed("no_route");
        }
    }
    if (!report("Checking the route", 0.0, 0.95, 1.0)) return failed("cancelled");
    double nm = 0;
    for (size_t i = 1; i < route.size(); ++i) nm += haversineM(route[i - 1], route[i]) / 1852.0;
    // Clearance report: sample each leg about every cell and look up the distance to blocked water. Samples within
    // 1 km of either end are skipped, since a start in a marina or a berth is at the shore by definition.
    if (hooks.debugGrid) hooks.debugGrid(grid, rawPath, route);
    std::vector<double> clearances;
    double travelled = 0.0, unsafeM = 0.0, cautionM = 0.0, narrowInM = 0.0, narrowNearOutM = 0.0;
    std::vector<float> narrowDist;  // distance to the nearest narrow-channel cell, for "near but outside"
    if (grid.cols() > 0) narrowDist = grid.distanceToNarrowChannelM();
    int cautionStretches = 0;
    bool inCaution = false;
    struct Spot { LatLon at; double alongM; };
    std::vector<Spot> unsafeSpots;  // first sample of each separate unsafe stretch
    bool inUnsafe = false;
    // Rule 10: maximal runs of samples inside a traffic lane, with how the route's heading relates to the lane's flow.
    struct LaneRun { LatLon at; double alongM = 0, lengthM = 0, thetaSum = 0, thetaMin = 180, thetaMax = 0; float lane = 0; int n = 0; };
    std::vector<LaneRun> laneRuns;
    bool inLane = false;
    // Restricted and dangerous areas the route crosses: metres and separate stretches in each, and where the first stretch starts.
    struct AreaCrossing { double m = 0; int stretches = 0; LatLon at{}; };
    std::map<int32_t, AreaCrossing> crossed;
    int32_t inArea = -1;
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
            if (grid.inBounds(hc)) {
                if (grid.isNarrowChannel(hc)) narrowInM += leg / steps;
                else if (!narrowDist.empty() && narrowDist[static_cast<size_t>(hc.row) * grid.cols() + hc.col] < 1500.0f && !grid.isChannel(hc)) narrowNearOutM += leg / steps;
            }
            const int32_t areaId = (grid.inBounds(hc) && !areaLayer.id.empty()) ? areaLayer.id[static_cast<size_t>(hc.row) * grid.cols() + hc.col] : -1;
            if (areaId >= 0) {
                AreaCrossing& ac = crossed[areaId];
                if (ac.stretches == 0) ac.at = here;
                if (areaId != inArea) ++ac.stretches;
                ac.m += leg / steps;
            }
            inArea = areaId;
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
        emitTo(hooks.out, "clearance from land, shoal or uncharted water (excluding 1 km at each end): closest %.0f m, "
                    "median %.0f m\n", clearances.front(), clearances[clearances.size() / 2]);
    }
    // Restricted and dangerous areas crossed, dearest first. The router treats these as costs (so a harbour is never cut off), which
    // means it will go through one when the way round is long: the skipper has to know, and check the rules for that area.
    std::vector<std::pair<int32_t, AreaCrossing>> areasCrossed(crossed.begin(), crossed.end());
    std::sort(areasCrossed.begin(), areasCrossed.end(), [&](const auto& a, const auto& b) {
        const float fa = areaLayer.notes[a.first].factor, fb = areaLayer.notes[b.first].factor;
        return fa != fb ? fa > fb : a.second.m > b.second.m;
    });
    double areasM = 0.0;
    for (const auto& [id, ac] : areasCrossed) areasM += ac.m;
    if (areasCrossed.empty()) {
        emitTo(hooks.out, "restricted or dangerous areas crossed: none charted\n");
    } else {
        emitTo(hooks.out, "restricted or dangerous areas crossed (check the rules for each before you go):\n");
        for (const auto& [id, ac] : areasCrossed) {
            const AreaNote& n = areaLayer.notes[id];
            emitTo(hooks.out, "  %s (x%.1f): %.1f nm in %d stretch(es), first near %.4f,%.4f%s%s\n", n.kind.c_str(), n.factor, ac.m / 1852.0,
                        ac.stretches, ac.at.lat, ac.at.lon, n.text.empty() ? "" : ": ", n.text.c_str());
        }
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
    // Buoy gates on this stretch of route: does the route pass between the pair? (Planar approximation around each gate.)
    int gatesNear = 0, gatesMissed = 0;
    for (const Gate& g : gates) {
        double nearest = 1e30;
        for (const LatLon& p : route) nearest = std::min(nearest, std::min(haversineM(p, g.port), haversineM(p, g.starboard)));
        if (nearest > 1500.0) continue;
        ++gatesNear;
        const double cl = std::cos(deg2rad(g.port.lat)), k = 111320.0;
        const double rx = (g.starboard.lon - g.port.lon) * cl * k, ry = (g.starboard.lat - g.port.lat) * k;
        bool hit = false;
        for (size_t i = 1; i < route.size() && !hit; ++i) {
            const double ax = (route[i - 1].lon - g.port.lon) * cl * k, ay = (route[i - 1].lat - g.port.lat) * k;
            const double bx = (route[i].lon - g.port.lon) * cl * k, by = (route[i].lat - g.port.lat) * k;
            const double den = (bx - ax) * ry - (by - ay) * rx, cr = rx * (by - ay) - ry * (bx - ax);
            if (std::fabs(den) < 1e-9 || std::fabs(cr) < 1e-9) continue;
            const double u = (-ax * ry + ay * rx) / den, t = (ax * (by - ay) - ay * (bx - ax)) / cr;
            hit = u >= 0 && u <= 1 && t >= 0 && t <= 1;
        }
        gatesMissed += !hit;
    }
    const auto printSummary = [&]() {
        char sb[1200];
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
        std::snprintf(sb, sizeof sb, "SUMMARY found=1 nm=%.2f straight_nm=%.2f waypoints=%zu charts=%d closest_m=%.0f median_m=%.0f blocked_m=%.0f "
                    "vessel_m=%.0f caution_m=%.0f restricted_m=%.0f narrow_in_m=%.0f narrow_near_out_m=%.0f gates=%d gates_missed=%d lane_runs=%zu with_flow=%d crossings=%d wrong_way_m=%.0f worst_off_deg=%.0f snap_start_m=%.0f snap_end_m=%.0f "
                    "seconds=%.1f", nm, haversineM(from, to) / 1852.0, route.size(), used,
                    clearances.empty() ? -1.0 : clearances.front(), clearances.empty() ? -1.0 : clearances[clearances.size() / 2],
                    unsafeM, lengthM, cautionM, areasM, narrowInM, narrowNearOutM, gatesNear, gatesMissed, laneRuns.size(), withFlow, crossings, wrongWayM, worstOff, snapStartM, snapEndM, secs);
        result.summaryLine = sb;
    };
    if (evaluating) {
        emitTo(hooks.out, "unsafe by the chart rules: %.0f m of %.0f m (%.1f%%) in %zu stretch(es)\n", unsafeM, nm * 1852.0,
                    100.0 * unsafeM / (nm * 1852.0), unsafeSpots.size());
        for (size_t i = 0; i < unsafeSpots.size() && i < 10; ++i) {
            emitTo(hooks.out, "  at %.5f,%.5f (%.1f nm along)\n", unsafeSpots[i].at.lat, unsafeSpots[i].at.lon,
                        unsafeSpots[i].alongM / 1852.0);
        }
        emitTo(hooks.out, "precautionary areas: %.0f m in %d stretch(es)\n", cautionM, cautionStretches);
        emitTo(hooks.out, "narrow channels: %.0f m inside, %.0f m running just outside; buoy gates: %d near the route, %d missed\n", narrowInM, narrowNearOutM, gatesNear, gatesMissed);
        emitTo(hooks.out, "traffic lanes (Rule 10): %zu lane transit(s): %d with the flow, %d crossing, %d wrong-way (%.0f m)\n",
                    laneRuns.size(), withFlow, crossings, wrongWay, wrongWayM);
        for (size_t i = 0; i < laneRuns.size() && i < 12; ++i) {
            const LaneRun& r = laneRuns[i];
            const double mean = r.thetaSum / r.n;
            const char* kind = mean <= 25.0 ? "with flow" : mean >= 155.0 ? "WRONG WAY" : "crossing";
            emitTo(hooks.out, "  %-9s lane %3.0f deg, %4.0f m in lane, heading %.0f deg off the flow", kind, r.lane, r.lengthM, mean);
            if (mean > 25.0 && mean < 155.0) emitTo(hooks.out, " (%.0f deg from square)", std::fabs(mean - 90.0));
            emitTo(hooks.out, " at %.4f,%.4f\n", r.at.lat, r.at.lon);
        }
        if (crossings > 0) emitTo(hooks.out, "  worst crossing is %.0f deg from square\n", worstOff);
        emitTo(hooks.out, "%zu charts, %zu waypoints, %.1f nm (straight line %.1f nm)\n", static_cast<size_t>(used), route.size(),
                    nm, haversineM(from, to) / 1852.0);
        printSummary();
        result.status = 0;
        result.route = route;
        result.nm = nm;
        result.straightNm = haversineM(from, to) / 1852.0;
        result.chartsUsed = used;
        result.grid = std::make_shared<CostGrid>(std::move(grid));
        return result;
    }
    // OpenCPN's Route Manager shows the route name and the names of the first and last waypoints (its From and To columns).
    char descBuf[200];
    std::snprintf(descBuf, sizeof descBuf, "open-autoroute: %.1f nm, %.1f m vessel, %.1f m draft plus %.1f m clearance", nm, lengthM, draft, clearance);
    std::string desc = descBuf;
    if (!areasCrossed.empty()) {
        desc += ". Crosses restricted or dangerous areas:";
        for (const auto& [id, ac] : areasCrossed) {
            char one[80];
            std::snprintf(one, sizeof one, " %s %.1f nm;", areaLayer.notes[id].kind.c_str(), ac.m / 1852.0);
            desc += one;
        }
    }
    if (routeName.empty()) {
        char nb[80];
        std::snprintf(nb, sizeof nb, "open-autoroute %.3f,%.3f to %.3f,%.3f", from.lat, from.lon, to.lat, to.lon);
        routeName = nb;
    }
    printSummary();
    result.status = 0;
    result.route = route;
    result.routeName = routeName;
    result.description = desc;
    result.nm = nm;
    result.straightNm = haversineM(from, to) / 1852.0;
    result.chartsUsed = used;
    result.snapStartM = snapStartM;
    result.snapEndM = snapEndM;
    for (const auto& [id, ac] : areasCrossed) {
        const AreaNote& n = areaLayer.notes[id];
        result.areasCrossed.push_back({n.kind, n.text, n.factor, ac.m, ac.stretches, ac.at});
    }
    result.grid = std::make_shared<CostGrid>(std::move(grid));
    return result;
}

}  // namespace oar
