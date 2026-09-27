#pragma once

#include <cstdint>
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

    /// Physical hazards a chart reports (wrecks, rocks, structures, a bridge with a charted low clearance) are remembered apart from the
    /// depth verdict, so a finer chart that only re-draws the depth areas cannot erase a hazard that only a coarser chart carries.
    bool isHazardShut(Cell c) const { return !hazardShut_.empty() && hazardShut_[index(c)]; }
    void setHazardShut(Cell c) {
        if (hazardShut_.empty()) hazardShut_.assign(cost_.size(), 0);
        hazardShut_[index(c)] = 1;
    }

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
    /// along lanes (small craft and sailing vessels, which under Rule 10(d) and 10(j) should stay out of the scheme where practicable
    /// and must not impede ships using it). Below 1 a vessel is drawn into lanes and stays in them (large ships).
    /// Clamped to [0.1, inf); the pathfinder scales its distance estimate by min(1, factor) to stay admissible.
    void setLaneUseFactor(double factor) { laneUseFactor_ = factor < 0.1 ? 0.1f : static_cast<float>(factor); }
    float laneUseFactor() const { return laneUseFactor_; }

    /// Separation zone / separation line cells of a traffic separation scheme (COLREGs Rule 10). A vessel may cross one square
    /// on to cross the scheme but never travel along it. Its direction is not charted, so assignZoneDirections() takes it from
    /// the nearest lane; a zone cell with no direction is treated as impassable.
    void setZone(Cell c) {
        if (zone_.empty()) { zone_.assign(cost_.size(), 0); zoneDir_.assign(cost_.size(), std::numeric_limits<float>::quiet_NaN()); }
        zone_[index(c)] = 1;
    }
    bool isZone(Cell c) const { return !zone_.empty() && zone_[index(c)] != 0; }
    float zoneDirection(Cell c) const { return zoneDir_.empty() ? std::numeric_limits<float>::quiet_NaN() : zoneDir_[index(c)]; }
    /// Give each zone cell the flow direction of the lane it borders (spread along the zone). Call once after all charts are
    /// stamped; zone cells that touch no lane keep no direction.
    void assignZoneDirections();

    /// Precautionary-area cells (where lanes converge and vessels must take particular care). Their extra cost is in cost(); this
    /// records where they are so a finished route can be checked and reported.
    void setCaution(Cell c) {
        if (caution_.empty()) caution_.assign(cost_.size(), 0);
        caution_[index(c)] = 1;
    }
    bool isCaution(Cell c) const { return !caution_.empty() && caution_[index(c)] != 0; }

    /// Cells inside a charted channel (fairway or dredged area, the dashed limits on a chart). Only the narrow stretches matter:
    /// markNarrowChannels() picks those out, and applyChannelPreference() makes leaving them costly so a route stays inside.
    void setChannel(Cell c) {
        if (channel_.empty()) channel_.assign(cost_.size(), 0);
        channel_[index(c)] = 1;
    }
    bool isChannel(Cell c) const { return !channel_.empty() && channel_[index(c)] != 0; }
    /// Flag the channel cells whose channel is narrow: local width (twice the largest distance from a channel cell to the nearest
    /// non-channel cell, searched within `maxWidthM`) of at most `maxWidthM`. Wide fairways, bays and traffic schemes are left
    /// alone, so the channel rules only act where a marked channel really confines a vessel.
    void markNarrowChannels(double maxWidthM);
    /// Distance in metres from each cell centre to the nearest narrow-channel cell (0 inside one; very large without any).
    std::vector<float> distanceToNarrowChannelM() const;
    bool isNarrowChannel(Cell c) const { return !narrow_.empty() && narrow_[index(c)] != 0; }
    /// Keep a vessel between the dashed limits of a NARROW channel. Open cells outside every charted channel but within `rangeM` of a
    /// narrow one remember the direction of the channel edge beside them; channelMarginFactor() then charges `1 + weight` for a move
    /// running along that edge and almost nothing for a move across it, so cutting a bend outside the limits or running alongside
    /// them costs, while crossing the channel stays free. Wide channels are ignored, and cells of a traffic lane, separation zone or
    /// precautionary area are never touched, so this cannot interfere with the traffic separation rules.
    void applyChannelPreference(double rangeM, double weight);
    /// Cost multiplier (>= 1) for a move on `headingDeg` through `c` from the channel-limit preference; 1 where it does not apply.
    float channelMarginFactor(Cell c, double headingDeg) const;

    /// Position across a buoyed channel for a cell near a red/green gate: t runs 0 at the port-hand mark to 1 at the starboard-hand
    /// mark, and `axisDeg` is the direction of buoyage (the way a vessel "returning from sea" travels). Used by gateSideFactor().
    void setGateCell(Cell c, float t, float axisDeg) {
        if (gateT_.empty()) {
            gateT_.assign(cost_.size(), std::numeric_limits<float>::quiet_NaN());
            gateAxis_.assign(cost_.size(), 0.0f);
        }
        gateT_[index(c)] = t;
        gateAxis_[index(c)] = axisDeg;
    }
    void setGateSideWeight(double w) { gateSideWeight_ = w < 0.0 ? 0.0f : static_cast<float>(w); }
    /// Rule 9: in a narrow channel keep to the starboard (right-hand) side. Cost multiplier (>= 1) for a move on `headingDeg` through
    /// `c`: 1 outside gate corridors, otherwise growing with distance from the preferred side. Rule 9(a) says to keep "as near to the outer
    /// limit of the channel or fairway which lies on her starboard side as is safe and practicable": t = 0.8 going with the direction of
    /// buoyage, t = 0.2 going against it (where the vessel's own starboard side lies), a fifth of the width in from the limit for leeway. Moves across the channel are not charged.
    float gateSideFactor(Cell c, double headingDeg) const;

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

    /// Distance in metres from each cell centre to the nearest traffic-lane or separation-zone cell (0 inside one; very large everywhere if
    /// the grid has no lanes).
    std::vector<float> distanceToLaneM() const;

    /// Keep vessels clear of traffic lanes by a margin, but only when they travel along them. Open cells within `rangeM` of a
    /// lane or separation-zone cell get a margin weight `weight * (1 - d / rangeM)^2`, and remember the axis of the nearest
    /// lane. laneMarginFactor() then charges `1 + weight` scaled by how parallel the move is to that axis, so skimming a lane's
    /// edge or the end of a lane part is dear while approaching it square on to cross costs nothing extra. For small craft under
    /// Rule 10(h), which says a vessel not using a scheme shall avoid it "by as wide a margin as is practicable". No-op without lanes.
    void applyLaneMargin(double rangeM, double weight);
    /// Cost multiplier (>= 1) for a move on `headingDeg` through `c` from the lane margin; 1 where no margin applies.
    float laneMarginFactor(Cell c, double headingDeg) const;

    /// Metres per cell along a row / column at the grid's latitude (cells are meant to be square in metres).
    double cellSizeM() const { return cellSizeDeg_ * 111320.0; }

