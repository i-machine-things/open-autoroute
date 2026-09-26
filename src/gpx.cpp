#include "openautoroute/gpx.hpp"

#include <cstdio>

namespace oar {
namespace {

// Route names come from users; escape them so the output is always well-formed XML.
std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

}  // namespace

std::string routeToGpx(const std::vector<LatLon>& waypoints, const std::string& routeName) {
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"open-autoroute\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        "  <rte>\n    <name>" + xmlEscape(routeName) + "</name>\n";
    char buf[128];
    int n = 1;
    for (const LatLon& p : waypoints) {
        std::snprintf(buf, sizeof buf, "    <rtept lat=\"%.6f\" lon=\"%.6f\"><name>WP%03d</name></rtept>\n", p.lat, p.lon, n++);
        out += buf;
    }
    out += "  </rte>\n</gpx>\n";
    return out;
}

}  // namespace oar
