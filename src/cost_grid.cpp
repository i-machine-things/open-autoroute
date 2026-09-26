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

std::vector<float> CostGrid::chamferM(std::vector<float> d) const {
    // Two-pass chamfer transform in cell units (weights 1 and sqrt 2), then scale to metres. Seeds are the zero cells.
    const float inf = 1e30f, diag = 1.41421356f;
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

std::vector<float> CostGrid::distanceToBlockedM() const {
    std::vector<float> d(cost_.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = cost_[i] == kBlocked ? 0.0f : 1e30f;
    return chamferM(std::move(d));
}

std::vector<float> CostGrid::distanceToLaneM() const {
    std::vector<float> d(cost_.size(), 1e30f);
    for (size_t i = 0; i < d.size(); ++i) {
        if ((!lane_.empty() && !std::isnan(lane_[i])) || (!zone_.empty() && zone_[i])) d[i] = 0.0f;
    }
    return chamferM(std::move(d));
}

void CostGrid::assignZoneDirections() {
    if (zone_.empty() || lane_.empty()) return;
    // Breadth-first from every lane cell; the direction spreads only through zone cells, so each zone cell takes the direction
    // of the lane it borders (zones run parallel to the lanes on either side of them).
    std::vector<size_t> frontier;
    for (size_t i = 0; i < lane_.size(); ++i) {
        if (!std::isnan(lane_[i])) frontier.push_back(i);
    }
    std::vector<float> dir = lane_;  // direction carried by each frontier cell
    while (!frontier.empty()) {
        std::vector<size_t> next;
        for (size_t i : frontier) {
            const int c = static_cast<int>(i % cols_), r = static_cast<int>(i / cols_);
            const int nc[4] = {c + 1, c - 1, c, c};
            const int nr[4] = {r, r, r + 1, r - 1};
            for (int k = 0; k < 4; ++k) {
                if (nc[k] < 0 || nc[k] >= cols_ || nr[k] < 0 || nr[k] >= rows_) continue;
                const size_t j = static_cast<size_t>(nr[k]) * cols_ + nc[k];
                if (!zone_[j] || !std::isnan(zoneDir_[j])) continue;
                zoneDir_[j] = dir[i];
                dir[j] = dir[i];
                next.push_back(j);
            }
        }
        frontier.swap(next);
    }
}

void CostGrid::applyLaneMargin(double rangeM, double weight) {
    if ((lane_.empty() && zone_.empty()) || rangeM <= 0.0 || weight <= 0.0) return;
    const std::vector<float> dist = distanceToLaneM();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    marginWeight_.assign(cost_.size(), 0.0f);
    marginAxis_.assign(cost_.size(), nan);
    // Axis of the nearest lane: breadth-first outward from every lane cell (zone cells carry their derived direction), so each
    // nearby cell inherits the direction of the closest lane. Only cells inside the margin range are ever visited.
    std::vector<size_t> frontier;
    for (size_t i = 0; i < cost_.size(); ++i) {
        float dir = nan;
        if (!lane_.empty() && !std::isnan(lane_[i])) dir = lane_[i];
        else if (!zone_.empty() && zone_[i] && !std::isnan(zoneDir_[i])) dir = zoneDir_[i];
        if (!std::isnan(dir)) { marginAxis_[i] = dir; frontier.push_back(i); }
    }
    while (!frontier.empty()) {
        std::vector<size_t> next;
        for (size_t i : frontier) {
            const int c = static_cast<int>(i % cols_), r = static_cast<int>(i / cols_);
            const int nc[4] = {c + 1, c - 1, c, c}, nr[4] = {r, r, r + 1, r - 1};
            for (int k = 0; k < 4; ++k) {
                if (nc[k] < 0 || nc[k] >= cols_ || nr[k] < 0 || nr[k] >= rows_) continue;
                const size_t j = static_cast<size_t>(nr[k]) * cols_ + nc[k];
                if (!std::isnan(marginAxis_[j]) || dist[j] >= rangeM) continue;
                marginAxis_[j] = marginAxis_[i];
                next.push_back(j);
            }
        }
        frontier.swap(next);
    }
    for (size_t i = 0; i < cost_.size(); ++i) {
        if (cost_[i] == kBlocked || dist[i] == 0.0f || dist[i] >= rangeM || std::isnan(marginAxis_[i])) continue;  // lane/zone cells are not re-priced
        const double t = 1.0 - dist[i] / rangeM;
        marginWeight_[i] = static_cast<float>(weight * t * t);
    }
}

float CostGrid::laneMarginFactor(Cell c, double headingDeg) const {
    if (marginWeight_.empty()) return 1.0f;
    const size_t i = index(c);
    if (marginWeight_[i] == 0.0f) return 1.0f;
    // cos^2 of the angle between the move and the lane axis: 1 when running alongside (either way), 0 when square across. A
    // 20% floor keeps a little cost on passing close by at a right angle (the tip of a lane part), so it is not entirely free.
    const double diff = (headingDeg - marginAxis_[i]) * 3.14159265358979 / 180.0;
    const double along = 0.2 + 0.8 * std::cos(diff) * std::cos(diff);
    return static_cast<float>(1.0 + marginWeight_[i] * along);
}

std::vector<float> CostGrid::distanceToChannelM() const {
    std::vector<float> d(cost_.size(), 1e30f);
    for (size_t i = 0; i < d.size(); ++i) {
        if (!channel_.empty() && channel_[i]) d[i] = 0.0f;
    }
    return chamferM(std::move(d));
}

void CostGrid::applyChannelPreference(double rangeM, double penalty) {
    if (channel_.empty() || rangeM <= 0.0 || penalty <= 1.0) return;
    const std::vector<float> dist = distanceToChannelM();
    for (size_t i = 0; i < cost_.size(); ++i) {
        if (cost_[i] == kBlocked || channel_[i] || dist[i] >= rangeM) continue;
        cost_[i] *= static_cast<float>(penalty);
    }
}

float CostGrid::gateSideFactor(Cell c, double headingDeg) const {
    if (gateT_.empty() || gateSideWeight_ <= 0.0f) return 1.0f;
    const size_t i = index(c);
    const float t = gateT_[i];
    if (std::isnan(t)) return 1.0f;
    const double diff = (headingDeg - gateAxis_[i]) * 3.14159265358979 / 180.0;
    const double along = std::cos(diff);                       // 1 going with the buoyage direction, -1 against it
    if (std::fabs(along) < 0.5) return 1.0f;                   // crossing the channel: no side to keep to
    const double preferred = along > 0 ? 0.7 : 0.3;            // the vessel's own starboard side of the channel
    const double off = t - preferred;
    return static_cast<float>(1.0 + gateSideWeight_ * 4.0 * off * off);
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
