# Coding Best Practices & Reminders

> **Style rule:** Notes must be clear and concise — 300 characters or less each. Group by topic, not by date. Whenever a PR review (CodeRabbit or human) catches a mistake, add or amend a note here right away so it isn't repeated.

## Resource Cleanup & Temporary Files

**IMPORTANT**: Always add proper cleanup code in programs to prevent lingering temp files after closing.

### Best Practices:

1. **GUI Applications (PyQt, Tkinter, etc.)**
   - Implement `closeEvent()` handler to cleanup resources on window close
   - Call `deleteLater()` on widgets to ensure proper Qt object cleanup
   - Process pending events with `app.processEvents()` before exit

2. **File Handling**
   - Use context managers (`with` statements) for file operations
   - Explicitly close file handles when not using context managers
   - Release file locks before program exit
   - Clean up temporary files in temp directories

3. **Background Threads & Workers**
   - Stop and join all background threads before exit
   - Cancel any pending operations
   - Clean up thread-specific resources

4. **Testing Cleanup**
   - After closing the program, verify the executable can be:
     - Deleted immediately
     - Moved to another location
     - Replaced with a new version
   - If the file is locked, cleanup code is missing or incomplete

### Example Implementation (PyQt6):

```python
def closeEvent(self, event):
    """Handle window close event - ensure proper cleanup"""
    # Cleanup modules/components
    for module in self.modules:
        try:
            module.cleanup()
        except Exception as e:
            print(f"Error cleaning up module: {e}")

    # Save state
    self.save_settings()

    # Accept close event
    event.accept()
    QApplication.quit()

def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()

    exit_code = app.exec()

    # Final cleanup
    window.deleteLater()
    app.processEvents()

    sys.exit(exit_code)
```

### PyInstaller Specific:

In `.spec` file, add:
```python
exe = EXE(
    ...
    bootloader_ignore_signals=True,  # Better cleanup handling
    ...
)
```

## Date: 2025-12-16
This note was created based on issues encountered with PyInstaller executables remaining locked after closing.

## Branch Protection

- **GitHub branch protection requires a public repo, or GitHub Pro/Team/Enterprise, on private repos** — `PUT .../branches/.../protection` 403s with "Upgrade to GitHub Pro or make this repository public" otherwise. The bootstrap workflow handles this by warning and continuing, not failing the job — check the workflow run's log if a downstream repo's protection didn't apply.
- **Don't set required status-check contexts before the repo has CI that produces them.** A required context that never reports a status permanently blocks merges. `ci.yml` isn't part of the bootstrap file set (it needs per-project tailoring — see "tailor or language-agnostic" above), so bootstrap-time protection only sets what's safe without knowing future job names (no force-push, no deletion, enforced for admins); add required checks once that repo's own CI is customized and green.
- **Avoid `required_pull_request_reviews` on a solo-maintained repo.** GitHub won't let an author approve their own PR, so requiring even 1 approval with no other reviewer deadlocks every merge. Rely on required status checks (which do block direct pushes too, since a bare push's commit never gets the PR-triggered check runs) instead of an approval-count gate.

## C++ Input Validation & Output Formats

- **Validate public-API inputs.** Reject negative penalties and distances, non-finite coordinates and out-of-range cells by throwing or returning false; never assume callers pass sane values.
- **Bounds-check both ends before walking a segment.** A helper that takes two cells (e.g. `lineOfSight`) must return false for out-of-grid endpoints, not index outside the grid.
- **Format numbers for file formats with a classic-locale stream, not printf.** A comma-decimal global locale makes `%f` write `46,100000`, which is invalid GPX/XML.
- **Strip XML 1.0-forbidden control characters from text.** Everything below 0x20 except tab, LF and CR is illegal even when escaped; escaping alone does not make it valid.

## S-57 Chart Objects and COLREGs (how to handle each class)

- **Source.** Rule text checked: international Rules 9 and 10 and US Inland Rule 9 (33 USC 2009). Other rule numbers here are from memory; verify before citing in code.
- **COLREGs govern behaviour, not access.** Restricted areas, security zones and bridge clearances come from national law (33 CFR), not COLREGs. Say which when noting a class.
- **Inland vs international.** Landward of the COLREGS demarcation lines the US Inland Rules apply. The router does not know which regime applies and assumes the international rules.

