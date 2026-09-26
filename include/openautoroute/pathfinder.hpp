#pragma once

#include <vector>

#include "openautoroute/cost_grid.hpp"

namespace oar {

/// A* over an 8-connected grid, followed by a line-of-sight pass that removes the staircase artefacts of grid
/// search (any-angle output). Returns waypoints as cell-centre coordinates, start and goal included;
/// empty when no route exists or either endpoint is blocked or out of bounds.
std::vector<LatLon> findRoute(const CostGrid& grid, LatLon start, LatLon goal);

/// True when the straight segment between two cells crosses no blocked cell. Exposed for testing.
bool lineOfSight(const CostGrid& grid, Cell a, Cell b);

}  // namespace oar
