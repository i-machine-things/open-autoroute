#include "openautoroute/chart_grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace oar {
namespace {

enum : uint8_t { kUnknown = 0, kOpen = 1, kShut = 2 };

// Scanline even-odd fill over every ring of a feature at once, so hole rings cut out of the exterior for free. Cost is
// rows x edges instead of cells x edges, which matters for the huge coarse-scale polygons in approach/coastal charts.
template <typename Fn>
void forEachCellInArea(const ChartFeature& f, const CostGrid& grid, Fn&& visit) {
    double minLat = 1e9, maxLat = -1e9;
    for (const Ring& r : f.parts) {
        if (r.points.size() < 3) return;  // degenerate outline
        for (const LatLon& p : r.points) {
            minLat = std::min(minLat, p.lat);
            maxLat = std::max(maxLat, p.lat);
        }
    }
    // Rows run south, so the north edge gives the first row.
    const int firstRow = std::max(grid.cellAt({maxLat, 0.0}).row, 0);
    const int lastRow = std::min(grid.cellAt({minLat, 0.0}).row, grid.rows() - 1);
    std::vector<double> xs;
    for (int row = firstRow; row <= lastRow; ++row) {
        const double lat = grid.centre({0, row}).lat;
        xs.clear();
        for (const Ring& r : f.parts) {
            const auto& v = r.points;
            for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
                if ((v[i].lat > lat) != (v[j].lat > lat)) {
                    xs.push_back(v[i].lon + (lat - v[i].lat) / (v[j].lat - v[i].lat) * (v[j].lon - v[i].lon));
                }
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            // Cells whose centre lies in [xs[k], xs[k+1]].
            const double c0 = std::ceil((xs[k] - grid.centre({0, 0}).lon) / grid.cellSizeLonDeg());
            const double c1 = std::floor((xs[k + 1] - grid.centre({0, 0}).lon) / grid.cellSizeLonDeg());
            const int from = static_cast<int>(std::max(c0, 0.0));
            const int to = static_cast<int>(std::min(c1, static_cast<double>(grid.cols() - 1)));
            for (int col = from; col <= to; ++col) visit(row, col);
        }
    }
}

void paintArea(const ChartFeature& f, uint8_t value, const CostGrid& grid, std::vector<uint8_t>& state) {
    forEachCellInArea(f, grid, [&](int row, int col) {
        uint8_t& s = state[static_cast<size_t>(row) * grid.cols() + col];
        s = (s == kShut || value == kShut) ? static_cast<uint8_t>(kShut) : value;  // shut always wins
    });
}

// Mark the cells a polyline passes through as shut (Bresenham between consecutive vertices).
void paintLine(const ChartFeature& f, const CostGrid& grid, std::vector<uint8_t>& state) {
    for (const Ring& r : f.parts) {
        for (size_t i = 1; i < r.points.size(); ++i) {
            const Cell a = grid.cellAt(r.points[i - 1]), b = grid.cellAt(r.points[i]);
            int x = a.col, y = a.row;
            const int dx = std::abs(b.col - a.col), dy = std::abs(b.row - a.row);
            const int sx = a.col < b.col ? 1 : -1, sy = a.row < b.row ? 1 : -1;
            int err = dx - dy;
            // A wildly long segment far outside the grid would only waste time, so clip by the grid's own size.
            if (std::max(dx, dy) > 4 * (grid.cols() + grid.rows())) continue;
            while (true) {
                if (grid.inBounds({x, y})) state[static_cast<size_t>(y) * grid.cols() + x] = kShut;
                if (x == b.col && y == b.row) break;
                const int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx) { err += dx; y += sy; }
            }
        }
    }
}

}  // namespace

