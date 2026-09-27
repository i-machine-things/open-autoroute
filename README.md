# open-autoroute

A boat route planner that reads NOAA electronic charts and tries to draw a route a person would actually be willing to follow. It is free, open source, and early. It is a development tool, **not for navigation**: check every route against the chart yourself.

The parts are a C++ core, a command-line tool that writes a GPX file you can import into OpenCPN, and an OpenCPN plugin (`openautoroute_pi`) that calls the same core from inside OpenCPN. A standalone app (`openautoroute-app`) is planned but does not exist yet.

## Read this first

This project is not part of OpenCPN. OpenCPN's developers do not endorse it or support it, so do not report problems with it to them.

What OpenCPN itself offers is different. Routes are drawn by hand. Its Weather Routing plugin plans routes by weather and can avoid land, and its manual warns that such routes "may not consider or 'see' normal navigation considerations and issues, therefore every route should be checked very carefully for navigation markers, shallow depths, bad currents, rocks, land and other obstacles and hazards." Adding depth, hazard and marker awareness to routing, and a route checker, is discussed in OpenCPN's own issue tracker ([issue #4387](https://github.com/OpenCPN/OpenCPN/issues/4387)); this project is separate from that work.

The same warning applies here, more strongly. A chart is a picture of what was surveyed at some past date, at chart datum, with no tide, current, weather, traffic or local knowledge in it, and a line drawn by software can look more trustworthy than it is. A route that avoids every charted hazard can still put a boat on something the chart does not show.

**Use it entirely at your own risk.** It is offered as a planning aid only, with no warranty of any kind (see the license). You, the skipper, are always responsible for the vessel, for checking every waypoint and every leg against up-to-date official charts and notices to mariners, and for keeping a proper lookout. The router can be wrong, and it is known to fail or to give poor routes in places (see "What it does not do" below and the routes listed as failing under "How it is tested").

## What it does today

- Reads S-57 ENC cells directly (no GDAL) and builds a cost grid from them: depth against your draft plus clearance, land, obstructions, wrecks, unsurveyed and uncharted water.
- Finds a route across that grid, then straightens it and thins out the waypoints.
- Keeps some distance off shores, and follows the basic rules of the road it can read from a chart:
  - **Traffic separation schemes (Rule 10):** lanes are followed in the charted direction, zones are only crossed square-on, precautionary areas are avoided by small craft, and a boat under 20 m or under sail stays out of lanes where it can.
  - **Narrow channels (Rule 9):** between the dashed limits of a charted narrow channel, and between chains of red and green buoys, keeping to the starboard side.
- Goes through navigation locks (the chamber, its gates and a short approach corridor are treated as passable) and lists them as a crossing, since a lockage means calling the lockmaster and waiting. Lock size and schedules are not checked.
- Tells you which restricted or dangerous areas (military and security zones, reserves and sanctuaries, dumping grounds) a route crosses, with the chart's own wording, on screen and in the GPX description. The router only prices most of these, so it can still cross one when the way round is long; read that list before you go.
- Scores any GPX route against the same rules (`--eval`), so it can be used to check a route from somewhere else.

## What it does not do

It reads about 60 of the 146 object classes in the NOAA data: depth and land, obstructions, wrecks and rocks, unsurveyed areas, restricted, military and security areas, dumping grounds, piers and other structures, buoys and beacons (for channels and danger marks), bridges and overhead cables (checked against an air draft, which you can set with `--air-draft-m` or leave to be estimated from the vessel length), and navigation locks. The rest is still ignored, notably quality-of-data zones, leading lines and recommended tracks, and marks other than lateral and cardinal ones. Depths are at chart datum with no tide, and there is no weather or current. Cell update files (`.001` and later) are not applied. It does not know whether US Inland Rules or the international rules apply where you are, and it assumes the international ones. [docs/S57_OBJECTS.md](docs/S57_OBJECTS.md) lists every chart object class, what it is for and whether it is used.

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
| `--air-draft-m` | Height of the vessel above the water, in metres, checked against bridges and overhead cables (default: estimated from the length) |
| `--cell-m` | Grid size in metres (default 30); smaller is finer and slower |
| `--min-leg-m` | Preferred minimum distance between waypoints (default 460 m) |
| `--via LAT,LON` | A point the route must pass through; repeat it for more, in order |
| `--eval route.gpx` | Score an existing route instead of planning one |
| `--map LAT,LON,CELLS` | Print an ASCII picture of the grid and route around a point, for debugging |

The core works in metres and nautical miles. Converting to feet is left to whatever front end sits on top.

## How it is tested

- `make test` runs unit tests on small hand-built grids and chart records.
- `benchmarks/run.sh` plans 29 routes across the Columbia River (including Camas to Hood River through the Bonneville lock), Puget Sound and the San Juans, San Francisco, Los Angeles, New York, Chesapeake Bay, Boston, Houston, the Keys, Lake Michigan, Maine and Hawaii, and prints one scored table. `benchmarks/compare.sh` compares two runs and flags anything that got less safe. Several of those routes still fail (Deception Pass, Ilwaco to Astoria, one in the Keys) and are listed as such.
- Routes are also checked by hand against the charts in OpenCPN, which is the check that has found the most problems.

## OpenCPN plugin

`plugin/` holds an OpenCPN plugin that calls the same planner: a toolbar button and right-click items ("Auto-route from here", "Auto-route to here") open a small dialog for the two positions, vessel size (metres or feet), draft, and your ENC folder. Planning runs in the background with a progress bar and a Cancel button, and the finished route is added to the Route Manager. The crossed restricted areas and locks are listed in the dialog.

Right-click a route that is already in OpenCPN for two more items:

- **Auto-route this route** re-plans it through its own waypoints, in order: your waypoints stay (with their names and symbols) and the legs between them are filled in. The route is replaced in place, and the original is kept as a hidden copy named "(before auto-route)" in the Route Manager, since OpenCPN has no undo. If you are navigating that route, it asks first: yes replaces it and restarts navigation, no leaves it alone and adds the plan beside it in green. A waypoint on land or in shallow water is moved to the nearest safe water and the dialog says so.
- **Check this route** scores the route as drawn against the same rules and changes nothing: stretches over land or too-shallow water, precautionary areas, narrow channels and buoy gates, traffic-lane crossings, and restricted areas. It checks on the planning grid, so a hazard smaller than a grid cell can be missed.

These need OpenCPN 5.14 or later (plugin API 1.20 and the route interface published in 5.14).

It builds against OpenCPN's own plugin header and wxWidgets 3.2 (GTK3), so match the OpenCPN release you run:

```bash
# OpenCPN's plugin API header, from the source tree of your release
git clone --depth 1 --branch "upstream/5.14.2+dfsg" https://github.com/OpenCPN/OpenCPN ~/ocpn_src
cmake -S . -B build_pi -DOAR_BUILD_PLUGIN=ON -DOCPN_INCLUDE_DIR=$HOME/ocpn_src/include
cmake --build build_pi --target openautoroute_pi
mkdir -p ~/.local/lib/opencpn && cp build_pi/plugin/libopenautoroute_pi.so ~/.local/lib/opencpn/
```

Then restart OpenCPN and enable "Auto-route" under Options, Plugins. It is early: a planning aid, not for navigation.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the planned milestones, from the core router through weather, polars, live instrument data, the standalone app and currents, and for the regions the router is meant to be tested against.

## Contributing and license

Issues, bug reports and pull requests are welcome. Reports of a route that goes somewhere it should not, with the positions and the chart cell, are the most useful. Licensed under the GNU General Public License v3.0 (see [LICENSE](LICENSE)), the same family as OpenCPN.