### Principles

- **Safety over distance.** A longer route is fine; never trade a hazard, restriction or clearance for miles. Rule 2 (prudence) and Rule 6 (draught against depth, proximity of hazards) are the reason.
- **Unknown is unsafe.** No chart data, unsurveyed area, missing DRVAL1 or VALSOU: block, never assume safe. Rules 5 to 8 and 11 to 19 need live traffic and cannot be decided from a chart.
- **Rule 9(a): keep near the starboard outer limit as safe and practicable.** Not mid-channel. Prefer about a fifth of the width in from the starboard limit, with leeway. Same wording in the US Inland Rule 9(a).
- **Rule 9(b), 10(j): under 20 m or under sail must not impede.** Stay out of the way of vessels confined to a channel or lane. Under Rule 3, a sailboat with its engine on is power-driven.
- **Direction of buoyage from CATLAM, not from guessing.** Port-hand (1) is on the left going with the buoyage; the vector port to starboard rotated 90 degrees anticlockwise is the direction.

### Rule 10 traffic separation (verified paragraphs)

- **TSSLPT (10(b)(i)).** Go with the flow (ORIENT); wrong way is never allowed. Small craft avoid running along it; large vessels are drawn into it.
- **TSEZNE, TSELNE (10(b)(ii), 10(e)).** Never run along a zone or line; a crossing vessel may cross square-on. A zone with no adjacent lane has no direction: treat as impassable.
- **Crossing (10(c)).** Cross lanes as nearly as practicable at right angles. Measure the angle from square and report the worst; steep cost above 25 degrees off the flow.
- **PRCARE (10(f)).** Particular caution near terminations. Small craft treat a precautionary area as costly (12x); ships expect to pass through it. Never turn or place a waypoint inside one if avoidable.
- **Joining or leaving (10(b)(iii)).** At a lane termination, or from the side at as small an angle as practicable. Not yet modelled; a side entry is currently a crossing.
- **Vessels not using a scheme (10(h)).** Avoid it by as wide a margin as practicable. Small craft get a 1.5 km directional margin that bites on moves along a lane, not across it.
- **ISTZNE (10(d)).** Inshore traffic zone: through traffic that can use the lane must not; under 20 m, sailing and fishing vessels may. Small craft: preferred water. Ships: avoid.
- **TSSBND, TSSRON, TSSCRS.** TSSBND is scheme outline only. TSSRON and TSSCRS are absent from every US chart; if they ever appear, roundabouts run counter-clockwise and crossings are squarely crossable.
- **DWRTPT, DWRTCL, TWRTPT.** Deep-water routes are for vessels constrained by draught (Rule 28); small craft keep clear. Two-way route parts carry traffic both ways.
- **RCTLPT, RCRTCL, RECTRC, NAVLNE, FERYRT.** Recommended lanes, track and navigation lines are guidance to follow inside a channel. Ferry routes: cross square and briskly.
- **10(g) anchoring.** Never anchor in a TSS or near its ends. ACHARE, ACHBRT, ACHPNT within or near a scheme are hazards for routing (vessels at anchor), not destinations.

### Rule 9 narrow channels

