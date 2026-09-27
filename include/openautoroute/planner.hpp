#pragma once

// The whole route-planning pipeline as one library call, so a front end (the command line, the OpenCPN plugin, later the app) is just
// a caller: read the charts, build the cost grid, search, smooth, and report what the route crosses.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "openautoroute/cost_grid.hpp"

namespace oar {

/// Everything a plan depends on. Distances are metres, depths metres, positions decimal degrees (the core is metric and nautical;
/// converting to feet or knots is the front end's job).
struct PlanRequest {
    std::string encDir;                 // folder searched recursively for ENC base cells (.000)
    LatLon from{}, to{};
    std::vector<LatLon> evalRoute;      // if two or more points: score this route instead of planning one (from/to are taken from it)
    double draftM = 1.5, clearanceM = 1.0;
    double lengthM = 12.0;
    bool underSail = false;             // engine off: treated as a small vessel at any length (COLREGs Rule 10(j))
    double airDraftM = -1.0;            // height above the waterline; negative = estimate from length (see vessel.hpp)
    double cellM = 30.0;                // grid resolution; smaller is finer and slower
    double minLegM = 460.0;             // preferred minimum distance between waypoints
    double simplifyTolerance = 0.05;
    std::string routeName;              // for the GPX/route: default "open-autoroute lat,lon to lat,lon"
    std::string startName = "START", endName = "END";

    // Tuning that has a sensible default; a front end normally leaves these alone.
    double shoreMarginM = 500.0, shoreMarginWeight = 10.0;
    double laneMarginM = 1500.0, laneMarginWeight = 12.0;
    double laneUse = -1.0, caution = -1.0;  // negative = decide from the vessel
    bool applyTss = true;

    // Developer switches, used to compare runs; not for a user interface.
    bool useMarks = true, useChannels = true, useHazards = true, useCatalogue = true;   // useCatalogue=false opens every cell, to compare speed
    std::vector<std::string> skipClasses;
};

/// One step of progress, offered to the caller while planning. `phase` is a short label ("Reading charts", "Searching"...),
/// `fraction` is how far through that phase (0 to 1) and `overall` is a rough position in the whole plan (0 to 1).
struct PlanProgress {
    const char* phase = "";
    double fraction = 0.0;
    double overall = 0.0;
};

struct PlanHooks {
    std::function<void(const std::string&)> out;   // ordinary messages, each with its trailing newline
    std::function<void(const std::string&)> err;   // problems, each with its trailing newline
    std::function<bool(const PlanProgress&)> progress;  // return false to cancel; may be called from long loops, keep it quick
    std::function<void(const CostGrid&, const std::vector<Cell>&, const std::vector<LatLon>&)> debugGrid;  // grid, raw A* cells, route
};

/// A restricted, dangerous or costed area the route crosses (see AreaNote), with how much of the route is inside it.
struct AreaCrossing {
    std::string kind, text;
    float factor = 1.0f;
    double metres = 0.0;
    int stretches = 0;
    LatLon at{};
};

struct PlanResult {
    int status = 0;                 // 0 planned (or scored), 1 no route or cancelled, 2 the request itself was unusable
    std::string failReason;         // machine-readable when status is not 0: "disconnected_water", "no_route", "cancelled", ...
    bool cancelled = false;
    std::vector<LatLon> route;      // waypoints, start to end; on a plan the first and last are the (possibly moved) endpoints
    std::string routeName, description;  // for the GPX <name> and <desc>
    double nm = 0.0, straightNm = 0.0;
    int chartsUsed = 0;
    double snapStartM = 0.0, snapEndM = 0.0;  // how far the endpoints were moved to reach safe water
    std::vector<AreaCrossing> areasCrossed;   // dearest first
    std::string summaryLine;        // one machine-readable line ("SUMMARY found=1 ..." or "SUMMARY found=0 reason=..."), for scripts
    std::shared_ptr<CostGrid> grid; // the router's view of the water, for drawing a picture
};

/// The extent of one ENC cell, in degrees, as listed in the catalogue (CATALOG.031) that ships with NOAA's ENC_ROOT.
struct CellExtent {
    double south = 0, west = 0, north = 0, east = 0;
};

/// Read the cell extents from the bytes of a CATALOG.031 file, keyed by cell name (e.g. "US5WA3CJ"). Lets the planner skip the thousands
/// of cells a route does not touch without opening them. Entries it cannot read are left out; an empty map means "no catalogue".
std::map<std::string, CellExtent> parseEncCatalog(const std::string& bytes);

/// A cell size in metres for a route between two points: `preferredM` when the grid stays within `maxCells`, otherwise the smallest
/// multiple of 10 m that does. A front end uses this so a long passage never asks for more memory than the machine has (the planner
/// itself refuses a grid over 25 million cells).
double suggestedCellM(LatLon from, LatLon to, double preferredM = 30.0, double maxCells = 6e6);

/// Plan (or, with `evalRoute`, score) a route. Never throws for chart or routing problems; those come back in `status` and `failReason`.
PlanResult planRoute(const PlanRequest& request, const PlanHooks& hooks = {});

}  // namespace oar
