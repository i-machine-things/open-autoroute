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

// Cost multiplier for one move between adjacent cells: the worst lane factor of the two cells (1 outside lanes).
float moveFactor(const CostGrid& grid, Cell a, Cell b, double headingDeg) {
    float f = 1.0f;
    for (Cell c : {a, b}) {
        const float lane = grid.laneDirection(c);
        if (std::isnan(lane)) continue;
        float lf = laneFactor(headingDeg, lane);
        if (lf == 1.0f) lf = grid.laneUseFactor();  // with the flow: normal use of the lane, priced by the vessel type
        f = f == 1.0f ? lf : std::max(f, lf);       // of two lane cells the worse applies; a cheap one never discounts a crossing
    }
    return f;
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
    return static_cast<float>(3.0 + 6.0 * (1.0 - s));
}

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

    // Cumulative cost along the grid path, so a shortcut can be compared with the stretch it replaces.
    std::vector<float> cum(cells.size(), 0.0f);
    for (size_t i = 1; i < cells.size(); ++i) cum[i] = cum[i - 1] + moveCost(grid, cells[i - 1], cells[i], cellHeading(cells[i - 1], cells[i]));

    // Greedy string-pulling: keep a waypoint only where a shortcut becomes unsafe or more expensive. Checking cost as well
    // as line of sight stops smoothing from cutting through penalised cells (a channel edge, or a traffic lane at an angle).
    std::vector<LatLon> route{grid.centre(cells.front())};
    size_t anchor = 0;
    for (size_t i = 2; i < cells.size(); ++i) {
        if (i - anchor < 2) continue;
        if (!lineOfSight(grid, cells[anchor], cells[i]) ||
            segmentCost(grid, cells[anchor], cells[i]) > cum[i] - cum[anchor] + 1e-3f) {
            anchor = i - 1;
            route.push_back(grid.centre(cells[anchor]));
        }
    }
    if (cells.size() > 1) route.push_back(grid.centre(cells.back()));
    return route;
}

}  // namespace oar
