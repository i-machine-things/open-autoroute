#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

#include "openautoroute/chart_grid.hpp"
#include "openautoroute/cost_grid.hpp"
#include "openautoroute/geo.hpp"
#include "openautoroute/gpx.hpp"
#include "openautoroute/pathfinder.hpp"
#include "openautoroute/planner.hpp"
#include "openautoroute/s57.hpp"
#include "openautoroute/vessel.hpp"

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

using namespace oar;

static CostGrid makeGrid(int cols, int rows) { return CostGrid(cols, rows, {46.0, -124.0}, 0.001); }

static void testHaversine() {
    // One degree of latitude is ~111.2 km.
    CHECK(std::fabs(haversineM({0, 0}, {1, 0}) - 111195.0) < 100.0);
}

static void testCellRoundTrip() {
    CostGrid g = makeGrid(10, 10);
    const Cell c{3, 4};
    const Cell back = g.cellAt(g.centre(c));
    CHECK(back == c);
}

static void testDepthBarrier() {
    CostGrid g = makeGrid(3, 1);
    g.applyDepthBarrier({5.0f, 1.9f, std::nanf("")}, 1.5, 0.5);  // needs >= 2.0 m
    CHECK(!g.blocked({0, 0}));
    CHECK(g.blocked({1, 0}));
    CHECK(!g.blocked({2, 0}));  // no data stays open
}

static void testStraightRoute() {
    CostGrid g = makeGrid(20, 5);
    auto route = findRoute(g, g.centre({0, 2}), g.centre({19, 2}));
    CHECK(route.size() == 2);  // open water collapses to a single leg
}

static void testRoutesAroundWall() {
    CostGrid g = makeGrid(20, 20);
    for (int r = 0; r < 15; ++r) g.setCost({10, r}, kBlocked);  // wall with a gap at the bottom
    auto route = findRoute(g, g.centre({2, 2}), g.centre({17, 2}));
    CHECK(route.size() >= 3);
    for (const LatLon& p : route) CHECK(!g.blocked(g.cellAt(p)));
}

static void testLongExpensiveRouteIsFound() {
    // A one-cell-wide corridor of expensive cells: there is exactly one way through, so one wrongly dropped queue entry ends the
    // search. Cost-so-far reaches about 1.5 million, where float rounding once made the stale-entry test drop valid entries and the
    // search reported no route at all.
    CostGrid g = makeGrid(3000, 1);
    for (int c = 0; c < 3000; ++c) g.setCost({c, 0}, 500.0f);
    const auto route = findRoute(g, g.centre({0, 0}), g.centre({2999, 0}));
    CHECK(route.size() >= 2);
    CHECK(!route.empty() && g.cellAt(route.back()).col == 2999);
    g.setLaneUseFactor(0.5);  // and with the scaled distance estimate large vessels use
    CHECK(!findRoute(g, g.centre({0, 0}), g.centre({2999, 0})).empty());
    // Costs that differ from cell to cell make the rounding vary along the way.
    CostGrid v = makeGrid(3000, 1);
    for (int c = 0; c < 3000; ++c) v.setCost({c, 0}, 400.0f + static_cast<float>((c * 7919) % 300) * 0.37f);
    CHECK(!findRoute(v, v.centre({0, 0}), v.centre({2999, 0})).empty());
}

static void testNoRoute() {
    CostGrid g = makeGrid(10, 10);
    for (int r = 0; r < 10; ++r) g.setCost({5, r}, kBlocked);  // full wall
    CHECK(findRoute(g, g.centre({1, 1}), g.centre({8, 8})).empty());
    CHECK(findRoute(g, g.centre({5, 1}), g.centre({8, 8})).empty());  // blocked start
}

static void testChannelCentering() {
    // Start and goal sit off the centreline (row 2); the route must detour into the centre lane (row 1).
    CostGrid g = makeGrid(20, 3);
    std::vector<float> dist(20 * 3, 100.0f);
    for (int c = 0; c < 20; ++c) dist[1 * 20 + c] = 0.0f;
    for (int c = 0; c < 20; ++c) g.setCost({c, 0}, kBlocked);
    g.applyChannelCentering(dist, 50.0, 4.0);
    CHECK(g.cost({5, 1}) < g.cost({5, 2}));
    auto route = findRoute(g, g.centre({0, 2}), g.centre({19, 2}));
    CHECK(route.size() >= 3);
    int centreWaypoints = 0;
    for (const LatLon& p : route) centreWaypoints += g.cellAt(p).row == 1;
    CHECK(centreWaypoints >= 2);
}

static void testShoreMargin() {
    // Open 20 x 9 basin with a blocked "shore" row along the top. With a margin the route must run well below it.
    CostGrid g = makeGrid(20, 9);
    for (int c = 0; c < 20; ++c) g.setCost({c, 0}, kBlocked);
    const auto dist = g.distanceToBlockedM();
    CHECK(dist[0] == 0.0f);
    CHECK(std::fabs(dist[3 * 20 + 5] - static_cast<float>(3 * g.cellSizeM())) < 1.0f);  // 3 rows below the shore

    auto closestRow = [&](const CostGrid& grid) {
        auto route = findRoute(grid, grid.centre({0, 1}), grid.centre({19, 1}));  // both ends hug the shore
        int minRow = 99;
        for (const LatLon& p : route) minRow = std::min(minRow, grid.cellAt(p).row);
        return std::make_pair(route.size(), minRow);
    };
    CostGrid plain = g;
    const auto before = closestRow(plain);
    CHECK(before.second == 1);  // no margin: straight along the shore

    g.applyShoreMargin(6 * g.cellSizeM(), 6.0);
    CHECK(g.cost({5, 1}) > g.cost({5, 6}));  // near the shore costs more than open water
    CHECK(g.blocked({5, 0}));                // blocked cells stay blocked
    const auto after = closestRow(g);
    CHECK(after.first >= 3 && after.second >= 1);
    int deepest = 0;
    auto route = findRoute(g, g.centre({0, 1}), g.centre({19, 1}));
    for (const LatLon& p : route) deepest = std::max(deepest, g.cellAt(p).row);
    CHECK(deepest >= 4);  // the route swings out from the shore mid-way
}

