# Roadmap

### `v0.1.0` — Rule-Compliant Core Auto-Router & Live Testing

* **Static S-57 Parsing:** Extract depth contours (`DEPCNT`), land polygons (`LNDARE`), fairways (`FAIRWY`), traffic separation schemes (`TSS`), and hazards from ENC vector charts.  
* **Safety Cost Grid:** Build a 2D spatial cost matrix enforcing hard barriers at `vessel draft + safety clearance` with exponential edge penalties for channel centering.  
* **COLREGs Compliance:**  
  * **Rule 9 (Narrow Channels):** Force routing biases to the starboard/right side of channels and fairways.  
  * **Rule 10 (Traffic Separation Schemes):** Restrict routing to established lane vectors and enforce perpendicular crossings.  
* **Pathfinding Core:** Implement \$A^\*\$ / Any-Angle pathfinding to export clean GPX route outputs into OpenCPN's Route Manager.  
* **Legacy Hardware Integration:** Investigate NMEA 0183 route (`RTE`) and waypoint (`WPL`) sentence streaming directly into legacy serial inputs (such as the Garmin GPSMAP 541s) to bypass manual SD card GPX imports.  
* **Live On-the-Water Testing:** Conduct live field validation runs on target marine hardware and legacy plotters.

### `v0.2.0` — Weather & Point-of-Sail Router

* **GRIB2 Weather Integration:** Ingest wind vectors and wave heights for point-of-sail routing and motor vs. sail recommendations.  
* **Tack/Gybe Waypoint Generation:** Automatically generate optimized tacking and gybing legs based on true wind angles.

### `v0.3.0` — Polar Performance Library

* **Polar Database:** Integrate preset boat polars and ORC VPP database profiles to map vessel performance curves directly into the routing engine.

### `v0.4.0` — Adaptive Self-Tuning Engine

* **Live Telemetry Ingestion:** Ingest real-time NMEA 0183 / Signal K data (wind speed/direction, Speed Through Water, engine RPM, fuel flow).  
* **Dynamic Auto-Tuning:** Provide user-selectable options for dynamic polar auto-tuning and real-time fuel range modeling.

### `v0.5.0` — Standalone Cross-Platform App Release

* **Flutter Framework:** Launch the standalone, hardware-accelerated cross-platform application for Android, iOS, Windows, macOS, and Linux.  
* **Online NOAA Sync:** Connect the standalone app directly to live NOAA hydrographic data repositories and online chart distribution networks.

### `v0.6.0+` — Environmental Flow Modeling

* **Current & Flow Integration:** Incorporate USGS river flow gauges, NOAA CO-OPS tidal streams, and discharge current matrices into pathfinding weight calculations.


---

## Validation & Benchmark Regions

The routing engine will be tested against complex real-world hydrographic environments using NOAA ENC chart data:

* **Columbia River Entrance & Sand Island Spurs:** Rule 9 channel-keeping around pile dikes, shifting sandbars, and restricted cutoffs.  
* **Deception Pass & San Juan Islands (WA):** Tight-gap pathfinding through narrow passages with severe depth drop-offs.  
* **Puget Sound Traffic Separation Schemes:** Rule 10 TSS lane direction compliance and perpendicular transit validation.  
* **Multnomah Channel & Columbia River Sloughs:** Riverine channel midline alignment, bridge clearance checks, and shallow-bank avoidance.

