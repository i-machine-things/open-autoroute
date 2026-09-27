#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openautoroute/cost_grid.hpp"
#include "openautoroute/s57.hpp"

namespace oar {

/// Paint one chart's verdict onto `grid`: only cells the chart actually covers (any DEPARE or LNDARE polygon) are
/// overwritten, so stamping charts from coarse to fine scale lets the largest-scale chart win where it has data.
///
/// The exception is physical hazards (wrecks, rocks, obstructions, structures, marks, a bridge with a charted low clearance): those
/// accumulate over every chart, because a finer chart often just does not draw what a coarser one does. Areas (restricted, military,
/// unsurveyed, dumping, caution) and bridges of unknown clearance are the covering chart's own, since overview charts draw them roughly.
///
/// Safety rules (a cell is open only if the chart positively says it is deep enough):
///   - DEPARE or DRGARE (dredged) with DRVAL1 >= minDepthM is open; shallower, or DRVAL1 unknown, is blocked
///   - LNDARE is blocked
///   - OBSTRN / WRECKS / UWTROC / SOUNDG points shoaler than minDepthM block their cell; an OBSTRN, WRECKS or UWTROC
///     with no depth value is treated as unsafe
/// Cells no chart covers stay whatever the caller filled the grid with (normally kBlocked: no data is not safe water).
///
/// With `applyTss` (default) the chart's traffic separation scheme is applied too, for COLREGs Rule 10: TSSLPT lane parts
/// store their flow direction (ORIENT) on the grid for the router.
///
/// Separation zones (TSEZNE) and separation lines (TSELNE) become zone cells: crossable square on, never along (see
/// CostGrid::setZone; call grid.assignZoneDirections() after stamping all charts). Precautionary areas (PRCARE) cost
/// `cautionFactor` times normal to move through, since vessels there must navigate with particular caution.
void stampChart(const ChartData& chart, double minDepthM, CostGrid& grid, bool applyTss = true, double cautionFactor = 1.0);

/// A restricted or dangerous area the router may still cross at a cost (military or security zone, reserve, dumping ground...),
/// kept so the tool can tell the skipper which ones a route goes through.
struct AreaNote {
    std::string kind;   // what sort of area, e.g. "military area", "reserve or sanctuary"
    std::string text;   // the chart's own wording (INFORM), shortened; may be empty
    float factor = 1.0f;  // how much dearer the router treats it
};

/// Which AreaNote covers each grid cell (-1 none). It mirrors the cost the router sees: a finer chart that covers a cell with no such
/// area clears it, and where areas overlap the dearer one is kept.
struct AreaLayer {
    std::vector<AreaNote> notes;
    std::vector<int32_t> id;
};

/// Working arrays for stampChart. A route can be stamped from a hundred charts, and each stamp needs a dozen grid-sized arrays: reusing
/// one StampScratch keeps them allocated between charts and only the part of the grid a chart touches is cleared again afterwards. The
/// contents mean nothing between calls; it only has to belong to one grid at a time. Optional (stampChart makes its own without one).
struct StampScratch {
    size_t n = 0;  // cells in the grid these arrays were made for
    std::vector<uint8_t> state, covered, land, zone, caution, channel, shut, shutLocal, lock, lockNear;
    std::vector<float> penalty;
    std::vector<int32_t> sinkAt;
};

/// Everything stampChart needs to know about the vessel and the rules to apply.
struct StampOptions {
    double minDepthM = 2.5;     // draft plus clearance
    bool applyTss = true;       // traffic separation schemes (Rule 10)
    double cautionFactor = 1.0; // cost multiplier for precautionary areas
    double airDraftM = 0.0;     // height above the waterline, for bridge and overhead cable clearance
    double vesselLengthM = 12.0; // areas to be avoided bind ships, not small craft (see RESARE below)
    bool hazardObjects = true;  // the chart-object hazard rules below (developer switch, for comparing runs)
    std::vector<std::string> skipClasses;  // developer switch: hazard classes to ignore, to find which rule blocks a route
    AreaLayer* areas = nullptr;  // if set, records the costed restricted areas per cell (for the crossed-areas report)
    StampScratch* scratch = nullptr;  // reuse working arrays between charts (see StampScratch)
};

/// stampChart plus the hazard rules for chart objects beyond depth and land, each following the notes in .claude/CODING_NOTES.md and
/// the attribute tables in OpenCPN's s57expectedinput.csv. Anything that can be struck or that forbids entry blocks its cells; softer
/// warnings raise the cost. Blocking is always the safe direction, so these rules apply even where the chart has no depth area.
///   - OBSTRN, WRECKS, UWTROC (points, lines and areas): blocked when awash, dry, partly submerged, uncovering, of unknown depth, or shallower
///     than draft plus clearance
///   - UNSARE, FSHFAC, MARCUL, PRDARE, OSPARE, HULKES: blocked
///   - fixed structures (SLCONS, PONTON, PILPNT, MORFAC, FNCLNE, DYKCON, CAUSWY, CONVYR, PYLONS, FLODOC, DRYDOC, GATCON, DAMCON, GRIDRN,
///     OILBAR, OFSPLF) and RAPIDS / WATFAL: blocked; offshore platform points also get a 250 m berth
///   - RESARE: blocked for entry prohibited, and for an area to be avoided when the vessel is 50 m
///     or longer (those bind ships; a smaller vessel pays x20); a military area is x30 only if the chart restricts entry or lists no restriction (x2 when it lists only anchoring, fishing, trawling and the like), and a charted minefield x5 (in NOAA data these are former minefields open to surface navigation; both block if entry is also prohibited); costly for
///     entry restricted or an offshore safety zone (x10, these are usually security zones needing permission; x1.5 when the chart text says
///     it is a Regulated Navigation Area, which binds particular vessels such as tankers and tows), swimming and dredging areas; anchoring, fishing and similar restrictions do not stop a transit.
///     Nature reserves and sanctuaries (whale sanctuary, bird, seal, fish, ecological) only ask for care and cost x1.2.
///     The chart's own wording settles military and security zones: text saying the area is closed, "keep out" or "no entry" blocks it; text
///     saying only to use caution while transiting makes it x2
///   - BOYISD / BCNISD: 100 m blocked; BOYCAR / BCNCAR: the danger side of the mark blocked out to 150 m
///   - BRIDGE, CBLOHD, PIPOHD: blocked unless the clearance (closed clearance for an opening bridge) is at least air draft plus 1 m
///   - DMPGRD: chemical, nuclear and explosives dumping grounds are blocked; spoil and vessel grounds x15
///   - MIPARE x30 (danger zones usually apply only while in use and can span a waterway; x2 if only anchoring/fishing limits or a caution note are listed; blocked if the text says keep out),
///     CTNARE x3 (x1.5 when the text is only the "less detail, use a more detailed chart" note), ACHARE x3, WEDKLP x1.5, WATTUR x3, SPLARE x5: costlier
void stampChart(const ChartData& chart, const StampOptions& options, CostGrid& grid);
/// Fairways (FAIRWY) and dredged areas (DRGARE) are also recorded as channel cells (the dashed limits drawn on a chart) for
/// CostGrid::applyChannelPreference; they are never made cheaper or dearer by stampChart itself.

/// A port-hand or starboard-hand lateral mark (red/green buoy or beacon). Category is CATLAM: 1 port-hand, 2 starboard-hand.
struct LateralMark {
    LatLon at;
    int category = 0;
};

/// A pair of opposite lateral marks across a channel: a vessel following the channel passes between them.
struct Gate {
    LatLon port;
    LatLon starboard;
};

/// Append the port-hand and starboard-hand lateral marks (BOYLAT, BCNLAT) of a chart to `out`.
void collectLateralMarks(const ChartData& chart, std::vector<LateralMark>& out);

/// Turn lateral marks into a buoyed corridor. Each port-hand mark is paired with its nearest starboard-hand mark when the two
/// are each other's nearest opposite within `maxGateM` (across the channel); the limit is what defines a NARROW channel, so a
/// wide pair of marks (a bay, a traffic scheme) makes no gate and changes nothing. A gate also needs at least two neighbours within
/// 1.5 km: a lone pair is not a channel. The rules act only in the corridor of such a chain, and reach only `lateralRangeM` beyond
/// the marks, so open water away from a marked channel is unaffected. Along the stretch of channel each gate is
/// responsible for (half the way to its neighbours), cells beyond either mark, out to `lateralRangeM`, cost `outsidePenalty`
/// times normal so a route stays between the marks, and cells between them record their position across the channel so the
/// router can prefer the starboard side (COLREGs Rule 9, weight `sideWeight`, see CostGrid::gateSideFactor). Call once after all
/// charts are stamped and before any margins. Returns the gates found, for reporting.
std::vector<Gate> applyChannelGates(CostGrid& grid, const std::vector<LateralMark>& marks, double maxGateM = 500.0,
                                    double outsidePenalty = 8.0, double sideWeight = 1.5, double lateralRangeM = 400.0);

}  // namespace oar
