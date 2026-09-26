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

void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid, bool applyTss) {
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

    // COLREGs Rule 10: separation zones and lines are never entered; lane parts record the flow direction so the router
    // can enforce it per move. Lane polygons are painted afterwards and only set direction, never open or shut water.
    if (applyTss) {
        for (const ChartFeature& f : chart.features) {
            if (f.objectClass == "TSEZNE" && f.geometry == Geometry::Area) paintArea(f, kShut, grid, state);
            if (f.objectClass == "TSELNE" && f.geometry == Geometry::Line) paintLine(f, grid, state);
        }
        for (const ChartFeature& f : chart.features) {
            if (f.objectClass != "TSSLPT" || f.geometry != Geometry::Area || std::isnan(f.orient)) continue;
            forEachCellInArea(f, grid, [&](int row, int col) { grid.setLaneDirection({col, row}, static_cast<float>(f.orient)); });
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
            const uint8_t s = state[static_cast<size_t>(row) * grid.cols() + col];
            if (s != kUnknown) grid.setCost({col, row}, s == kOpen ? 1.0f : kBlocked);
        }
    }
}

}  // namespace oar
