#pragma once

#include <vector>

#include "openautoroute/cost_grid.hpp"

namespace oar {

/// Cost multiplier for travelling on `headingDeg` through a traffic-lane cell whose flow is `laneDeg` (COLREGs Rule 10):
///   within 25 degrees of the flow: 1 (using the lane normally)
///   within 25 degrees of against the flow: kBlocked (wrong-way travel is not allowed)
///   otherwise it is a crossing: 2 at exactly 90 degrees, rising steeply as the crossing gets more oblique (about 6 at
///   60 degrees to the flow, 17 at 30, 19 at 25), so the router spends as little time as possible in the lane and only
///   crosses at right angles, not just at the cheapest place
float laneFactor(double headingDeg, double laneDeg);

/// A* over an 8-connected grid, followed by a line-of-sight pass that removes the staircase artefacts of grid
/// search (any-angle output). Returns waypoints as cell-centre coordinates, start and goal included;
/// empty when no route exists or either endpoint is blocked or out of bounds.
///
/// `simplifyTolerance` (a fraction, e.g. 0.05) lets a straight shortcut replace a stretch of the path even when it costs
/// up to that much more, which merges near-equal detours into single legs and cuts the waypoint count. Hard limits (blocked
/// cells, wrong-way lane travel) are never traded away. 0 keeps every waypoint the cost comparison requires.
///
/// `rawPath`, if given, receives the unsmoothed grid cells A* chose, for debugging (see the CLI's --map).
std::vector<LatLon> findRoute(const CostGrid& grid, LatLon start, LatLon goal, double simplifyTolerance = 0.0,
                              std::vector<Cell>* rawPath = nullptr);

/// True when the straight segment between two cells crosses no blocked cell. Exposed for testing.
bool lineOfSight(const CostGrid& grid, Cell a, Cell b);

}  // namespace oar
