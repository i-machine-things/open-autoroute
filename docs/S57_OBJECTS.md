# Chart objects in the NOAA ENC set

An inventory of every S-57 object class found in the full NOAA ENC download (7,345 cells), what each one is for, and whether the router reads it. Counts are features across all cells; the same real-world object often appears on several charts of different scale. Names and codes come from the S-57 object catalogue shipped with OpenCPN.

**146 classes are present. The router uses 53 and partly uses 0; 87 are ignored.** Status: **used** = read and acted on, **partly** = read but only some geometry or attributes, **no** = ignored.

## Why 146 classes

The S-57 catalogue shipped with OpenCPN lists 250 classes: 161 geographic marine classes, 89 meta and cartographic ones. The NOAA download contains 146 of them (138 geographic, 8 meta). Most of the 104 that are absent belong to other products: about 60 are inland-waterway objects (codes 17000 and up, 18001 and up), about 10 are cartographic drawing objects, and the tide and tidal-stream prediction objects (`T_HMON`, `TS_PRH`...) are not shipped in NOAA ENCs. Of the 161 geographic marine classes, the 23 not present are rare or foreign: light vessels, radar lines and ranges, canal and river banks, and the traffic-scheme crossing and roundabout objects (`TSSCRS`, `TSSRON`), which no US chart uses. Only base cells (`.000`) were read; update files change existing objects and do not normally add classes.

What matters more than the count is the attributes. Each class carries several, and the safety decisions live there: whether entry to a restricted area is prohibited (`RESTRN`), a bridge's clearance (`VERCLR`), a mark's category (`CATLAM`), a depth over an obstruction (`VALSOU`).

## Safety gaps, most important first

The hazard, restricted-area, structure, bridge and cable, cardinal and isolated-danger, and lock classes are read and acted on (see the tables below). What is still missing or only partly done, most important first:

1. **Cell update files (`.001` and later).** Only the base cell is read, so new wrecks and obstructions added by updates are missed.
2. **SBDARE**: Seabed areas, including rock and coral, are not read; a rocky bottom is not treated as a hazard.
3. **M_QUAL**: Data quality zones. Depths in low-quality areas should get extra margin.
4. **Tide and datum.** Depths are at chart datum; there is no tide, current or squat allowance.
5. **BRIDGE horizontal clearance and beam.** Only vertical clearance against air draft is checked. HORCLR and a beam setting are not used, and an opening bridge is treated as closed.
6. **BOYSPP, BOYSAW, BCNSPP, LIGHTS on piles.** Special-purpose and safe-water marks are not read, and lateral marks are used to form channels but are not obstacles themselves.
7. **RECTRC, NAVLNE, DAYMAR, LIGHTS.** Recommended tracks, leading lines and the red and white shore range marks that define a channel axis are not used.
8. **Inland versus international rules.** The router assumes the international rules everywhere.

