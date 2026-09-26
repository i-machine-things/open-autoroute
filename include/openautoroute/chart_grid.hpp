#pragma once

#include <vector>

#include "openautoroute/cost_grid.hpp"
#include "openautoroute/s57.hpp"

namespace oar {

/// Paint one chart's verdict onto `grid`: only cells the chart actually covers (any DEPARE or LNDARE polygon) are
/// overwritten, so stamping charts from coarse to fine scale lets the largest-scale chart win where it has data.
///
/// Safety rules (a cell is open only if the chart positively says it is deep enough):
///   - DEPARE or DRGARE (dredged) with DRVAL1 >= minDepthM is open; shallower, or DRVAL1 unknown, is blocked
///   - LNDARE is blocked
///   - OBSTRN / WRECKS / UWTROC / SOUNDG points shoaler than minDepthM block their cell; an OBSTRN, WRECKS or UWTROC
///     with no depth value is treated as unsafe
/// Cells no chart covers stay whatever the caller filled the grid with (normally kBlocked: no data is not safe water).
///
/// With `applyTss` (default) the chart's traffic separation scheme is applied too, for COLREGs Rule 10: TSSLPT lane parts
/// store their flow direction (ORIENT) on the grid for the router.
///
/// Separation zones (TSEZNE) and separation lines (TSELNE) become zone cells: crossable square on, never along (see
/// CostGrid::setZone; call grid.assignZoneDirections() after stamping all charts). Precautionary areas (PRCARE) cost
/// `cautionFactor` times normal to move through, since vessels there must navigate with particular caution.
void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid, bool applyTss = true, double cautionFactor = 1.0);
/// Fairways (FAIRWY) and dredged areas (DRGARE) are also recorded as channel cells (the dashed limits drawn on a chart) for
/// CostGrid::applyChannelPreference; they are never made cheaper or dearer by stampChart itself.

/// A port-hand or starboard-hand lateral mark (red/green buoy or beacon). Category is CATLAM: 1 port-hand, 2 starboard-hand.
struct LateralMark {
    LatLon at;
    int category = 0;
};

/// A pair of opposite lateral marks across a channel: a vessel following the channel passes between them.
struct Gate {
    LatLon port;
    LatLon starboard;
};

/// Append the port-hand and starboard-hand lateral marks (BOYLAT, BCNLAT) of a chart to `out`.
void collectLateralMarks(const ChartData& chart, std::vector<LateralMark>& out);

/// Turn lateral marks into a buoyed corridor. Each port-hand mark is paired with its nearest starboard-hand mark when the two
/// are each other's nearest opposite within `maxGateM` (across the channel); the limit is what defines a NARROW channel, so a
/// wide pair of marks (a bay, a traffic scheme) makes no gate and changes nothing. Along the stretch of channel each gate is
/// responsible for (half the way to its neighbours), cells beyond either mark, out to `lateralRangeM`, cost `outsidePenalty`
/// times normal so a route stays between the marks, and cells between them record their position across the channel so the
/// router can prefer the starboard side (COLREGs Rule 9, weight `sideWeight`, see CostGrid::gateSideFactor). Call once after all
/// charts are stamped and before any margins. Returns the gates found, for reporting.
std::vector<Gate> applyChannelGates(CostGrid& grid, const std::vector<LateralMark>& marks, double maxGateM = 500.0,
                                    double outsidePenalty = 8.0, double sideWeight = 1.5, double lateralRangeM = 2500.0);

}  // namespace oar