private:
    std::vector<float> chamferM(std::vector<float> d) const;  // shared distance transform, seeds are the zero cells
    size_t index(Cell c) const { return static_cast<size_t>(c.row) * cols_ + c.col; }

    int cols_;
    int rows_;
    LatLon northWest_;
    double cellSizeDeg_;
    double cellSizeLonDeg_;
    std::vector<float> cost_;
    std::vector<uint8_t> hazardShut_;  // empty until a hazard blocks a cell
    std::vector<float> lane_;  // empty until a lane is set
    std::vector<float> marginWeight_;  // empty until applyLaneMargin sets it
    std::vector<float> marginAxis_;    // degrees, axis of the nearest lane
    std::vector<uint8_t> caution_;  // empty until a precautionary-area cell is set
    std::vector<uint8_t> channel_;  // empty until a channel cell is set
    std::vector<uint8_t> narrow_;   // empty until markNarrowChannels finds one
    std::vector<float> chanWeight_;  // empty until applyChannelPreference sets it
    std::vector<float> chanAxis_;    // degrees, direction of the nearest narrow channel edge
    std::vector<float> gateT_;      // NaN outside gate corridors; empty until a gate is applied
    std::vector<float> gateAxis_;
    float gateSideWeight_ = 0.0f;
    std::vector<uint8_t> zone_;   // empty until a zone cell is set
    std::vector<float> zoneDir_;
    float laneUseFactor_ = 1.0f;
};

}  // namespace oar