## Hazards and depth

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| DEPARE (42) | Depth area | Depth area: the water between two depth values (DRVAL1 shallowest, DRVAL2 deepest). | Open only if DRVAL1 >= draft + clearance; unknown depth is blocked. | 564,684 area | 7,295 | **used** |
| DRGARE (46) | Dredged area | Dredged area: maintained channel or basin (DRVAL1). | Treated like DEPARE; also a marker of a real channel. | 16,350 area | 2,177 | **used** |
| SOUNDG (129) | Sounding | Individual depth soundings. | A sounding shallower than draft + clearance blocks its cell. | 3,423,568 point | 7,019 | **used** |
| OBSTRN (86) | Obstruction | Obstruction: anything that endangers a hull (pile fields, fish havens, foul ground, stumps), points, lines and areas, often with VALSOU. | Block if the depth over it is unknown or under draft + clearance. Areas and lines matter as much as points. | 68,369 point, 3,449 line, 37,820 area | 5,323 | **used** |
| WRECKS (159) | Wreck | Wreck, with CATWRK (dangerous / non-dangerous) and VALSOU. | Same as OBSTRN, including wreck areas. | 23,456 point, 1,201 area | 3,583 | **used** |
| UWTROC (153) | Underwater rock / awash rock | Underwater or awash rock, VALSOU. | Block if awash, unknown or shallow. | 288,620 point | 3,501 | **used** |
| UNSARE (154) | Unsurveyed area | Unsurveyed area: no depth information at all. | Never assume safe: block. | 4,206 area | 1,072 | **used** |
| DEPCNT (43) | Depth contour | Depth contour line (VALDCO). | Redundant with DEPARE for routing; useful for showing margins. | 602,511 line | 6,838 | **no** |
| SBDARE (121) | Seabed area | Seabed area (rock, mud, sand...), useful for anchoring and for spotting rock. | Rock (NATSUR) is a hazard flag; otherwise information. | 205,252 point, 5 line, 84,133 area | 5,917 | **no** |
| WEDKLP (158) | Weed/Kelp | Weed or kelp. | Small craft: mild penalty (fouls propellers). | 29,307 point, 4,622 area | 1,852 | **used** |
| SNDWAV (118) | Sand waves | Sand waves: shifting sea bed. | Treat depth as uncertain: add margin. | 358 point, 476 area | 306 | **no** |
| WATTUR (156) | Water turbulence | Water turbulence: tide rips, overfalls, eddies. | Penalty for small craft. | 2,902 point, 80 line, 622 area | 967 | **used** |
| SLOGRD (127) | Sloping ground | Sloping ground. | Information. | 15,462 point, 149 area | 766 | **no** |
| RAPIDS (107) | Rapids | Rapids. | Block for a boat. | 113 line, 50 area | 56 | **used** |
| WATFAL (157) | Waterfall | Waterfall. | Block. | 115 line | 19 | **used** |
| SPRING (130) | Spring | Spring (submarine freshwater). | Information. | 10 point | 9 | **no** |

## Restricted and regulated areas

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| RESARE (112) | Restricted area | Restricted area: entry prohibited, no anchoring, no fishing, speed or wake limits, etc. (RESTRN, CATREA). | Block where entry is prohibited; penalise or flag the rest. Needs its attributes. | 12,525 area | 3,940 | **used** |
| CTNARE (27) | Caution area | Caution area: a hazard or special condition to be aware of. | Penalty; the "caution zone" you flagged. | 4,631 point, 11,834 area | 3,876 | **used** |
| MIPARE (83) | Military practice area | Military practice area. | Avoid: block or heavy penalty. | 8 point, 1,014 area | 594 | **used** |
| DMPGRD (48) | Dumping ground | Dumping ground. | Avoid: heavy penalty (unknown depth changes). | 644 point, 5,225 area | 1,575 | **used** |
| CBLARE (20) | Cable area | Cable area: submarine cables, no anchoring. | Transit is fine; penalise anchoring only. | 4,843 area | 1,894 | **no** |
| PIPARE (92) | Pipeline area | Pipeline area: no anchoring. | Transit is fine. | 2,614 area | 971 | **no** |
| ACHARE (4) | Anchorage area | Anchorage area. | Vessels lie at anchor here: keep a margin. | 323 point, 1,657 area | 933 | **used** |
| ACHBRT (3) | Anchor berth | Anchor berth. | As ACHARE. | 702 point, 1,064 area | 66 | **no** |
| FSHGRD (56) | Fishing ground | Fishing ground. | Nets and gear likely: penalty. | 9 area | 6 | **no** |
| FSHFAC (55) | Fishing facility | Fishing facility: fish traps, weirs, stakes. | Block (fixed gear). | 1,241 point, 345 line, 164 area | 171 | **used** |
| FSHZNE (54) | Fishery zone | Fishery zone. | Information. | 62 area | 26 | **no** |
| MARCUL (82) | Marine farm/culture | Marine farm / aquaculture. | Block with margin. | 782 point, 31 line, 911 area | 451 | **used** |
| OFSPLF (87) | Offshore platform | Offshore platform. | Block with a safety margin (500 m zones are common). | 11,452 point, 114 area | 1,081 | **used** |
| PRDARE (97) | Production / storage area | Production or storage area. | Avoid. | 154 point, 364 area | 277 | **used** |
| OSPARE (88) | Offshore production area | Offshore production area. | Avoid. | 21 area | 12 | **used** |
| ISTZNE (68) | Inshore traffic zone | Inshore traffic zone. | Allowed for small craft under Rule 10(d)(i); avoid for ships. | 17 area | 8 | **no** |
| SWPARE (134) | Swept Area | Swept area: surveyed clear to a depth (DRVAL1). | Can raise confidence in a depth. | 1,889 area | 130 | **no** |
| ICEARE (66) | Ice area | Ice area. | Information. | 1,725 area | 155 | **no** |
| SPLARE (120) | Sea-plane landing area | Seaplane landing area. | Keep clear. | 7 point, 21 area | 24 | **used** |
| CTSARE (25) | Cargo transshipment area | Cargo transshipment area. | Vessels working: keep clear. | 37 area | 26 | **no** |
| HRBARE (63) | Harbour area (administrative) | Harbour area (administrative). | Information. | 20 area | 16 | **no** |
| DOCARE (45) | Dock area | Dock area. | Information. | 5 area | 4 | **no** |
| GRIDRN (62) | Gridiron | Gridiron (for beaching ships). | Structure: block. | 9 area | 5 | **used** |
| LOGPON (80) | Log pond | Log pond. | Obstructed water: block. | 11 point, 112 area | 57 | **no** |

