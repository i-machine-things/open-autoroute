#include "openautoroute/pathfinder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace oar {
namespace {

struct Node {
    float f;
    int idx;
    bool operator>(const Node& o) const { return f > o.f; }
};

// Octile distance: exact shortest path on an obstacle-free 8-connected grid where every cell costs >= 1.
float heuristic(Cell a, Cell b) {
    const float dx = static_cast<float>(std::abs(a.col - b.col));
    const float dy = static_cast<float>(std::abs(a.row - b.row));
    return (dx + dy) + (1.41421356f - 2.0f) * std::min(dx, dy);
}

// Visit every cell that the straight segment between the centres of a and b passes through, in order (a "supercover" walk:
// unlike Bresenham it never skips a cell the real line clips, and on an exact corner it visits both neighbours). Hard checks
// (blocked water, wrong-way lane travel) use this so they agree with what the continuous route actually touches.
template <typename Fn>
void forEachCellOnSegment(Cell a, Cell b, Fn&& visit) {
    const int dx = b.col - a.col, dy = b.row - a.row;
    const int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    const double inf = 1e30;
    const double tDx = dx != 0 ? 1.0 / std::abs(dx) : inf, tDy = dy != 0 ? 1.0 / std::abs(dy) : inf;
    double tMaxX = dx != 0 ? 0.5 * tDx : inf, tMaxY = dy != 0 ? 0.5 * tDy : inf;
    int x = a.col, y = a.row;
    visit(Cell{x, y});
    while (x != b.col || y != b.row) {
        const double eps = 1e-9;
        if (tMaxX < tMaxY - eps) {
            x += sx; tMaxX += tDx;
        } else if (tMaxY < tMaxX - eps) {
            y += sy; tMaxY += tDy;
        } else {  // passes exactly through a corner: both neighbours count, then step diagonally
            visit(Cell{x + sx, y});
            visit(Cell{x, y + sy});
            x += sx; y += sy; tMaxX += tDx; tMaxY += tDy;
        }
        visit(Cell{x, y});
    }
}

// What one cell does to a move on `headingDeg`: 1 = nothing, > 1 = dearer, < 1 = cheaper (a lane a large vessel wants to use),
// kBlocked = not allowed. Lane cells: with the flow uses the vessel's lane-use factor, against it is refused, and a crossing
// costs the crossing formula (scaled up a little for small craft, which should avoid lanes). Separation zone cells: running
// along them is refused, crossing them costs double a lane crossing, and a zone with no known direction is impassable.
constexpr float kZoneCrossingScale = 2.0f;

float cellFactor(const CostGrid& grid, Cell c, double headingDeg) {
    const float use = grid.laneUseFactor();
    const float crossScale = use > 1.0f ? 1.0f + 0.2f * (use - 1.0f) : 1.0f;  // small craft: 6 -> 2x on crossings, not 6x
    if (grid.isZone(c)) {
        const float zd = grid.zoneDirection(c);
        if (std::isnan(zd)) return kBlocked;
        const float lf = laneFactor(headingDeg, zd);
        if (lf == 1.0f || lf == kBlocked) return kBlocked;  // running along a zone, in either direction
        return lf * kZoneCrossingScale * crossScale;
    }
    const float lane = grid.laneDirection(c);
    if (std::isnan(lane)) return 1.0f;
    const float lf = laneFactor(headingDeg, lane);
    if (lf == 1.0f) return use;  // with the flow
    if (lf == kBlocked) return kBlocked;
    return lf * crossScale;
}

// Cost multiplier for one move between adjacent cells: the worse of the two cells, or the cheaper one when both are lane
// cells travelled with the flow (so a large vessel is drawn along a lane rather than dipping out of it).
float moveFactor(const CostGrid& grid, Cell a, Cell b, double headingDeg) {
    const float fa = cellFactor(grid, a, headingDeg), fb = cellFactor(grid, b, headingDeg);
    if (fa == kBlocked || fb == kBlocked) return kBlocked;
    const float f = (fa > 1.0f || fb > 1.0f) ? std::max(fa, fb) : std::min(fa, fb);
    // Leeway margin next to lanes: charged by direction, so it discourages skimming a lane but not approaching to cross it.
    // Rule 9: keep to the starboard side of a buoyed channel (moves across it are not charged).
    const float side = std::max(grid.gateSideFactor(a, headingDeg), grid.gateSideFactor(b, headingDeg));
    // Between the dashed limits of a narrow channel: running alongside the outside of them is dear, crossing them is not.
    const float edge = std::max(grid.channelMarginFactor(a, headingDeg), grid.channelMarginFactor(b, headingDeg));
    return f * std::max(grid.laneMarginFactor(a, headingDeg), grid.laneMarginFactor(b, headingDeg)) * side * edge;
}

// Heading in degrees true of a move from a to b. Cells are square in metres and rows run south.
double cellHeading(Cell a, Cell b) {
    const double deg = std::atan2(static_cast<double>(b.col - a.col), static_cast<double>(a.row - b.row)) * 180.0 / 3.14159265358979;
    return deg < 0 ? deg + 360.0 : deg;
}

float moveCost(const CostGrid& grid, Cell a, Cell b, double headingDeg) {
    const float len = (a.col != b.col && a.row != b.row) ? 1.41421356f : 1.0f;
    return len * 0.5f * (grid.cost(a) + grid.cost(b)) * moveFactor(grid, a, b, headingDeg);
}

// Cost of the straight segment a->b, walking the Bresenham cells and using the segment's own heading for lane factors.
// kBlocked when the segment runs against a lane's flow. Only meaningful after lineOfSight().
float segmentCost(const CostGrid& grid, Cell a, Cell b) {
    const double heading = cellHeading(a, b);
    bool refused = false;
    forEachCellOnSegment(a, b, [&](Cell c) { refused = refused || cellFactor(grid, c, heading) == kBlocked; });
    if (refused) return kBlocked;  // wrong way in a lane, or along a zone, anywhere on the real line (not just Bresenham cells)
    float total = 0.0f;
    int x = a.col, y = a.row;
    const int dx = std::abs(b.col - a.col), dy = std::abs(b.row - a.row);
    const int sx = a.col < b.col ? 1 : -1, sy = a.row < b.row ? 1 : -1;
    int err = dx - dy;
    while (!(x == b.col && y == b.row)) {
        const Cell from{x, y};
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
        total += moveCost(grid, from, {x, y}, heading);
    }
    return total;
}

}  // namespace