static void testInputValidation() {
    CostGrid g = makeGrid(3, 1);
    const std::vector<float> dist{0.0f, 10.0f, 20.0f};
    bool threw = false;
    try { g.applyChannelCentering(dist, 50.0, -1.0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { g.applyChannelCentering({0.0f, -5.0f, 20.0f}, 50.0, 1.0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    // lineOfSight must refuse out-of-grid endpoints instead of indexing outside the grid.
    CostGrid open = makeGrid(5, 5);
    CHECK(!lineOfSight(open, {0, 0}, {9, 9}));
    CHECK(!lineOfSight(open, {-1, 2}, {3, 2}));
    CHECK(lineOfSight(open, {0, 0}, {4, 4}));

    threw = false;
    try { routeToGpx({{std::nan(""), 0.0}}, "x"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { routeToGpx({{91.0, 0.0}}, "x"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // Control characters in the name are dropped; tab/newline are kept.
    const std::string gpx = routeToGpx({{46.0, -124.0}}, std::string("a\x01" "b\x1f" "c\td"));
    CHECK(gpx.find("<name>abc\td</name>") != std::string::npos);
}

static void testGpxNames() {
    const std::vector<LatLon> pts{{46.0, -124.0}, {46.1, -124.1}, {46.2, -124.2}};
    const std::string plain = routeToGpx(pts, "r");
    CHECK(plain.find("<name>WP001</name>") != std::string::npos && plain.find("<name>WP003</name>") != std::string::npos);
    CHECK(plain.find("<desc>") == std::string::npos);
    // OpenCPN fills its From and To columns from the first and last waypoint names.
    const std::string named = routeToGpx(pts, "Edmonds to Kingston", "START", "END", "4.7 nm & more");
    CHECK(named.find("<name>Edmonds to Kingston</name>") != std::string::npos);
    CHECK(named.find("<name>START</name>") != std::string::npos && named.find("<name>END</name>") != std::string::npos);
    CHECK(named.find("<name>WP002</name>") != std::string::npos);      // the middle one keeps its number
    CHECK(named.find("<name>WP001</name>") == std::string::npos && named.find("<name>WP003</name>") == std::string::npos);
    CHECK(named.find("<desc>4.7 nm &amp; more</desc>") != std::string::npos);
    // A route of one waypoint has a first that is also its last: START wins, and nothing is emitted twice.
    CHECK(routeToGpx({{46.0, -124.0}}, "r", "START", "END").find("<name>START</name>") != std::string::npos);
}

static void testGpxLocaleIndependent() {
    // A comma-decimal global locale must not leak into the GPX numbers. The locale may not be installed; then skip.
    try {
        const std::locale previous = std::locale::global(std::locale("de_DE.UTF-8"));
        const std::string gpx = routeToGpx({{46.5, -124.25}}, "r");
        std::locale::global(previous);
        CHECK(gpx.find("lat=\"46.500000\" lon=\"-124.250000\"") != std::string::npos);
    } catch (const std::runtime_error&) {
    }
}

static void testGpx() {
    const std::string gpx = routeToGpx({{46.1, -124.0}, {46.2, -124.1}}, "A & B");
    CHECK(gpx.find("<rtept lat=\"46.100000\" lon=\"-124.000000\">") != std::string::npos);
    CHECK(gpx.find("A &amp; B") != std::string::npos);
}

static ChartFeature areaFeature(const char* cls, double lat0, double lat1, double lon0, double lon1,
                                double drval1 = std::nan("")) {
    ChartFeature f;
    f.objectClass = cls;
    f.geometry = Geometry::Area;
    f.drval1 = drval1;
    Ring r;
    r.points = {{lat1, lon0}, {lat1, lon1}, {lat0, lon1}, {lat0, lon0}, {lat1, lon0}};
    f.parts.push_back(r);
    return f;
}

static ChartFeature pointFeature(const char* cls, Cell c, const CostGrid& g, double valsou) {
    ChartFeature f;
    f.objectClass = cls;
    f.geometry = Geometry::Point;
    f.valsou = valsou;
    f.parts.push_back({{g.centre(c)}, false});
    return f;
}

static void testStampChart() {
    CostGrid g = makeGrid(12, 10);  // cell 0.001 deg, NW corner (46.0, -124.0); columns 0..11
    g.fill(7.0f);                   // sentinel: cells no chart covers must keep their value
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.995, 5.0));   // cols 0-4: deep
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -123.995, -123.990, 1.0));  // cols 5-9: shoal
    d.features.push_back(areaFeature("LNDARE", 45.997, 45.999, -123.999, -123.997));    // island in the deep area
    d.features.push_back(areaFeature("DEPARE", 45.995, 45.996, -123.990, -123.988, std::nan("")));  // unknown depth
    d.features.push_back(pointFeature("WRECKS", {3, 3}, g, 1.0));                       // shallow wreck
    d.features.push_back(pointFeature("OBSTRN", {2, 4}, g, std::nan("")));              // obstruction, depth unknown
    d.features.push_back(pointFeature("WRECKS", {4, 3}, g, 9.0));                       // deep wreck: harmless
    stampChart(d, 2.5, g);

    CHECK(g.cost({0, 0}) == 1.0f);       // deep water
    CHECK(g.blocked({7, 5}));            // shoal area
    CHECK(g.blocked({1, 1}));            // land
    CHECK(g.blocked({3, 3}));            // shallow wreck
    CHECK(g.blocked({2, 4}));            // obstruction with no depth is treated as unsafe
    CHECK(g.cost({4, 3}) == 1.0f);       // wreck deeper than draft + clearance is harmless
    CHECK(g.cost({11, 8}) == 7.0f);      // not covered by the chart: untouched
    CHECK(g.blocked({10, 4}));           // unknown DRVAL1 area is blocked, not assumed safe
}

static void testFinerChartWins() {
    CostGrid g = makeGrid(10, 10);
    g.fill(kBlocked);
    ChartData coarse, fine;
    coarse.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.990, 1.0));  // says shoal everywhere
    fine.features.push_back(areaFeature("DEPARE", 45.994, 45.996, -124.0, -123.990, 6.0));  // dredged channel
    stampChart(coarse, 2.5, g);
    stampChart(fine, 2.5, g);
    CHECK(g.blocked({3, 1}));    // outside the fine chart's polygon: coarse verdict stands
    CHECK(!g.blocked({3, 5}));   // inside the fine chart's polygon: it overrides the coarse chart
    CHECK(!findRoute(g, g.centre({0, 5}), g.centre({9, 5})).empty());
}

// --- COLREGs Rule 10 -----------------------------------------------------------------------------------------------

static void testLaneFactor() {
    // No cliff at the tolerance: the cost rises steadily from 25 to 32 degrees off the flow, where it meets the crossing cost. (A heading of 25.4
    // degrees used to cost about 19 while a near-square crossing cost about 4, so a path following a 65 degree lane alternated with-flow steps
    // and cheap crossings: a sawtooth of waypoints.)
    CHECK(laneFactor(25.0, 0.0) == 1.0f);
    CHECK(laneFactor(25.4, 0.0) < laneFactor(70.0, 0.0));   // barely off the flow is cheaper than a near-square crossing
    CHECK(laneFactor(26.0, 0.0) < laneFactor(28.0, 0.0) && laneFactor(28.0, 0.0) < laneFactor(31.0, 0.0));
    CHECK(std::fabs(laneFactor(32.0, 0.0) - (2.0f + 30.0f * (1.0f - std::sin(32.0f * 3.14159265f / 180.0f)))) < 1e-3f);  // meets the crossing cost
    CHECK(std::fabs(laneFactor(45.0, 0.0) - (2.0f + 30.0f * (1.0f - std::sin(45.0f * 3.14159265f / 180.0f)))) < 1e-3f);  // beyond it: unchanged
    CHECK(laneFactor(0.0, 0.0) == 1.0f);        // with the flow
    CHECK(laneFactor(20.0, 0.0) == 1.0f);       // within the 25 degree tolerance
    CHECK(laneFactor(350.0, 0.0) == 1.0f);      // wraps around north
    CHECK(laneFactor(180.0, 0.0) == kBlocked);  // against the flow
    CHECK(laneFactor(170.0, 0.0) == kBlocked);
    CHECK(std::fabs(laneFactor(90.0, 0.0) - 2.0f) < 1e-4f);  // perpendicular crossing: cheapest way through
    CHECK(laneFactor(45.0, 0.0) > laneFactor(90.0, 0.0));    // oblique crossings cost more
    CHECK(laneFactor(60.0, 0.0) > laneFactor(80.0, 0.0));
    CHECK(std::fabs(laneFactor(270.0, 0.0) - laneFactor(90.0, 0.0)) < 1e-4f);  // crossing either way costs the same
}

static double legHeading(const CostGrid& g, LatLon a, LatLon b) {
    const Cell ca = g.cellAt(a), cb = g.cellAt(b);
    return bearingDeg(g.centre(ca), g.centre(cb));
}

static void testNoWrongWayInLane() {
    // Eastbound lane band across the middle of the grid (rows 3-5). Going east along it is fine; going west is not.
    CostGrid g = makeGrid(24, 9);
    for (int r = 3; r <= 5; ++r) for (int c = 0; c < 24; ++c) g.setLaneDirection({c, r}, 90.0f);
    auto east = findRoute(g, g.centre({1, 4}), g.centre({22, 4}));
    CHECK(east.size() == 2);  // straight along the lane, with the flow

    auto west = findRoute(g, g.centre({22, 4}), g.centre({1, 4}));
    CHECK(!west.empty());
    for (size_t i = 1; i < west.size(); ++i) {
        const LatLon mid{(west[i - 1].lat + west[i].lat) / 2, (west[i - 1].lon + west[i].lon) / 2};
        const float lane = g.laneDirection(g.cellAt(mid));
        if (!std::isnan(lane)) CHECK(laneFactor(legHeading(g, west[i - 1], west[i]), lane) != kBlocked);
    }
}

static void testCrossesLaneAtRightAngles() {
    // Northbound lane band (cols 10-13) running the full height. A diagonal route must still cross it square-on.
    CostGrid g = makeGrid(24, 24);
    for (int r = 0; r < 24; ++r) for (int c = 10; c <= 13; ++c) g.setLaneDirection({c, r}, 0.0f);
    auto route = findRoute(g, g.centre({1, 1}), g.centre({22, 22}));
    CHECK(route.size() >= 3);
    int crossingLegs = 0;
    for (size_t i = 1; i < route.size(); ++i) {
        const Cell a = g.cellAt(route[i - 1]), b = g.cellAt(route[i]);
        const bool inBand = (a.col >= 10 && a.col <= 13) || (b.col >= 10 && b.col <= 13) || (a.col < 10 && b.col > 13);
        if (!inBand || a.col == b.col) continue;
        ++crossingLegs;
        CHECK(angleDiffDeg(legHeading(g, route[i - 1], route[i]), 90.0) < 15.0);  // near 90 degrees to the flow
    }
    CHECK(crossingLegs >= 1);

    // With the lane not known (Rule 10 off) the same route is the plain diagonal.
    CostGrid plain = makeGrid(24, 24);
    CHECK(findRoute(plain, plain.centre({1, 1}), plain.centre({22, 22})).size() == 2);
}

static void testLaneUseFactor() {
    // Eastbound lane (rows 3-5) with open water either side. A vessel that just wants to travel east should use the
    // lane when it is free to, and stay out of it when lane use is priced high enough.
    CostGrid g = makeGrid(24, 9);
    for (int r = 3; r <= 5; ++r) for (int c = 0; c < 24; ++c) g.setLaneDirection({c, r}, 90.0f);
    auto lanes = [&](const std::vector<LatLon>& route) {
        int inLane = 0;
        for (const LatLon& p : route) inLane += !std::isnan(g.laneDirection(g.cellAt(p)));
        return inLane;
    };
    auto free = findRoute(g, g.centre({1, 4}), g.centre({22, 4}));
    CHECK(free.size() == 2 && lanes(free) == 2);  // both ends are in the lane; the whole leg runs along it

    g.setLaneUseFactor(0.01);  // clamped: a lane is never made (almost) free
    CHECK(std::fabs(g.laneUseFactor() - 0.1f) < 1e-6f);

    // Both ends sit inside the lane. Priced at 3x, running the whole way in it costs more than stepping out, running
    // alongside and stepping back in, so the middle of the route must leave the lane.
    g.setLaneUseFactor(3.0);
    auto priced = findRoute(g, g.centre({1, 4}), g.centre({22, 4}));
    CHECK(priced.size() >= 3);
    bool leftLane = false;
    for (size_t i = 1; i < priced.size(); ++i) {  // judge legs by their midpoint: the run outside is one long leg
        const Cell mid = g.cellAt({(priced[i - 1].lat + priced[i].lat) / 2, (priced[i - 1].lon + priced[i].lon) / 2});
        leftLane |= mid.col >= 4 && mid.col <= 19 && std::isnan(g.laneDirection(mid));
    }
    CHECK(leftLane);
}

static void testVesselClass() {
    CHECK(isSmallVessel({19.9, false}));   // Rule 10(j): under 20 m
    CHECK(!isSmallVessel({20.0, false}));  // 20 m and over is not covered
    CHECK(isSmallVessel({60.0, true}));    // a vessel under sail is small whatever its length
    CHECK(!isSmallVessel({25.0, false}));  // the same 25 m yacht motoring is power-driven, so judged on length
    CHECK(defaultLaneUseFactor({12.0, false}) > 1.0);   // small craft keep out of lanes
    CHECK(defaultLaneUseFactor({120.0, false}) < 1.0);  // large vessels are drawn into them
}

static void testSmallCraftDoesNotClipLaneCorner() {
    // A lane band (cols 10-13) that ends at row 12. The straight line from (2,14) to (20,10) would clip its last row.
    CostGrid g = makeGrid(24, 24);
    for (int r = 0; r <= 12; ++r) for (int c = 10; c <= 13; ++c) g.setLaneDirection({c, r}, 0.0f);
    auto laneCellsOnRoute = [&](const std::vector<LatLon>& route) {
        int hits = 0;
        for (size_t i = 1; i < route.size(); ++i) {
            for (int k = 0; k <= 200; ++k) {  // sample each leg finely; any lane cell counts
                const double t = k / 200.0;
                const LatLon p{route[i - 1].lat + t * (route[i].lat - route[i - 1].lat),
                               route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)};
                hits += !std::isnan(g.laneDirection(g.cellAt(p)));
            }
        }
        return hits;
    };
    g.setLaneUseFactor(6.0);
    CHECK(laneCellsOnRoute(findRoute(g, g.centre({2, 14}), g.centre({20, 10}))) == 0);  // small craft go around

    // When there is no way round (lane spans the whole grid) a small craft still crosses, and still square-on.
    CostGrid full = makeGrid(24, 24);
    for (int r = 0; r < 24; ++r) for (int c = 10; c <= 13; ++c) full.setLaneDirection({c, r}, 0.0f);
    full.setLaneUseFactor(6.0);
    auto route = findRoute(full, full.centre({1, 1}), full.centre({22, 22}));
    CHECK(route.size() >= 3);
    for (size_t i = 1; i < route.size(); ++i) {
        const Cell a = full.cellAt(route[i - 1]), b = full.cellAt(route[i]);
        if ((a.col >= 10 && a.col <= 13) || (b.col >= 10 && b.col <= 13) || (a.col < 10 && b.col > 13)) {
            if (a.col != b.col) CHECK(angleDiffDeg(legHeading(full, route[i - 1], route[i]), 90.0) < 15.0);
        }
    }
}

static void testNoTurnInsideLane() {
    // A one-cell-wide lane (the sliver where two lane parts join) at col 14, walled off on both sides except at row 15,
    // so a small craft must cross it at exactly that gap. The straight shortcut from the start fails on the wall just
    // past the lane, which used to leave the turning point on the lane cell itself. The turn belongs before the lane.
    CostGrid g = makeGrid(30, 30);
    for (int r = 0; r < 30; ++r) {
        g.setLaneDirection({14, r}, 0.0f);
        if (r != 15) { g.setCost({13, r}, kBlocked); g.setCost({15, r}, kBlocked); }
    }
    g.setLaneUseFactor(6.0);
    auto route = findRoute(g, g.centre({2, 3}), g.centre({27, 22}));
    CHECK(route.size() >= 3);
    bool crossed = false;
    for (size_t i = 0; i < route.size(); ++i) {
        const Cell c = g.cellAt(route[i]);
        if (i > 0 && i + 1 < route.size()) CHECK(std::isnan(g.laneDirection(c)));  // no turning point on the lane
        crossed |= i > 0 && g.cellAt(route[i - 1]).col < 14 && c.col > 14;
    }
    CHECK(crossed);
    for (size_t i = 1; i < route.size(); ++i) {
        const Cell a = g.cellAt(route[i - 1]), b = g.cellAt(route[i]);
        if (a.col < 14 && b.col > 14) CHECK(angleDiffDeg(legHeading(g, route[i - 1], route[i]), 90.0) < 15.0);
    }
}

static void testSimplifyTolerance() {
    // A patch of slightly dearer water in the middle. Exact costing bends the route round it; a 25% tolerance lets one
    // straight leg replace the bend, and looser tolerances never add waypoints. A wall of blocked cells must never be cut through however loose the tolerance is.
    CostGrid g = makeGrid(30, 10);
    for (int r = 3; r <= 6; ++r) for (int c = 10; c <= 20; ++c) g.setCost({c, r}, 1.3f);
    const auto exact = findRoute(g, g.centre({0, 4}), g.centre({29, 4}));
    const auto loose = findRoute(g, g.centre({0, 4}), g.centre({29, 4}), 0.25);
    CHECK(exact.size() > 2);
    CHECK(loose.size() == 2);
    size_t previous = exact.size();
    for (double tol : {0.05, 0.10, 0.20, 0.40}) {
        const size_t n = findRoute(g, g.centre({0, 4}), g.centre({29, 4}), tol).size();
        CHECK(n <= previous);
        previous = n;
    }

    CostGrid wall = makeGrid(30, 10);
    for (int r = 0; r < 9; ++r) wall.setCost({15, r}, kBlocked);  // gap only at the bottom row
    const auto detour = findRoute(wall, wall.centre({0, 4}), wall.centre({29, 4}), 5.0);  // absurdly loose
    CHECK(detour.size() > 2);
    for (const LatLon& p : detour) CHECK(!wall.blocked(wall.cellAt(p)));
    for (size_t i = 1; i < detour.size(); ++i) CHECK(lineOfSight(wall, wall.cellAt(detour[i - 1]), wall.cellAt(detour[i])));
}

static void testLineOfSightSeesEveryTouchedCell() {
    // From (0,0) to (6,1) the real line crosses a row boundary in the middle of column 3, so it touches both (3,0) and
    // (3,1). A thin Bresenham line only visits one of them; either being blocked must stop the shortcut.
    CostGrid a = makeGrid(8, 3), b = makeGrid(8, 3);
    a.setCost({3, 0}, kBlocked);
    b.setCost({3, 1}, kBlocked);
    CHECK(!lineOfSight(a, {0, 0}, {6, 1}));
    CHECK(!lineOfSight(b, {0, 0}, {6, 1}));
    CostGrid open = makeGrid(8, 3);
    CHECK(lineOfSight(open, {0, 0}, {6, 1}));
    CostGrid corner = makeGrid(5, 5);
    corner.setCost({2, 1}, kBlocked);  // the pure diagonal (0,0)-(4,4) passes exactly through corners: (2,1) is a neighbour
    CHECK(!lineOfSight(corner, {0, 0}, {4, 4}));
}

static void testDirectionalLaneMargin() {
    // Lane (cols 10-11, flow north) with a separation zone beside it (cols 12-13): the margin should bite hard on moves along
    // the lane and only lightly on moves square across it.
    CostGrid g = makeGrid(30, 30);
    for (int r = 0; r < 30; ++r) {
        for (int c = 10; c <= 11; ++c) g.setLaneDirection({c, r}, 0.0f);
        for (int c = 12; c <= 13; ++c) g.setZone({c, r});
    }
    g.assignZoneDirections();
    CHECK(g.laneMarginFactor({8, 15}, 0.0) == 1.0f);  // no margin until applied
    g.applyLaneMargin(6 * g.cellSizeM(), 6.0);
    const float along = g.laneMarginFactor({9, 15}, 0.0), square = g.laneMarginFactor({9, 15}, 90.0);
    CHECK(along > 3.0f);                 // running beside the lane edge: heavily charged
    CHECK(square < along);
    CHECK(square < 0.4f * along);        // crossing squarely is much cheaper than running along
    CHECK(square >= 1.0f);
    CHECK(g.laneMarginFactor({1, 15}, 0.0) == 1.0f);   // far from any lane: nothing
    CHECK(g.laneMarginFactor({10, 15}, 0.0) == 1.0f);  // inside the lane itself: not re-priced
    CHECK(g.laneMarginFactor({16, 15}, 0.0) > 1.0f);   // and the zone counts too, from its far side
    CHECK(g.cost({9, 15}) == 1.0f);                    // the margin never touches the base cost
}

static void testLaneMarginBowsAwayFromLaneRun() {
    // A small craft that has to travel north beside a lane should stand off from it rather than skim its edge.
    CostGrid g = makeGrid(30, 40);
    for (int r = 0; r < 40; ++r) for (int c = 20; c <= 21; ++c) g.setLaneDirection({c, r}, 0.0f);
    g.setLaneUseFactor(6.0);
    auto nearestLaneM = [](const CostGrid& grid, const std::vector<LatLon>& route) {
        const auto d = grid.distanceToLaneM();
        double nearest = 1e30;
        for (size_t i = 1; i < route.size(); ++i) {
            for (int k = 0; k <= 100; ++k) {
                const double t = k / 100.0;
                const Cell c = grid.cellAt({route[i - 1].lat + t * (route[i].lat - route[i - 1].lat),
                                            route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)});
                if (c.row < 10 || c.row > 30) continue;  // the start and goal sit beside the lane by design; judge the middle
                nearest = std::min<double>(nearest, d[static_cast<size_t>(c.row) * grid.cols() + c.col]);
            }
        }
        return nearest;
    };
    // Start and goal are one cell off the lane edge, so the straight line skims it.
    const auto plain = findRoute(g, g.centre({18, 38}), g.centre({18, 2}));
    const double before = nearestLaneM(g, plain);
    CHECK(before < 3.0 * g.cellSizeM());  // control: with no margin the straight north run stays right beside the lane
    g.applyLaneMargin(8 * g.cellSizeM(), 6.0);
    const auto bowed = findRoute(g, g.centre({18, 38}), g.centre({18, 2}));
    CHECK(!bowed.empty());
    CHECK(nearestLaneM(g, bowed) > before + 1.0 * g.cellSizeM());  // stands off from the lane
}

