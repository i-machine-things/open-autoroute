#include <cmath>
#include <cstdio>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

#include "openautoroute/cost_grid.hpp"
#include "openautoroute/geo.hpp"
#include "openautoroute/gpx.hpp"
#include "openautoroute/pathfinder.hpp"
#include "openautoroute/s57.hpp"

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

static void testS57Unavailable() {
    ChartData d;
    std::string err;
    CHECK(!loadS57("nonexistent.000", d, err));
    CHECK(!err.empty());
}

int main() {
    testHaversine();
    testCellRoundTrip();
    testDepthBarrier();
    testStraightRoute();
    testRoutesAroundWall();
    testNoRoute();
    testChannelCentering();
    testInputValidation();
    testGpxLocaleIndependent();
    testGpx();
    testS57Unavailable();
    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;
}