float laneFactor(double headingDeg, double laneDeg) {
    const double theta = angleDiffDeg(headingDeg, laneDeg);  // 0 = with the flow, 180 = against it
    if (theta <= 25.0) return 1.0f;
    if (theta >= 155.0) return kBlocked;
    const double s = std::sin(theta * 3.14159265358979 / 180.0);
    return static_cast<float>(2.0 + 30.0 * (1.0 - s));
}

bool lineOfSight(const CostGrid& grid, Cell a, Cell b) {
    // Both ends inside the grid keeps every cell on the segment inside it too, so the walk never indexes out of range.
    if (!grid.inBounds(a) || !grid.inBounds(b)) return false;
    bool clear = true;
    forEachCellOnSegment(a, b, [&](Cell c) { clear = clear && !grid.blocked(c); });
    return clear;
}

WaterBodies findWaterBodies(const CostGrid& grid) {
    WaterBodies out;
    const int cols = grid.cols(), rows = grid.rows();
    out.label.assign(static_cast<size_t>(cols) * rows, -1);
    std::vector<int> stack;
    for (int start = 0; start < cols * rows; ++start) {
        if (out.label[start] != -1 || grid.blocked({start % cols, start / cols})) continue;
        const int id = static_cast<int>(out.size.size());
        out.size.push_back(0);
        out.label[start] = id;
        stack.assign(1, start);
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            ++out.size[id];
            const Cell c{cur % cols, cur / cols};
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    const Cell nb{c.col + dc, c.row + dr};
                    if (!grid.inBounds(nb) || grid.blocked(nb)) continue;
                    if (dr != 0 && dc != 0 && (grid.blocked({c.col + dc, c.row}) || grid.blocked({c.col, c.row + dr}))) continue;
                    const int idx = nb.row * cols + nb.col;
                    if (out.label[idx] != -1) continue;
                    out.label[idx] = id;
                    stack.push_back(idx);
                }
            }
        }
    }
    return out;
}