static void testMinimumLegLength() {
    // A round island (radius 8 cells) in the way. The exact route bends round it with several waypoints close together; a
    // minimum leg length must thin them out without ever cutting through the island.
    CostGrid g = makeGrid(40, 40);
    for (int r = 0; r < 40; ++r) {
        for (int c = 0; c < 40; ++c) {
            if ((c - 20) * (c - 20) + (r - 20) * (r - 20) <= 64) g.setCost({c, r}, kBlocked);
        }
    }
    g.applyShoreMargin(4 * g.cellSizeM(), 6.0);  // the margin makes the smoothing keep a curve of waypoints round the island
    const LatLon a = g.centre({2, 20}), b = g.centre({37, 20});
    const auto tight = findRoute(g, a, b, 0.0, nullptr, 0.0);
    const double minLeg = 8.0 * g.cellSizeM();
    const auto thinned = findRoute(g, a, b, 0.0, nullptr, minLeg);
    CHECK(tight.size() > 3);                  // control: without a minimum there are several waypoints round the island
    CHECK(thinned.size() >= 2 && thinned.size() < tight.size());
    auto shortest = [&](const std::vector<LatLon>& r) {
        double best = 1e30;
        for (size_t i = 1; i < r.size(); ++i) best = std::min(best, haversineM(r[i - 1], r[i]));
        return best;
    };
    CHECK(shortest(thinned) > shortest(tight));
    for (size_t i = 1; i < thinned.size(); ++i) {
        CHECK(lineOfSight(g, g.cellAt(thinned[i - 1]), g.cellAt(thinned[i])));  // never cuts through the island
    }
    CHECK(thinned.front().lat == tight.front().lat && thinned.back().lon == tight.back().lon);  // same endpoints
}

// Does the route cross the straight segment between the two marks of a gate? (Planar, fine over a few kilometres.)
static bool routePassesGate(const std::vector<LatLon>& route, const Gate& g) {
    const double cl = std::cos(deg2rad(g.port.lat)), k = 111320.0;
    const double rx = (g.starboard.lon - g.port.lon) * cl * k, ry = (g.starboard.lat - g.port.lat) * k;
    for (size_t i = 1; i < route.size(); ++i) {
        const double ax = (route[i - 1].lon - g.port.lon) * cl * k, ay = (route[i - 1].lat - g.port.lat) * k;
        const double bx = (route[i].lon - g.port.lon) * cl * k, by = (route[i].lat - g.port.lat) * k;
        const double den = (bx - ax) * ry - (by - ay) * rx;
        if (std::fabs(den) < 1e-9) continue;
        const double u = (-ax * ry + ay * rx) / den;                                 // along the route leg
        const double t = (ax * (by - ay) - ay * (bx - ax)) / (rx * (by - ay) - ry * (bx - ax));  // along the gate
        if (u >= 0 && u <= 1 && t >= 0 && t <= 1) return true;
    }
    return false;
}

static void testChannelGates() {
    // A dog-leg channel (east, a bend, then north) marked by red/green pairs every 4 cells. Cutting the corner leaves the
    // channel; the gates must make the route follow it.
    const double latStep = 0.001, lonStep = latStep / std::cos(deg2rad(46.0));  // square in metres
    auto makeSquare = [&]() { return CostGrid(60, 50, {46.0, -124.0}, latStep, lonStep); };
    CostGrid g = makeSquare();
    std::vector<std::pair<double, double>> centre;  // channel centreline in (col, row) cells
    for (int c = 4; c <= 24; c += 4) centre.push_back({static_cast<double>(c), 40.0});
    for (int k = 1; k <= 4; ++k) centre.push_back({24.0 + 3.0 * k, 40.0 - 3.0 * k});
    for (int r = 28; r >= 4; r -= 4) centre.push_back({36.0, static_cast<double>(r)});
    std::vector<LateralMark> marks;
    for (size_t i = 0; i < centre.size(); ++i) {
        const auto a = centre[i == 0 ? 0 : i - 1], b = centre[i == 0 ? 1 : i];
        double dx = b.first - a.first, dy = b.second - a.second;
        const double len = std::hypot(dx, dy);
        dx /= len; dy /= len;
        // Travelling from west/south toward north/east: right-hand side is (dy, -dx) in (col, row) with rows running south.
        const double rc = centre[i].first - dy * 3.0, rr = centre[i].second + dx * 3.0;  // right of the direction of travel
        const double lc = centre[i].first + dy * 3.0, lr = centre[i].second - dx * 3.0;  // left
        marks.push_back({g.centre({static_cast<int>(std::lround(lc)), static_cast<int>(std::lround(lr))}), 1});   // port-hand
        marks.push_back({g.centre({static_cast<int>(std::lround(rc)), static_cast<int>(std::lround(rr))}), 2});   // starboard-hand
    }
    const LatLon start = g.centre({4, 40}), goal = g.centre({36, 4});

    auto crossedCount = [&](const CostGrid& grid, const std::vector<Gate>& gates) {
        const auto route = findRoute(grid, start, goal);
        int n = 0;
        for (const Gate& gate : gates) n += routePassesGate(route, gate);
        return n;
    };
    CostGrid plain = makeSquare();
    CostGrid gated = makeSquare();
    const std::vector<Gate> gates = applyChannelGates(gated, marks, 1000.0, 8.0, 1.5, 2500.0);  // (test channel is wider than a real narrow one)
    CHECK(gates.size() >= centre.size() - 4);  // nearly every pair became a gate (the chain rule drops a few at the bends)
    const int without = crossedCount(plain, gates), with = crossedCount(gated, gates);
    CHECK(without < static_cast<int>(gates.size()));  // control: the straight corner cut misses gates
    CHECK(with > without);                            // the gates pull the route into the channel
    CHECK(with >= static_cast<int>(gates.size()) - 1);  // and it passes between (nearly) every pair

    // Beyond a mark is heavily penalised; between the marks the base cost is untouched (the side preference is separate).
    CHECK(gated.cost({12, 30}) >= 8.0f - 1e-3f || gated.cost({12, 50}) >= 8.0f - 1e-3f);
    CHECK(gated.cost({12, 40}) == 1.0f);

    // Marks with no partner across the channel (only one side marked) make no gate and change nothing.
    CostGrid lone = makeSquare();
    std::vector<LateralMark> oneSided{{lone.centre({10, 20}), 1}, {lone.centre({20, 20}), 1}};
    CHECK(applyChannelGates(lone, oneSided).empty());
    CHECK(lone.cost({15, 20}) == 1.0f);

    // A single pair of marks (a harbour entrance) is not a channel: no gate, nothing changes. Three in a row are.
    CostGrid pair = makeSquare();
    const std::vector<LateralMark> onePair{{pair.centre({20, 18}), 1}, {pair.centre({20, 22}), 2}};
    CHECK(applyChannelGates(pair, onePair, 1000.0).empty());
    CHECK(pair.cost({20, 10}) == 1.0f);
    CostGrid chain = makeSquare();
    std::vector<LateralMark> threePairs;
    for (int c : {14, 20, 26}) { threePairs.push_back({chain.centre({c, 18}), 1}); threePairs.push_back({chain.centre({c, 22}), 2}); }
    CHECK(applyChannelGates(chain, threePairs, 1000.0).size() == 3);

    // Reach beyond the marks is short: open water far from the channel keeps its cost.
    CostGrid reach = makeSquare();
    applyChannelGates(reach, threePairs, 1000.0, 8.0, 1.5, 400.0);
    CHECK(reach.cost({20, 1}) == 1.0f);   // about 2 km past the marks
    CHECK(reach.cost({20, 45}) == 1.0f);
    // Preferred-channel and unknown categories are ignored when collecting marks.
    ChartData d;
    ChartFeature f;
    f.objectClass = "BOYLAT"; f.geometry = Geometry::Point; f.catlam = 3.0; f.parts.push_back({{{46.0, -124.0}}, false});
    d.features.push_back(f);
    f.catlam = 2.0;
    d.features.push_back(f);
    std::vector<LateralMark> got;
    collectLateralMarks(d, got);
    CHECK(got.size() == 1 && got[0].category == 2);
}

static void testKeepToStarboardSide() {
    // A straight east-west buoyed channel (rows 17-23), port-hand marks on the north side and starboard-hand marks on the south, so
    // the direction of buoyage is east. Going east the vessel's starboard side is the south half; going west it is the north half.
    const double latStep = 0.001, lonStep = latStep / std::cos(deg2rad(46.0));
    auto make = [&]() { return CostGrid(70, 40, {46.0, -124.0}, latStep, lonStep); };
    CostGrid g = make();
    std::vector<LateralMark> marks;
    for (int c = 4; c < 66; c += 6) {
        marks.push_back({g.centre({c, 17}), 1});  // port-hand, north side
        marks.push_back({g.centre({c, 23}), 2});  // starboard-hand, south side
    }
    const auto gates = applyChannelGates(g, marks, 1000.0, 8.0, 3.0, 2500.0);
    CHECK(gates.size() >= 8);
    auto meanRow = [&](const std::vector<LatLon>& route) {
        double sum = 0; int n = 0;
        for (size_t i = 1; i < route.size(); ++i) {
            for (int k = 0; k <= 40; ++k) {
                const double t = k / 40.0;
                sum += g.cellAt({route[i - 1].lat + t * (route[i].lat - route[i - 1].lat), route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)}).row;
                ++n;
            }
        }
        return sum / n;
    };
    const auto east = findRoute(g, g.centre({6, 20}), g.centre({62, 20}));
    const auto west = findRoute(g, g.centre({62, 20}), g.centre({6, 20}));
    CHECK(!east.empty() && !west.empty());
    CHECK(meanRow(east) > 20.5);  // eastbound keeps to the south (right) side
    CHECK(meanRow(west) < 19.5);  // westbound keeps to the north (right) side
    CHECK(g.gateSideFactor({30, 21}, 90.0) != g.gateSideFactor({30, 21}, 270.0));  // off-centre, so it depends on the direction of travel
    CHECK(g.gateSideFactor({30, 20}, 0.0) == 1.0f);   // crossing the channel: no side to keep to
    CHECK(g.gateSideFactor({30, 3}, 90.0) == 1.0f);   // outside any gate corridor
}

static void testChannelPreference() {
    // An L-shaped NARROW charted channel (5 cells, about 550 m). Cutting the corner outside the limits costs; crossing the channel is
    // free; and nothing happens away from a narrow channel.
    CostGrid g = makeGrid(40, 40);
    for (int r = 0; r < 40; ++r) {
        for (int c = 0; c < 40; ++c) {
            if ((r >= 30 && r <= 34) || (c >= 30 && c <= 34)) g.setChannel({c, r});
        }
    }
    g.markNarrowChannels(1000.0);
    CHECK(g.isNarrowChannel({10, 32}));
    CHECK(g.channelMarginFactor({10, 27}, 90.0) == 1.0f);  // no preference until applied
    g.applyChannelPreference(16 * g.cellSizeM(), 6.0);  // long enough to reach across the bend
    // Just north of the horizontal arm (outside it): running east along the edge is dear, crossing north-south is nearly free.
    const float along = g.channelMarginFactor({10, 28}, 90.0), across = g.channelMarginFactor({10, 28}, 0.0);
    CHECK(along > 5.0f);
    CHECK(across < 1.0f + 0.2f * 6.0f);
    CHECK(across < 0.35f * along);
    CHECK(g.channelMarginFactor({10, 32}, 90.0) == 1.0f);  // inside the channel: untouched
    CHECK(g.cost({10, 28}) == 1.0f);                        // and the base cost is never changed

    // Cutting the corner is worse than following the channel round the bend.
    const LatLon a = g.centre({2, 32}), b = g.centre({32, 2});
    auto outside = [&](const std::vector<LatLon>& route) {
        int n = 0;
        for (size_t i = 1; i < route.size(); ++i) {
            for (int k = 0; k <= 60; ++k) {
                const double t = k / 60.0;
                n += !g.isChannel(g.cellAt({route[i - 1].lat + t * (route[i].lat - route[i - 1].lat), route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)}));
            }
        }
        return n;
    };
    CostGrid plain = makeGrid(40, 40);
    for (int r = 0; r < 40; ++r) for (int c = 0; c < 40; ++c) if ((r >= 30 && r <= 34) || (c >= 30 && c <= 34)) plain.setChannel({c, r});
    CHECK(outside(findRoute(plain, a, b)) > 20);   // control: without the preference the diagonal cuts the corner
    CHECK(outside(findRoute(g, a, b)) < outside(findRoute(plain, a, b)));  // with it the route hugs the channel more

    CostGrid none = makeGrid(10, 10);
    none.applyChannelPreference(5 * none.cellSizeM(), 6.0);  // no channels charted: a no-op
    CHECK(none.channelMarginFactor({3, 3}, 90.0) == 1.0f);

    // A WIDE channel (a bay-sized fairway) is not narrow: it changes nothing, so open-water routing is untouched.
    CostGrid wide = makeGrid(60, 60);
    for (int r = 10; r < 50; ++r) for (int c = 0; c < 60; ++c) wide.setChannel({c, r});  // 40 cells wide, about 4.4 km
    wide.markNarrowChannels(600.0);
    CHECK(!wide.isNarrowChannel({30, 30}));
    wide.applyChannelPreference(10 * wide.cellSizeM(), 6.0);
    CHECK(wide.channelMarginFactor({30, 5}, 90.0) == 1.0f);

    // Traffic separation is never touched: a lane beside a narrow channel keeps its cost even inside the channel's range.
    CostGrid lanes = makeGrid(40, 40);
    for (int r = 0; r < 40; ++r) for (int c = 18; c <= 21; ++c) lanes.setChannel({c, r});
    lanes.markNarrowChannels(1000.0);
    for (int r = 0; r < 40; ++r) lanes.setLaneDirection({24, r}, 0.0f);
    lanes.applyChannelPreference(10 * lanes.cellSizeM(), 6.0);
    CHECK(lanes.channelMarginFactor({24, 10}, 0.0) == 1.0f);   // a lane cell in range: untouched
    CHECK(lanes.channelMarginFactor({26, 10}, 0.0) > 1.0f);    // ordinary water in range, running along the edge: charged
}

