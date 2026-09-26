#pragma once

namespace oar {

/// COLREGs Rule 10(j): a vessel under this length, or any sailing vessel, must not impede a power-driven vessel that is
/// following a traffic lane, so it keeps out of lanes where practicable. The core is metric throughout (metres, and
/// nautical miles / knots for distance and speed); converting from feet belongs in the user interface.
constexpr double kSmallVesselLengthM = 20.0;

struct Vessel {
    double lengthM = 12.0;
    /// True only while the vessel is actually under sail. Under Rule 3(c) a sailing vessel with its engine running is a
    /// power-driven vessel, so a sailboat that is motoring leaves this false and is judged on length like any other.
    bool underSail = false;
};

inline bool isSmallVessel(const Vessel& v) { return v.underSail || v.lengthM < kSmallVesselLengthM; }

/// Default CostGrid::setLaneUseFactor value for a vessel: small craft avoid running along lanes (they may still cross
/// them square-on), larger vessels are drawn into lanes and stay in them.
inline double defaultLaneUseFactor(const Vessel& v) { return isSmallVessel(v) ? 6.0 : 0.5; }

}  // namespace oar