namespace {

// Every cell whose centre is within `radiusM` of `p`, with the bearing from `p` to the cell, for disks and sectors round point objects.
template <typename Fn>
void forEachCellNear(const CostGrid& grid, LatLon p, double radiusM, Fn&& visit) {
    const Cell c0 = grid.cellAt(p);
    const int r = static_cast<int>(std::ceil(radiusM / grid.cellSizeM())) + 1;
    for (int row = c0.row - r; row <= c0.row + r; ++row) {
        for (int col = c0.col - r; col <= c0.col + r; ++col) {
            if (!grid.inBounds({col, row})) continue;
            const double dx = (col - c0.col) * grid.cellSizeM(), dy = (c0.row - row) * grid.cellSizeM();  // east, north
            if (std::hypot(dx, dy) > radiusM) continue;
            double bearing = std::atan2(dx, dy) * 180.0 / kPi;
            if (bearing < 0) bearing += 360.0;
            visit(row, col, bearing);
        }
    }
}

bool contains(const char* list[], size_t n, const std::string& cls) {
    for (size_t i = 0; i < n; ++i) {
        if (cls == list[i]) return true;
    }
    return false;
}

// Obstructions, wrecks and rocks are only harmless with water over them that is known and deep enough. WATLEV (S-57 attribute 187):
// 1 partly submerged at high water, 2 always dry, 4 covers and uncovers, 5 awash, 6 subject to flooding are hazards whatever VALSOU says.
bool depthUnsafe(const ChartFeature& f, double minDepthM) {
    if (!std::isnan(f.watlev)) {
        const int w = static_cast<int>(f.watlev);
        if (w == 1 || w == 2 || w == 4 || w == 5 || w == 6) return true;
    }
    return std::isnan(f.valsou) || f.valsou < minDepthM;  // unknown depth is unsafe
}

// Block a feature's geometry: cells inside an area, along a line, or at a point.
void shutGeometry(const ChartFeature& f, const CostGrid& grid, std::vector<uint8_t>& mask, std::vector<uint8_t>& scratch) {
    (void)scratch;
    if (f.geometry == Geometry::Area) {
        forEachCellInArea(f, grid, [&](int row, int col) { mask[static_cast<size_t>(row) * grid.cols() + col] = 1; });
        // A polygon thinner than a cell can miss every cell centre; also shut the cells along its outline so it is never invisible.
        paintLine(f, grid, mask);
    } else if (f.geometry == Geometry::Line) {
        paintLine(f, grid, mask);
    } else if (!f.parts.empty() && !f.parts[0].points.empty()) {
        const Cell c = grid.cellAt(f.parts[0].points[0]);
        if (grid.inBounds(c)) mask[static_cast<size_t>(c.row) * grid.cols() + c.col] = 1;
    }
}

void raise(std::vector<float>& penalty, const CostGrid& grid, const ChartFeature& f, float factor) {
    if (f.geometry != Geometry::Area) return;
    forEachCellInArea(f, grid, [&](int row, int col) {
        float& p = penalty[static_cast<size_t>(row) * grid.cols() + col];
        p = std::max(p, factor);
    });
}

void applyHazardObjects(const ChartData& chart, const StampOptions& opt, const CostGrid& grid, std::vector<uint8_t>& shut,
                        std::vector<float>& penalty) {
    // Fixed things standing in the water, and areas nobody should enter, that the depth areas call open water.
    static const char* kBlockAlways[] = {"UNSARE", "FSHFAC", "MARCUL", "PRDARE", "OSPARE", "HULKES", "SLCONS", "PONTON", "PILPNT",
                                         "MORFAC", "FNCLNE", "DYKCON", "CAUSWY", "CONVYR", "PYLONS", "FLODOC", "DRYDOC", "GATCON", "DAMCON",
                                         "GRIDRN", "OILBAR", "RAPIDS", "WATFAL"};
    std::vector<uint8_t> scratch;
    for (const ChartFeature& f : chart.features) {
        const std::string& cls = f.objectClass;
        if (std::find(opt.skipClasses.begin(), opt.skipClasses.end(), cls) != opt.skipClasses.end()) continue;  // developer switch
        if (contains(kBlockAlways, sizeof kBlockAlways / sizeof *kBlockAlways, cls)) {
            shutGeometry(f, grid, shut, scratch);
        } else if (cls == "OBSTRN" || cls == "WRECKS" || cls == "UWTROC") {
            if (depthUnsafe(f, opt.minDepthM)) shutGeometry(f, grid, shut, scratch);
        } else if (cls == "OFSPLF") {
            shutGeometry(f, grid, shut, scratch);
            if (f.geometry == Geometry::Point && !f.parts.empty() && !f.parts[0].points.empty()) {  // 250 m berth round a platform
                forEachCellNear(grid, f.parts[0].points[0], 250.0, [&](int row, int col, double) { shut[static_cast<size_t>(row) * grid.cols() + col] = 1; });
            }
        } else if (cls == "BOYISD" || cls == "BCNISD") {  // isolated danger: a hazard directly beneath, 100 m clear
            if (!f.parts.empty() && !f.parts[0].points.empty()) {
                forEachCellNear(grid, f.parts[0].points[0], 100.0, [&](int row, int col, double) { shut[static_cast<size_t>(row) * grid.cols() + col] = 1; });
            }
        } else if ((cls == "BOYCAR" || cls == "BCNCAR") && !f.parts.empty() && !f.parts[0].points.empty() && !std::isnan(f.catcam)) {
            // Safe water lies on the mark's named side, so the danger is on the opposite side: a north mark is passed to its north.
            static const double kDanger[] = {0, 180.0, 270.0, 0.0, 90.0};  // by CATCAM 1 north, 2 east, 3 south, 4 west
            const int cat = static_cast<int>(f.catcam);
            if (cat >= 1 && cat <= 4) {
                const double danger = kDanger[cat];
                forEachCellNear(grid, f.parts[0].points[0], 150.0, [&](int row, int col, double bearing) {
                    if (angleDiffDeg(bearing, danger) <= 90.0) shut[static_cast<size_t>(row) * grid.cols() + col] = 1;
                });
                shutGeometry(f, grid, shut, scratch);
            }
        } else if (cls == "RESARE") {
            // RESTRN (attribute 131): 7 entry prohibited, 8 entry restricted, 14 area to be avoided. Anchoring, fishing and similar
            // restrictions (1 to 6, 9 to 13, 15, 16) do not stop a transit. CATREA (attribute 56): 1 offshore safety zone, 9 military
            // area and 14 minefield forbid entry; the rest only ask for care.
            const uint32_t restrn = f.restrn, catrea = f.catrea;
            const bool ship = opt.vesselLengthM >= 50.0;  // "area to be avoided" designations (ATBA) are aimed at large ships
            const bool forbidden = (restrn & (1u << 7)) || (catrea & (1u << 14)) || ((restrn & (1u << 14)) && ship);
            if (forbidden) {
                shutGeometry(f, grid, shut, scratch);
            } else {
                float factor = 1.0f;
                if (restrn & (1u << 14)) factor = 20.0f;                                   // area to be avoided, for a smaller vessel
                if (catrea & (1u << 9)) factor = std::max(factor, 30.0f);                  // military area: usually only while in use
                if ((restrn & (1u << 8)) || (catrea & (1u << 1))) factor = std::max(factor, 10.0f);  // entry restricted; an offshore safety zone (security zones use it)
                if (catrea & (1u << 18)) factor = std::max(factor, 20.0f);                 // swimming area
                if (catrea & ((1u << 21) | (1u << 8) | (1u << 12))) factor = std::max(factor, 8.0f);  // dredging, degaussing range, aid safety zone
                if (catrea & ((1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10) | (1u << 20) | (1u << 22) | (1u << 23))) factor = std::max(factor, 5.0f);  // reserves, sanctuaries, wreck and research areas
                if (catrea & ((1u << 25) | (1u << 26))) factor = std::max(factor, 3.0f);   // swinging and water-skiing areas
                if (factor > 1.0f) raise(penalty, grid, f, factor);
            }
        } else if (cls == "MIPARE") {
            // Military practice and danger areas usually restrict passage only while in use, and one can span a whole waterway (Puget
            // Sound), so blocking it would cut the water in two. Very costly instead: avoided whenever there is any way round.
            raise(penalty, grid, f, 30.0f);
        } else if (cls == "CTNARE") {
            raise(penalty, grid, f, 3.0f);
        } else if (cls == "DMPGRD") {
            // CATDPG (attribute 23): 2 chemical waste, 3 nuclear waste, 4 explosives, 5 spoil ground, 6 vessel dumping ground. The first
            // three are dangerous however deep the water is: blocked. Spoil and vessel grounds change depth and hold debris: very costly.
            if (f.catdpg & ((1u << 2) | (1u << 3) | (1u << 4))) shutGeometry(f, grid, shut, scratch);
            else raise(penalty, grid, f, 15.0f);
        } else if (cls == "ACHARE") {
            raise(penalty, grid, f, 3.0f);  // vessels lie at anchor here
        } else if (cls == "WEDKLP") {
            raise(penalty, grid, f, 1.5f);
        } else if (cls == "WATTUR") {
            raise(penalty, grid, f, 3.0f);
        } else if (cls == "SPLARE") {
            raise(penalty, grid, f, 5.0f);
        } else if (cls == "BRIDGE" || cls == "CBLOHD" || cls == "PIPOHD") {
            // A span is only passable if it is higher than the mast. An opening bridge is treated as closed (VERCCL); unknown is unsafe.
            const double clearance = !std::isnan(f.verccl) ? f.verccl : f.verclr;
            if (std::isnan(clearance) || clearance < opt.airDraftM + 1.0) shutGeometry(f, grid, shut, scratch);
        }
    }
}

}  // namespace

