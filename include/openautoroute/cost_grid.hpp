#pragma once

#include <limits>
#include <vector>

#include "openautoroute/geo.hpp"

namespace oar {

/// Cost value for a cell no vessel may enter (land, water shallower than draft + clearance).
constexpr float kBlocked = std::numeric_limits<float>::infinity();

struct Cell {
    int col = 0;
    int row = 0;
    bool operator==(const Cell& o) const { return col == o.col && row == o.row; }
};

/// A 2D safety cost matrix over a regular lat/lon grid. Every open cell costs at least 1.0 so that the
/// pathfinder's distance heuristic stays admissible; hazards raise the cost, hard barriers are kBlocked.
class CostGrid {
public:
    /// `northWest` is the outer corner of cell (0, 0); rows run south, columns run east. Cells are `cellSizeDeg` tall;
    /// pass `cellSizeLonDeg` (> 0) to make them wider than tall in degrees so they stay square in metres at latitude.
    CostGrid(int cols, int rows, LatLon northWest, double cellSizeDeg, double cellSizeLonDeg = 0.0);

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    double cellSizeDeg() const { return cellSizeDeg_; }
    double cellSizeLonDeg() const { return cellSizeLonDeg_; }
    bool inBounds(Cell c) const { return c.col >= 0 && c.col < cols_ && c.row >= 0 && c.row < rows_; }

    float cost(Cell c) const { return cost_[index(c)]; }
    bool blocked(Cell c) const { return cost(c) == kBlocked; }
    void setCost(Cell c, float cost) { cost_[index(c)] = cost; }
    void fill(float cost) { cost_.assign(cost_.size(), cost); }

    /// Traffic-lane flow direction (degrees true) for a cell inside a traffic separation scheme lane, NaN elsewhere.
    /// The router uses it to keep vessels going the right way in a lane and crossing lanes near 90 degrees (COLREGs
    /// Rule 10). Kept apart from the cost because it changes the cost of a step depending on the heading.
    void setLaneDirection(Cell c, float degrees) {
        if (lane_.empty()) lane_.assign(cost_.size(), std::numeric_limits<float>::quiet_NaN());
        lane_[index(c)] = degrees;
    }
    float laneDirection(Cell c) const {
        return lane_.empty() ? std::numeric_limits<float>::quiet_NaN() : lane_[index(c)];
    }

    /// Cost multiplier for travelling with the flow inside a traffic lane (default 1). Above 1 a vessel avoids running
    /// along lanes (small craft and sailing vessels, which under Rule 10 should stay out of the scheme where practicable
    /// and must not impede ships using it). Below 1 a vessel is drawn into lanes and stays in them (large ships).
    /// Clamped to [0.1, inf); the pathfinder scales its distance estimate by min(1, factor) to stay admissible.
    void setLaneUseFactor(double factor) { laneUseFactor_ = factor < 0.1 ? 0.1f : static_cast<float>(factor); }
    float laneUseFactor() const { return laneUseFactor_; }

    LatLon centre(Cell c) const;
    /// Cell containing `p`; the result may be out of bounds, check with inBounds().
    Cell cellAt(LatLon p) const;

    /// Hard barrier: block every cell whose charted depth is shallower than `draftM + clearanceM`.
    /// `depthM` is row-major, one value per cell; NaN means "no depth data" and is left open.
    void applyDepthBarrier(const std::vector<float>& depthM, double draftM, double clearanceM);

    /// Multiply the cost of open cells by up to `1 + penalty`, rising exponentially with distance from a
    /// fairway centreline, so routes prefer the middle of a channel over its edges.
    /// `distToCentreM` is row-major; cells farther than `halfWidthM` get the full penalty.
    void applyChannelCentering(const std::vector<float>& distToCentreM, double halfWidthM, double penalty);

    /// Distance in metres from each cell centre to the nearest blocked cell centre (0 for blocked cells; a very large
    /// value everywhere if the grid has no blocked cells). Chamfer approximation, accurate to a few percent.
    std::vector<float> distanceToBlockedM() const;

    /// Steer routes away from land, shoals and uncharted water: open cells within `rangeM` of a blocked cell get their
    /// cost multiplied by `1 + weight * (1 - d / rangeM)^2`, so the cheapest line keeps well off the shore and, in a
    /// narrow channel, drifts toward the middle. Blocked cells are untouched; rangeM <= 0 or weight <= 0 does nothing.
    void applyShoreMargin(double rangeM, double weight);

    /// Metres per cell along a row / column at the grid's latitude (cells are meant to be square in metres).
    double cellSizeM() const { return cellSizeDeg_ * 111320.0; }

private:
    size_t index(Cell c) const { return static_cast<size_t>(c.row) * cols_ + c.col; }

    int cols_;
    int rows_;
    LatLon northWest_;
    double cellSizeDeg_;
    double cellSizeLonDeg_;
    std::vector<float> cost_;
    std::vector<float> lane_;  // empty until a lane is set
    float laneUseFactor_ = 1.0f;
};

}  // namespace oar
