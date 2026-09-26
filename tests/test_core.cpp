#include <cmath>
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
    CHECK(g.cost({11, 5}) == 7.0f);      // not covered by the chart: untouched
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
    const std::vector<Gate> gates = applyChannelGates(gated, marks, 1000.0, 8.0, 1.5, 2500.0);
    CHECK(gates.size() >= centre.size() - 2);  // essentially every pair became a gate
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
    // An L-shaped charted channel. Cutting the corner leaves it; with the preference the route stays inside the dashed limits.
    CostGrid g = makeGrid(40, 40);
    for (int r = 0; r < 40; ++r) {
        for (int c = 0; c < 40; ++c) {
            if ((r >= 30 && r <= 34) || (c >= 30 && c <= 34)) g.setChannel({c, r});
        }
    }
    const LatLon a = g.centre({2, 32}), b = g.centre({32, 2});
    auto cellsOutside = [&](const std::vector<LatLon>& route) {
        int out = 0;
        for (size_t i = 1; i < route.size(); ++i) {
            for (int k = 0; k <= 60; ++k) {
                const double t = k / 60.0;
                out += !g.isChannel(g.cellAt({route[i - 1].lat + t * (route[i].lat - route[i - 1].lat), route[i - 1].lon + t * (route[i].lon - route[i - 1].lon)}));
            }
        }
        return out;
    };
    CHECK(cellsOutside(findRoute(g, a, b)) > 20);  // control: without the preference the diagonal cuts the corner
    g.applyChannelPreference(10 * g.cellSizeM(), 8.0);
    CHECK(g.cost({10, 26}) == 8.0f);               // open water beside the channel now costs more
    CHECK(g.cost({10, 32}) == 1.0f);               // inside the channel is untouched
    CHECK(cellsOutside(findRoute(g, a, b)) < 10);  // the route follows the channel round the bend
    CostGrid none = makeGrid(10, 10);
    none.applyChannelPreference(5 * none.cellSizeM(), 8.0);  // no channels charted: a no-op
    CHECK(none.cost({3, 3}) == 1.0f);
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

int main() {
    testHaversine();
    testCellRoundTrip();
    testDepthBarrier();
    testStraightRoute();
    testRoutesAroundWall();
    testNoRoute();
    testChannelCentering();
    testShoreMargin();
    testInputValidation();
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
    testS57MissingFile();
    testS57Synthetic();
    testS57Malformed();
    testS57RealCell();
    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;
}