void stampChart(const ChartData& chart, const StampOptions& options, CostGrid& grid) {
    const double minDepthM = options.minDepthM, cautionFactor = options.cautionFactor;
    const bool applyTss = options.applyTss;
    std::vector<uint8_t> state(static_cast<size_t>(grid.cols()) * grid.rows(), kUnknown);
    std::vector<uint8_t> covered(state.size(), 0);

    for (const ChartFeature& f : chart.features) {
        if (f.geometry != Geometry::Area) continue;
        if (f.objectClass == "DEPARE" || f.objectClass == "DRGARE") {
            paintArea(f, (!std::isnan(f.drval1) && f.drval1 >= minDepthM) ? kOpen : kShut, grid, state);
        } else if (f.objectClass == "LNDARE") {
            paintArea(f, kShut, grid, state);
        }
    }
    covered = state;  // point hazards below must not extend the chart's coverage

    // COLREGs Rule 10 lane parts record their flow direction so the router can enforce it per move. Painted only onto the grid,
    // never open or shut water. Zones and lines are marked after the water verdict below, since a zone cell must stay open.
    std::vector<uint8_t> zone(state.size(), 0), caution(state.size(), 0), channel(state.size(), 0);
    for (const ChartFeature& f : chart.features) {
        if ((f.objectClass == "FAIRWY" || f.objectClass == "DRGARE") && f.geometry == Geometry::Area) {
            forEachCellInArea(f, grid, [&](int row, int col) { channel[static_cast<size_t>(row) * grid.cols() + col] = 1; });
        }
    }
    if (applyTss) {
        for (const ChartFeature& f : chart.features) {
            if (f.objectClass != "TSSLPT" || f.geometry != Geometry::Area || std::isnan(f.orient)) continue;
            forEachCellInArea(f, grid, [&](int row, int col) { grid.setLaneDirection({col, row}, static_cast<float>(f.orient)); });
        }
        for (const ChartFeature& f : chart.features) {
            if (f.objectClass == "TSEZNE" && f.geometry == Geometry::Area) {
                forEachCellInArea(f, grid, [&](int row, int col) { zone[static_cast<size_t>(row) * grid.cols() + col] = 1; });
            } else if (f.objectClass == "TSELNE" && f.geometry == Geometry::Line) {
                paintLine(f, grid, zone);
            } else if (f.objectClass == "PRCARE" && f.geometry == Geometry::Area && cautionFactor > 1.0) {
                forEachCellInArea(f, grid, [&](int row, int col) { caution[static_cast<size_t>(row) * grid.cols() + col] = 1; });
            }
        }
    }

    for (const ChartFeature& f : chart.features) {
        if (f.geometry != Geometry::Point || f.parts.empty() || f.parts[0].points.empty()) continue;
        const bool hazard = f.objectClass == "OBSTRN" || f.objectClass == "WRECKS" || f.objectClass == "UWTROC";
        if (!hazard && f.objectClass != "SOUNDG") continue;
        const bool unsafe = std::isnan(f.valsou) ? hazard : f.valsou < minDepthM;
        if (!unsafe) continue;
        const Cell c = grid.cellAt(f.parts[0].points[0]);
        if (grid.inBounds(c) && covered[static_cast<size_t>(c.row) * grid.cols() + c.col]) {
            state[static_cast<size_t>(c.row) * grid.cols() + c.col] = kShut;
        }
    }

    // ---- Hazard objects (see StampOptions / stampChart docs). `shut` cells are blocked whatever the depth areas say; `penalty` is a cost
    // multiplier (never compounded across charts: the largest wins).
    std::vector<uint8_t> shut(state.size(), 0);
    std::vector<float> penalty(state.size(), 1.0f);
    if (options.hazardObjects) applyHazardObjects(chart, options, grid, shut, penalty);

    for (int row = 0; row < grid.rows(); ++row) {
        for (int col = 0; col < grid.cols(); ++col) {
            const size_t i = static_cast<size_t>(row) * grid.cols() + col;
            const uint8_t s = state[i];
            if (s != kUnknown) grid.setCost({col, row}, s == kOpen ? 1.0f : kBlocked);
            if (s == kOpen && channel[i]) grid.setChannel({col, row});
            if (s == kOpen && zone[i]) grid.setZone({col, row});  // water, but a separation zone: crossable only square on
            if (s == kOpen && caution[i]) {
                grid.setCost({col, row}, static_cast<float>(cautionFactor));
                grid.setCaution({col, row});
            }
            if (shut[i]) grid.setCost({col, row}, kBlocked);
            else if (penalty[i] > 1.0f && !grid.blocked({col, row})) grid.setCost({col, row}, std::max(grid.cost({col, row}), penalty[i]));
        }
    }
}

