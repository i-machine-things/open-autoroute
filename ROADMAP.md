# Roadmap

How versions work here: a numbered milestone below is a scoped piece of work, and finishing it (its "Done when" test, judged by the maintainer) is what bumps the minor version, in order: `v0.1.0`, `v0.2.0`, and so on. Every other release is a patch of the current milestone (`v0.1.0` then `v0.1.1`). Nothing after `v0.5.0` has a number yet: it lives in the backlog at the end of this file, and an item gets a numbered milestone only when it is scoped. See [docs/RELEASING.md](docs/RELEASING.md).

### `v0.1.0` — Rule-Compliant Core Auto-Router

Status: the core router, the GPX output, the OpenCPN plugin and the 29-route benchmark exist. Live testing on the water moved to `v0.2.0`, with the autopilot and plotter communications it depends on.

* **Static S-57 Parsing:** Extract depth contours (`DEPCNT`), land polygons (`LNDARE`), fairways (`FAIRWY`), traffic separation schemes (`TSS`), and hazards from ENC vector charts.  
* **Safety Cost Grid:** Build a 2D spatial cost matrix enforcing hard barriers at `vessel draft + safety clearance` with exponential edge penalties for channel centering.  
* **COLREGs Compliance:**  
  * **Rule 9 (Narrow Channels):** Force routing biases to the starboard/right side of channels and fairways.  
  * **Rule 10 (Traffic Separation Schemes):** Restrict routing to established lane vectors and enforce perpendicular crossings.  
* **Pathfinding Core:** Implement \$A^\*\$ / Any-Angle pathfinding to export clean GPX route outputs into OpenCPN's Route Manager.  

**Done when:** the router produces rule-compliant routes on the benchmark regions below with no regression against the last approved run, and the OpenCPN plugin plans a route and adds it to the Route Manager from inside OpenCPN.

### `v0.2.0` — Communications & Plotter Integration

Everything about moving routes and live data between the router and other equipment, kept apart from the routing itself:

* **Legacy Plotter Output:** Stream a planned route as NMEA 0183 route (`RTE`) and waypoint (`WPL`) sentences directly into legacy serial inputs (such as the Garmin GPSMAP 541s), so there is no manual SD card GPX import.  
* **Live Data Input:** Receive live NMEA 0183 and Signal K data (position, wind, speed through water, engine and fuel) over serial, network and OpenCPN's own connections, as a transport the later features build on; interpreting it stays with those features.  
* **Link Handling:** Connection setup, error handling and safe defaults for each link (no route is ever sent to a device without the user asking for it).
* **Live On-the-Water Testing:** Field validation runs on real marine hardware: planned routes checked against a real plotter and followed by an autopilot on the water. This waits for the plotter and autopilot links above, since it depends on them.

**Done when (proposed):** a planned route appears on a legacy plotter over its serial input without touching a memory card, live position and wind from a real instrument feed reach the router through the same layer, and at least one planned route has been checked and followed on the water with a real plotter and autopilot.

### `v0.3.0` — Weather & Point-of-Sail Router

* **GRIB2 Weather Integration:** Ingest wind vectors and wave heights for point-of-sail routing and motor vs. sail recommendations.  
* **Tack/Gybe Waypoint Generation:** Automatically generate optimized tacking and gybing legs based on true wind angles.

**Done when (proposed):** a route can be planned with a GRIB wind and wave forecast, and the tack and gybe legs it produces match hand-worked ones on at least three test passages.

### `v0.4.0` — Polar Performance Library

* **Polar Database:** Integrate preset boat polars and ORC VPP database profiles to map vessel performance curves directly into the routing engine.

**Done when (proposed):** a boat is chosen by name or polar file, and the planned time on a test passage is within a stated tolerance of the polar-predicted time.

### `v0.5.0` — Adaptive Self-Tuning Engine

* **Live Telemetry Use:** Use the live NMEA 0183 / Signal K data that the communications layer (`v0.2.0`) delivers (wind speed/direction, Speed Through Water, engine RPM, fuel flow).  
* **Dynamic Auto-Tuning:** Provide user-selectable options for dynamic polar auto-tuning and real-time fuel range modeling.

**Done when (proposed):** live wind, speed through water and fuel flow can feed the router, and its tuned polar is closer to logged performance than the preset one on a recorded trip.

### `v0.6.0` — Standalone Cross-Platform App Release

* **Flutter Framework:** Launch the standalone, hardware-accelerated cross-platform application for Android, iOS, Windows, macOS, and Linux.  
* **Online NOAA Sync:** Connect the standalone app directly to live NOAA hydrographic data repositories and online chart distribution networks.

**Done when (proposed):** the app installs and plans a route on Android and one desktop system, using charts it downloaded itself.

---

## Backlog and themes (not scheduled, not numbered)

