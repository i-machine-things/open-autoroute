# open-autoroute

A boat route planner that reads NOAA electronic charts and tries to draw a route a person would actually be willing to follow. It is free, open source, and early. It is a development tool, **not for navigation**: check every route against the chart yourself.

The plan is a C++ core with two front ends, an OpenCPN plugin (`openautoroute_pi`) and a standalone app (`openautoroute-app`). Neither exists yet. What exists today is the core and a command-line tool that writes a GPX file you can import into OpenCPN and look at on the same charts.

## What it does today

- Reads S-57 ENC cells directly (no GDAL) and builds a cost grid from them: depth against your draft plus clearance, land, obstructions, wrecks, unsurveyed and uncharted water.
- Finds a route across that grid, then straightens it and thins out the waypoints.
- Keeps some distance off shores, and follows the basic rules of the road it can read from a chart:
  - **Traffic separation schemes (Rule 10):** lanes are followed in the charted direction, zones are only crossed square-on, precautionary areas are avoided by small craft, and a boat under 20 m or under sail stays out of lanes where it can.
  - **Narrow channels (Rule 9):** between the dashed limits of a charted narrow channel, and between chains of red and green buoys, keeping to the starboard side.
- Tells you which restricted or dangerous areas (military and security zones, reserves and sanctuaries, dumping grounds) a route crosses, with the chart's own wording, on screen and in the GPX description. The router only prices most of these, so it can still cross one when the way round is long; read that list before you go.
- Scores any GPX route against the same rules (`--eval`), so it can be used to check a route from somewhere else.

## What it does not do

Most of the chart is still ignored. It reads about 15 of the 146 object classes in the NOAA data. Obstruction and wreck areas, unsurveyed areas, restricted areas, piers and other structures, and bridge and cable clearances are not read yet, and there is no air draft setting. There is no weather, tide or current. It does not know whether US Inland Rules or the international rules apply where you are, and it assumes the international ones. [docs/S57_OBJECTS.md](docs/S57_OBJECTS.md) lists every chart object class, what it is for and whether it is used.

## Building

You need a C++17 compiler. With CMake:

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Without CMake, `make` does the same with plain g++ (`make test` runs the tests).

## Trying a route

Point it at a folder of ENC cells (an `ENC_ROOT` directory from NOAA) and give it two positions:

```bash
build/openautoroute --enc ~/Documents/Charts/ENC_ROOT \
    --from 47.605,-122.360 --to 48.115,-122.760 \
    --draft 1.5 --clearance 1.0 --length-m 12 -o route.gpx
```

In OpenCPN, open the Route & Mark Manager and use Import GPX. The route sits on the same charts it was planned from.

Options you are likely to want:

| Option | Meaning |
|---|---|
| `--draft`, `--clearance` | Depth needed is draft plus clearance, in metres (defaults 1.5 and 1.0) |
| `--length-m` | Vessel length in metres (default 12); under 20 m stays out of traffic lanes |
| `--under-sail` | The vessel is sailing, not motoring; treated as small at any length |
| `--cell-m` | Grid size in metres (default 30); smaller is finer and slower |
| `--min-leg-m` | Preferred minimum distance between waypoints (default 460 m) |
| `--eval route.gpx` | Score an existing route instead of planning one |
| `--map LAT,LON,CELLS` | Print an ASCII picture of the grid and route around a point, for debugging |

The core works in metres and nautical miles. Converting to feet is left to whatever front end sits on top.

## How it is tested

- `make test` runs unit tests on small hand-built grids and chart records.
- `benchmarks/run.sh` plans 28 routes across the Columbia River, Puget Sound and the San Juans, San Francisco, Los Angeles, New York, Chesapeake Bay, Boston, Houston, the Keys, Lake Michigan, Maine and Hawaii, and prints one scored table. `benchmarks/compare.sh` compares two runs and flags anything that got less safe. Several of those routes still fail (Deception Pass, Ilwaco to Astoria, one in the Keys) and are listed as such.
- Routes are also checked by hand against the charts in OpenCPN, which is the check that has found the most problems.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the planned milestones, from the core router through weather, polars, live instrument data, the standalone app and currents, and for the regions the router is meant to be tested against.

## Contributing and license

Issues, bug reports and pull requests are welcome. Reports of a route that goes somewhere it should not, with the positions and the chart cell, are the most useful. Licensed under the GNU General Public License v3.0 (see [LICENSE](LICENSE)), the same family as OpenCPN.