- **FAIRWY, DRGARE, CANALS, RIVERS.** Charted channel limits (the dashed lines). Narrow if under 600 m wide (local width, not edge distance). Within 1.5 km of one, running along or cutting across the outside is costly; crossing it is free. Wide fairways change nothing.
- **BOYLAT, BCNLAT.** Pair a port-hand with its mutual-nearest starboard-hand mark within 500 m; a gate counts only in a chain of three or more within 1.5 km. Stay between the marks; outside penalty reaches only 400 m past them.
- **BOYSAW, BCNSAW.** Safe water: mid-channel or landfall, passable close on either side. Treat as gate-neutral; do not pair with lateral marks.
- **BOYCAR, BCNCAR.** Cardinal: pass on the named side (north mark, pass north). Block the cell on the danger side; never pair with lateral marks.
- **BOYISD, BCNISD.** Isolated danger directly below: block with a margin of at least 100 m.
- **BOYSPP, BCNSPP, LITFLT, TOPMAR.** Special purpose: read its function (CATSPM); usually marks a restriction, cable, anchorage or race course. Topmark and light float are information.
- **Rule 9(d), 9(f).** Crossing a channel must not impede vessels confined to it: cross square and promptly. At bends and obstructed parts use extra margin; do not cut the inside of a bend.
- **9(g) anchoring.** Never plan to anchor in a narrow channel.
- **DAYMAR, LIGHTS, RTPBCN, RETRFL, FOGSIG, LNDMRK.** Range marks and sector lights define a channel axis (the red and white bars on shore). Use with NAVLNE and RECTRC to line up; otherwise information for the boater.
- **M_NSYS.** Gives the marks system (IALA A or B) and direction of buoyage. US is IALA B: red on the starboard side returning from sea. Prefer this over inferring direction from mark colours.

### Hazards and depth (Rules 2 and 6)

- **DEPARE, DRGARE.** Open only if DRVAL1 is at least draft plus clearance. DRVAL1 missing means unknown depth: blocked.
- **SOUNDG.** One point per sounding; a value under draft plus clearance blocks its cell. Datum is in M_SDAT; tidal range is not modelled.
- **DEPCNT, SBDARE, SLOGRD, SLOTOP, SPRING.** Contours, seabed and slope lines are information (SLOTOP is a slope top line, usually on land). Rock in SBDARE (NATSUR) is a hazard flag; add a margin over rocky ground.
- **OBSTRN, WRECKS, UWTROC.** Block if depth over it is unknown (VALSOU missing), awash or under draft plus clearance. Points, lines and areas all count; areas are the bulk (37,820 OBSTRN areas) and are not yet read.
- **UNSARE.** Unsurveyed: block. Never rely on DEPARE inside it.
- **WEDKLP, SNDWAV, WATTUR, RAPIDS, WATFAL.** Kelp fouls propellers, sand waves move (add depth margin), tide rips and turbulence are dear for small craft, rapids and falls are blocked.

### Restricted and regulated areas (national law, not COLREGs)

- **RESARE.** Entry prohibited (RESTRN 7) blocks; area to be avoided (14) blocks ships of 50 m or more and costs x20 for smaller vessels. Entry restricted, offshore safety and security zones (CATREA 1) cost x10, but x1.5 if INFORM says Regulated Navigation Area (33 CFR 165: aimed at tankers and tows, not a small craft). Anchoring, fishing and wake limits do not stop a transit.
- **Minefields (CATREA 14).** In NOAA data these are FORMER minefields (Delaware Bay, New Jersey): the chart text says surface navigation is unrestricted and the danger is to anchoring, dredging and trawling. A caution (x5), not a block, unless RESTRN also says entry prohibited. Check INFORM before hard-blocking anything: the free text often decides what the codes cannot (kept only for RESARE, CTNARE, MIPARE, DMPGRD).
- **MIPARE, DMPGRD.** Military practice areas usually apply only while in use and can span a waterway: cost x30, never a wall. Dumping grounds: block chemical, nuclear and explosives (CATDPG 2, 3, 4); spoil and vessel grounds cost x15.
- **CTNARE.** Caution area: penalty, not a block; read INFORM for the reason.
- **CBLARE, PIPARE, CBLSUB, PIPSOL.** No anchoring; a transit is fine. Penalise anchoring only; cable and pipe lines themselves are not obstacles under a keel.
- **FSHGRD, FSHFAC, FSHZNE, MARCUL.** Fishing gear and farms are fixed or floating obstructions: block FSHFAC and MARCUL with a margin, penalise FSHGRD, treat FSHZNE as information. Rule 9(c), 10(i): do not impede fishing vessels.
- **OFSPLF, PRDARE, OSPARE.** Platforms and production areas: block with a safety margin (500 m zones are common).
- **SWPARE.** Swept area cleared to a depth; use it to raise confidence in DRVAL1, never to lower a margin.
- **OILBAR.** Oil barrier: a floating obstruction across water; block along its line.
- **ICEARE, SPLARE, CTSARE, HRBARE, DOCARE, GRIDRN, LOGPON.** Ice: warn. Seaplane areas and cargo transshipment: keep clear. Harbour and dock areas are information. Gridirons and log ponds are obstructed: block.