## Traffic routing

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| TSSLPT (148) | Traffic Separation Scheme  Lane part | Traffic separation scheme lane part; ORIENT = direction of traffic flow. | Direction-aware lane rules (Rule 10). | 824 area | 238 | **used** |
| TSEZNE (150) | Traffic Separation Zone | Traffic separation zone. | Cross square-on only, never along. | 303 area | 192 | **used** |
| TSELNE (145) | Traffic Separation Line | Traffic separation line. | As TSEZNE. | 10 line | 10 | **used** |
| TSSBND (146) | Traffic Separation Scheme  Boundary | Traffic separation scheme boundary. | Outline of a scheme: information. | 649 line | 236 | **no** |
| PRCARE (96) | Precautionary area | Precautionary area: lanes converge, extra care. | Small craft avoid it heavily. | 340 area | 248 | **used** |
| TWRTPT (152) | Two-way route  part | Two-way route part. | Route through which traffic runs both ways. | 161 area | 74 | **no** |
| DWRTPT (41) | Deep water route part | Deep water route part. | For deep draft ships; small craft keep clear. | 71 area | 20 | **no** |
| DWRTCL (40) | Deep water route centerline | Deep water route centreline. | As DWRTPT. | 4 line | 4 | **no** |
| RCTLPT (110) | Recommended Traffic Lane Part | Recommended traffic lane part. | Advisory lane. | 25 area | 12 | **no** |
| RCRTCL (108) | Recommended route centerline | Recommended route centreline. | Advisory line to follow in a channel. | 27 line | 24 | **no** |
| RECTRC (109) | Recommended track | Recommended track (dashed line), often between leading marks, with ORIENT and TRAFIC. | The "channel vector": follow it inside a confined channel. | 4,638 line, 96 area | 1,808 | **no** |
| NAVLNE (85) | Navigation line | Navigation line: leading line / range line / bearing line. | The line drawn between the red and white shore range marks: an axis to line up on in a channel. | 2,327 line | 992 | **no** |
| FERYRT (53) | Ferry route | Ferry route. | Crossing traffic: cross square and quickly. | 509 line, 1 area | 242 | **no** |

## Channels

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| FAIRWY (51) | Fairway | Fairway: the navigable channel (its dashed boundary). | Channel limits; charted channel width. | 6,945 area | 2,753 | **used** |
| CANALS (23) | Canal | Canal. | Confined water. | 1,279 line, 1,477 area | 502 | **no** |
| RIVERS (114) | River | River (centre line / area). | Information. | 111,030 line, 10,806 area | 4,714 | **no** |