static void testWaterBodies() {
    // A wall splits a 10x6 basin in two; a diagonal pair of blocked cells must not let water leak through the corner.
    CostGrid g = makeGrid(10, 6);
    for (int r = 0; r < 6; ++r) g.setCost({5, r}, kBlocked);
    const WaterBodies split = findWaterBodies(g);
    CHECK(split.size.size() == 2);
    CHECK(split.size[0] + split.size[1] == 54);  // every open cell belongs to one body (60 cells minus the 6-cell wall)
    CHECK(split.label[5] == -1);                 // blocked cells have no body
    CHECK(split.label[0] != split.label[9]);

    CostGrid gap = makeGrid(10, 6);
    for (int r = 0; r < 6; ++r) gap.setCost({5, r}, kBlocked);
    gap.setCost({5, 3}, 1.0f);  // a one-cell gap joins them
    CHECK(findWaterBodies(gap).size.size() == 1);

    CostGrid diag = makeGrid(4, 4);
    diag.setCost({1, 0}, kBlocked); diag.setCost({0, 1}, kBlocked);  // corner cell (0,0) is shut in by two blocked neighbours
    const WaterBodies pocket = findWaterBodies(diag);
    CHECK(pocket.size.size() == 2);                       // (0,0) is a one-cell pocket, not joined through the diagonal
    CHECK(pocket.label[0] != pocket.label[2 * 4 + 2]);
}

static void testSeparationZoneCrossing() {
    // Lane (cols 6-9, flow north) | zone (cols 10-12) | lane (cols 13-16, flow south), full height. A crossing vessel must be
    // able to cross the whole scheme square on, and must never run along the zone.
    CostGrid g = makeGrid(24, 24);
    for (int r = 0; r < 24; ++r) {
        for (int c = 6; c <= 9; ++c) g.setLaneDirection({c, r}, 0.0f);
        for (int c = 13; c <= 16; ++c) g.setLaneDirection({c, r}, 180.0f);
        for (int c = 10; c <= 12; ++c) g.setZone({c, r});
    }
    CHECK(std::isnan(g.zoneDirection({11, 5})));  // no direction until it is derived from the lanes
    g.assignZoneDirections();
    CHECK(g.zoneDirection({11, 5}) == 0.0f || g.zoneDirection({11, 5}) == 180.0f);  // parallel to the lanes beside it

    auto across = findRoute(g, g.centre({1, 12}), g.centre({22, 12}));  // due east, straight over the scheme
    CHECK(across.size() == 2);                                           // one straight, square crossing
    auto along = findRoute(g, g.centre({11, 1}), g.centre({11, 22}));    // start and end inside the zone, along it
    CHECK(along.empty() || along.size() > 2);                            // may not run along the zone
    for (size_t i = 1; i < along.size(); ++i) {
        const Cell a = g.cellAt(along[i - 1]), b = g.cellAt(along[i]);
        if (a.col >= 10 && a.col <= 12 && b.col >= 10 && b.col <= 12) CHECK(a.col != b.col || a.row == b.row);
    }

    // A zone cell no lane touches has no direction, so it stays impassable.
    CostGrid lone = makeGrid(10, 10);
    for (int r = 0; r < 10; ++r) lone.setZone({5, r});
    lone.assignZoneDirections();
    CHECK(std::isnan(lone.zoneDirection({5, 5})));
    CHECK(findRoute(lone, lone.centre({1, 5}), lone.centre({8, 5})).empty());
}

static void testPrecautionaryAreaCost() {
    CostGrid g = makeGrid(12, 10);
    g.fill(kBlocked);
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
    d.features.push_back(areaFeature("PRCARE", 45.990, 46.0, -123.996, -123.992));  // cols 4-7
    stampChart(d, 2.5, g, true, 3.0);
    CHECK(g.cost({5, 5}) == 3.0f);
    CHECK(g.cost({1, 5}) == 1.0f);
    CHECK(g.isCaution({5, 5}) && !g.isCaution({1, 5}));  // remembered so a finished route can be checked
    CostGrid off = makeGrid(12, 10);
    off.fill(kBlocked);
    stampChart(d, 2.5, off, true, 1.0);  // no caution factor: ordinary water
    CHECK(off.cost({5, 5}) == 1.0f);
}

static void testLargeVesselStaysInLane() {
    // Eastbound lane (rows 3-5); both ends are in open water beside it. A large vessel (cheap lane) should run along
    // the lane; the default vessel should not touch it.
    CostGrid g = makeGrid(30, 9);
    for (int r = 3; r <= 5; ++r) for (int c = 0; c < 30; ++c) g.setLaneDirection({c, r}, 90.0f);
    auto legInLane = [&](const std::vector<LatLon>& route) {
        bool in = false;
        for (size_t i = 1; i < route.size(); ++i) {
            const Cell mid = g.cellAt({(route[i - 1].lat + route[i].lat) / 2, (route[i - 1].lon + route[i].lon) / 2});
            in |= !std::isnan(g.laneDirection(mid));
        }
        return in;
    };
    g.setLaneUseFactor(1.0);
    CHECK(!legInLane(findRoute(g, g.centre({1, 2}), g.centre({28, 2}))));  // straight along row 2, outside the lane
    g.setLaneUseFactor(0.2);
    CHECK(legInLane(findRoute(g, g.centre({1, 2}), g.centre({28, 2}))));   // drawn into the lane and follows it east
    // The wrong way is still refused however cheap lanes are: westbound traffic must stay out of an eastbound lane.
    auto west = findRoute(g, g.centre({28, 2}), g.centre({1, 2}));
    CHECK(!west.empty() && !legInLane(west));
}

static ChartFeature laneFeature(const char* cls, double lat0, double lat1, double lon0, double lon1, double orient) {
    ChartFeature f = areaFeature(cls, lat0, lat1, lon0, lon1);
    f.orient = orient;
    return f;
}

static void testStampTss() {
    CostGrid g = makeGrid(12, 10);
    g.fill(kBlocked);
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));       // all deep water
    d.features.push_back(laneFeature("TSSLPT", 45.990, 46.0, -123.999, -123.997, 346.0));    // lane, cols 1-2
    d.features.push_back(areaFeature("TSEZNE", 45.990, 46.0, -123.996, -123.994));           // zone, cols 4-5
    ChartFeature line;
    line.objectClass = "TSELNE";
    line.geometry = Geometry::Line;
    line.parts.push_back({{{45.9995, -123.9925}, {45.9905, -123.9925}}, false});             // separation line, col 7
    d.features.push_back(line);
    stampChart(d, 2.5, g);

    CHECK(g.laneDirection({1, 5}) == 346.0f && g.laneDirection({2, 5}) == 346.0f);
    CHECK(std::isnan(g.laneDirection({0, 5})) && std::isnan(g.laneDirection({3, 5})));
    CHECK(!g.blocked({1, 5}));  // a lane is open water: it only restricts direction
    CHECK(g.isZone({4, 5}) && g.isZone({5, 5}));    // separation zone: water, but only crossable square on
    CHECK(!g.blocked({4, 5}) && !g.blocked({5, 5}));
    CHECK(g.isZone({7, 5}));                        // separation line is a zone too
    CHECK(!g.isZone({6, 5}) && !g.isZone({8, 5}));

    CostGrid off = makeGrid(12, 10);
    off.fill(kBlocked);
    stampChart(d, 2.5, off, false);
    CHECK(std::isnan(off.laneDirection({1, 5})));  // Rule 10 off: no lanes recorded
    CHECK(!off.isZone({4, 5}));                    // and the zone is ordinary water
}

// --- hazard objects -----------------------------------------------------------------------------------------------------------------

static ChartFeature lineFeature(const char* cls, std::vector<LatLon> pts) {
    ChartFeature f;
    f.objectClass = cls;
    f.geometry = Geometry::Line;
    f.parts.push_back({std::move(pts), false});
    return f;
}

// A 12 x 10 grid of deep open water (cell = 0.001 degrees, about 111 m) stamped from one DEPARE plus the given features.
static CostGrid stampHazards(std::vector<ChartFeature> extra, double airDraft = 0.0, bool hazards = true) {
    CostGrid g = makeGrid(12, 10);
    g.fill(kBlocked);
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
    for (auto& f : extra) d.features.push_back(std::move(f));
    StampOptions o;
    o.minDepthM = 2.5;
    o.airDraftM = airDraft;
    o.hazardObjects = hazards;
    stampChart(d, o, g);
    return g;
}

static void testHazardObstructions() {
    // Unknown depth over an obstruction is unsafe; known and deep enough is safe.
    ChartFeature unknownArea = areaFeature("OBSTRN", 45.994, 45.996, -123.998, -123.996);           // cols 2-3, rows 4-5, no VALSOU
    ChartFeature deepArea = areaFeature("OBSTRN", 45.994, 45.996, -123.994, -123.992);              // cols 6-7
    deepArea.valsou = 10.0;
    ChartFeature shallowPoint = pointFeature("WRECKS", {9, 2}, makeGrid(12, 10), 1.0);               // 1 m over a wreck
    ChartFeature awash = pointFeature("UWTROC", {9, 7}, makeGrid(12, 10), 10.0);                     // deep VALSOU but awash
    awash.watlev = 5.0;
    ChartFeature submerged = pointFeature("UWTROC", {10, 7}, makeGrid(12, 10), 10.0);                // always under water, deep
    submerged.watlev = 3.0;
    ChartFeature coversUncovers = pointFeature("OBSTRN", {1, 8}, makeGrid(12, 10), 20.0);
    coversUncovers.watlev = 4.0;
    ChartFeature obstructionLine = lineFeature("OBSTRN", {{45.9975, -123.9995}, {45.9975, -123.9955}});  // row 2, cols 0-4, no depth
    CostGrid g = stampHazards({unknownArea, deepArea, shallowPoint, awash, submerged, coversUncovers, obstructionLine});
    CHECK(g.blocked({2, 4}) && g.blocked({3, 5}));   // obstruction area of unknown depth
    CHECK(!g.blocked({6, 4}) && !g.blocked({7, 5})); // obstruction area with 10 m over it
    CHECK(g.blocked({9, 2}));                        // wreck with 1 m over it
    CHECK(g.blocked({9, 7}));                        // awash, whatever VALSOU says
    CHECK(!g.blocked({10, 7}));                      // always submerged with deep water over it
    CHECK(g.blocked({1, 8}));                        // covers and uncovers
    CHECK(g.blocked({1, 2}) && g.blocked({3, 2}));   // an obstruction LINE blocks the cells it crosses
    // The hazard rules are separable for comparison runs, and the older entry point never applies them.
    CostGrid off = stampHazards({unknownArea}, 0.0, false);
    CHECK(!off.blocked({2, 4}));
}

static void testHazardBlocksAlways() {
    ChartFeature unsurveyed = areaFeature("UNSARE", 45.994, 45.996, -123.998, -123.996);
    ChartFeature pier = lineFeature("SLCONS", {{45.9915, -123.9915}, {45.9915, -123.9895}});          // row 8, a line of cells
    ChartFeature pile = pointFeature("PILPNT", {0, 0}, makeGrid(12, 10), 0.0);
    pile.valsou = std::nan("");
    ChartFeature farm = areaFeature("MARCUL", 45.990, 45.992, -123.994, -123.992);                    // rows 8-9, cols 6-7
    ChartFeature platform = pointFeature("OFSPLF", {6, 3}, makeGrid(12, 10), 0.0);                     // 250 m berth round it
    CostGrid g = stampHazards({unsurveyed, pier, pile, farm, platform});
    CHECK(g.blocked({2, 4}));                 // unsurveyed: never assumed safe, even inside a deep depth area
    CHECK(g.blocked({9, 8}) && g.blocked({10, 8}));  // pier
    CHECK(g.blocked({0, 0}));                 // pile
    CHECK(g.blocked({6, 8}) && g.blocked({7, 9}));   // marine farm
    CHECK(g.blocked({6, 3}));                 // the platform itself
    CHECK(g.blocked({7, 3}) && g.blocked({6, 2}));   // and the 250 m berth round it (111 m cells: the neighbours)
    CHECK(!g.blocked({6, 0}));                // but not far away
}

