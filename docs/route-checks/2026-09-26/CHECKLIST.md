# open-autoroute route check

Check these by eye in OpenCPN. Everything below was planned with the code of PR #14 (route names) on top of commit `631c3cd` (branch `feat/hazard-objects`, PR #12) against the NOAA ENC download of 2026-09-25, using `benchmarks/run.sh` (the routes are listed in `benchmarks/routes.csv`). The route files are in `gpx/` next to this file.

## Setup

1. In OpenCPN: **Options, Charts, Chart Files**: make sure `~/Documents/Charts/ENC_ROOT` is listed. Set the display to S-57 vector charts, display category **All**, so obstructions, restricted areas and marks are drawn.
2. **Route & Mark Manager, Import GPX** and pick a file from `gpx/`. Then **Zoom to Route**. Each route is named for where it goes and the vessel (for example `Edmonds to Kingston (12 m)`), its first and last waypoints are named for the two places, and its description gives the distance and depth needed, so the Route Manager's Route Name, From and To columns tell them apart. OpenCPN fills From and To from the first and last waypoint names.
3. Vessel assumed unless the route name says otherwise: **12 m, 1.5 m draft plus 1.0 m clearance (needs 2.5 m of water), 4.8 m mast** (estimated from the length). Files ending `_large` are a 120 m ship (Houston: 100 m).
4. Depths come from the charts at their datum. Tides, river stage and currents are not modelled. These routes are for checking the logic, not for navigating.

## For every route

- [ ] Does it stay in **water**? No crossing of land, piers or breakwaters.
- [ ] Does it keep off **shoals, rocks, wrecks and obstructions** (including the shaded areas, not only the point symbols)?
- [ ] Does it avoid **restricted, military and caution areas** (magenta dashed outlines)? Note any it crosses.
- [ ] **Traffic lanes:** small craft should stay out of lanes and cross square-on; ships should go with the flow arrows, never against.
- [ ] **Precautionary areas** (the magenta circles where lanes meet): a small craft should not be routed through one.
- [ ] **Channels:** between the dashed channel limits, on the starboard side; on the correct side of red and green marks.
- [ ] **Bridges and overhead cables:** does it pass under only where a 4.8 m mast fits?
- [ ] **Waypoints:** any stacked a few tens of metres apart, or turns placed inside a lane or a channel?
- [ ] **Clearance:** any place it squeezes through a gap or hugs a shore where wider water was available?
- [ ] Does it look like a route a person would actually be willing to follow?

## Look at these first, in this order

### puget_seattle_port_townsend_small.gpx

Vessel 12 m. **42.5 nm**, 35 waypoints, closest approach to hazards 242 m, 0.0 km in precautionary areas, 2 lane crossing(s), worst 11 degrees off square. Flags: none.

**Why look:** The route you approved. Confirm it is still the same: open east water, ONE square crossing near Point No Point, nothing in a precautionary area.

**Expect:** Two lane crossings, about 11 degrees off square. No time in a precautionary area. 35 waypoints (was 30).

- [ ] Looks right   - [ ] Problem (where, and what):


### puget_edmonds_kingston.gpx

Vessel 12 m. **4.7 nm**, 5 waypoints, closest approach to hazards 597 m, 0.0 km in precautionary areas, 2 lane crossing(s), worst 8 degrees off square. Flags: none.

**Why look:** Ferry crossing that briefly went 3 nm off its line. Should be a clean straight crossing again.

**Expect:** 4.7 nm, 5 waypoints, two lane crossings about 8 degrees off square. It crosses a Regulated Navigation Area lightly (x1.5), so it should NOT detour.

- [ ] Looks right   - [ ] Problem (where, and what):


### puget_seattle_port_townsend_large.gpx

Vessel 120 m. **38.6 nm**, 27 waypoints, closest approach to hazards 339 m, 4.3 km in precautionary areas, 1 lane crossing(s), worst 27 degrees off square. Flags: none.

