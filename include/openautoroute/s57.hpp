#pragma once

#include <string>
#include <vector>

#include "openautoroute/geo.hpp"

namespace oar {

/// One chart object reduced to what routing needs: its S-57 class and its outline in lat/lon.
struct ChartFeature {
    std::string objectClass;  // e.g. "DEPCNT", "LNDARE", "FAIRWY", "TSSLPT", "OBSTRN"
    std::vector<LatLon> outline;
    double depthM = 0.0;  // valid for DEPCNT / DEPARE / OBSTRN
};

struct ChartData {
    std::vector<ChartFeature> features;
};

/// Read routing-relevant objects from an S-57 ENC cell (v0.1.0: DEPCNT, LNDARE, FAIRWY, TSS, hazards).
/// Requires the OAR_WITH_GDAL build option. On failure returns false and fills `error`.
bool loadS57(const std::string& path, ChartData& out, std::string& error);

}  // namespace oar