static void testHazardRestrictedAreas() {
    auto resare = [](double lat0, double lat1, double lon0, double lon1, uint32_t restrn, uint32_t catrea) {
        ChartFeature f = areaFeature("RESARE", lat0, lat1, lon0, lon1);
        f.restrn = restrn;
        f.catrea = catrea;
        return f;
    };
    // Areas that forbid entry. (A blocked polygon's outline is painted too, so a neighbour can be fringed by one cell: the safe
    // direction. Keep forbidden areas apart from the ones checked for staying open.)
    CostGrid forbidden = stampHazards({resare(45.990, 46.0, -124.000, -123.998, 1u << 7, 0),      // cols 0-1: entry prohibited (RESTRN 7)
                                       resare(45.990, 46.0, -123.994, -123.992, 1u << 7, 1u << 1),  // cols 6-7: offshore safety zone with entry prohibited
                                       resare(45.990, 46.0, -123.990, -123.988, 1u << 7, 1u << 14)});  // cols 10-11: minefield AND entry prohibited
    CHECK(forbidden.blocked({0, 5}) && forbidden.blocked({1, 5}));
    CHECK(forbidden.blocked({6, 5}) && forbidden.blocked({7, 5}));
    CHECK(forbidden.blocked({10, 5}) && forbidden.blocked({11, 5}));

    // A charted minefield alone is a caution, not a wall: in NOAA charts these are former minefields (Delaware Bay) whose text says
    // surface navigation is unrestricted and only anchoring, dredging and trawling are dangerous (RESTRN 2, 6 and 9 here).
    CostGrid former = stampHazards({resare(45.990, 46.0, -124.000, -123.996, (1u << 2) | (1u << 6) | (1u << 9), 1u << 14)});
    CHECK(!former.blocked({1, 5}) && former.cost({1, 5}) == 5.0f);

    // An area to be avoided (RESTRN 14) binds ships: blocked at 120 m, but only x20 for a 12 m boat. A military area (CATREA 9) is x30.
    auto avoid = [&](double length) {
        CostGrid g = makeGrid(12, 10);
        g.fill(kBlocked);
        ChartData d;
        d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        d.features.push_back(resare(45.990, 46.0, -124.000, -123.996, 1u << 14, 0));   // cols 0-3: area to be avoided
        d.features.push_back(resare(45.990, 46.0, -123.994, -123.990, 0, 1u << 9));    // cols 6-9: military area
        StampOptions o;
        o.vesselLengthM = length;
        stampChart(d, o, g);
        return g;
    };
    // A military area that limits only anchoring, fishing, trawling and dragging (RESTRN 1, 3, 5, 24: 33 CFR 334.360, Hampton Roads) can be
    // transited, so it costs little; one that restricts ENTRY, or lists nothing, keeps the full cost.
    auto military = [&](uint32_t restrn) { return stampHazards({resare(45.990, 46.0, -124.000, -123.996, restrn, 1u << 9)}); };
    CHECK(military((1u << 1) | (1u << 3) | (1u << 5) | (1u << 24)).cost({1, 5}) == 2.0f);
    CHECK(military(1u << 8).cost({1, 5}) == 30.0f);                       // entry restricted
    CHECK(military((1u << 1) | (1u << 8)).cost({1, 5}) == 30.0f);         // anchoring AND entry restricted
    CHECK(military(0).cost({1, 5}) == 30.0f);                             // nothing listed: assume the worst
    ChartFeature mip = areaFeature("MIPARE", 45.990, 46.0, -124.000, -123.996);
    mip.restrn = (1u << 1) | (1u << 3);
    CHECK(stampHazards({mip}).cost({1, 5}) == 2.0f);                      // military practice area listing only anchoring/fishing
    ChartFeature mip2 = areaFeature("MIPARE", 45.990, 46.0, -124.000, -123.996);
    CHECK(stampHazards({mip2}).cost({1, 5}) == 30.0f);
    ChartFeature scale = areaFeature("CTNARE", 45.990, 46.0, -124.000, -123.996);
    scale.inform = "Most features, including bathymetry, are omitted in this area. Mariners should use a more appropriate navigational purpose chart.";
    CHECK(stampHazards({scale}).cost({1, 5}) == 1.5f);                    // a chart-scale note, not a hazard
    ChartFeature real = areaFeature("CTNARE", 45.990, 46.0, -124.000, -123.996);
    real.inform = "Submerged pipeline: strong currents";
    CHECK(stampHazards({real}).cost({1, 5}) == 3.0f);
    CostGrid ship = avoid(120.0), boat = avoid(12.0);
    CHECK(ship.blocked({1, 5}));                              // a ship must avoid it
    CHECK(!boat.blocked({1, 5}) && boat.cost({1, 5}) == 20.0f);  // a small craft is only warned off
    CHECK(!ship.blocked({7, 5}) && ship.cost({7, 5}) == 30.0f);  // military area: dear, never a wall
    CHECK(boat.cost({7, 5}) == 30.0f);

    // Areas that only ask for care, or restrict something other than passing through.
    CostGrid soft = stampHazards({resare(45.990, 46.0, -124.000, -123.997, 1u << 1, 0),           // cols 0-2: anchoring prohibited only
                                  resare(45.990, 46.0, -123.996, -123.994, 1u << 8, 0),           // cols 4-5: entry restricted (8)
                                  resare(45.990, 46.0, -123.993, -123.991, 0, 1u << 18),          // cols 7-8: swimming area (CATREA 18)
                                  resare(45.990, 46.0, -123.990, -123.988, (1u << 3) | (1u << 13), 0)});  // cols 10-11: no fishing, no wake
    CHECK(!soft.blocked({1, 5}) && soft.cost({1, 5}) == 1.0f);  // a transit is fine
    CHECK(soft.cost({4, 5}) == 10.0f);                          // entry restricted: costly, not blocked
    CHECK(soft.cost({7, 5}) == 20.0f);                          // swimming area
    CHECK(!soft.blocked({10, 5}) && soft.cost({10, 5}) == 1.0f);

    // An offshore safety zone with entry only RESTRICTED (a security zone needing permission, as at the Battery and Long Beach) is
    // costly but never a wall: otherwise a harbour or a whole waterway could be cut off.
    CostGrid zone = stampHazards({resare(45.990, 46.0, -124.000, -123.996, 1u << 8, 1u << 1)});
    CHECK(!zone.blocked({1, 5}) && zone.cost({1, 5}) == 10.0f);

    // The same codes with chart text saying it is a Regulated Navigation Area (33 CFR 165) bind particular vessels, not a small craft:
    // only a light cost, so a crossing is not pushed miles off its line.
    ChartFeature rna = resare(45.990, 46.0, -124.000, -123.996, (1u << 4) | (1u << 8), 0);
    rna.inform = "Regulated navigation area, 33 CFR 165.1301 & 165.1303";
    CostGrid rnaGrid = stampHazards({rna});
    CHECK(!rnaGrid.blocked({1, 5}) && rnaGrid.cost({1, 5}) == 1.5f);
    ChartFeature security = resare(45.990, 46.0, -124.000, -123.996, 1u << 8, 0);
    security.inform = "Security zone, 33 CFR 165.1315";
    CHECK(stampHazards({security}).cost({1, 5}) == 10.0f);  // any other wording keeps the full cost
}

// Nature reserves and sanctuaries do not close the water; the chart's own wording decides military and security zones; and the areas a
// route pays to cross are recorded so they can be reported.
static void testRestrictedAreaTextAndNotes() {
    auto resare = [](uint32_t restrn, uint32_t catrea, const char* text) {
        ChartFeature f = areaFeature("RESARE", 45.990, 46.0, -124.000, -123.996);   // cols 0-3
        f.restrn = restrn;
        f.catrea = catrea;
        f.inform = text;
        return f;
    };
    // A whale sanctuary (CATREA 4 to 7, 22, 23) is passable and nearly free.
    CostGrid sanctuary = stampHazards({resare(1u << 16, 1u << 22, "Discharging prohibited, approaching within 100 yards of a humpback whale prohibited. 15 CFR 922")});
    CHECK(!sanctuary.blocked({1, 5}) && sanctuary.cost({1, 5}) == 1.2f);
    // Text that closes a military or security zone blocks it, whatever the codes say; the same codes without that text do not.
    CHECK(stampHazards({resare(1u << 8, 1u << 9, "The indicated area at Pearl Harbor is a Naval Defense Sea Area and is closed to the public.")}).blocked({1, 5}));
    CHECK(stampHazards({resare(1u << 8, 1u << 1, "United States Coast Guard, SECURITY ZONE - KEEP OUT, Vessels Not Authorized Entry Pursuant to 33 CFR Part 165.814 are Prohibited.")}).blocked({1, 5}));
    CHECK(!stampHazards({resare(1u << 8, 1u << 9, "Danger zone, 33 CFR 334.230")}).blocked({1, 5}));
    ChartFeature closedMip = areaFeature("MIPARE", 45.990, 46.0, -124.000, -123.996);
    closedMip.inform = "Restricted area, closed to the public";
    CHECK(stampHazards({closedMip}).blocked({1, 5}));
    // Wording that only asks for care while transiting takes a military area down from x30 to x2.
    const char* careText = "Naval Operating Area.  Vessels should use caution while transiting this area due to naval test operations";
    CHECK(stampHazards({resare(0, 1u << 9, careText)}).cost({1, 5}) == 2.0f);
    ChartFeature careMip = areaFeature("MIPARE", 45.990, 46.0, -124.000, -123.996);
    careMip.inform = careText;
    CHECK(stampHazards({careMip}).cost({1, 5}) == 2.0f);
    ChartFeature torpedo = areaFeature("MIPARE", 45.990, 46.0, -124.000, -123.996);   // Maui submarine practice areas
    torpedo.inform = "As submarines may be submerged in these areas, vessels should proceed with caution. During torpedo practice firing, all vessels are cautioned to keep well clear of Naval Target Vessels flying a large red flag at the highest masthead.";
    CHECK(stampHazards({torpedo}).cost({1, 5}) == 2.0f);
    CHECK(stampHazards({resare(0, 1u << 9, "")}).cost({1, 5}) == 30.0f);  // no wording, no relief

    // Notes: the costed area is recorded per cell with its wording; open cells and blocked ones carry none.
    auto stampNoted = [](std::vector<ChartFeature> extra, AreaLayer& layer, bool withDepth = true) {
        CostGrid g = makeGrid(12, 10);
        g.fill(kBlocked);
        ChartData d;
        if (withDepth) d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        for (auto& f : extra) d.features.push_back(std::move(f));
        StampOptions o;
        o.areas = &layer;
        stampChart(d, o, g);
        return g;
    };
    AreaLayer layer;
    stampNoted({resare(0, 1u << 9, careText)}, layer);
    CHECK(layer.notes.size() == 1);
    CHECK(layer.notes[0].kind == "military area" && layer.notes[0].factor == 2.0f);
    CHECK(layer.notes[0].text.find("Naval Operating Area") == 0);
    CHECK(layer.id[5 * 12 + 1] == 0);
    CHECK(layer.id[5 * 12 + 8] == -1);             // outside the area
    // A finer chart that covers the same water and draws no such area replaces it, as it replaces the cost (overview-chart areas are rough).
    stampNoted({}, layer);
    CHECK(layer.id[5 * 12 + 1] == -1);
    stampNoted({resare(0, 1u << 9, careText)}, layer);
    stampNoted({resare(0, 1u << 9, careText)}, layer);
    CHECK(layer.notes.size() == 1 && layer.id[5 * 12 + 1] == 0);
    // A closed area is noted too (an --eval route that crosses one should say so).
    AreaLayer closed;
    stampNoted({resare(1u << 7, 0, "")}, closed);
    CHECK(closed.id[5 * 12 + 1] >= 0 && closed.notes[closed.id[5 * 12 + 1]].kind.find("entry prohibited") == 0);
}

// A finer chart re-draws the depth areas but not every hazard: a wreck, an obstruction or a structure that only the coarser chart carries
// must survive it. Areas are different: an overview chart draws restricted, military and unsurveyed areas roughly (a Golden Gate security
// zone spans the whole strait), so the covering chart's own drawing replaces them.
static void testHazardsSurviveFinerChart() {
    CostGrid g = makeGrid(12, 10);
    g.fill(kBlocked);
    ChartData coarse, fine;
    coarse.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
    ChartFeature wreck = pointFeature("WRECKS", {9, 3}, g, 1.0);   // 1 m over it, on a cell of its own
    ChartFeature banned = areaFeature("RESARE", 45.990, 46.0, -123.994, -123.992);          // cols 6-7
    banned.restrn = 1u << 7;
    ChartFeature military = areaFeature("MIPARE", 45.990, 46.0, -123.990, -123.988);        // cols 10-11
    ChartFeature unsurveyed = areaFeature("UNSARE", 45.990, 46.0, -123.998, -123.996);      // cols 2-3, rows all
    ChartFeature overview = areaFeature("CTNARE", 45.990, 45.992, -124.0, -123.996);        // cols 0-3, rows 8-9
    overview.inform = "Most features, including bathymetry, are omitted in this area. Mariners should use a more appropriate navigational purpose chart.";
    coarse.features.push_back(wreck);
    coarse.features.push_back(banned);
    coarse.features.push_back(military);
    coarse.features.push_back(unsurveyed);
    coarse.features.push_back(overview);
    fine.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));   // the same water, nothing else drawn
    StampOptions o;
    stampChart(coarse, o, g);
    CHECK(g.blocked({9, 3}) && g.blocked({2, 5}) && g.blocked({6, 5}) && g.cost({10, 5}) == 30.0f && g.cost({0, 8}) == 1.5f);
    stampChart(fine, o, g);
    CHECK(g.blocked({9, 3}));                        // the wreck survives
    CHECK(!g.blocked({6, 5}) && g.cost({6, 5}) == 1.0f);   // an overview chart's prohibited area does not wall off what the finer chart shows
    CHECK(g.cost({10, 5}) == 1.0f);                  // nor does its military area cost carry on
    CHECK(!g.blocked({3, 5}));                       // the finer chart has surveyed what the coarse one called unsurveyed
    CHECK(g.cost({0, 8}) == 1.0f);                   // and the overview-chart note is gone
}

// Bridges: an overview chart's bridge with no charted clearance does not outlive a finer chart; a charted low clearance does.
static void testBridgeAcrossScales() {
    auto stamp2 = [](double clearance) {
        CostGrid g = makeGrid(12, 10);
        g.fill(kBlocked);
        ChartData coarse, fine;
        coarse.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        ChartFeature bridge = lineFeature("BRIDGE", {{45.9954, -123.9955}, {45.9954, -123.9895}});   // row 4
        bridge.verclr = clearance;
        coarse.features.push_back(bridge);
        fine.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        StampOptions o;
        o.airDraftM = 5.0;
        stampChart(coarse, o, g);
        const bool blockedByCoarse = g.blocked({6, 4});
        stampChart(fine, o, g);
        return std::make_pair(blockedByCoarse, g.blocked({6, 4}));
    };
    const auto unknown = stamp2(std::nan(""));
    CHECK(unknown.first && !unknown.second);   // unknown clearance blocks its own chart, the finer chart decides
    const auto low = stamp2(3.0);
    CHECK(low.first && low.second);            // a charted clearance under the air draft holds
    const auto high = stamp2(30.0);
    CHECK(!high.first && !high.second);
}

// Land or a shoal narrower than a cell must still block: a cell centre test alone lets a mole or a spit vanish.
static void testThinPolygonsStillBlock() {
    CostGrid g = makeGrid(12, 10);
    g.fill(kBlocked);
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
    // 0.00002 degrees is about 2 m: a breakwater far thinner than a 111 m cell, running through row 4 between cell centres.
    d.features.push_back(areaFeature("LNDARE", 45.99590, 45.99592, -123.998, -123.992));
    // A shoal strip the same way, in row 7, drawn as a shallow depth area.
    d.features.push_back(areaFeature("DEPARE", 45.99290, 45.99292, -123.998, -123.992, 0.5));
    StampOptions o;
    stampChart(d, o, g);
    bool land = false, shoal = false;
    for (int col = 2; col <= 8; ++col) {
        land = land || g.blocked({col, 4}) || g.blocked({col, 3});
        shoal = shoal || g.blocked({col, 7}) || g.blocked({col, 6});
    }
    CHECK(land);
    CHECK(shoal);
    // A normal coast is not thickened: a polygon several cells wide keeps its open water right up to its edge.
    CostGrid h = makeGrid(12, 10);
    h.fill(kBlocked);
    ChartData e;
    e.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
    e.features.push_back(areaFeature("LNDARE", 45.990, 46.0, -124.0, -123.995));   // cols 0-4
    stampChart(e, o, h);
    CHECK(h.blocked({4, 5}) && !h.blocked({5, 5}) && !h.blocked({6, 5}));
}