**Why look:** 120 m vessel: should USE the lanes with the flow, not fight them.

**Expect:** One crossing, worst 27 degrees off square, 4.3 km inside precautionary areas (expected for a ship). It was 46 degrees before a lane-cost fix; check the crossing is sensible and it never runs the wrong way.

- [ ] Looks right   - [ ] Problem (where, and what):


### puget_tacoma_seattle_small.gpx

Vessel 12 m. **26.5 nm**, 55 waypoints, closest approach to hazards 242 m, 0.0 km in precautionary areas, 1 lane crossing(s), worst 34 degrees off square. Flags: X.

**Why look:** Small craft: one lane crossing came out 34 degrees off square (flagged X).

**Expect:** Find the crossing. Is it a real lane crossing, and could it have been squarer? It should stay out of the lanes otherwise.

- [ ] Looks right   - [ ] Problem (where, and what):


### la_approach_long_beach_large.gpx

Vessel 120 m. **17.1 nm**, 11 waypoints, closest approach to hazards 210 m, 16.7 km in precautionary areas, 1 lane crossing(s), worst 15 degrees off square. Flags: D.

**Why look:** Ship approach is 17.1 nm; it was 10.4 nm before the restricted-area rules (flagged D).

**Expect:** Is there a security zone across the direct line? If so a detour is right; if not, this is over-penalising. 16.7 km in precautionary areas.

- [ ] Looks right   - [ ] Problem (where, and what):


### hawaii_honolulu_maalaea.gpx

Vessel 12 m. **125.2 nm**, 25 waypoints, closest approach to hazards 60 m, 0.0 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: S.

**Why look:** 125 nm; it was 85 nm before the hazard rules.

**Expect:** Likely detouring around military practice areas (x30). Is a real danger area on the direct line, or is something else forcing it out?

- [ ] Looks right   - [ ] Problem (where, and what):


### columbia_astoria_portland.gpx

Vessel 12 m. **85.3 nm**, 207 waypoints, closest approach to hazards 60 m, 0.0 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: S.

**Why look:** Your sandbars-and-narrow-channels case. 207 waypoints.

**Expect:** Stays between the dashed channel limits, starboard side at the bends. Bridges: it assumes a 4.8 m mast (12 m boat); check every bridge it passes under. The Portland end was moved 1.7 km to reach safe water: check where it stops.

- [ ] Looks right   - [ ] Problem (where, and what):


### boston_provincetown.gpx

Vessel 12 m. **51.3 nm**, 40 waypoints, closest approach to hazards 255 m, 14.2 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: P.

**Why look:** 14 km inside precautionary areas for a small craft (flagged P).

**Expect:** Is there any way round the precautionary area, or is it the only water? If there is a way round, tell me where.

- [ ] Looks right   - [ ] Problem (where, and what):


### sf_sausalito_half_moon_bay.gpx

Vessel 12 m. **28.2 nm**, 28 waypoints, closest approach to hazards 150 m, 3.6 km in precautionary areas, 1 lane crossing(s), worst 27 degrees off square. Flags: P.

**Why look:** 3.6 km in precautionary areas (flagged P), one crossing 27 degrees off square.

**Expect:** Golden Gate: does it cross the traffic scheme sensibly and stay out of the precautionary area where it can?

- [ ] Looks right   - [ ] Problem (where, and what):


### ny_sandy_hook_battery.gpx

Vessel 12 m. **16.6 nm**, 15 waypoints, closest approach to hazards 342 m, 4.6 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: P.

**Why look:** 4.5 km in precautionary areas (flagged P).

**Expect:** Lower New York harbour: ship channels and anchorages. Does it pass anything it should not (anchorage areas, restricted areas)?

- [ ] Looks right   - [ ] Problem (where, and what):


### la_marina_catalina.gpx

Vessel 12 m. **41.5 nm**, 22 waypoints, closest approach to hazards 30 m, 0.0 km in precautionary areas, 2 lane crossing(s), worst 3 degrees off square. Flags: none.