### Structures in or over the water

- **SLCONS, PONTON, PILPNT, MORFAC, FNCLNE, DYKCON, CAUSWY, HULKES, CONVYR, PYLONS, FLODOC, DRYDOC, GRIDRN.** Fixed things in water the depth data calls open: block along their line or cell, with a small margin. SLCONS covers piers, jetties and breakwaters.
- **GATCON, DAMCON, LOKBSN.** Impassable except through the gate or lock; block.
- **BRIDGE, CBLOHD, PIPOHD.** Compare VERCLR (and HORCLR) with the boat's air draft; a fixed span lower than the mast is impassable. The router has no air draft yet. Opening bridges need CATBRG and schedules; treat as closed.
- **TUNNEL.** Not passable by boat. **CRANES, BERTHS, HRBFAC, SMCFAC, SILTNK, BUISGL, BUAARE.** On land or at the quay; information, except SMCFAC (fuel, launch) which helps the boater.

### Land, coast and physical geography

- **TIDEWY.** Tideway: an area that dries at low water; treat as unsafe unless DRVAL1 says otherwise.
- **LNDARE, COALNE, LNDELV, LNDRGN, LAKARE, SEAARE, VEGATN, ROADWY, RAILWY, AIRARE, RUNWAY.** LNDARE is blocked. Coastline, elevation, regions, roads and airports are information; SEAARE is a named sea area. Never let LAKARE or SEAARE mark water safe.
- **LNDMRK.** Landmark for position fixing; information.

### Currents, tides and magnetics (Rule 6)

- **CURENT, TS_FEB, TS_TIS.** Current strength and direction matter for Rule 6 and for the v0.6.0 flow milestone; not modelled. TS_PRH, TS_PNH, T_HMON and friends are not shipped in NOAA ENCs.
- **MAGVAR, LOCMAG.** Variation converts bearings for display; a local anomaly warns of compass error. Information.

### Stations, signals and pilotage

- **PILBOP.** Pilot boarding place: ships converge and slow; keep well clear.
- **RDOCAL.** VTS calling-in point (33 CFR 161): where a vessel must report. Information for the boater, not a routing block.
- **SISTAW, SISTAT, RDOSTA, RADSTA, CGUSTA, RSCSTA, DISMAR, CTRPNT, FORSTC.** Warning and traffic signals, radio, radar, coastguard and rescue stations, distance marks, survey control points and forts are information.

### Chart metadata and boundaries

- **M_COVR.** Where the chart has data. No coverage is not safe water: block (already the default).
- **M_QUAL.** Data quality zones (CATZOC); poor quality should add depth margin. M_SREL and M_CSCL are reliability and scale hints. Not yet used.
- **M_NPUB, M_SDAT, M_VDAT.** Publication info, sounding datum and vertical datum. The vertical datum matters for bridge clearance heights.
- **ADMARE, EXEZNE, CONZNE, COSARE, TESARE, _texto, NEWOBJ.** Legal boundaries, notes and user objects are information; read NEWOBJ attributes before deciding anything.

## General Style Notes

- **Keep lines under 120 characters.** Long lines are hard to review side-by-side in a diff or split editor pane, and tend to signal a line doing too many things at once. Wrap or break up expressions rather than letting them run long.
- **Add docstrings to explain code.** Focus on *why* a function/class exists or *why* it does something non-obvious — the code itself already shows *what* it does. A docstring worth writing usually covers intent, assumptions, edge cases, or a gotcha a future reader would otherwise have to rediscover the hard way.
- **Strip docstrings when building a release.** Release builds don't need internal rationale shipped alongside the binary — it bloats the artifact and can leak implementation notes you didn't mean to publish. Run Python with `-OO` (or an equivalent build step) to drop docstrings and assertions from the compiled output before packaging.
