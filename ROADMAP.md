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
* **Route Context Menu:** Right-click an existing OpenCPN route to re-plan it through its own waypoints (the original kept as a hidden copy) or to check it against the same rules without changing it ([#38](https://github.com/i-machine-things/open-autoroute/issues/38)).  

**Done when:** the router produces rule-compliant routes on the benchmark regions below with no regression against the last approved run, and the OpenCPN plugin plans a route and adds it to the Route Manager from inside OpenCPN.

### `v0.2.0` — Communications & Plotter Integration

Everything about moving routes and live data between the router and other equipment, kept apart from the routing itself:

* **Legacy Plotter Output:** Stream a planned route as NMEA 0183 route (`RTE`) and waypoint (`WPL`) sentences directly into legacy serial inputs (such as the Garmin GPSMAP 541s), so there is no manual SD card GPX import. The plugin does not need its own output for this: its routes are ordinary Route Manager routes, so OpenCPN's built-in **Send to GPS** sends them. Only the standalone app needs its own `RTE`/`WPL` output. *Pending hardware test:* Garmin's installation guide lists `WPL` but not `RTE` among the 541s's NMEA 0183 inputs, so standard NMEA mode should give waypoints only. The test is OpenCPN's Garmin (GRMN) host mode, with the plotter's serial port set to Garmin Data Transfer; nobody has yet confirmed that a whole route arrives that way.  
* **Live Data Input:** Receive live NMEA 0183 and Signal K data (position, wind, speed through water, engine and fuel) over serial, network and OpenCPN's own connections, as a transport the later features build on; interpreting it stays with those features.  
* **Link Handling:** Connection setup, error handling and safe defaults for each link (no route is ever sent to a device without the user asking for it).
* **Live On-the-Water Testing:** Field validation runs on real marine hardware: planned routes checked against a real plotter and followed by an autopilot on the water. This waits for the plotter and autopilot links above, since it depends on them.

**Done when (proposed):** a planned route appears on a legacy plotter over its serial input without touching a memory card, live position and wind from a real instrument feed reach the router through the same layer, and at least one planned route has been checked and followed on the water with a real plotter and autopilot.

### `v0.3.0` — Weather Routing Handoff

OpenCPN already has a mature plugin for this: **Weather Routing** plans isochrone routes from GRIB wind against a boat's own polar (CSV or XML), counts tacks and sail changes, and estimates voyage time. Reimplementing GRIB ingestion and tack/gybe math here would just be a worse copy of it. README.md already draws this boundary (Weather Routing's own manual warns its routes may not "see" normal navigation hazards) — this milestone is the other half of it: open-autoroute owns whether a path is safe and rule-compliant, Weather Routing owns how to sail it fastest.

* **Handoff to Weather Routing:** open-autoroute's own hazard/COLREGs-aware route becomes the input Weather Routing plans against for wind and boat performance, instead of a second GRIB/point-of-sail engine built from scratch. Exact mechanism — an ordinary Route Manager route Weather Routing can already read as a hint or constraint, versus a more direct call between the two plugins — is still to be scoped when this milestone starts.
* **Boat performance stays with Weather Routing:** its own polar files and performance modeling remain the source of truth; open-autoroute does not build a separate polar database or ORC VPP integration.
* **Ocean-crossing trigger:** a scoped exception to "no chart coverage is unsafe, block" (see `docs/S57_OBJECTS.md`'s `M_COVR` handling) — a genuine gap in ENC coverage along the great-circle path (mid-Pacific, mid-Atlantic) means there is no hazard to route around, not an unknown one, so that stretch hands straight to Weather Routing instead of blocking. Scoped by actual coverage, not a named-region list: the Great Lakes are fully chart-covered, have real shipping lanes and islands, and keep their own COLREGs handling (Inland Rule 9) — they never hit this trigger. A coastal gap in coverage still blocks as today; this exception is only for a stretch with no coverage on either side of it either, wide enough that it cannot be a survey gap next to charted water.

**Done when (proposed):** a route planned by open-autoroute is handed to Weather Routing, which produces a weather-optimized version of it (tacks, gybes, timing) without open-autoroute duplicating any of that math itself, and a route crossing a genuine ENC coverage gap (a real ocean crossing) hands that stretch to Weather Routing instead of reporting no route.

### `v0.4.0` — Weather-Routed Result Re-Check

Closes the gap Weather Routing's own manual admits to: its routes aren't checked against navigation hazards. `v0.1.0`'s Route Context Menu already checks an existing OpenCPN route against open-autoroute's rules without changing it — this milestone is applying that same, already-built checker to whatever Weather Routing produces from `v0.3.0`'s handoff, not building a new one.

* **Re-check after weather routing:** run the existing hazard/COLREGs check against a Weather-Routing-optimized route, flagging anything it introduces (a tack through a TSS, a gybe over a shoal) that the safety layer would not have allowed on its own.

**Done when (proposed):** a Weather-Routing-produced route can be checked the same way an OpenCPN route is checked today, and at least one real case is found where weather-optimized tacking crossed a hazard or restriction the safety layer would have avoided.

### `v0.5.0` — Adaptive Self-Tuning Engine

* **Live Telemetry Use:** Use the live NMEA 0183 / Signal K data that the communications layer (`v0.2.0`) delivers (wind speed/direction, Speed Through Water, engine RPM, fuel flow).  
* **Dynamic Auto-Tuning:** Feed live telemetry to Weather Routing for its own dynamic polar auto-tuning (`v0.3.0`'s handoff, not a polar open-autoroute owns), plus real-time fuel range modeling here.

**Done when (proposed):** live wind, speed through water and fuel flow can feed the router, that data can reach Weather Routing for its own polar auto-tuning (once `v0.3.0`'s handoff exists), and real-time fuel range modeling is closer to logged performance than a static estimate.

### `v0.6.0` — Standalone Route-Planning App

Not a chart plotter — OpenCPN already is one, official and full-featured, on every platform this could target except iOS (OpenCPN's own FAQ: the App Store restricts it; the only current workaround is VNC into a Raspberry Pi). This is a focused planning tool instead: load charts, plan a route, export it. No live chart display, no position tracking, no AIS — those stay OpenCPN's job everywhere OpenCPN already runs.

* **Flutter Framework:** A cross-platform planning app (Android, iOS, Windows, macOS, Linux) on the same core as the plugin. Cross-platform because Flutter builds all of them from one codebase, not because each platform needs its own competing chart plotter — iOS is the actual gap being filled; the rest are a convenience for planning without installing OpenCPN.
* **Online NOAA Sync:** Download the charts a planned route needs, directly, without a separate chart-management step.

**Done when (proposed):** the app installs and plans a route on iOS and one other platform, using charts it downloaded itself, with no live chart-display or tracking feature creeping into scope.

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
* Tide and datum: height of tide, low-water datum on the Great Lakes, and squat. Depths are at chart datum with no allowance today. Check the depth units of each cell. Reuse the same tide-prediction data source existing OpenCPN plugins already consume (Admiralty Tide Tables, UKtides, TideFinder) rather than building harmonic-constituent prediction from scratch — those are display tools reading their own database, not something with a callable API, so this means sharing the data source, not calling the plugin; confirm which when this is actually picked up.

**Rules of the road**

* Inland versus international rules (the COLREGS demarcation line): apply or at least warn about the differences, for example Inland Rule 9(a)(ii) on the Great Lakes and Western Rivers.
* Recommended tracks, leading lines and range marks (`RECTRC`, `NAVLNE`, `DAYMAR`) as channel guidance; area-to-be-avoided rules by tonnage, not vessel length.

**Route checking**

* Score any route, not only ones this tool planned: `--eval` does it on the command line, and the plugin's "Check this route" does it on a route already in OpenCPN (see `v0.1.0`). Still to do: mark the unsafe spots on the chart instead of only listing them. OpenCPN's own issue tracker discusses the need for a route checker ([issue #4387](https://github.com/OpenCPN/OpenCPN/issues/4387)).

**Performance and resolution**

* A variable grid: fine cells near shores, channels and harbours, coarse in open water. It would cut memory and time, and would deal with thin features (a mole, a lock chamber) properly, in place of the extra painting and corridors used today.
* A time estimate for the phases with known totals, and a cancel that never leaves partly built state; a cache of which cells cover where, across runs.

**Known failing routes** (benchmark): Ilwaco to Astoria and Deception Pass report disconnected water (shallow marina and pass entrances at the default clearance); one Keys endpoint is not in safe water; and one benchmark start point lies inside a real area to be avoided and needs replacing.

**The plugin and platforms**

* Draw the route options and progress on the chart, a preferences page, listing in OpenCPN's plugin catalogue, and builds for other systems and OpenCPN versions; an automated test that loads the plugin, which needs a running OpenCPN.
* Low priority idea: send a route to a Garmin plotter over NMEA 2000 by posing as another Garmin plotter and using Garmin's plotter-to-plotter "Clone User Data" transfer. That transfer uses Garmin-proprietary messages that have not been decoded publicly (canboat has none for it), and the GPSMAP 400/500 series does not receive the standard NMEA 2000 route messages, so the format would have to be captured and worked out first. Only worth it if the serial (GRMN) route upload in `v0.2.0` fails.

**Currents and flow** (formerly the last roadmap entry; mostly superseded by the `v0.3.0` handoff)

* Weather Routing already factors ocean currents into its isochrone routing, including a wind-vs-current dangerous-seas constraint — riding or avoiding current for speed is its job once `v0.3.0`'s handoff exists, not a pathfinding weight to duplicate here.
* What could still belong here is safety-only, not speed-optimization: USGS river flow gauges or NOAA CO-OPS tidal streams strong enough to be hazardous for a given vessel at a given state, flagged the same way `WEDKLP`/`SNDWAV`/`WATTUR` already are. Weather Routing's current-awareness is ocean/GRIB-scale; the actual hazard case is a narrow pass running hard on a tide stage, which is what plugins like NCDF Tidal Currents, oTcurrent and frcurrents specialize in predicting — reuse that same data rather than building tidal-current prediction from scratch. Deception Pass, already one of this project's own known-failing benchmark routes below, is exactly this case.

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

