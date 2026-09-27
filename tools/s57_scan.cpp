// Parse every ENC base cell under a directory and summarise: failures, timing and feature counts per class.
// Usage: s57_scan <dir-with-ENC_ROOT> — meant for bulk validation on a machine with the full NOAA set.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "openautoroute/s57.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <dir>\n", argv[0]);
        return 2;
    }
    namespace fs = std::filesystem;
    std::vector<fs::path> cells;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (e.is_regular_file() && e.path().extension() == ".000") cells.push_back(e.path());
    }

    std::map<std::string, size_t> perClass;
    std::vector<std::string> failures;
    size_t features = 0, emptyCells = 0, openRings = 0, badCoords = 0, depareNoDepth = 0, depareInverted = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& p : cells) {
        oar::ChartData d;
        std::string err;
        if (!oar::loadS57(p.string(), d, err)) {
            failures.push_back(p.filename().string() + ": " + err);
            continue;
        }
        emptyCells += d.features.empty();
        features += d.features.size();
        for (const auto& f : d.features) {
            ++perClass[f.objectClass];
            for (const auto& ring : f.parts) {
                for (const auto& pt : ring.points) badCoords += std::fabs(pt.lat) > 90.0 || std::fabs(pt.lon) > 180.0;
                // Unclosed area rings are expected only where a cell's data limit truncates a polygon.
                const auto& pts = ring.points;
                openRings += f.geometry == oar::Geometry::Area && !pts.empty() &&
                             (pts.front().lat != pts.back().lat || pts.front().lon != pts.back().lon);
            }
            if (f.objectClass == "DEPARE") {
                depareNoDepth += std::isnan(f.drval1);
                depareInverted += !std::isnan(f.drval1) && !std::isnan(f.drval2) && f.drval1 > f.drval2;
            }
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::printf("%zu cells, %zu parsed ok, %zu failed, %zu with no routing features, %zu features, %.1fs\n",
                cells.size(), cells.size() - failures.size(), failures.size(), emptyCells, features, secs);
    std::printf("checks: %zu open area rings, %zu out-of-range vertices, %zu DEPARE without DRVAL1, %zu with DRVAL1>DRVAL2\n",
                openRings, badCoords, depareNoDepth, depareInverted);
    for (const auto& [cls, n] : perClass) std::printf("  %-7s %zu\n", cls.c_str(), n);
    for (size_t i = 0; i < failures.size() && i < 20; ++i) std::printf("FAIL %s\n", failures[i].c_str());
    return failures.empty() ? 0 : 1;
}
