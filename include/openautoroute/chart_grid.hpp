#pragma once

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

}  // namespace oar
