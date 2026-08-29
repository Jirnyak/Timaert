// The ONE terrain-optical propagation of the macro map. Anything that travels
// by line of sight — a town's night glow (macro_lighting.cpp) and the player's
// SIGHT (macro/knowledge.h) — spreads through the same bounded Dijkstra over
// the same per-cell optical costs, so "how far a light carries" and "how far
// an eye sees" are one physics with one tuning, never two.
//
// Optical distance: each traversed cell SPENDS budget. Open land is the 1.0
// baseline; roads spend less (a clear corridor carries light and sight far);
// tree canopy adds per unit of density (a massif smothers both); every UPHILL
// step pays per unit of rise (a ridge walls a valley off) while downhill is
// free (a hilltop overlooks the plain below — and its glow spills down into
// it). Torus-wrapped, 8-neighbour with Euclidean step lengths (octile), and a
// settled distance is the CHEAPEST optical path from the source — light and
// sight bend around obstacles instead of shining through them.
#pragma once
#include "core/torus.h"
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

#include "macro/features.h"

namespace sm {

// Per-feature optical cost: the budget one cell of that feature spends — THE
// feature registry's column (macro/features.h kFeatureDefs; this held a
// private float array until 2026-08-29). A new feature's optical behaviour is
// its row's column, never a branch in the sweep.
//
// Mountains and forests are deliberately absent from the registry: mountains
// are a *biome* (biomes.h Biome::Mountain, occluded via the elevation term
// below) and forests are the tree-count field (macro/tree_layer.h) — canopy
// occlusion is the CONTINUOUS kCanopyOpticalCost term added per unit of tree
// density.

// Full forest (density 1.0) spends 1.0 + 1.5 = 2.5 per cell — the exact
// budget the old binary FT_Tree feature row charged.
inline constexpr float kCanopyOpticalCost = 1.50f;

// Optical cost of climbing one full unit of normalized height (per Dijkstra
// step, scaled by the step's rise). Tuned so a typical massif rise (~0.2
// above the surrounding plain) exceeds any settlement glow radius — a range
// wall reads opaque — while gentle hills only shorten the reach. Downhill is
// free. One knob for glow and sight alike.
inline constexpr float kClimbOpticalCost = 60.0f;

inline float feature_optical_cost(FeatureType t) {
    const float c = feature_def(t).opticalCost;
    return c < 0.05f ? 0.05f : c;  // floor keeps the Dijkstra frontier finite
}

// The optical world — ONE payload naming every terrain input the sweep reads.
// All fields are optional views over caller-owned storage: a null `features`
// means uniform open ground; `heights` (normalized 0..1 macro heights) and
// `treeDensity` (tree count / 16384) participate only when they cover the
// grid exactly (fail-closed to absent otherwise).
struct OpticalWorld {
    const FeatureLayer* features = nullptr;
    const std::vector<float>* heights = nullptr;
    const std::vector<float>* treeDensity = nullptr;
};

// Caller-owned scratch reused across sweeps: `dist` is reset only over the
// cells the last sweep touched — never a full width×height clear per call.
struct OpticalScratch {
    std::vector<float> dist;
    std::vector<std::size_t> touched;
};

namespace optics_detail {
struct Node {
    float d;
    int x, y;
};
struct NodeGreater {
    bool operator()(const Node& a, const Node& b) const { return a.d > b.d; }
};
// (was a private copy of the torus wrap — core/torus.h owns it)
inline int wrap_index(int v, int n) { return wrapi(v, n); }
}  // namespace optics_detail

// Bounded Dijkstra from (sx, sy): calls visit(cellIdx, opticalDist) exactly
// once per reached cell, at its settled (minimum) optical distance — the
// source at 0, every other cell strictly under `budget`. Scratch is restored
// on exit, so back-to-back sweeps over one grid pay only what they touch.
template <class Visit>
void optical_sweep(int width, int height, int sx, int sy, float budget,
                   const OpticalWorld& world, OpticalScratch& scratch,
                   Visit&& visit) {
    if (width <= 0 || height <= 0 || budget <= 0.0f) return;
    const std::size_t cells = std::size_t(width) * std::size_t(height);

    const FeatureLayer* feat =
        (world.features && world.features->width == width
         && world.features->height == height) ? world.features : nullptr;
    const std::vector<float>* hs =
        (world.heights && world.heights->size() == cells) ? world.heights : nullptr;
    const std::vector<float>* td =
        (world.treeDensity && world.treeDensity->size() == cells) ? world.treeDensity
                                                                  : nullptr;

    if (scratch.dist.size() != cells) {
        scratch.dist.assign(cells, std::numeric_limits<float>::infinity());
        scratch.touched.clear();
    }

    auto index = [width](int x, int y) {
        return std::size_t(y) * std::size_t(width) + std::size_t(x);
    };

    // 8-neighbourhood with Euclidean step lengths so open-terrain optical
    // distance tracks true distance (octile), not Manhattan.
    static constexpr int kNX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static constexpr int kNY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static constexpr float kNL[8] = {1.0f,        1.0f,        1.0f,        1.0f,
                                     1.41421356f, 1.41421356f, 1.41421356f,
                                     1.41421356f};

    using optics_detail::Node;
    using optics_detail::NodeGreater;
    using optics_detail::wrap_index;

    std::priority_queue<Node, std::vector<Node>, NodeGreater> pq;
    const int wsx = wrap_index(sx, width);
    const int wsy = wrap_index(sy, height);
    const std::size_t s = index(wsx, wsy);
    scratch.dist[s] = 0.0f;
    scratch.touched.push_back(s);
    pq.push({0.0f, wsx, wsy});

    while (!pq.empty()) {
        const Node cur = pq.top();
        pq.pop();
        const std::size_t ci = index(cur.x, cur.y);
        if (cur.d > scratch.dist[ci])
            continue;  // stale entry superseded by a cheaper relaxation

        visit(ci, cur.d);

        for (int k = 0; k < 8; ++k) {
            const int nx = wrap_index(cur.x + kNX[k], width);
            const int ny = wrap_index(cur.y + kNY[k], height);
            const std::size_t ni = index(nx, ny);
            float base = feat ? feature_optical_cost(feat->at(nx, ny)) : 1.0f;
            if (td) base += kCanopyOpticalCost * (*td)[ni];
            float step = kNL[k] * base;
            if (hs) {
                const float rise = (*hs)[ni] - (*hs)[ci];
                if (rise > 0.0f) step += kClimbOpticalCost * rise;
            }
            const float nd = cur.d + step;
            if (nd >= budget)
                continue;  // beyond reach — prune the frontier
            if (nd < scratch.dist[ni]) {
                if (scratch.dist[ni] == std::numeric_limits<float>::infinity())
                    scratch.touched.push_back(ni);
                scratch.dist[ni] = nd;
                pq.push({nd, nx, ny});
            }
        }
    }

    for (std::size_t t : scratch.touched)
        scratch.dist[t] = std::numeric_limits<float>::infinity();
    scratch.touched.clear();
}

} // namespace sm