**Why look:** Was failing to route (a search bug). Now 41.5 nm.

**Expect:** Marina exit and the two lane crossings (about 3 degrees off square): check it leaves the marina cleanly and does not clip the breakwaters.

- [ ] Looks right   - [ ] Problem (where, and what):


### columbia_bar_out.gpx

Vessel 12 m. **15.0 nm**, 29 waypoints, closest approach to hazards 120 m, 0.0 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: none.

**Why look:** The Columbia bar. Closest approach 120 m.

**Expect:** Should follow the entrance channel between the jetties, and stay clear of the shoaling on either side.

- [ ] Looks right   - [ ] Problem (where, and what):


### multnomah_channel.gpx

Vessel 12 m. **13.4 nm**, 56 waypoints, closest approach to hazards 133 m, 0.0 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: none.

**Why look:** Narrow slough, 56 waypoints.

**Expect:** Mid-channel, off the banks, and under any bridge or cable with enough room for a 4.8 m mast.

- [ ] Looks right   - [ ] Problem (where, and what):


### galveston_houston_large.gpx

Vessel 100 m. **47.9 nm**, 109 waypoints, closest approach to hazards 30 m, 0.0 km in precautionary areas, 0 lane crossing(s), worst 0 degrees off square. Flags: none.

**Why look:** 100 m ship in the Houston Ship Channel, closest 30 m, 109 waypoints.

**Expect:** Inside the channel limits; not cutting across dredged-area edges.

- [ ] Looks right   - [ ] Problem (where, and what):


## The rest

| Route | Vessel (m) | nm | Waypoints | Note |
|---|---|---|---|---|
| columbia_astoria_cathlamet.gpx | 12 | 22.1 | 66 | River channel; 66 waypoints. |
| san_juan_anacortes_friday.gpx | 12 | 20.1 | 24 | Islands and narrow passes; 20.1 nm. |
| san_juan_friday_roche.gpx | 12 | 11.3 | 13 | Tight channels; closest approach 20 m. |
| san_juan_bellingham_roche.gpx | 12 | 35.9 | 26 |  |
| puget_tacoma_seattle_large.gpx | 120 | 24.9 | 25 | Ship; no precautionary flag. |
| puget_everett_bainbridge.gpx | 12 | 30.1 | 42 | Two crossings about 7 degrees off square; closest approach 30 m, check where. |
| sf_approach_oakland_large.gpx | 120 | 21.6 | 15 | Golden Gate zigzag reported in review and fixed (was 26 waypoints, a stack of 12 in 300 m near the bridge): please confirm it is clean. 19 km in precautionary areas (expected for a ship). |
| chesapeake_annapolis_norfolk.gpx | 12 | 133.1 | 79 | 133 nm; endpoint moved 1.5 km to reach safe water, check the Norfolk end. |
| lake_michigan_chicago_milwaukee.gpx | 12 | 72.8 | 19 | Open water; harbour entrances at each end. |
| maine_portland_rockland.gpx | 12 | 70.0 | 40 | Rocky coast, many islands. |

## No route (nothing to check)

- `columbia_ilwaco_astoria` and `deception_pass`: the router finds the two ends in different bodies of water. Not yet understood; entrance or marina depths under 2.5 m is my guess, unconfirmed.
- `keys_key_west_marathon`: the end point has no water of 2.5 m or more within 1.2 km (Florida Bay flats). Likely correct for a 1.5 m draft plus 1 m clearance.
- `juan_de_fuca_large`: the start is inside an area to be avoided, which really is closed to a 120 m ship. A bad test point, not a fault.

## What to send back

For anything wrong, just give me: **the file name, roughly where (a lat/lon from the cursor readout is ideal), and what is wrong** (crosses a shoal, goes through a restricted area, wrong way in a lane, too close to a bank, silly waypoints, and so on). A screenshot is the best. I can pull up that exact spot on the grid and see what the router thought was there.
