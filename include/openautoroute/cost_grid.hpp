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

private:
    size_t index(Cell c) const { return static_cast<size_t>(c.row) * cols_ + c.col; }

    int cols_;
    int rows_;
    LatLon northWest_;
    double cellSizeDeg_;
    double cellSizeLonDeg_;
    std::vector<float> cost_;
};

}  // namespace oar
