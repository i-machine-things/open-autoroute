// Parse every ENC base cell under a directory and summarise: failures, timing and feature counts per class.
// Usage: s57_scan <dir-with-ENC_ROOT> — meant for bulk validation on a machine with the full NOAA set.
#include <chrono>
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
    size_t features = 0, emptyCells = 0;
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
        for (const auto& f : d.features) ++perClass[f.objectClass];
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::printf("%zu cells, %zu parsed ok, %zu failed, %zu with no routing features, %zu features, %.1fs\n",
                cells.size(), cells.size() - failures.size(), failures.size(), emptyCells, features, secs);
    for (const auto& [cls, n] : perClass) std::printf("  %-7s %zu\n", cls.c_str(), n);
    for (size_t i = 0; i < failures.size() && i < 20; ++i) std::printf("FAIL %s\n", failures[i].c_str());
    return failures.empty() ? 0 : 1;
}
