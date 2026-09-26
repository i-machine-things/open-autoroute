#include "openautoroute/cost_grid.hpp"

#include <cmath>
#include <stdexcept>

namespace oar {

CostGrid::CostGrid(int cols, int rows, LatLon northWest, double cellSizeDeg)
    : cols_(cols), rows_(rows), northWest_(northWest), cellSizeDeg_(cellSizeDeg) {
    if (cols <= 0 || rows <= 0 || cellSizeDeg <= 0.0) {
        throw std::invalid_argument("CostGrid needs positive dimensions and cell size");
    }
    cost_.assign(static_cast<size_t>(cols) * rows, 1.0f);
}

LatLon CostGrid::centre(Cell c) const {
    return {northWest_.lat - (c.row + 0.5) * cellSizeDeg_, northWest_.lon + (c.col + 0.5) * cellSizeDeg_};
}

Cell CostGrid::cellAt(LatLon p) const {
    return {static_cast<int>(std::floor((p.lon - northWest_.lon) / cellSizeDeg_)),
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

}  // namespace oar
