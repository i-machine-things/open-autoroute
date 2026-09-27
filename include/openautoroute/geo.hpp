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

/// Initial bearing from a to b in degrees true, 0..360. Flat-earth approximation, fine at chart-scale distances.
inline double bearingDeg(LatLon a, LatLon b) {
    const double dy = b.lat - a.lat;
    const double dx = (b.lon - a.lon) * std::cos(deg2rad((a.lat + b.lat) / 2));
    const double deg = std::atan2(dx, dy) * 180.0 / kPi;
    return deg < 0 ? deg + 360.0 : deg;
}

/// Smallest angle between two bearings, 0..180 degrees.
inline double angleDiffDeg(double a, double b) {
    const double d = std::fabs(std::fmod(a - b, 360.0));
    return d > 180.0 ? 360.0 - d : d;
}

}  // namespace oar
