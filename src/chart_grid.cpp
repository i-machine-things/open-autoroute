#include "openautoroute/chart_grid.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace oar {
namespace {

enum : uint8_t { kUnknown = 0, kOpen = 1, kShut = 2 };

// Even-odd point-in-polygon over every ring of a feature at once, so hole rings cut out of the exterior for free.
bool insideRings(const std::vector<Ring>& rings, LatLon p) {
    bool inside = false;
    for (const Ring& r : rings) {
        const auto& v = r.points;
        for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
            if ((v[i].lat > p.lat) != (v[j].lat > p.lat) &&
                p.lon < (v[j].lon - v[i].lon) * (p.lat - v[i].lat) / (v[j].lat - v[i].lat) + v[i].lon) {
                inside = !inside;
            }
        }
    }
    return inside;
}

void paintArea(const ChartFeature& f, uint8_t value, const CostGrid& grid, std::vector<uint8_t>& state) {
    double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
    for (const Ring& r : f.parts) {
        if (r.points.size() < 3) return;  // degenerate outline
        for (const LatLon& p : r.points) {
            minLat = std::min(minLat, p.lat); maxLat = std::max(maxLat, p.lat);
            minLon = std::min(minLon, p.lon); maxLon = std::max(maxLon, p.lon);
        }
    }
    // Rows run south, so the north edge gives the first row.
    const Cell a = grid.cellAt({maxLat, minLon}), b = grid.cellAt({minLat, maxLon});
    for (int row = std::max(a.row, 0); row <= std::min(b.row, grid.rows() - 1); ++row) {
        for (int col = std::max(a.col, 0); col <= std::min(b.col, grid.cols() - 1); ++col) {
            if (!insideRings(f.parts, grid.centre({col, row}))) continue;
            uint8_t& s = state[static_cast<size_t>(row) * grid.cols() + col];
            s = (s == kShut || value == kShut) ? static_cast<uint8_t>(kShut) : value;  // shut always wins, whichever polygon came first
        }
    }
}

}  // namespace

void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid) {
    std::vector<uint8_t> state(static_cast<size_t>(grid.cols()) * grid.rows(), kUnknown);
    std::vector<uint8_t> covered(state.size(), 0);

    for (const ChartFeature& f : chart.features) {
        if (f.geometry != Geometry::Area) continue;
        if (f.objectClass == "DEPARE") {
            paintArea(f, (!std::isnan(f.drval1) && f.drval1 >= minDepthM) ? kOpen : kShut, grid, state);
        } else if (f.objectClass == "LNDARE") {
            paintArea(f, kShut, grid, state);
        }
    }
    covered = state;  // point hazards below must not extend the chart's coverage

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
