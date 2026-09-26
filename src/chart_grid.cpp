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

void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid, bool applyTss, double cautionFactor) {
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
