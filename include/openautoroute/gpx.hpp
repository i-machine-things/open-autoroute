#pragma once

#include <string>
#include <vector>

#include "openautoroute/geo.hpp"

namespace oar {

/// Serialise waypoints as a GPX 1.1 route (<rte>), the format OpenCPN's Route Manager imports. OpenCPN fills its From and To columns
/// from the names of the first and last waypoints, so `startName` and `endName` (default WP001 and the last WPnnn) are worth setting to
/// something a person can tell routes apart by. `description`, if given, is written as the route's <desc>.
std::string routeToGpx(const std::vector<LatLon>& waypoints, const std::string& routeName, const std::string& startName = "",
                       const std::string& endName = "", const std::string& description = "");

}  // namespace oar
