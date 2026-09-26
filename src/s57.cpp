#include "openautoroute/s57.hpp"

namespace oar {

bool loadS57(const std::string& path, ChartData& out, std::string& error) {
    (void)path;
    (void)out;
#ifdef OAR_WITH_GDAL
    // TODO(v0.1.0): open with GDAL's S57 driver and map DEPCNT/LNDARE/FAIRWY/TSS/hazard layers to ChartFeature.
    error = "S-57 parsing is not implemented yet";
#else
    error = "built without GDAL; configure with -DOAR_WITH_GDAL=ON";
#endif
    return false;
}

}  // namespace oar
