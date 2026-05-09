// A* pathfinding over a torus cost grid. Verbatim port of pathfinding.ts.
//
// Edge cost = costGrid[dest] * stepLen (1.0 cardinal, sqrt(2) diagonal).
// Octile heuristic with torus wrap (consistent for 8-directional A*).
// Indexed binary min-heap collapses duplicates, matching TS MinHeap.

#include "macro/pathfinding.h"
#include "macro/movement_cost.h"
#include "macro/biomes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sm {

namespace {

constexpr int   DX[8]        = { 0,  1,  1,  1,  0, -1, -1, -1};
constexpr int   DY[8]        = {-1, -1,  0,  1,  1,  1,  0, -1};
constexpr float STEP_COST[8] = {1.0f, 1.4142136f, 1.0f, 1.4142136f,
                                 1.0f, 1.4142136f, 1.0f, 1.4142136f};

inline int pf_wrap(int v, int m) {
    int r = v % m;
    return r < 0 ? r + m : r;
}

inline float octile_torus(int x1, int y1, int x2, int y2, int w, int h) {
    int dx = std::abs(x2 - x1);
    int dy = std::abs(y2 - y1);
    if (dx > w / 2) dx = w - dx;
    if (dy > h / 2) dy = h - dy;
    return dx > dy
        ? float(dx) + 0.4142136f * float(dy)
        : float(dy) + 0.4142136f * float(dx);
}

struct NodeRec { int x, y; float g, f; };

struct IndexedHeap {
    std::vector<NodeRec>      items;
    std::vector<std::int32_t> indexOf;
    int                       width = 0;

    void init(int w, int h) {
        width = w;
        indexOf.assign(std::size_t(w) * h, -1);
        items.clear();
    }
    inline std::size_t key(int x, int y) const {
        return std::size_t(y) * width + x;
    }
    inline bool empty() const { return items.empty(); }

    void push(const NodeRec& n) {
        std::size_t k = key(n.x, n.y);
        std::int32_t at = indexOf[k];
        if (at >= 0) {
            if (n.f < items[std::size_t(at)].f) {
                items[std::size_t(at)] = n;
                bubble_up(at);
            }
            return;
        }
        items.push_back(n);
        std::int32_t idx = std::int32_t(items.size() - 1);
        indexOf[k] = idx;
        bubble_up(idx);
    }
    NodeRec pop() {
        NodeRec top = items.front();
        indexOf[key(top.x, top.y)] = -1;
        NodeRec last = items.back();
        items.pop_back();
        if (!items.empty()) {
            items.front() = last;
            indexOf[key(last.x, last.y)] = 0;
            sink_down(0);
        }
        return top;
    }
    void bubble_up(std::int32_t i) {
        while (i > 0) {
            std::int32_t p = (i - 1) >> 1;
            if (items[std::size_t(i)].f >= items[std::size_t(p)].f) return;
            swap_at(i, p);
            i = p;
        }
    }
    void sink_down(std::int32_t i) {
        std::int32_t n = std::int32_t(items.size());
        for (;;) {
            std::int32_t l = 2 * i + 1, r = 2 * i + 2, s = i;
            if (l < n && items[std::size_t(l)].f < items[std::size_t(s)].f) s = l;
            if (r < n && items[std::size_t(r)].f < items[std::size_t(s)].f) s = r;
            if (s == i) return;
            swap_at(i, s);
            i = s;
        }
    }
    void swap_at(std::int32_t a, std::int32_t b) {
        std::swap(items[std::size_t(a)], items[std::size_t(b)]);
        indexOf[key(items[std::size_t(a)].x, items[std::size_t(a)].y)] = a;
        indexOf[key(items[std::size_t(b)].x, items[std::size_t(b)].y)] = b;
    }
};

} // namespace

PathCostData build_cost_grid(const TerrainData& td,
                             const FeatureLayer* features,
                             float seaLevel) {
    PathCostData out;
    out.width  = td.width;
    out.height = td.height;
    int n = td.width * td.height;
    out.costGrid.resize(std::size_t(n));
    std::uint8_t sl8 = std::uint8_t(seaLevel * 255.0f);
    for (int i = 0; i < n; ++i) {
        std::uint8_t h = td.rgba[std::size_t(i) * 4 + 0];
        Biome b;
        if (h < sl8) {
            b = Biome::Water;
        } else {
            float t01 = td.rgba[std::size_t(i) * 4 + 2] / 255.0f;
            float m01 = td.rgba[std::size_t(i) * 4 + 1] / 255.0f;
            b = biome_from_climate(t01, m01);
        }
        FeatureType f = features
            ? FeatureType(features->data[std::size_t(i)])
            : FT_None;
        out.costGrid[std::size_t(i)] = cell_sp_weight(b, f);
    }
    return out;
}

PathResult find_path(const PathCostData& data,
                     int startX, int startY,
                     int endX,   int endY,
                     int maxSteps) {
    PathResult out;
    const int W = data.width, H = data.height;
    if (W <= 0 || H <= 0) return out;

    int sx = pf_wrap(startX, W), sy = pf_wrap(startY, H);
    int ex = pf_wrap(endX,   W), ey = pf_wrap(endY,   H);

    if (sx == ex && sy == ey) {
        out.path.push_back({sx, sy});
        out.found = true;
        return out;
    }

    const std::size_t cells = std::size_t(W) * H;
    // `maxSteps<=0` (default) → give A* enough budget to visit every cell
    // once. With the closed-set check this guarantees termination on any
    // grid size while letting paths be found anywhere on the torus.
    if (maxSteps <= 0) maxSteps = int(cells);
    std::vector<std::uint8_t> closed(cells, 0);
    std::vector<float>        gScores(cells, std::numeric_limits<float>::infinity());
    std::vector<std::int32_t> parentX(cells, -1);
    std::vector<std::int32_t> parentY(cells, -1);

    IndexedHeap open;
    open.init(W, H);

    NodeRec start{sx, sy, 0.0f, octile_torus(sx, sy, ex, ey, W, H)};
    gScores[std::size_t(sy) * W + sx] = 0.0f;
    open.push(start);

    int steps = 0;
    while (!open.empty() && steps < maxSteps) {
        ++steps;
        NodeRec cur = open.pop();
        std::size_t cidx = std::size_t(cur.y) * W + cur.x;

        if (cur.x == ex && cur.y == ey) {
            int cx = cur.x, cy = cur.y;
            for (;;) {
                out.path.push_back({cx, cy});
                std::size_t k = std::size_t(cy) * W + cx;
                std::int32_t px = parentX[k], py = parentY[k];
                if (px < 0) break;
                cx = px; cy = py;
            }
            std::reverse(out.path.begin(), out.path.end());
            out.found = true;
            return out;
        }

        if (closed[cidx]) continue;
        closed[cidx] = 1;

        for (int d = 0; d < 8; ++d) {
            int nx = pf_wrap(cur.x + DX[d], W);
            int ny = pf_wrap(cur.y + DY[d], H);
            std::size_t nidx = std::size_t(ny) * W + nx;
            if (closed[nidx]) continue;

            float cost  = data.costGrid[nidx] * STEP_COST[d];
            float tentG = cur.g + cost;
            if (tentG >= gScores[nidx]) continue;

            gScores[nidx] = tentG;
            parentX[nidx] = std::int32_t(cur.x);
            parentY[nidx] = std::int32_t(cur.y);

            float hh = octile_torus(nx, ny, ex, ey, W, H);
            open.push({nx, ny, tentG, tentG + hh});
        }
    }
    return out;
}

} // namespace sm