## Aids to navigation

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| BOYLAT (17) | Buoy, lateral | Lateral buoy: red or green, marks the side of a channel (CATLAM). | Pair into gates: stay between them, starboard side. | 25,316 point | 2,712 | **used** |
| BCNLAT (7) | Beacon, lateral | Lateral beacon (fixed mark, red or green). | As BOYLAT. | 34,871 point | 2,989 | **used** |
| BOYCAR (14) | Buoy, cardinal | Cardinal buoy: safe water lies on the named side. | Pass on the named side. | 33 point | 18 | **used** |
| BCNCAR (5) | Beacon, cardinal | Cardinal beacon. | As BOYCAR. | 4 point | 2 | **used** |
| BOYISD (16) | Buoy, isolated danger | Isolated danger buoy: a hazard directly beneath. | Keep well clear. | 108 point | 71 | **used** |
| BCNISD (6) | Beacon, isolated danger | Isolated danger beacon. | Keep well clear. | 4 point | 3 | **used** |
| BOYSAW (18) | Buoy, safe water | Safe water buoy: mid-channel or landfall. | Pass close on either side. | 666 point | 425 | **no** |
| BCNSAW (8) | Beacon, safe water | Safe water beacon. | As BOYSAW. | 18 point | 16 | **no** |
| BOYSPP (19) | Buoy, special purpose/general | Special purpose buoy (anchorage, restricted area, cable, race course...). | Read its purpose; often marks a restriction. | 6,807 point | 1,583 | **no** |
| BCNSPP (9) | Beacon, special purpose/general | Special purpose beacon. | As BOYSPP. | 21,782 point | 2,937 | **no** |
| DAYMAR (39) | Daymark | Daymark: a board or shape on shore, often the red and white bars. | Range marks that define a channel axis. | 41,045 point | 3,327 | **no** |
| LIGHTS (75) | Light | Lights, including sector lights whose colour sectors show safe water. | Sectors mark channels; not used. | 51,058 point | 4,473 | **no** |
| LITFLT (76) | Light float | Light float. | Moored mark. | 44 point | 15 | **no** |
| TOPMAR (144) | Topmark | Topmark on a buoy or beacon. | Information. | 613 point | 355 | **no** |
| RTPBCN (103) | Radar transponder beacon | Radar transponder beacon (racon). | Information. | 320 point | 208 | **no** |
| FOGSIG (58) | Fog signal | Fog signal. | Information. | 3,333 point | 1,047 | **no** |
| RETRFL (113) | Retro-reflector | Retro-reflector. | Information. | 7,544 point | 623 | **no** |
| LNDMRK (74) | Landmark | Landmark used for fixing position. | Information. | 17,237 point, 4 line, 28 area | 3,288 | **no** |