// A dam across the water with a navigation lock through it: the lock basin is open, the gates at its ends open, and everything else about
// the dam (and any gate that is not a lock's) stays a wall. The chamber is narrower than a cell, as a real one is at 30 m cells.
static void testNavigationLock() {
    CostGrid grid = makeGrid(12, 10);
    grid.fill(kBlocked);
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
    d.features.push_back(areaFeature("LNDARE", 45.990, 46.0, -123.9940, -123.9930));                    // the dam: land across column 6
    const LatLon mid = grid.centre({6, 4});
    // Lock chamber: 3 cells long, 20 m wide, through the dam on row 4. Its walls are shoreline construction lines.
    ChartFeature basin;
    basin.objectClass = "LOKBSN";
    basin.geometry = Geometry::Area;
    const double w = 0.00009, l = 0.0025;   // 10 m either side, 280 m long
    basin.parts.push_back({{{mid.lat + w, mid.lon - l / 2}, {mid.lat + w, mid.lon + l / 2}, {mid.lat - w, mid.lon + l / 2}, {mid.lat - w, mid.lon - l / 2}}, true});
    d.features.push_back(basin);
    d.features.push_back(lineFeature("SLCONS", {{mid.lat + w, mid.lon - l / 2}, {mid.lat + w, mid.lon + l / 2}}));
    d.features.push_back(lineFeature("SLCONS", {{mid.lat - w, mid.lon - l / 2}, {mid.lat - w, mid.lon + l / 2}}));
    // A gate at each end, just outside the chamber's own cells (the chart draws them across the entrances).
    d.features.push_back(lineFeature("GATCON", {{mid.lat + 0.0003, mid.lon - l / 2 - 0.0009}, {mid.lat - 0.0003, mid.lon - l / 2 - 0.0009}}));
    d.features.push_back(lineFeature("GATCON", {{mid.lat + 0.0003, mid.lon + l / 2 + 0.0009}, {mid.lat - 0.0003, mid.lon + l / 2 + 0.0009}}));
    d.features.push_back(lineFeature("GATCON", {grid.centre({2, 8}), grid.centre({2, 9})}));           // a flood gate far from any lock
    // The gate walkway at the chamber's east end is charted as a bridge with no clearance; a bridge elsewhere in the dam is a wall.
    d.features.push_back(lineFeature("BRIDGE", {{mid.lat + 0.0003, mid.lon + l / 2 + 0.0004}, {mid.lat - 0.0003, mid.lon + l / 2 + 0.0004}}));
    d.features.push_back(lineFeature("BRIDGE", {grid.centre({9, 8}), grid.centre({9, 9})}));
    AreaLayer areas;
    StampOptions o;
    o.areas = &areas;
    stampChart(d, o, grid);
    CHECK(!grid.blocked({5, 4}) && !grid.blocked({6, 4}) && !grid.blocked({7, 4}));   // the chamber and the dam cell it passes through
    CHECK(grid.blocked({6, 1}) && grid.blocked({6, 8}));                              // the rest of the dam
    CHECK(grid.blocked({2, 8}));                                                      // a gate that is no lock's
    CHECK(grid.blocked({9, 8}));                                                      // nor is a bridge away from the lock part of it
    CHECK(!findRoute(grid, grid.centre({1, 4}), grid.centre({10, 4})).empty());       // through the lock
    bool noted = false;
    for (const AreaNote& n : areas.notes) noted = noted || n.kind.find("navigation lock") == 0;
    CHECK(noted);
    // With the hazard rules off (developer comparison switch) the lock is not read, so the dam is a wall with no way through.
    CostGrid off = makeGrid(12, 10);
    off.fill(kBlocked);
    StampOptions no;
    no.hazardObjects = false;
    stampChart(d, no, off);
    CHECK(off.blocked({6, 4}));
}

// A lock's approach is a corridor on the line of the chamber. Once every chart is stamped it is passable through whatever thin walls, cables
// and shoal-to-the-bank depth areas seal it at chart resolution; land, and everything off the line or beyond its reach, stays shut.
static void testLockCorridor() {
    CostGrid g = makeGrid(30, 10);   // cells of about 111 m by 78 m
    g.fill(kBlocked);
    ChartData d;
    d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.970, 0.5));         // shoal everywhere: nothing is open
    const LatLon mid = g.centre({12, 5});
    ChartFeature basin;
    basin.objectClass = "LOKBSN";
    basin.geometry = Geometry::Area;
    const double lw = 0.0002, ll = 0.0012;   // 22 m half-width, 94 m half-length: long axis east-west
    basin.parts.push_back({{{mid.lat + lw, mid.lon - ll}, {mid.lat + lw, mid.lon + ll}, {mid.lat - lw, mid.lon + ll}, {mid.lat - lw, mid.lon - ll}}, true});
    d.features.push_back(basin);
    d.features.push_back(lineFeature("SLCONS", {g.centre({9, 2}), g.centre({9, 8})}));         // a guide wall across the approach line
    d.features.push_back(areaFeature("LNDARE", 45.9940, 45.9950, -123.9930, -123.9910));         // a land cell on the line (col 8, row 5)
    // Hazards on the approach line that must survive the corridor: a shallow wreck (col 10), a bridge charted lower than the mast (col 14, air
    // draft 5 m), and an unsurveyed area (col 16).
    d.features.push_back(pointFeature("WRECKS", {10, 5}, g, 1.0));
    ChartFeature lowBridge = lineFeature("BRIDGE", {g.centre({14, 3}), g.centre({14, 7})});
    lowBridge.verclr = 3.0;
    d.features.push_back(lowBridge);
    d.features.push_back(areaFeature("UNSARE", 45.9945, 45.9955, -123.9845, -123.9835));       // col 15-16, row 5
    StampOptions o;
    o.airDraftM = 5.0;
    stampChart(d, o, g);
    CHECK(g.blocked({9, 5}) && g.blocked({14, 8}));     // before the pass: the approach is a wall of shoal and pier
    g.openLockCorridors();
    CHECK(!g.blocked({12, 5}));                         // the chamber
    CHECK(!g.blocked({9, 5}) && g.cost({9, 5}) == 5.0f);  // the guide wall and the shoal on the line
    CHECK(g.blocked({10, 5}));                          // a charted wreck on the line is not an artefact: it stays shut
    CHECK(g.blocked({14, 5}));                          // nor a bridge charted lower than the mast
    CHECK(g.blocked({15, 5}) || g.blocked({16, 5}));    // nor an unsurveyed area
    CHECK(!g.blocked({17, 5}));                         // but the shoal beyond them, still in reach, opens
    CHECK(g.blocked({8, 5}));                           // land stays land, though it lies on the line
    CHECK(g.blocked({12, 4}) && g.blocked({12, 6}) && g.blocked({10, 4}));  // off the line, a cell to either side
    CHECK(g.blocked({22, 5}));                          // beyond reach
}

// The search reports progress that never goes backwards and can be cancelled.
static void testSearchProgressAndCancel() {
    CostGrid g = makeGrid(400, 300);   // 120,000 cells of open water
    g.fill(1.0f);
    std::vector<double> seen;
    std::function<bool(double)> record = [&](double f) { seen.push_back(f); return true; };
    const auto route = findRoute(g, g.centre({2, 2}), g.centre({396, 296}), 0.05, nullptr, 0.0, &record);
    CHECK(!route.empty());
    CHECK(!seen.empty());
    for (size_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i] >= 0.0 && seen[i] <= 1.0);
        if (i > 0) CHECK(seen[i] >= seen[i - 1]);
    }
    int calls = 0;
    std::function<bool(double)> cancelSecond = [&](double) { return ++calls < 2; };   // says stop on its second look
    CHECK(findRoute(g, g.centre({2, 2}), g.centre({396, 296}), 0.05, nullptr, 0.0, &cancelSecond).empty());
    CHECK(calls == 2);
}

// A route's cell size stays fine for a short trip and coarsens (in whole 10 m steps) so a long passage stays within the memory budget.
static void testSuggestedCellSize() {
    CHECK(suggestedCellM({47.6, -122.4}, {48.1, -122.7}) == 30.0);                    // Seattle to Port Townsend
    const double big = suggestedCellM({21.31, -157.87}, {20.79, -156.51}, 30.0, 1e6);  // Honolulu to Maui with a small budget
    CHECK(big > 30.0 && std::fmod(big, 10.0) == 0.0);
    const double height = (0.52 + 2 * std::max(0.03, 0.25 * 1.36)) * 111320.0, width = (1.36 + 2 * std::max(0.03, 0.25 * 1.36)) * 111320.0 * std::cos(21.05 * 3.14159265 / 180.0);
    CHECK(width * height / (big * big) <= 1e6 * 1.001);                                // fits the budget
    CHECK(width * height / ((big - 10.0) * (big - 10.0)) > 1e6);                       // and it is the smallest step that does
}

// The catalogue that ships with NOAA's ENC_ROOT gives each cell's extent, so a route need not open cells it cannot touch.
static void testEncCatalog() {
    const std::string us = "\x1f";
    const std::string rec1 = "CD0000000002US1EEZ1M\\US1EEZ1A.TXT" + us + us + "V01X01" + us + "TXT" + us + us + us + us + us + us;   // a text file: ignored
    const std::string rec2 = "CD0000000003US5WA3CJ\\US5WA3CJ.000" + us + "Columbia River" + us + "V01X01" + us + "BIN45.600000" + us + "-121.950000" + us +
                             "45.680000" + us + "-121.880000" + us + "ABCD" + us + us;
    const std::string rec3 = "CD0000000004US2PACWY\\US2PACWY.000" + us + us + "V01X01" + us + "BIN43.2" + us + "-124.7" + us + "48.0" + us + "-120.5" + us + us + us;
    const std::string leader = "00119 D     00053   550400010000600000CATD0006000006";
    const std::string bytes = leader + "\x1e" + rec1 + "\x1e" + leader + "\x1e" + rec2 + "\x1e" + rec3 + "\x1e";
    const auto cat = parseEncCatalog(bytes);
    CHECK(cat.size() == 2);
    CHECK(cat.count("US5WA3CJ") == 1 && cat.count("US1EEZ1A") == 0);
    const CellExtent& e = cat.at("US5WA3CJ");
    CHECK(std::fabs(e.south - 45.6) < 1e-9 && std::fabs(e.west + 121.95) < 1e-9 && std::fabs(e.north - 45.68) < 1e-9 && std::fabs(e.east + 121.88) < 1e-9);
    CHECK(std::fabs(cat.at("US2PACWY").north - 48.0) < 1e-9);
    CHECK(parseEncCatalog("").empty());
    CHECK(parseEncCatalog("not a catalogue at all").empty());
}

// Stamping several charts with one shared scratch, which only clears what each chart touched, gives exactly the grid that a fresh scratch per
// chart gives, and a chart that covers only a corner of a big grid still stamps correctly.
static void testSharedScratchStamping() {
    auto build = [](bool shared) {
        CostGrid g = makeGrid(40, 30);
        g.fill(kBlocked);
        ChartData coarse, fine1, fine2;
        coarse.features.push_back(areaFeature("DEPARE", 45.970, 46.0, -124.0, -123.960, 30.0));
        ChartFeature banned = areaFeature("RESARE", 45.975, 45.985, -123.995, -123.985);
        banned.restrn = 1u << 7;
        coarse.features.push_back(banned);
        fine1.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.990, 1.0));                 // top-left corner only: shoal
        fine1.features.push_back(pointFeature("WRECKS", {3, 3}, g, 1.0));
        fine2.features.push_back(areaFeature("DEPARE", 45.975, 45.985, -123.975, -123.965, 30.0));            // lower right patch: deep
        ChartFeature military = areaFeature("MIPARE", 45.976, 45.984, -123.974, -123.966);
        fine2.features.push_back(military);
        StampScratch sc;
        AreaLayer areas;
        for (ChartData* d : {&coarse, &fine1, &fine2}) {
            StampOptions o;
            o.areas = &areas;
            if (shared) o.scratch = &sc;
            stampChart(*d, o, g);
        }
        return g;
    };
    // The scratch is left exactly as it was found (all zero, penalty 1, notes -1), so the next chart cannot inherit anything from this one.
    {
        CostGrid g = makeGrid(40, 30);
        g.fill(kBlocked);
        ChartData d;
        d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.990, 30.0));
        d.features.push_back(pointFeature("WRECKS", {3, 3}, g, 1.0));
        ChartFeature banned = areaFeature("RESARE", 45.992, 45.998, -123.998, -123.992);
        banned.restrn = 1u << 7;
        d.features.push_back(banned);
        ChartFeature mil = areaFeature("MIPARE", 45.990, 45.994, -123.998, -123.992);
        d.features.push_back(mil);
        StampScratch sc;
        AreaLayer areas;
        StampOptions o;
        o.areas = &areas;
        o.scratch = &sc;
        stampChart(d, o, g);
        bool clean = sc.n == 40u * 30u;
        for (auto* v : {&sc.state, &sc.covered, &sc.land, &sc.zone, &sc.caution, &sc.channel, &sc.shut, &sc.shutLocal, &sc.lock, &sc.lockNear}) {
            for (uint8_t x : *v) clean = clean && x == 0;
        }
        for (float x : sc.penalty) clean = clean && x == 1.0f;
        for (int32_t x : sc.sinkAt) clean = clean && x == -1;
        CHECK(clean);
    }
    CostGrid a = build(true), b = build(false);
    bool same = true;
    for (int row = 0; row < 30; ++row) {
        for (int col = 0; col < 40; ++col) {
            const float ca = a.cost({col, row}), cb = b.cost({col, row});
            same = same && (ca == cb || (std::isinf(ca) && std::isinf(cb)));
        }
    }
    CHECK(same);
    CHECK(a.blocked({3, 3}) && a.blocked({5, 2}));           // the wreck and the shoal corner
    CHECK(!a.blocked({32, 20}) && a.cost({32, 20}) == 30.0f);  // the deep patch under a military area
    CHECK(!a.blocked({20, 25}));                             // open water the coarse chart supplies, away from both fine charts
}

