#pragma once

#include <cmath>

namespace oar {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371008.8;

struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

inline double deg2rad(double d) { return d * kPi / 180.0; }

/// Great-circle distance in metres. Haversine is accurate enough for chart-scale legs and avoids the
/// edge cases of the spherical law of cosines for very short distances.
inline double haversineM(LatLon a, LatLon b) {
    const double dlat = deg2rad(b.lat - a.lat);
    const double dlon = deg2rad(b.lon - a.lon);
    const double h = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(deg2rad(a.lat)) * std::cos(deg2rad(b.lat)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(h));
}

}  // namespace oar
