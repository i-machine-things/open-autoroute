#pragma once

#include <vector>

#include "openautoroute/cost_grid.hpp"

namespace oar {

/// Cost multiplier for travelling on `headingDeg` through a traffic-lane cell whose flow is `laneDeg` (COLREGs Rule 10):
///   within 25 degrees of the flow: 1 (using the lane normally)
///   within 25 degrees of against the flow: kBlocked (wrong-way travel is not allowed)
///   otherwise it is a crossing: 3 at exactly 90 degrees, rising toward 9 as the crossing gets more oblique, so the
///   router spends as little time as possible in the lane and prefers to cross at right angles
float laneFactor(double headingDeg, double laneDeg);

/// A* over an 8-connected grid, followed by a line-of-sight pass that removes the staircase artefacts of grid
/// search (any-angle output). Returns waypoints as cell-centre coordinates, start and goal included;
/// empty when no route exists or either endpoint is blocked or out of bounds.
std::vector<LatLon> findRoute(const CostGrid& grid, LatLon start, LatLon goal);

/// True when the straight segment between two cells crosses no blocked cell. Exposed for testing.
bool lineOfSight(const CostGrid& grid, Cell a, Cell b);

}  // namespace oar
