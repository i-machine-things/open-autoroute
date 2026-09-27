#include "openautoroute/gpx.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace oar {
namespace {

// Route names come from users; escape them so the output is always well-formed XML.
std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        // XML 1.0 forbids control characters other than tab, newline and carriage return, even escaped.
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
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

std::string routeToGpx(const std::vector<LatLon>& waypoints, const std::string& routeName, const std::string& startName,
                       const std::string& endName, const std::string& description) {
    // Format through a stream imbued with the classic locale: printf-style formatting would write "46,100000" under a
    // comma-decimal locale, which is not a valid GPX number.
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<gpx version=\"1.1\" creator=\"open-autoroute\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
        << "  <rte>\n    <name>" << xmlEscape(routeName) << "</name>\n";
    if (!description.empty()) out << "    <desc>" << xmlEscape(description) << "</desc>\n";
    out << std::fixed << std::setprecision(6);
    int n = 1;
    for (const LatLon& p : waypoints) {
        if (!std::isfinite(p.lat) || !std::isfinite(p.lon) || std::fabs(p.lat) > 90.0 || std::fabs(p.lon) > 180.0) {
            throw std::invalid_argument("waypoint coordinates must be finite and within lat/lon range");
        }
        const bool first = n == 1, last = n == static_cast<int>(waypoints.size());
        out << "    <rtept lat=\"" << p.lat << "\" lon=\"" << p.lon << "\"><name>";
        if (first && !startName.empty()) out << xmlEscape(startName);
        else if (last && !endName.empty()) out << xmlEscape(endName);
        else out << "WP" << std::setw(3) << std::setfill('0') << n << std::setfill(' ');
        out << "</name></rtept>\n";
        ++n;
    }
    out << "  </rte>\n</gpx>\n";
    return out.str();
}

}  // namespace oar