These come from real findings: independent reviews of the code, the gaps listed in [docs/S57_OBJECTS.md](docs/S57_OBJECTS.md), and routes that still fail. None has a version number. An item becomes a numbered milestone (`### \`v0.6.0\` — ...`, with a "Done when") only when it is scoped.

**Safety hardening**

* A hard clearance buffer round rocks, wrecks and structures: a point hazard blocks only its own grid cell today, with no allowance for chart accuracy, position error or the cell size.
* Bridge horizontal clearance and a vessel beam setting; opening bridges flagged as "call the bridge" with any charted schedule. Only vertical clearance against air draft is checked today.
* Buoys and beacons as obstacles (lateral marks form channels but are not avoided as objects), and the special-purpose and safe-water marks (`BOYSPP`, `BOYSAW`, `BCNSPP`, lit piles).
* Seabed rock and coral (`SBDARE`), and data-quality zones (`M_QUAL` / `CATZOC`) with extra margin where the depths are poorly surveyed.
* Record a moved start or end point in the route file itself, not only on screen.

**Data freshness and datum**

* Apply ENC update files (`.001` and later) and show each cell's edition and issue date. Only base cells are read today, so a new wreck added by an update is missed.
* Tide and datum: height of tide, low-water datum on the Great Lakes, and squat. Depths are at chart datum with no allowance today. Check the depth units of each cell.

**Rules of the road**

* Inland versus international rules (the COLREGS demarcation line): apply or at least warn about the differences, for example Inland Rule 9(a)(ii) on the Great Lakes and Western Rivers.
* Recommended tracks, leading lines and range marks (`RECTRC`, `NAVLNE`, `DAYMAR`) as channel guidance; area-to-be-avoided rules by tonnage, not vessel length.

**Route checking**

* Score any route, not only ones this tool planned: `--eval` does it on the command line. Bring it into the plugin as "check this route" on a route already in OpenCPN, listing the hazards, restricted areas and traffic lanes it crosses. OpenCPN's own issue tracker discusses the need for a route checker ([issue #4387](https://github.com/OpenCPN/OpenCPN/issues/4387)).

**Performance and resolution**

* A variable grid: fine cells near shores, channels and harbours, coarse in open water. It would cut memory and time, and would deal with thin features (a mole, a lock chamber) properly, in place of the extra painting and corridors used today.
* A time estimate for the phases with known totals, and a cancel that never leaves partly built state; a cache of which cells cover where, across runs.

**Known failing routes** (benchmark): Ilwaco to Astoria and Deception Pass report disconnected water (shallow marina and pass entrances at the default clearance); one Keys endpoint is not in safe water; and one benchmark start point lies inside a real area to be avoided and needs replacing.

**The plugin and platforms**

* Draw the route options and progress on the chart, a preferences page, listing in OpenCPN's plugin catalogue, and builds for other systems and OpenCPN versions; an automated test that loads the plugin, which needs a running OpenCPN.

**Currents and flow** (formerly the last roadmap entry)

* Incorporate USGS river flow gauges, NOAA CO-OPS tidal streams and discharge current matrices into pathfinding weight calculations.

---

## Validation & Benchmark Regions

The routing engine will be tested against complex real-world hydrographic environments using NOAA ENC chart data:

* **Columbia River Entrance & Sand Island Spurs:** Rule 9 channel-keeping around pile dikes, shifting sandbars, and restricted cutoffs.  
* **Deception Pass & San Juan Islands (WA):** Tight-gap pathfinding through narrow passages with severe depth drop-offs.  
* **Puget Sound Traffic Separation Schemes:** Rule 10 TSS lane direction compliance and perpendicular transit validation.  
* **Multnomah Channel & Columbia River Sloughs:** Riverine channel midline alignment, bridge clearance checks, and shallow-bank avoidance.

---

## Cross-cutting: progress for long routes

A long route (an 85 nm river passage, a full coast) takes seconds to minutes: reading charts, building the grid, searching and smoothing. Any front end (the OpenCPN plugin, the standalone app) needs to show that instead of freezing, so the core should report progress and be cancellable:

* **Progress by phase:** loading charts (cell _n_ of _N_), building the grid, searching, smoothing, each with a fraction complete.
* **A countdown where it can be estimated:** chart loading and grid building have known totals; the search does not, but distance covered against the straight-line distance gives a usable estimate.
* **Cancel:** the search must be interruptible without leaving a half-built grid.

Status: built. `planRoute()` (the whole pipeline behind one call) takes hooks for messages, progress by phase (with cancel) and a debug view, and `findRoute` reports how far the search front has got and stops when asked. The OpenCPN plugin uses this for its progress bar and Cancel button. Still to do: a time estimate for the phases that have known totals, and cancelling without leaving a partly built grid in a shared cache, once there is one.