## Structures in or over the water

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| SLCONS (122) | Shoreline Construction | Shoreline construction: pier, jetty, breakwater, wharf, groyne (CATSLC). | A fixed obstruction in the water: block along its line. | 3,004 point, 367,257 line, 6,704 area | 4,565 | **used** |
| PONTON (95) | Pontoon | Pontoon: floating dock or marina finger. | Block. | 45,279 line, 2,285 area | 1,190 | **used** |
| PILPNT (90) | Pile | Pile or post standing in the water. | Block, with a margin. | 64,218 point | 2,985 | **used** |
| MORFAC (84) | Mooring/warping facility | Mooring facility: bollard, dolphin, mooring buoy. | Block. | 19,072 point, 30 line, 559 area | 1,749 | **used** |
| FNCLNE (52) | Fence/wall | Fence or wall. | Block. | 311 line | 191 | **used** |
| DYKCON (49) | Dyke | Dyke. | Block. | 6,463 line, 24 area | 774 | **used** |
| GATCON (61) | Gate | Gate (tidal gate, flood barrage). | Block unless it opens. | 3 point, 619 line, 92 area | 249 | **used** |
| DAMCON (38) | Dam | Dam or barrage. | Block. | 1 point, 352 line, 275 area | 364 | **used** |
| LOKBSN (79) | Lock basin | Lock basin. | Passable only via the lock. | 10 area | 9 | **used** |
| FLODOC (57) | Floating dock | Floating dock. | Block. | 6 line, 63 area | 40 | **used** |
| DRYDOC (47) | Dry dock | Dry dock. | Block. | 177 area | 71 | **used** |
| HULKES (65) | Hulk | Hulk (moored derelict). | Block. | 12 point, 253 area | 118 | **used** |
| CONVYR (34) | Conveyor | Conveyor. | Structure over water. | 373 line, 17 area | 155 | **used** |
| PYLONS (98) | Pylon/bridge support | Pylon or bridge support in the water. | Block. | 2,900 point, 1,166 area | 750 | **used** |
| CAUSWY (26) | Causeway | Causeway. | A fixed obstruction: block. | 11 line, 55 area | 33 | **used** |
| BRIDGE (11) | Bridge | Bridge, with VERCLR (vertical clearance), HORCLR and opening type (CATBRG). | Compare VERCLR with the boat's air draft; fixed spans under it are impassable to a tall mast. | 6,211 line, 14,712 area | 2,721 | **used** |
| CBLOHD (21) | Cable, overhead | Overhead cable with VERCLR. | Same: air draft check. | 10,260 line | 1,937 | **used** |
| PIPOHD (93) | Pipeline, overhead | Overhead pipeline with VERCLR. | Same. | 599 line | 318 | **used** |
| TUNNEL (151) | Tunnel | Tunnel. | Not for a boat. | 15 line, 72 area | 54 | **no** |
| CRANES (35) | Crane | Crane. | Structure on shore. | 41 point, 1 area | 25 | **no** |
| BERTHS (10) | Berth | Berth. | Vessels alongside. | 1,409 point, 4 area | 114 | **no** |
| HRBFAC (64) | Harbour facility | Harbour facility. | Information. | 1,564 point, 107 area | 698 | **no** |
| SMCFAC (128) | Small craft facility | Small craft facility (fuel, launch, pump-out). | Information for the boater. | 1,785 point, 13 area | 758 | **no** |
| SILTNK (125) | Silo / tank | Silo or tank. | On land. | 7,066 point, 5,123 area | 2,008 | **no** |

## Land and coast

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| LNDARE (71) | Land area | Land area. | Blocked. | 92,227 point, 17,831 line, 194,416 area | 6,088 | **used** |
| COALNE (30) | Coastline | Coastline. | Edge of water; DEPARE/LNDARE already cover it. | 463,494 line | 5,984 | **no** |
| LNDELV (72) | Land elevation | Land elevation. | Information. | 27,303 point, 2,382 line | 1,828 | **no** |
| LNDRGN (73) | Land region | Land region. | Information. | 8,579 point, 142,598 area | 5,261 | **no** |
| LAKARE (69) | Lake | Lake. | Information. | 24,877 area | 3,063 | **no** |
| SEAARE (119) | Sea area / named water area | Named sea area. | Information. | 613 point, 61,904 area | 7,159 | **no** |
| VEGATN (155) | Vegetation | Vegetation. | On land. | 1,906 point, 10 line, 7,868 area | 758 | **no** |
| BUAARE (13) | Built-up area | Built-up area. | On land. | 12,812 point, 9,365 area | 3,772 | **no** |
| BUISGL (12) | Building, single | Building. | On land. | 33,416 point, 47,326 area | 3,395 | **no** |
| ROADWY (116) | Road | Road. | On land. | 4,374 line | 103 | **no** |
| RAILWY (106) | Railway | Railway. | On land. | 384 line | 48 | **no** |
| AIRARE (2) | Airport / airfield | Airport. | On land. | 34 point, 548 area | 471 | **no** |
| RUNWAY (117) | Runway | Runway. | On land. | 60 line, 319 area | 303 | **no** |

## Currents, tides and magnetics

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| CURENT (36) | Current - non - gravitational | Current (velocity, direction). | Needed for the currents and flow theme in the roadmap backlog. | 1,239 point | 70 | **no** |
| TS_FEB (160) | Tidal stream - flood/ebb | Tidal stream, flood and ebb. | The currents and flow theme in the roadmap backlog. | 470 point, 2 area | 114 | **no** |
| TS_TIS (139) | Tidal stream - time series | Tidal stream time series. | The currents and flow theme in the roadmap backlog. | 8 point | 1 | **no** |
| MAGVAR (81) | Magnetic variation | Magnetic variation. | Bearings for display. | 3,680 point, 12,718 area | 7,337 | **no** |
| LOCMAG (78) | Local magnetic anomaly | Local magnetic anomaly. | Compass deviation warning. | 151 point, 142 area | 178 | **no** |