// Cells are found under the folder however deep the catalogue is: the folder given may be ENC_ROOT or one above it.
static void testFindEncCells() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "oar_test_enc_tree";
    fs::remove_all(root);
    fs::create_directories(root / "ENC_ROOT" / "US5WA3CJ");
    fs::create_directories(root / "ENC_ROOT" / "US5FL1XX");
    fs::create_directories(root / "ENC_ROOT" / "US5ZZ9NO");
    for (const char* n : {"US5WA3CJ", "US5FL1XX", "US5ZZ9NO"}) std::ofstream(root / "ENC_ROOT" / n / (std::string(n) + ".000")) << "x";
    const std::string us = "\x1f", leader = "00119 D     00053   550400010000600000CATD0006000006";
    auto rec = [&](int id, const char* n, const char* s, const char* w, const char* nn, const char* e) {
        return std::string("CD") + (id < 10 ? "000000000" : "00000000") + std::to_string(id) + n + "\\" + n + ".000" + us + us + "V01X01" + us + "BIN" + s + us + w + us + nn + us + e + us + us + us;
    };
    std::ofstream(root / "ENC_ROOT" / "CATALOG.031") << leader << "\x1e" << rec(2, "US5WA3CJ", "45.6", "-121.95", "45.68", "-121.88") << "\x1e" << leader << "\x1e"
                                                     << rec(3, "US5FL1XX", "25.0", "-81.0", "25.5", "-80.5") << "\x1e";   // US5ZZ9NO is not listed
    const auto near = findEncCells(root.string(), 45.62, 45.66, -121.93, -121.90);        // the folder ABOVE ENC_ROOT
    std::vector<std::string> names;
    for (const auto& c : near) names.push_back(c.first);
    std::sort(names.begin(), names.end());
    CHECK(names.size() == 2 && names[0] == "US5WA3CJ" && names[1] == "US5ZZ9NO");        // the far cell is skipped; an unlisted one is kept
    CHECK(findEncCells(root.string(), 45.62, 45.66, -121.93, -121.90, false).size() == 3);   // without the catalogue every cell stays
    CHECK(findEncCells((root / "ENC_ROOT").string(), 25.1, 25.3, -80.9, -80.6).size() == 2);  // the ENC_ROOT folder itself works too
    fs::remove_all(root);
}

// Review findings on land: one bad ring does not drop a whole polygon, a thin part joined to a big polygon still blocks, and land drawn as
// a point or a line blocks its cells.
static void testLandRasterisation() {
    // A big land polygon with a hole ring of only two points (a truncated fragment): the polygon is still painted.
    {
        CostGrid g = makeGrid(12, 10);
        g.fill(kBlocked);
        ChartData d;
        d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        ChartFeature land = areaFeature("LNDARE", 45.992, 45.998, -123.998, -123.990);
        land.parts.push_back({{{45.995, -123.995}, {45.994, -123.994}}, true});   // degenerate ring
        d.features.push_back(land);
        StampOptions o;
        stampChart(d, o, g);
        CHECK(g.blocked({4, 4}));   // inside the polygon
    }
    // A mole 11 m wide and 880 m long joined to a big land body: the polygon as a whole is thick (so it is not "thin"), and the mole runs
    // between the rows of cell centres, so only painting the outline stops it vanishing.
    {
        CostGrid g = makeGrid(24, 16);
        g.fill(kBlocked);
        ChartData d;
        d.features.push_back(areaFeature("DEPARE", 45.984, 46.0, -124.0, -123.976, 30.0));
        ChartFeature land;
        land.objectClass = "LNDARE";
        land.geometry = Geometry::Area;
        land.parts.push_back({{{45.995, -124.0}, {45.995, -123.990}, {45.99005, -123.990}, {45.99005, -123.982}, {45.98995, -123.982}, {45.98995, -123.990},
                               {45.985, -123.990}, {45.985, -124.0}}, true});
        d.features.push_back(land);
        StampOptions o;
        stampChart(d, o, g);
        bool moleBlocked = false;
        for (int col = 11; col <= 16; ++col) moleBlocked = moleBlocked || g.blocked({col, 9}) || g.blocked({col, 10});
        CHECK(moleBlocked);
        CHECK(!g.blocked({20, 2}));   // open water elsewhere
    }
    // An islet drawn as a point, and a land line, block their cells (hazard rules on).
    {
        CostGrid g = makeGrid(12, 10);
        g.fill(kBlocked);
        ChartData d;
        d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        d.features.push_back(pointFeature("LNDARE", {6, 5}, g, 0.0));
        d.features.push_back(lineFeature("LNDARE", {g.centre({2, 2}), g.centre({4, 2})}));
        StampOptions o;
        stampChart(d, o, g);
        CHECK(g.blocked({6, 5}));
        CHECK(g.blocked({3, 2}));
        CHECK(!g.blocked({9, 8}));
    }
}

// A request with a non-finite number is refused up front (status 2), before any chart is opened.
static void testPlannerRejectsBadNumbers() {
    PlanRequest good;
    good.encDirs = {"/nonexistent/enc"};
    good.from = {47.6, -122.4};
    good.to = {48.1, -122.7};
    PlanHooks quiet;
    CHECK(planRoute(good, quiet).status != 2);   // (it fails later: no charts there, not a bad request)
    for (int variant = 0; variant < 6; ++variant) {
        PlanRequest bad = good;
        const double nan = std::nan(""), inf = std::numeric_limits<double>::infinity();
        if (variant == 0) bad.cellM = nan;
        if (variant == 1) bad.cellM = inf;
        if (variant == 2) bad.draftM = nan;
        if (variant == 3) bad.clearanceM = inf;
        if (variant == 4) bad.from.lat = nan;
        if (variant == 5) bad.lengthM = 0.0;
        const PlanResult r = planRoute(bad, quiet);
        CHECK(r.status == 2 && r.failReason == "bad_request");
    }
}

// A wreck recorded only on a coarser chart must not be reopened by a lock corridor that a finer chart supplies: the finer chart's own
// (empty) verdict for that cell must not erase the physical hazard.
static void testLockCorridorKeepsCoarseChartHazard() {
    CostGrid g = makeGrid(30, 10);
    g.fill(kBlocked);
    ChartData coarse, fine;
    coarse.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.970, 0.5));   // shoal everywhere
    coarse.features.push_back(pointFeature("WRECKS", {10, 5}, g, 1.0));                     // a wreck only the coarse chart carries
    fine.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.970, 0.5));      // the same water, no wreck drawn
    const LatLon mid = g.centre({12, 5});
    ChartFeature basin;
    basin.objectClass = "LOKBSN";
    basin.geometry = Geometry::Area;
    basin.parts.push_back({{{mid.lat + 0.0002, mid.lon - 0.0012}, {mid.lat + 0.0002, mid.lon + 0.0012}, {mid.lat - 0.0002, mid.lon + 0.0012}, {mid.lat - 0.0002, mid.lon - 0.0012}}, true});
    fine.features.push_back(basin);
    StampOptions o;
    stampChart(coarse, o, g);
    stampChart(fine, o, g);
    g.openLockCorridors();
    CHECK(g.blocked({10, 5}));      // the wreck stays shut
    CHECK(!g.blocked({12, 5}));     // the chamber itself opens
    CHECK(!g.blocked({9, 5}));      // and the shoal beside the wreck, on the approach line
}

static void testHazardPenalties() {
    ChartFeature military = areaFeature("MIPARE", 45.996, 46.0, -124.000, -123.996);   // cols 0-3, rows 0-3: heavily costly, never a wall
    ChartFeature caution = areaFeature("CTNARE", 45.990, 45.994, -124.000, -123.996);   // cols 0-3, rows 6-9
    ChartFeature dumping = areaFeature("DMPGRD", 45.990, 45.994, -123.996, -123.992);   // cols 4-7 (spoil ground by default)
    dumping.catdpg = 1u << 5;
    ChartFeature anchorage = areaFeature("ACHARE", 45.990, 45.994, -123.992, -123.990); // cols 8-9
    ChartFeature kelp = areaFeature("WEDKLP", 45.990, 45.994, -123.990, -123.988);      // cols 10-11
    ChartFeature overlap = areaFeature("ACHARE", 45.990, 45.994, -124.000, -123.996);   // over the caution area
    CostGrid g = stampHazards({military, caution, dumping, anchorage, kelp, overlap});
    CHECK(!g.blocked({1, 1}) && g.cost({1, 1}) == 30.0f);  // a military practice area is dear, not a wall
    CHECK(g.cost({1, 8}) == 3.0f);    // caution area, and the anchorage on top of it does not compound: the larger factor wins
    CHECK(g.cost({5, 8}) == 15.0f);   // spoil ground
    ChartFeature explosives = areaFeature("DMPGRD", 45.990, 45.994, -123.994, -123.992);
    explosives.catdpg = 1u << 4;      // explosives dumping ground: never entered
    CHECK(stampHazards({explosives}).blocked({6, 8}));
    ChartFeature chemical = areaFeature("DMPGRD", 45.990, 45.994, -123.994, -123.992);
    chemical.catdpg = 1u << 2;
    CHECK(stampHazards({chemical}).blocked({6, 8}));
    CHECK(g.cost({8, 8}) == 3.0f);    // anchorage
    CHECK(g.cost({10, 8}) == 1.5f);   // kelp
    CHECK(g.cost({5, 1}) == 1.0f);    // elsewhere untouched
}

static void testHazardMarks() {
    CostGrid ref = makeGrid(12, 10);
    ChartFeature isolated = pointFeature("BOYISD", {2, 5}, ref, 0.0);
    ChartFeature north = pointFeature("BOYCAR", {8, 5}, ref, 0.0);
    north.catcam = 1.0;  // north cardinal: safe water lies north, so the danger side is south
    ChartFeature west = pointFeature("BCNCAR", {5, 2}, ref, 0.0);
    west.catcam = 4.0;   // west cardinal: danger lies to the east
    CostGrid g = stampHazards({isolated, north, west});
    CHECK(g.blocked({2, 5}));                      // isolated danger
    CHECK(!g.blocked({2, 3}) && !g.blocked({4, 5}));  // but only its own 100 m
    CHECK(g.blocked({8, 5}) && g.blocked({8, 6}));                       // north mark: the south side is blocked (150 m: the next cell)
    CHECK(!g.blocked({8, 4}) && !g.blocked({8, 3}));                     // the north side stays open
    CHECK(g.blocked({6, 2}));                      // west mark: east side blocked
    CHECK(!g.blocked({4, 2}));                     // west side open
}

static void testHazardOverheadClearance() {
    auto bridgeAt = [](double clearance, double closed) {
        ChartFeature b = lineFeature("BRIDGE", {{45.9955, -123.9995}, {45.9955, -123.9885}});   // a span across the whole grid, row 4
        b.verclr = clearance;
        b.verccl = closed;
        return b;
    };
    const double nan = std::nan("");
    // Fixed bridge, 10 m clearance: needs air draft plus 1 m, so a 8.9 m mast passes and a 9.5 m mast does not.
    CHECK(!stampHazards({bridgeAt(10.0, nan)}, 8.9).blocked({5, 4}));
    CHECK(stampHazards({bridgeAt(10.0, nan)}, 9.5).blocked({5, 4}));
    // An opening bridge is treated as closed: 30 m open but 5 m closed blocks an 8 m mast.
    CHECK(stampHazards({bridgeAt(30.0, 5.0)}, 8.0).blocked({5, 4}));
    // Unknown clearance is unsafe, whatever the mast.
    CHECK(stampHazards({bridgeAt(nan, nan)}, 0.0).blocked({5, 4}));
    // ...but an overhead cable with no charted clearance counts as infinitely high: free to cross, not a wall (see applyHazardObjects).
    ChartFeature unknownCable = lineFeature("CBLOHD", {{45.9955, -123.9995}, {45.9955, -123.9885}});
    CostGrid cableGrid = stampHazards({unknownCable}, 5.0);
    CHECK(!cableGrid.blocked({5, 4}) && cableGrid.cost({5, 4}) == 1.0f);
    {   // and it is still reported, at no cost
        CostGrid g = makeGrid(12, 10);
        g.fill(kBlocked);
        ChartData d;
        d.features.push_back(areaFeature("DEPARE", 45.990, 46.0, -124.0, -123.988, 30.0));
        d.features.push_back(unknownCable);
        AreaLayer areas;
        StampOptions o;
        o.areas = &areas;
        stampChart(d, o, g);
        bool noted = false;
        for (const AreaNote& n : areas.notes) noted = noted || n.kind.find("overhead cable") == 0;
        CHECK(noted && areas.id[4 * 12 + 5] >= 0 && g.cost({5, 4}) == 1.0f);
    }
    ChartFeature unknownPipe = lineFeature("PIPOHD", {{45.9955, -123.9995}, {45.9955, -123.9885}});
    CHECK(stampHazards({unknownPipe}, 5.0).blocked({5, 4}));
    // Overhead cables and pipelines follow the same rule.
    ChartFeature cable = lineFeature("CBLOHD", {{45.9955, -123.9995}, {45.9955, -123.9885}});
    cable.verclr = 12.0;
    CHECK(!stampHazards({cable}, 5.0).blocked({5, 4}));
    CHECK(stampHazards({cable}, 12.0).blocked({5, 4}));
}

static void testS57MissingFile() {
    ChartData d;
    std::string err;
    CHECK(!loadS57("nonexistent.000", d, err));
    CHECK(!err.empty());
}

// --- hand-built ISO 8211 / S-57 records --------------------------------------------------------------------------

using Bytes = std::vector<uint8_t>;