std::vector<LatLon> findRoute(const CostGrid& grid, LatLon start, LatLon goal, double simplifyTolerance,
                              std::vector<Cell>* rawPath, double minLegM) {
    const Cell s = grid.cellAt(start), g = grid.cellAt(goal);
    if (!grid.inBounds(s) || !grid.inBounds(g) || grid.blocked(s) || grid.blocked(g)) return {};

    const int cols = grid.cols();
    const auto idxOf = [cols](Cell c) { return c.row * cols + c.col; };
    const size_t n = static_cast<size_t>(cols) * grid.rows();
    std::vector<float> best(n, kBlocked);
    std::vector<int> parent(n, -1);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    // Lanes cheaper than open water (factor < 1, large vessels) would make the plain distance estimate overshoot the true
    // cost, so scale it down by the cheapest possible multiplier to keep A* exact.
    const float hScale = std::min(1.0f, grid.laneUseFactor());
    const auto h = [&](Cell c) { return heuristic(c, g) * hScale; };

    best[idxOf(s)] = 0.0f;
    open.push({h(s), idxOf(s)});
    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        const Cell c{cur.idx % cols, cur.idx / cols};
        if (c == g) break;
        if (cur.f - h(c) > best[cur.idx] + 1e-4f) continue;  // stale queue entry
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                const Cell nb{c.col + dc, c.row + dr};
                if (!grid.inBounds(nb) || grid.blocked(nb)) continue;
                if (dr != 0 && dc != 0 && (grid.blocked({c.col + dc, c.row}) || grid.blocked({c.col, c.row + dr}))) {
                    continue;  // no squeezing diagonally between two blocked cells
                }
                const float move = moveCost(grid, c, nb, cellHeading(c, nb));
                if (move == kBlocked) continue;  // wrong way through a traffic lane
                const float cand = best[cur.idx] + move;
                if (cand < best[idxOf(nb)]) {
                    best[idxOf(nb)] = cand;
                    parent[idxOf(nb)] = cur.idx;
                    open.push({cand + h(nb), idxOf(nb)});
                }
            }
        }
    }
    if (parent[idxOf(g)] == -1 && !(s == g)) return {};

    std::vector<Cell> cells;
    for (int i = idxOf(g); i != -1; i = parent[i]) cells.push_back({i % cols, i / cols});
    std::reverse(cells.begin(), cells.end());
    if (rawPath) *rawPath = cells;

    // Cumulative cost along the grid path, so a shortcut can be compared with the stretch it replaces.
    std::vector<float> cum(cells.size(), 0.0f);
    for (size_t i = 1; i < cells.size(); ++i) cum[i] = cum[i - 1] + moveCost(grid, cells[i - 1], cells[i], cellHeading(cells[i - 1], cells[i]));

    // Greedy string-pulling: keep a waypoint only where a shortcut becomes unsafe or costs more than the stretch it replaces
    // (plus `simplifyTolerance`, a fraction, so near-equal detours collapse into one leg). Checking cost as well as line of
    // sight stops smoothing from cutting through penalised cells (a channel edge, or a traffic lane at an angle).
    const double slack = 1.0 + std::max(0.0, simplifyTolerance);
    std::vector<size_t> keep{0};  // indices into `cells` of the waypoints kept so far
    size_t anchor = 0;
    for (size_t i = 2; i < cells.size(); ++i) {
        if (i - anchor < 2) continue;
        if (!lineOfSight(grid, cells[anchor], cells[i]) ||
            segmentCost(grid, cells[anchor], cells[i]) > (cum[i] - cum[anchor]) * slack + 1e-3f) {
            // Put the turning point at the last cell before the failed shortcut, but never inside a traffic lane if there is
            // an earlier cell outside one: turning in a lane means the vessel is manoeuvring there, and it makes the
            // crossing angle depend on the turn instead of on a clean leg.
            size_t a = i - 1;
            for (size_t j = i - 1; j > anchor; --j) {
                if (std::isnan(grid.laneDirection(cells[j]))) { a = j; break; }
            }
            anchor = a;
            keep.push_back(anchor);
            i = anchor + 1;  // resume just past the new anchor (the loop increment moves to anchor + 2)
        }
    }
    keep.push_back(cells.size() - 1);

    // Minimum leg length: waypoints stacked a few tens of metres apart (a staircase round a headland) are useless on a
    // chartplotter. Repeatedly try to drop the waypoint that has the shortest neighbouring leg while that leg is under the
    // minimum, but only if the straight leg replacing it is legal and no more than 25% dearer than the stretch of the
    // original path it replaces (measured against that path, so repeated drops cannot drift).
    if (minLegM > 0.0 && keep.size() > 2) {
        const double minCells = minLegM / grid.cellSizeM();
        auto gap = [&](size_t a, size_t b) {
            const double dx = cells[keep[a]].col - cells[keep[b]].col, dy = cells[keep[a]].row - cells[keep[b]].row;
            return std::sqrt(dx * dx + dy * dy);
        };
        std::vector<size_t> pinned;  // waypoints already found necessary
        while (keep.size() > 2) {
            size_t pick = 0;
            double shortest = minCells;
            for (size_t k = 1; k + 1 < keep.size(); ++k) {
                if (std::find(pinned.begin(), pinned.end(), keep[k]) != pinned.end()) continue;
                const double g = std::min(gap(k - 1, k), gap(k, k + 1));
                if (g < shortest) { shortest = g; pick = k; }
            }
            if (pick == 0) break;  // nothing left under the minimum that can still be dropped
            const size_t ia = keep[pick - 1], ib = keep[pick + 1];
            const bool legal = lineOfSight(grid, cells[ia], cells[ib]);
            const float cost = legal ? segmentCost(grid, cells[ia], cells[ib]) : kBlocked;
            if (legal && cost <= (cum[ib] - cum[ia]) * 1.25f + 1e-3f) keep.erase(keep.begin() + static_cast<long>(pick));
            else pinned.push_back(keep[pick]);
        }
    }
    std::vector<LatLon> route;
    for (size_t k = 0; k + 1 < keep.size(); ++k) route.push_back(grid.centre(cells[keep[k]]));
    if (cells.size() > 1) route.push_back(grid.centre(cells.back()));
    return route;
}

}  // namespace oar
