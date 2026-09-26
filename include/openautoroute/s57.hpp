#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "openautoroute/geo.hpp"

namespace oar {

enum class Geometry { Point, Line, Area };

/// A run of vertices. For areas, `hole` marks an interior ring (an island inside a water polygon, say).
struct Ring {
    std::vector<LatLon> points;
    bool hole = false;
};

/// One chart object reduced to what routing needs. Attributes the object does not carry stay NaN.
struct ChartFeature {
    std::string objectClass;  // S-57 acronym, e.g. "DEPARE", "DEPCNT", "LNDARE", "FAIRWY", "TSSLPT", "WRECKS"
    Geometry geometry = Geometry::Point;
    std::vector<Ring> parts;  // Point: one ring, one vertex. Line: one polyline. Area: exterior + hole rings.
    double drval1 = std::numeric_limits<double>::quiet_NaN();  // DEPARE: shallowest depth in the area (m)
    double drval2 = std::numeric_limits<double>::quiet_NaN();  // DEPARE: deepest depth in the area (m)
    double valdco = std::numeric_limits<double>::quiet_NaN();  // DEPCNT: contour depth (m)
    double valsou = std::numeric_limits<double>::quiet_NaN();  // OBSTRN/WRECKS/UWTROC/SOUNDG: sounding (m)
    double orient = std::numeric_limits<double>::quiet_NaN();  // TSSLPT/FAIRWY: orientation, degrees true
    double watlev = std::numeric_limits<double>::quiet_NaN();  // OBSTRN/WRECKS/UWTROC: 1 partly submerged, 2 always dry, 3 submerged, 4 covers and uncovers, 5 awash, 6 flooding
    double verclr = std::numeric_limits<double>::quiet_NaN();  // BRIDGE/CBLOHD/PIPOHD: vertical clearance (m)
    double verccl = std::numeric_limits<double>::quiet_NaN();  // BRIDGE: vertical clearance with the bridge closed (m)
    double catcam = std::numeric_limits<double>::quiet_NaN();  // BOYCAR/BCNCAR: 1 north, 2 east, 3 south, 4 west cardinal mark
    uint32_t restrn = 0;  // RESARE: bitmask of RESTRN values (bit n set = value n, e.g. bit 7 entry prohibited, bit 14 area to be avoided)
    uint32_t catrea = 0;  // RESARE: bitmask of CATREA values (bit 1 offshore safety zone, 9 military area, 14 minefield, ...)
    double catlam = std::numeric_limits<double>::quiet_NaN();  // BOYLAT/BCNLAT: 1 port-hand, 2 starboard-hand, 3/4 preferred channel
};

struct ChartData {
    std::vector<ChartFeature> features;
};

/// Read routing-relevant objects from an S-57 ENC base cell (`.000`). Kept: COALNE, DEPARE, DEPCNT, DRGARE, FAIRWY,
/// LNDARE, OBSTRN, PRCARE (precautionary area), BOYLAT/BCNLAT (lateral marks), the hazard, restriction, structure, bridge and cardinal-mark classes listed in classOf() in s57.cpp, SOUNDG (one point per sounding), TSELNE, TSSBND, TSSCRS, TSSLPT, TSSRON, TSEZNE, UWTROC, WRECKS. Update files
/// (`.001`, ...) are not applied. Pure C++ (ISO 8211 reader); no GDAL needed. On failure returns false, fills `error`.
bool loadS57(const std::string& path, ChartData& out, std::string& error);

/// Same as loadS57 for a cell already in memory. Exposed so tests can feed hand-built records.
bool loadS57Buffer(const std::vector<uint8_t>& bytes, ChartData& out, std::string& error);

}  // namespace oar
