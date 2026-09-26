#pragma once

#include <string>
#include <vector>

#include "openautoroute/geo.hpp"

namespace oar {

/// Serialise waypoints as a GPX 1.1 route (<rte>), the format OpenCPN's Route Manager imports.
std::string routeToGpx(const std::vector<LatLon>& waypoints, const std::string& routeName);

}  // namespace oar
