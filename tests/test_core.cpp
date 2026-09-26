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
    testS57MissingFile();
    testS57Synthetic();
    testS57Malformed();
    testS57RealCell();
    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;
}
