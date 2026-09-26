# open-autoroute

Welcome to the official repository for the Open-Source OpenCPN Auto-Routing Plugin and Standalone App project. This repository contains a high-performance, rule-compliant pathfinding engine built as a free, open-source alternative to commercial marine navigation tools.

---

## Architecture Overview

The project relies on a shared C++ core designed for two primary deployment targets:

1. **OpenCPN Plugin (`openautoroute_pi`):** A 100% offline-capable plugin operating directly within OpenCPN using local vector charts.  
2. **Standalone App (`openautoroute-app`):** A cross-platform Flutter application (Android, iOS, Windows, macOS, Linux) that pulls real-time NOAA hydrographic data online.

---

## Version Milestones & Release Roadmap

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

Contributions, issue reports, and pull requests are welcome. This project is licensed under open-source terms to support the marine community.