#pragma once

// Where each COLREGs rule the router applies lives in the code. The rule text was checked against the international rules and the US
// Inland Rules (33 USC 2009, 2010); .claude/CODING_NOTES.md has the full mapping from chart objects to rules.
//
// Rule 9  narrow channels     gate corridors and starboard side: chart_grid.hpp applyChannelGates, cost_grid.hpp gateSideFactor;
//                             stay between the charted limits: cost_grid.hpp markNarrowChannels / applyChannelPreference
// Rule 10 traffic separation  lanes, zones, precautionary areas: chart_grid.hpp stampChart; lane and zone move costs: pathfinder.cpp
//                             cellFactor; lane margin: cost_grid.hpp applyLaneMargin; vessel size (10(j)): vessel.hpp
// Rules 5-8, 11-19 (lookout, safe speed, encounters) depend on other traffic and cannot be decided from a chart: not a router concern.
