#pragma once

#include "openautoroute/cost_grid.hpp"
#include "openautoroute/s57.hpp"

namespace oar {

/// Paint one chart's verdict onto `grid`: only cells the chart actually covers (any DEPARE or LNDARE polygon) are
/// overwritten, so stamping charts from coarse to fine scale lets the largest-scale chart win where it has data.
///
/// Safety rules (a cell is open only if the chart positively says it is deep enough):
///   - DEPARE with DRVAL1 >= minDepthM is open; shallower, or DRVAL1 unknown, is blocked
///   - LNDARE is blocked
///   - OBSTRN / WRECKS / UWTROC / SOUNDG points shoaler than minDepthM block their cell; an OBSTRN, WRECKS or UWTROC
///     with no depth value is treated as unsafe
/// Cells no chart covers stay whatever the caller filled the grid with (normally kBlocked: no data is not safe water).
void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid);

}  // namespace oar
