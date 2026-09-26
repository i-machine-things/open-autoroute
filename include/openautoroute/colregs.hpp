#pragma once

// COLREGs rule enforcement hooks (v0.1.0 milestone). Declarations only for now; see ROADMAP.md.
//
// Rule 9 (narrow channels): bias the route to the starboard side of a fairway. This is direction-dependent, so it
//   cannot be a plain per-cell cost; it needs the travel direction (e.g. a directed cost or a post-route offset).
// Rule 10 (traffic separation schemes): restrict routing to lane vectors and force perpendicular crossings; needs
//   TSS lane geometry with direction from the S-57 parser.
