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

// Sum of cell costs along the Bresenham segment a->b (endpoints included). Only meaningful after lineOfSight().
float segmentCost(const CostGrid& grid, Cell a, Cell b) {
    float total = 0.0f;
    int x = a.col, y = a.row;
    const int dx = std::abs(b.col - a.col), dy = std::abs(b.row - a.row);
    const int sx = a.col < b.col ? 1 : -1, sy = a.row < b.row ? 1 : -1;
    int err = dx - dy;
    while (true) {
        total += grid.cost({x, y});
        if (x == b.col && y == b.row) return total;
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

}  // namespace

bool lineOfSight(const CostGrid& grid, Cell a, Cell b) {
    // Bresenham walk; also rejects diagonal corner cutting between two blocked neighbours. Both ends inside the grid keeps
    // every cell on the segment inside it too, so the walk below never indexes out of range.
    if (!grid.inBounds(a) || !grid.inBounds(b)) return false;
    int x = a.col, y = a.row;
    const int dx = std::abs(b.col - a.col), dy = std::abs(b.row - a.row);
    const int sx = a.col < b.col ? 1 : -1, sy = a.row < b.row ? 1 : -1;
    int err = dx - dy;
    while (true) {
        if (grid.blocked({x, y})) return false;
        if (x == b.col && y == b.row) return true;
        const int e2 = 2 * err;
        const int px = x, py = y;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
        if (x != px && y != py && (grid.blocked({x, py}) || grid.blocked({px, y}))) return false;
    }
}

std::vector<LatLon> findRoute(const CostGrid& grid, LatLon start, LatLon goal) {
    const Cell s = grid.cellAt(start), g = grid.cellAt(goal);
    if (!grid.inBounds(s) || !grid.inBounds(g) || grid.blocked(s) || grid.blocked(g)) return {};

    const int cols = grid.cols();
    const auto idxOf = [cols](Cell c) { return c.row * cols + c.col; };
    const size_t n = static_cast<size_t>(cols) * grid.rows();
    std::vector<float> best(n, kBlocked);
    std::vector<int> parent(n, -1);
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    best[idxOf(s)] = 0.0f;
    open.push({heuristic(s, g), idxOf(s)});
    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        const Cell c{cur.idx % cols, cur.idx / cols};
        if (c == g) break;
        if (cur.f - heuristic(c, g) > best[cur.idx] + 1e-4f) continue;  // stale queue entry
        for (int dr = -1; dr <= 1; ++dr) {
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                const Cell nb{c.col + dc, c.row + dr};
                if (!grid.inBounds(nb) || grid.blocked(nb)) continue;
                if (dr != 0 && dc != 0 && (grid.blocked({c.col + dc, c.row}) || grid.blocked({c.col, c.row + dr}))) {
                    continue;  // no squeezing diagonally between two blocked cells
                }
                const float step = (dr != 0 && dc != 0) ? 1.41421356f : 1.0f;
                const float cand = best[cur.idx] + step * 0.5f * (grid.cost(c) + grid.cost(nb));
                if (cand < best[idxOf(nb)]) {
                    best[idxOf(nb)] = cand;
                    parent[idxOf(nb)] = cur.idx;
                    open.push({cand + heuristic(nb, g), idxOf(nb)});
                }
            }
        }
    }
    if (parent[idxOf(g)] == -1 && !(s == g)) return {};

    std::vector<Cell> cells;
    for (int i = idxOf(g); i != -1; i = parent[i]) cells.push_back({i % cols, i / cols});
    std::reverse(cells.begin(), cells.end());

    // Greedy string-pulling: keep a waypoint only where a shortcut becomes unsafe or more expensive. Checking cost
    // as well as line of sight stops smoothing from cutting through penalised cells (e.g. out of a channel centre).
    std::vector<LatLon> route{grid.centre(cells.front())};
    size_t anchor = 0;
    float pathSum = grid.cost(cells[0]);  // cost of the grid path from cells[anchor] to cells[i-1]
    for (size_t i = 1; i < cells.size(); ++i) {
        pathSum += grid.cost(cells[i]);
        if (i - anchor < 2) continue;
        if (!lineOfSight(grid, cells[anchor], cells[i]) ||
            segmentCost(grid, cells[anchor], cells[i]) > pathSum + 1e-3f) {
            anchor = i - 1;
            route.push_back(grid.centre(cells[anchor]));
            pathSum = grid.cost(cells[anchor]) + grid.cost(cells[i]);
        }
    }
    if (cells.size() > 1) route.push_back(grid.centre(cells.back()));
    return route;
}

}  // namespace oar