## Signals and stations

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| PILBOP (91) | Pilot boarding place | Pilot boarding place. | Traffic converges: keep clear. | 416 point, 137 area | 357 | **no** |
| SISTAW (124) | Signal station, warning | Signal station, warning (storm, tide, danger). | Information. | 119 point | 83 | **no** |
| SISTAT (123) | Signal station, traffic | Signal station, traffic. | Information. | 31 point | 22 | **no** |
| RDOCAL (104) | Radio calling-in point | Radio calling-in point (VTS reporting). | Where a vessel must call in. | 203 point, 41 line | 103 | **no** |
| RDOSTA (105) | Radio station | Radio station. | Information. | 1,474 point | 683 | **no** |
| RADSTA (102) | Radar station | Radar station. | Information. | 4 point | 2 | **no** |
| CGUSTA (29) | Coastguard station | Coastguard station. | Information. | 508 point | 401 | **no** |
| RSCSTA (111) | Rescue station | Rescue station. | Information. | 2 point | 2 | **no** |
| DISMAR (44) | Distance mark | Distance mark (river kilometre / mile posts). | Information. | 1,774 point | 772 | **no** |
| CTRPNT (33) | Control point | Control point (survey). | Information. | 1,368 point | 413 | **no** |
| FORSTC (59) | Fortified structure | Fortified structure. | Information. | 37 point, 2 line, 98 area | 95 | **no** |

## Chart metadata and boundaries

| Code | Object | What it is | How a router should treat it | In the charts | Cells | Status |
|---|---|---|---|---|---|---|
| M_COVR (302) | Coverage | Data coverage: where this chart has data. | Defines what is charted at all. | 7,993 area | 7,345 | **no** |
| M_QUAL (308) | Quality of data | Data quality (CATZOC): how trustworthy the depths are. | Poor-quality zones deserve extra margin. | 79,683 area | 7,345 | **no** |
| M_NSYS (306) | Navigational system of marks | Navigational system of marks: IALA A or B and direction of buoyage. | Sets which colour is starboard. | 7,444 area | 7,345 | **no** |
| M_NPUB (305) | Nautical publication information | Nautical publication information. | Information. | 10,183 area | 7,345 | **no** |
| M_SDAT (309) | Sounding datum | Sounding datum. | Datum for depths. | 443 area | 223 | **no** |
| M_VDAT (312) | Vertical datum of data | Vertical datum. | Datum for heights and clearances. | 548 area | 87 | **no** |
| M_CSCL (301) | Compilation scale of data | Compilation scale. | Metadata. | 28 area | 23 | **no** |
| M_SREL (310) | Survey reliability | Survey reliability. | Metadata. | 26 line, 67 area | 15 | **no** |
| ADMARE (1) | Administration area (Named) | Administration area (named). | Information. | 8,267 area | 2,252 | **no** |
| EXEZNE (50) | Exclusive Economic Zone | Exclusive economic zone. | Legal boundary. | 1,437 area | 1,306 | **no** |
| CONZNE (31) | Contiguous zone | Contiguous zone. | Legal boundary. | 1,417 area | 1,220 | **no** |
| COSARE (32) | Continental shelf area | Continental shelf area. | Legal boundary. | 803 area | 737 | **no** |
| _texto (135) | Text | Text note on the chart. | Information. | 2,875 area | 1,258 | **no** |
| NEWOBJ (163) | New Object | New object (user defined). | Unknown: read its attributes. | 267 point | 136 | **no** |

## Other classes present

- CBLSUB (22) Cable, submarine: 7,676 line, 1687 cells
- OILBAR (89) Oil barrier: 29 line, 21 cells
- PIPSOL (94) Pipeline, submarine/on land: 31,148 line, 1791 cells
- SLOTOP (126) Slope topline: 12,171 line, 1397 cells
- TIDEWY (143) Tideway: 1 area, 1 cells
