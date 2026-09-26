#include "openautoroute/cost_grid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace oar {

CostGrid::CostGrid(int cols, int rows, LatLon northWest, double cellSizeDeg, double cellSizeLonDeg)
    : cols_(cols),
      rows_(rows),
      northWest_(northWest),
      cellSizeDeg_(cellSizeDeg),
      cellSizeLonDeg_(cellSizeLonDeg > 0.0 ? cellSizeLonDeg : cellSizeDeg) {
    if (cols <= 0 || rows <= 0 || cellSizeDeg <= 0.0) {
        throw std::invalid_argument("CostGrid needs positive dimensions and cell size");
    }
    cost_.assign(static_cast<size_t>(cols) * rows, 1.0f);
}

LatLon CostGrid::centre(Cell c) const {
    return {northWest_.lat - (c.row + 0.5) * cellSizeDeg_, northWest_.lon + (c.col + 0.5) * cellSizeLonDeg_};
}

Cell CostGrid::cellAt(LatLon p) const {
    return {static_cast<int>(std::floor((p.lon - northWest_.lon) / cellSizeLonDeg_)),
            static_cast<int>(std::floor((northWest_.lat - p.lat) / cellSizeDeg_))};
}

void CostGrid::applyDepthBarrier(const std::vector<float>& depthM, double draftM, double clearanceM) {
    if (depthM.size() != cost_.size()) throw std::invalid_argument("depth array does not match grid size");
    const double minDepth = draftM + clearanceM;
    for (size_t i = 0; i < cost_.size(); ++i) {
        if (!std::isnan(depthM[i]) && depthM[i] < minDepth) cost_[i] = kBlocked;
    }
}

void CostGrid::applyChannelCentering(const std::vector<float>& distToCentreM, double halfWidthM, double penalty) {
    if (distToCentreM.size() != cost_.size()) throw std::invalid_argument("distance array does not match grid size");
    if (halfWidthM <= 0.0) throw std::invalid_argument("halfWidthM must be positive");
    // A negative penalty would push costs below 1 and break the pathfinder's admissible heuristic.
    if (!(penalty >= 0.0)) throw std::invalid_argument("penalty must be non-negative");
    for (size_t i = 0; i < cost_.size(); ++i) {
        if (cost_[i] == kBlocked || std::isnan(distToCentreM[i])) continue;
        if (distToCentreM[i] < 0.0f) throw std::invalid_argument("distances must be non-negative");
        // 0 on the centreline, approaching 1 at the channel edge and beyond.
        const double t = 1.0 - std::exp(-3.0 * distToCentreM[i] / halfWidthM);
        cost_[i] *= static_cast<float>(1.0 + penalty * t);
    }
}

std::vector<float> CostGrid::distanceToBlockedM() const {
    // Two-pass chamfer transform in cell units (weights 1 and sqrt 2), then scale to metres.
    const float inf = 1e30f, diag = 1.41421356f;
    std::vector<float> d(cost_.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = cost_[i] == kBlocked ? 0.0f : inf;
    auto at = [&](int c, int r) -> float& { return d[static_cast<size_t>(r) * cols_ + c]; };
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            float& v = at(c, r);
            if (c > 0) v = std::min(v, at(c - 1, r) + 1.0f);
            if (r > 0) v = std::min(v, at(c, r - 1) + 1.0f);
            if (r > 0 && c > 0) v = std::min(v, at(c - 1, r - 1) + diag);
            if (r > 0 && c + 1 < cols_) v = std::min(v, at(c + 1, r - 1) + diag);
        }
    }
    for (int r = rows_ - 1; r >= 0; --r) {
        for (int c = cols_ - 1; c >= 0; --c) {
            float& v = at(c, r);
            if (c + 1 < cols_) v = std::min(v, at(c + 1, r) + 1.0f);
            if (r + 1 < rows_) v = std::min(v, at(c, r + 1) + 1.0f);
            if (r + 1 < rows_ && c + 1 < cols_) v = std::min(v, at(c + 1, r + 1) + diag);
            if (r + 1 < rows_ && c > 0) v = std::min(v, at(c - 1, r + 1) + diag);
        }
    }
    const float m = static_cast<float>(cellSizeM());
    for (float& v : d) v = v >= inf ? inf : v * m;
    return d;
}

void CostGrid::applyShoreMargin(double rangeM, double weight) {
    if (rangeM <= 0.0 || weight <= 0.0) return;
    const std::vector<float> dist = distanceToBlockedM();
    for (size_t i = 0; i < cost_.size(); ++i) {
        if (cost_[i] == kBlocked || dist[i] >= rangeM) continue;
        const double t = 1.0 - dist[i] / rangeM;
        cost_[i] *= static_cast<float>(1.0 + weight * t * t);
    }
}

}  // namespace oar