static void put32(Bytes& b, int32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> (8 * i)) & 0xFF));
}
static void put16(Bytes& b, int v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
static std::string pad(size_t v, int w) {
    std::string s = std::to_string(v);
    return std::string(w - s.size(), '0') + s;
}

struct F { std::string tag; Bytes data; };

static Bytes makeRecord(char leaderId, const std::vector<F>& fields) {
    std::string dir;
    Bytes body;
    for (const F& f : fields) {
        dir += f.tag + pad(f.data.size() + 1, 3) + pad(body.size(), 4);
        body.insert(body.end(), f.data.begin(), f.data.end());
        body.push_back(0x1E);
    }
    const size_t base = 24 + dir.size() + 1;
    std::string head = pad(base + body.size(), 5) + "3" + leaderId + "E1 " + "09" + pad(base, 5) + " ! " + "3404";
    Bytes out(head.begin(), head.end());
    out.insert(out.end(), dir.begin(), dir.end());
    out.push_back(0x1E);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static Bytes vrid(int rcnm, int rcid) {
    Bytes b{static_cast<uint8_t>(rcnm)};
    put32(b, rcid);
    put16(b, 1);
    b.push_back(1);
    return b;
}
static Bytes sg2d(std::vector<std::pair<int, int>> yx) {
    Bytes b;
    for (auto& p : yx) { put32(b, p.first); put32(b, p.second); }
    return b;
}
static Bytes vrpt(int beginId, int endId) {
    Bytes b;
    for (auto [id, topi] : {std::pair<int, int>{beginId, 1}, {endId, 2}}) {
        b.push_back(120);
        put32(b, id);
        b.push_back(255); b.push_back(255); b.push_back(topi); b.push_back(255);
    }
    return b;
}
static Bytes frid(int prim, int objl) {
    Bytes b{100};
    put32(b, 1);
    b.push_back(prim); b.push_back(1);
    put16(b, objl); put16(b, 1);
    b.push_back(1);
    return b;
}
static Bytes fspt(std::vector<std::array<int, 3>> refs) {  // {edge id, orientation, usage}
    Bytes b;
    for (auto& r : refs) { b.push_back(130); put32(b, r[0]); b.push_back(r[1]); b.push_back(r[2]); b.push_back(2); }
    return b;
}
static Bytes attf(int code, const std::string& text) {
    Bytes b;
    put16(b, code);
    b.insert(b.end(), text.begin(), text.end());
    b.push_back(0x1F);
    return b;
}

static Bytes buildCell() {
    const int k = 10000000;  // COMF
    Bytes cell;
    auto add = [&](const Bytes& r) { cell.insert(cell.end(), r.begin(), r.end()); };
    Bytes dspm(24, 0);
    for (int i = 0; i < 4; ++i) { dspm[16 + i] = (k >> (8 * i)) & 0xFF; }
    dspm[20] = 10;  // SOMF
    add(makeRecord('L', {{"0000", Bytes{'x'}}}));  // stand-in descriptive record
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"DSPM", dspm}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(120, 1)}, {"SG2D", sg2d({{460000000, -1240000000}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(120, 2)}, {"SG2D", sg2d({{461000000, -1239000000}})}}));
    // edge 10: node1 -> (46.0,-123.9) -> node2;  edge 11 is stored node1 -> (46.1,-124.0) -> node2 (so it runs backwards)
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(130, 10)}, {"VRPT", vrpt(1, 2)},
                         {"SG2D", sg2d({{460000000, -1239000000}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(130, 11)}, {"VRPT", vrpt(1, 2)},
                         {"SG2D", sg2d({{461000000, -1240000000}})}}));
    Bytes sounding;  // isolated node with two SG3D soundings: (46.05,-123.95) 12.3 m and (46.06,-123.95) 4.0 m
    for (auto t : {std::array<int, 3>{460500000, -1239500000, 123}, {460600000, -1239500000, 40}}) {
        for (int v : t) put32(sounding, v);
    }
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(110, 20)}, {"SG3D", sounding}}));
    // DEPARE polygon: edge 10 forward, then edge 11 reversed, closing the ring
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(3, 42)}, {"ATTF", attf(87, "1.8")},
                         {"FSPT", fspt({{10, 1, 1}, {11, 2, 1}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(2, 43)}, {"ATTF", attf(174, "5")},
                         {"FSPT", fspt({{10, 1, 1}})}}));                       // DEPCNT line
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(2, 9999)}, {"FSPT", fspt({{10, 1, 1}})}}));  // ignored
    Bytes ref{110};
    put32(ref, 20);
    ref.push_back(1); ref.push_back(1); ref.push_back(2);
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(1, 129)}, {"FSPT", ref}}));  // SOUNDG
    return cell;
}

static void testS57Synthetic() {
    ChartData d;
    std::string err;
    CHECK(loadS57Buffer(buildCell(), d, err));
    const ChartFeature *area = nullptr, *line = nullptr;
    int soundings = 0;
    for (const ChartFeature& f : d.features) {
        if (f.objectClass == "DEPARE") area = &f;
        if (f.objectClass == "DEPCNT") line = &f;
        soundings += f.objectClass == "SOUNDG";
    }
    CHECK(d.features.size() == 4);  // DEPARE, DEPCNT, 2 soundings; the unknown class is dropped
    CHECK(area && area->geometry == Geometry::Area && area->parts.size() == 1);
    if (area) {
        const auto& ring = area->parts[0].points;
        CHECK(ring.size() == 5);  // node1, corner, node2, corner, node1
        CHECK(ring.size() == 5 && ring.front().lat == ring.back().lat && ring.front().lon == ring.back().lon);
        CHECK(std::fabs(ring[0].lat - 46.0) < 1e-9 && std::fabs(ring[0].lon + 124.0) < 1e-9);
        CHECK(std::fabs(area->drval1 - 1.8) < 1e-9);
        CHECK(!area->parts[0].hole);
    }
    CHECK(line && line->parts.size() == 1 && line->parts[0].points.size() == 3 && std::fabs(line->valdco - 5.0) < 1e-9);
    CHECK(soundings == 2);
}

static Bytes buildHazardCell() {
    // Same two nodes and two edges as buildCell, plus features that carry the hazard attributes (S-57 attribute codes: RESTRN 131,
    // CATREA 56, VERCLR 181, VERCCL 182, VALSOU 179, WATLEV 187, CATCAM 13) and an isolated node for a point object.
    const int k = 10000000;
    Bytes cell;
    auto add = [&](const Bytes& r) { cell.insert(cell.end(), r.begin(), r.end()); };
    Bytes dspm(24, 0);
    for (int i = 0; i < 4; ++i) dspm[16 + i] = (k >> (8 * i)) & 0xFF;
    dspm[20] = 10;
    add(makeRecord('L', {{"0000", Bytes{'x'}}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"DSPM", dspm}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(120, 1)}, {"SG2D", sg2d({{460000000, -1240000000}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(120, 2)}, {"SG2D", sg2d({{461000000, -1239000000}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(130, 10)}, {"VRPT", vrpt(1, 2)}, {"SG2D", sg2d({{460000000, -1239000000}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(130, 11)}, {"VRPT", vrpt(1, 2)}, {"SG2D", sg2d({{461000000, -1240000000}})}}));
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"VRID", vrid(110, 21)}, {"SG2D", sg2d({{460500000, -1239500000}})}}));
    Bytes both = attf(131, "7,14");           // RESTRN: entry prohibited, area to be avoided
    const Bytes catrea = attf(56, "9");       // CATREA: military area
    both.insert(both.end(), catrea.begin(), catrea.end());
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(3, 112)}, {"ATTF", both}, {"FSPT", fspt({{10, 1, 1}, {11, 2, 1}})}}));   // RESARE area
    Bytes bridge = attf(181, "30");           // VERCLR 30 m open
    const Bytes closed = attf(182, "4.5");    // VERCCL 4.5 m closed
    bridge.insert(bridge.end(), closed.begin(), closed.end());
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(2, 11)}, {"ATTF", bridge}, {"FSPT", fspt({{10, 1, 1}})}}));                 // BRIDGE line
    Bytes point{110};
    put32(point, 21);
    point.push_back(1); point.push_back(1); point.push_back(2);
    Bytes rock = attf(179, "1.2");
    const Bytes awash = attf(187, "5");
    rock.insert(rock.end(), awash.begin(), awash.end());
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(1, 153)}, {"ATTF", rock}, {"FSPT", point}}));                               // UWTROC point
    add(makeRecord('D', {{"0001", Bytes{'0'}}, {"FRID", frid(1, 14)}, {"ATTF", attf(13, "3")}, {"FSPT", point}}));                      // BOYCAR south
    return cell;
}

static void testS57ReadsHazardAttributes() {
    ChartData d;
    std::string err;
    CHECK(loadS57Buffer(buildHazardCell(), d, err));
    const ChartFeature *resare = nullptr, *bridge = nullptr, *rock = nullptr, *cardinal = nullptr;
    for (const ChartFeature& f : d.features) {
        if (f.objectClass == "RESARE") resare = &f;
        if (f.objectClass == "BRIDGE") bridge = &f;
        if (f.objectClass == "UWTROC") rock = &f;
        if (f.objectClass == "BOYCAR") cardinal = &f;
    }
    CHECK(resare && (resare->restrn & (1u << 7)) && (resare->restrn & (1u << 14)) && !(resare->restrn & (1u << 1)));  // list attribute -> bitmask
    CHECK(resare && (resare->catrea & (1u << 9)));
    CHECK(bridge && std::fabs(bridge->verclr - 30.0) < 1e-9 && std::fabs(bridge->verccl - 4.5) < 1e-9);
    CHECK(rock && std::fabs(rock->valsou - 1.2) < 1e-9 && std::fabs(rock->watlev - 5.0) < 1e-9);
    CHECK(cardinal && std::fabs(cardinal->catcam - 3.0) < 1e-9);
    CHECK(d.features.size() == 4);  // and nothing else was kept
}

static void testS57Malformed() {
    ChartData d;
    std::string err;
    Bytes cell = buildCell();
    cell.resize(cell.size() - 7);  // truncate the last record
    CHECK(!loadS57Buffer(cell, d, err));
    CHECK(!err.empty());
    CHECK(!loadS57Buffer(Bytes{'h', 'e', 'l', 'l', 'o'}, d, err));
}

/// Set OAR_TEST_ENC to a real ENC base cell (e.g. .../US5GA1PH/US5GA1PH.000) to parse it as an integration check.
static void testS57RealCell() {
    const char* path = std::getenv("OAR_TEST_ENC");
    if (!path) return;
    ChartData d;
    std::string err;
    CHECK(loadS57(path, d, err));
    CHECK(!d.features.empty());
    std::printf("real cell %s: %zu features\n", path, d.features.size());
}

static void testRouteThroughViaPoints() {
    // A wall with gaps at the top and bottom: planned straight, the route takes the nearer (bottom) gap; with a via point in the
    // top gap it must go through the top one, and every given point is a waypoint of the result, in order.
    CostGrid g = makeGrid(30, 30);
    for (int r = 2; r < 28; ++r) g.setCost({15, r}, kBlocked);
    const LatLon a = g.centre({2, 25}), via = g.centre({15, 0}), b = g.centre({27, 25});
    std::vector<size_t> at;
    int failed = 7;
    const auto route = findRouteThrough(g, {a, via, b}, 0.0, nullptr, 0.0, nullptr, &at, &failed);
    CHECK(failed == -1);
    CHECK(at.size() == 3);
    CHECK(at.size() == 3 && at[0] == 0 && at[2] == route.size() - 1 && at[0] < at[1] && at[1] < at[2]);
    CHECK(at.size() == 3 && g.cellAt(route[at[1]]) == g.cellAt(via));
    for (const LatLon& p : route) CHECK(!g.blocked(g.cellAt(p)));
    // A doubled point adds no leg; a point with no way to it names the leg that failed.
    CHECK(findRouteThrough(g, {a, a, b}, 0.0, nullptr, 0.0, nullptr, &at, &failed).size() >= 2 && at.size() == 3 && at[1] == 0);
    for (int c = 16; c < 30; ++c) g.setCost({c, 10}, kBlocked);   // seal b's side off below the top gap
    for (int c = 16; c < 30; ++c) g.setCost({c, 1}, kBlocked);
    g.setCost({15, 0}, kBlocked);
    g.setCost({15, 1}, kBlocked);
    g.setCost({15, 28}, kBlocked);
    g.setCost({15, 29}, kBlocked);
    CHECK(findRouteThrough(g, {a, g.centre({10, 10}), b}, 0.0, nullptr, 0.0, nullptr, nullptr, &failed).empty());
    CHECK(failed == 1);
    CHECK(findRouteThrough(g, {a}, 0.0).empty());
    // Progress runs across all legs and cancelling stops the search.
    CostGrid open = makeGrid(30, 30);
    double last = 0.0;
    std::function<bool(double)> watch = [&](double f) { CHECK(f >= last - 1e-9 && f <= 1.0 + 1e-9); last = f; return true; };
    CHECK(!findRouteThrough(open, {open.centre({0, 0}), open.centre({29, 0}), open.centre({29, 29})}, 0.0, nullptr, 0.0, &watch).empty());
}

int main() {
    testHaversine();
    testCellRoundTrip();
    testDepthBarrier();
    testStraightRoute();
    testRoutesAroundWall();
    testLongExpensiveRouteIsFound();
    testNoRoute();
    testRouteThroughViaPoints();
    testChannelCentering();
    testShoreMargin();
    testInputValidation();
    testGpxNames();
    testGpxLocaleIndependent();
    testGpx();
    testStampChart();
    testFinerChartWins();
    testLaneFactor();
    testNoWrongWayInLane();
    testCrossesLaneAtRightAngles();
    testVesselClass();
    testLaneUseFactor();
    testSmallCraftDoesNotClipLaneCorner();
    testLineOfSightSeesEveryTouchedCell();
    testDirectionalLaneMargin();
    testLaneMarginBowsAwayFromLaneRun();
    testNoTurnInsideLane();
    testSimplifyTolerance();
    testMinimumLegLength();
    testChannelGates();
    testKeepToStarboardSide();
    testChannelPreference();
    testWaterBodies();
    testSeparationZoneCrossing();
    testPrecautionaryAreaCost();
    testLargeVesselStaysInLane();
    testStampTss();
    testHazardObstructions();
    testHazardBlocksAlways();
    testHazardRestrictedAreas();
    testRestrictedAreaTextAndNotes();
    testHazardsSurviveFinerChart();
    testBridgeAcrossScales();
    testNavigationLock();
    testSearchProgressAndCancel();
    testSuggestedCellSize();
    testEncCatalog();
    testFindEncCells();
    testLandRasterisation();
    testLockCorridorKeepsCoarseChartHazard();
    testPlannerRejectsBadNumbers();
    testSharedScratchStamping();
    testLockCorridor();
    testThinPolygonsStillBlock();
    testHazardPenalties();
    testHazardMarks();
    testHazardOverheadClearance();
    testS57MissingFile();
    testS57Synthetic();
    testS57ReadsHazardAttributes();
    testS57Malformed();
    testS57RealCell();
    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;
}
