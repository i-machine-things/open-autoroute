# open-autoroute

Welcome to the official repository for the Open-Source OpenCPN Auto-Routing Plugin and Standalone App project. This repository contains a high-performance, rule-compliant pathfinding engine built as a free, open-source alternative to commercial marine navigation tools.

---

## Architecture Overview

The project relies on a shared C++ core designed for two primary deployment targets:

1. **OpenCPN Plugin (`openautoroute_pi`):** A 100% offline-capable plugin operating directly within OpenCPN using local vector charts.  
2. **Standalone App (`openautoroute-app`):** A cross-platform Flutter application (Android, iOS, Windows, macOS, Linux) that pulls real-time NOAA hydrographic data online.

---

## Building

The `v0.1.0` core is in progress. It needs a C++17 compiler and CMake:

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

No CMake? `make` builds the same thing with plain g++ (`make test` runs the tests).

S-57 charts are read by a built-in ISO 8211 parser, so there is no GDAL dependency.

### Trying a route in OpenCPN

`build/openautoroute` reads ENC cells, routes between two points and writes a GPX file. Import it in OpenCPN
(Route Manager, Import) to see it on the same charts:

```bash
build/openautoroute --enc ~/Documents/Charts/ENC_ROOT \
    --from 47.605,-122.360 --to 48.115,-122.760 \
    --draft 1.5 --clearance 1.0 -o route.gpx
```

Cells are painted coarse-to-fine, and a cell is open only when a chart positively shows it is deep enough. Land,
shoals, shallow or depth-unknown obstructions and wrecks, and water with no chart coverage are all blocked. COLREGs
Rule 9 (keeping right in narrow channels) is not applied yet.
Rule 10 (traffic separation schemes) is applied: separation zones and lines are never entered, travel against a lane's
flow is refused, and lane crossings are priced so they are made as close to square to the flow as the grid allows. What a
vessel does with lanes depends on its size, following Rule 10(j): under 20 m or a vessel under sail (`--under-sail`, engine
off; a sailboat that is motoring is power-driven and judged on length) stays out
of lanes and only crosses them, while a larger vessel is drawn into them and stays in. `--length-m` sets the length
(default 12 m), `--no-tss` switches all of this off, and `--lane-use` overrides the lane cost directly.
`--eval route.gpx` scores any GPX route, such as one from another planner, against the same rules.

The core is metric (metres, nautical miles, knots); converting to feet is left to the user interface.

Routes are also kept off the shore: cost rises within `--margin-m` (default 500 m) of any blocked water, scaled by
`--margin-weight` (default 10). Set `--margin-weight 0` to turn that off. The tool prints the closest and median
clearance it achieved. It is a development tool, not for navigation.

---

## Roadmap

Planned releases run from `v0.1.0` (rule-compliant core router) through `v0.6.0+` (current and flow modeling). See [ROADMAP.md](ROADMAP.md) for the full milestones and the validation regions the router will be tested against.

---

## Feature Comparison Matrix

| Feature / Capability | Navionics Auto-Routing | Savvy Navvy | OpenCPN Plugin & Standalone App |
| :---- | :---: | :---: | :---: |
| **Chart Basis** | Proprietary Vector | Proprietary Hydrographic | **Standard NOAA / S-57 ENCs (Free/Open)** |
| **Data Connectivity** | Cached / Cloud hybrid | Requires Cloud / Signal | **Direct NOAA Database Integration (Standalone)** |
| **COLREGs Compliance (Rule 9 / 10\)** | Basic avoidance | General avoidance | **Built-in Rule 9 & Rule 10 Enforcement** |
| **Channel-Centering Bias** | Moderate | Basic | **Explicit fairway midline weighting** |
| **Cross-Platform App** | Yes (Proprietary) | Yes (Proprietary) | **Yes (Android, iOS, PC, Mac, Linux)** |
| **Weather & Point-of-Sail** | No | Yes | **Yes (`v0.2.0`)** |
| **Polar Support** | Generic Speed Avg | Engine / Sail profile | **Full `.pol` / ORC Database Library (`v0.3.0`)** |
| **Self-Tuning Polars** | No | No | **Yes — User-Selectable via NMEA / Signal K (`v0.4.0`)** |
| **Real-Time Fuel Modeling** | Basic estimate | Basic estimate | **Yes — Basic estimate or Live Telemetry Ingestion (`v0.4.0`)** |
| **Subscription / Fee** | Paid Annual | Paid Annual | **100% Free, Open Source & Donation-Funded** |

---

## Contributing & License

Contributions, issue reports, and pull requests are welcome. This project is licensed under the GNU General Public License v3.0 (see [LICENSE](LICENSE)), matching OpenCPN's GPL ecosystem, to support the marine community.