void collectLateralMarks(const ChartData& chart, std::vector<LateralMark>& out) {
    for (const ChartFeature& f : chart.features) {
        if ((f.objectClass != "BOYLAT" && f.objectClass != "BCNLAT") || f.geometry != Geometry::Point) continue;
        if (f.parts.empty() || f.parts[0].points.empty() || std::isnan(f.catlam)) continue;
        const int cat = static_cast<int>(f.catlam);
        if (cat == 1 || cat == 2) out.push_back({f.parts[0].points[0], cat});
    }
}

std::vector<Gate> applyChannelGates(CostGrid& grid, const std::vector<LateralMark>& marks, double maxGateM,
                                    double outsidePenalty, double sideWeight, double lateralRangeM) {
    // Work in metres on a local plane anchored at the grid's north-west corner (cells are square in metres there).
    const LatLon origin = grid.centre({0, 0});
    const double kx = std::cos(deg2rad(origin.lat)) * 111320.0, ky = 111320.0;
    struct P { double x, y; LatLon ll; };
    auto toP = [&](LatLon ll) { return P{(ll.lon - origin.lon) * kx, (ll.lat - origin.lat) * ky, ll}; };
    const double w = grid.cols() * grid.cellSizeM(), h = grid.rows() * grid.cellSizeM();

    // The same mark appears on charts of several scales; keep one per position and category.
    std::vector<P> port, star;
    for (const LateralMark& m : marks) {
        const P p = toP(m.at);
        if (p.x < -maxGateM || p.x > w + maxGateM || p.y > maxGateM || p.y < -h - maxGateM) continue;  // rows run south
        auto& list = m.category == 1 ? port : star;
        if (std::none_of(list.begin(), list.end(), [&](const P& q) { return std::hypot(q.x - p.x, q.y - p.y) < 15.0; })) list.push_back(p);
    }
    auto nearestIdx = [](const std::vector<P>& list, const P& p, double limit) {
        int best = -1;
        double bd = limit;
        for (size_t i = 0; i < list.size(); ++i) {
            const double d = std::hypot(list[i].x - p.x, list[i].y - p.y);
            if (d < bd) { bd = d; best = static_cast<int>(i); }
        }
        return best;
    };

    struct G { P a, b; };  // a = port-hand mark, b = starboard-hand mark
    std::vector<G> gates;
    for (size_t i = 0; i < port.size(); ++i) {
        const int j = nearestIdx(star, port[i], maxGateM);
        if (j >= 0 && nearestIdx(port, star[j], maxGateM) == static_cast<int>(i)) gates.push_back({port[i], star[j]});  // mutual nearest
    }
    // A marked channel is a CHAIN of pairs. A lone pair (a harbour entrance, a ferry slip, a single buoy pair in open water) is not
    // a channel, so keep only gates with at least two other gates within 1.5 km. Outside a chain none of these rules apply.
    {
        std::vector<G> chained;
        for (size_t i = 0; i < gates.size(); ++i) {
            const double mx = (gates[i].a.x + gates[i].b.x) / 2, my = (gates[i].a.y + gates[i].b.y) / 2;
            int neighbours = 0;
            for (size_t k = 0; k < gates.size(); ++k) {
                if (k == i) continue;
                if (std::hypot((gates[k].a.x + gates[k].b.x) / 2 - mx, (gates[k].a.y + gates[k].b.y) / 2 - my) <= 1500.0) ++neighbours;
            }
            if (neighbours >= 2) chained.push_back(gates[i]);
        }
        gates.swap(chained);
    }
    std::vector<Gate> out;
    for (const G& g : gates) out.push_back({g.a.ll, g.b.ll});
    if (gates.empty()) return out;
    grid.setGateSideWeight(sideWeight);

    // Each cell belongs to the gate whose along-channel position is nearest, out to that gate's reach (half the distance to the
    // next gate, so neighbours tile). t is the cell's position across that gate; beyond a mark it is outside the channel.
    const size_t n = static_cast<size_t>(grid.cols()) * grid.rows();
    std::vector<float> bestAlong(n, 1e30f), tOf(n, std::numeric_limits<float>::quiet_NaN()), axisOf(n, 0.0f);
    for (size_t gi = 0; gi < gates.size(); ++gi) {
        const G& g = gates[gi];
        const double mx = (g.a.x + g.b.x) / 2, my = (g.a.y + g.b.y) / 2;
        double dnear = 1e30;
        for (size_t k = 0; k < gates.size(); ++k) {
            if (k == gi) continue;
            dnear = std::min(dnear, std::hypot((gates[k].a.x + gates[k].b.x) / 2 - mx, (gates[k].a.y + gates[k].b.y) / 2 - my));
        }
        const double reach = std::clamp(0.5 * dnear, 100.0, 700.0);
        const double ax = g.b.x - g.a.x, ay = g.b.y - g.a.y, width = std::hypot(ax, ay);
        if (width < 1.0) continue;
        const double ux = ax / width, uy = ay / width;   // from the port mark to the starboard mark
        const double cx = -uy, cy = ux;                  // along the channel
        // Direction of buoyage: a vessel with the starboard mark on its right travels 90 degrees anticlockwise from port->starboard.
        double axis = std::atan2(ux, uy) * 180.0 / 3.14159265358979 - 90.0;
        if (axis < 0) axis += 360.0;
        const double radius = std::max(reach, lateralRangeM) + width;
        const Cell lo = grid.cellAt(LatLon{mx / kx * 0 + origin.lat + (my + radius) / ky, origin.lon + (mx - radius) / kx});
        const Cell hi = grid.cellAt(LatLon{origin.lat + (my - radius) / ky, origin.lon + (mx + radius) / kx});
        for (int row = std::max(0, std::min(lo.row, hi.row)); row <= std::min(grid.rows() - 1, std::max(lo.row, hi.row)); ++row) {
            for (int col = std::max(0, std::min(lo.col, hi.col)); col <= std::min(grid.cols() - 1, std::max(lo.col, hi.col)); ++col) {
                if (grid.blocked({col, row})) continue;
                // Never act on a traffic lane, separation zone or precautionary area: those have their own rules.
                if (!std::isnan(grid.laneDirection({col, row})) || grid.isZone({col, row}) || grid.isCaution({col, row})) continue;
                const P c = toP(grid.centre({col, row}));
                const double along = (c.x - mx) * cx + (c.y - my) * cy;
                if (std::fabs(along) > reach) continue;
                const double t = ((c.x - g.a.x) * ux + (c.y - g.a.y) * uy) / width;  // 0 at the port mark, 1 at the starboard mark
                if (t < -lateralRangeM / width || t > 1.0 + lateralRangeM / width) continue;
                const size_t i = static_cast<size_t>(row) * grid.cols() + col;
                if (std::fabs(along) < bestAlong[i]) {
                    bestAlong[i] = static_cast<float>(std::fabs(along));
                    tOf[i] = static_cast<float>(t);
                    axisOf[i] = static_cast<float>(axis);
                }
            }
        }
    }
    for (int row = 0; row < grid.rows(); ++row) {
        for (int col = 0; col < grid.cols(); ++col) {
            const size_t i = static_cast<size_t>(row) * grid.cols() + col;
            const float t = tOf[i];
            if (std::isnan(t) || grid.blocked({col, row})) continue;
            if (t < -0.05f || t > 1.05f) grid.setCost({col, row}, grid.cost({col, row}) * static_cast<float>(outsidePenalty));  // outside the marks
            else grid.setGateCell({col, row}, t, axisOf[i]);                                                                 // between them
        }
    }
    return out;
}

}  // namespace oar

namespace oar {

void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid, bool applyTss, double cautionFactor) {
    StampOptions o;
    o.minDepthM = minDepthM;
    o.applyTss = applyTss;
    o.cautionFactor = cautionFactor;
    o.hazardObjects = false;  // the old entry point keeps its old behaviour exactly; callers that want the hazard rules pass StampOptions
    stampChart(chart, o, grid);
}

}  // namespace oar
