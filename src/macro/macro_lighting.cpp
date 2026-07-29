#include "macro/macro_lighting.h"

#include "macro/features.h"
#include "macro/landmark_registry.h"
#include "macro/state.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <queue>

namespace sm {
namespace {

// Unpack the low 24 bits of an ARGB tint (as stored in LandmarkDef.color /
// lightColor) into linear 0..1 RGB. Alpha is ignored — emission is additive.
inline void unpack_rgb(std::uint32_t argb, float& r, float& g, float& b) {
    r = float((argb >> 16) & 0xFFu) / 255.0f;
    g = float((argb >> 8) & 0xFFu) / 255.0f;
    b = float((argb) & 0xFFu) / 255.0f;
}

// TS renderer.ts population → glow curve. Population drives a soft log-scaled
// intensity and a sqrt-scaled radius so a hamlet and a capital differ smoothly
// without any per-type tuning.
inline float pop_intensity(float pop) {
    return std::min(1.0f, 0.18f + std::log10(std::max(1.0f, pop)) * 0.32f);
}
inline float pop_radius(float pop) {
    return 1.5f + std::sqrt(std::max(1.0f, pop)) * 0.35f;
}

// Append one emitter. `tint == 0` means the landmark type opts out of night
// light, so nothing is emitted — the single data-driven gate.
void push_light(std::vector<MacroLight>& out, int w, int h,
                float cellX, float cellY, float pop, std::uint32_t tint) {
    if (tint == 0u || w <= 0 || h <= 0)
        return;
    MacroLight L{};
    L.nx = (cellX + 0.5f) / float(w);
    L.ny = (cellY + 0.5f) / float(h);
    L.radius = pop_radius(pop);
    L.intensity = pop_intensity(pop);
    unpack_rgb(tint, L.r, L.g, L.b);
    out.push_back(L);
}

inline int wrap_index(int v, int n) {
    int r = v % n;
    if (r < 0)
        r += n;
    return r;
}

// ── Increment B: terrain-occluded propagation ──────────────────────────────
// Per-feature optical cost: how much "reach budget" traversing one cell of that
// feature spends. Open land is the 1.0 baseline; roads spend less so light runs
// along them; tree canopy spends several× so glow dies sooner and a solid stand
// goes dark inside. Data-driven: a new feature's light behaviour is one row
// here, never a branch in the bake loop. Indexed by FeatureType, so the row
// order MUST track features.h's byte contract.
//
// Mountains are deliberately absent: they are a *biome* (biomes.h
// Biome::Mountain, classified by elevation >= kMountainBiomeLevel), not a
// feature, so they never appear in this grid. A forested mountain still occludes
// via its FT_Tree feature; occluding a bare rocky massif would mean sampling
// elevation in the bake — a follow-up once the biome pass settles (see the
// bake_light_field doc in the header).
constexpr float kFeatureOpticalCost[] = {
    1.00f,  // FT_None     open baseline
    0.65f,  // FT_Road     clear + reflective — carries light furthest
    2.50f,  // FT_Tree     canopy dims light: edge transmits, interior darkens
    0.85f,  // FT_DirtRoad mostly clear — light runs along it, near open
};
inline float feature_optical_cost(FeatureType t) {
    const std::size_t i = std::size_t(t);
    const std::size_t n = sizeof(kFeatureOpticalCost) / sizeof(kFeatureOpticalCost[0]);
    const float c = (i < n) ? kFeatureOpticalCost[i] : 1.0f;
    return c < 0.05f ? 0.05f : c;  // floor keeps the Dijkstra frontier finite
}

// Accumulate one light's isotropic radial (Euclidean) contribution — the
// increment-A path, used when no feature layer is supplied. Exact falloff, so
// the no-terrain output is bit-for-bit the original radial bake.
void accumulate_radial(const MacroLight& L, int width, int height,
                       std::vector<float>& acc) {
    const float radius = std::max(L.radius, 0.001f);
    const int reach = int(std::ceil(radius));
    const int sx = int(std::floor(L.nx * float(width)));
    const int sy = int(std::floor(L.ny * float(height)));
    const float wr = L.r * L.intensity;
    const float wg = L.g * L.intensity;
    const float wb = L.b * L.intensity;

    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const float dist = std::sqrt(float(dx * dx + dy * dy));
            const float f = 1.0f - dist / radius;
            if (f <= 0.0f)
                continue;
            const float w2 = f * f;
            const int tx = wrap_index(sx + dx, width);
            const int ty = wrap_index(sy + dy, height);
            const std::size_t o = (std::size_t(ty) * std::size_t(width)
                                   + std::size_t(tx)) * 3u;
            acc[o + 0] += wr * w2;
            acc[o + 1] += wg * w2;
            acc[o + 2] += wb * w2;
        }
    }
}

// Priority-queue node for the bounded Dijkstra: optical distance + cell.
struct LightNode {
    float d;
    int x, y;
};
struct LightNodeGreater {
    bool operator()(const LightNode& a, const LightNode& b) const {
        return a.d > b.d;
    }
};

// Accumulate one light's terrain-occluded contribution — the increment-B path.
// Bounded Dijkstra from the emitter over optical distance: the settled distance
// of each cell is its cheapest optical path from the source, so light bends
// around obstacles instead of shining through them. `dist` and `touched` are
// caller-owned scratch reused across lights — `dist` is reset only over the
// cells this light touched (never a full width*height clear per light).
void accumulate_occluded(const MacroLight& L, int width, int height,
                         const FeatureLayer& feat, std::vector<float>& acc,
                         std::vector<float>& dist,
                         std::vector<std::size_t>& touched) {
    const float radius = std::max(L.radius, 0.001f);
    const float wr = L.r * L.intensity;
    const float wg = L.g * L.intensity;
    const float wb = L.b * L.intensity;
    const int sx = wrap_index(int(std::floor(L.nx * float(width))), width);
    const int sy = wrap_index(int(std::floor(L.ny * float(height))), height);

    auto index = [width](int x, int y) {
        return std::size_t(y) * std::size_t(width) + std::size_t(x);
    };

    // 8-neighbourhood with Euclidean step lengths so open-terrain optical
    // distance tracks true distance (octile), not Manhattan.
    static const int kNX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kNY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float kNL[8] = {1.0f,        1.0f,        1.0f,        1.0f,
                                 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f};

    std::priority_queue<LightNode, std::vector<LightNode>, LightNodeGreater> pq;
    const std::size_t s = index(sx, sy);
    dist[s] = 0.0f;
    touched.push_back(s);
    pq.push({0.0f, sx, sy});

    while (!pq.empty()) {
        const LightNode cur = pq.top();
        pq.pop();
        const std::size_t ci = index(cur.x, cur.y);
        if (cur.d > dist[ci])
            continue;  // stale entry superseded by a cheaper relaxation

        // Deposit this cell's glow at its settled (minimum) optical distance.
        const float f = 1.0f - cur.d / radius;
        if (f > 0.0f) {
            const float w2 = f * f;
            const std::size_t o = ci * 3u;
            acc[o + 0] += wr * w2;
            acc[o + 1] += wg * w2;
            acc[o + 2] += wb * w2;
        }

        for (int k = 0; k < 8; ++k) {
            const int nx = wrap_index(cur.x + kNX[k], width);
            const int ny = wrap_index(cur.y + kNY[k], height);
            const float nd =
                cur.d + kNL[k] * feature_optical_cost(feat.at(nx, ny));
            if (nd >= radius)
                continue;  // beyond reach (f <= 0) — prune the frontier
            const std::size_t ni = index(nx, ny);
            if (nd < dist[ni]) {
                if (dist[ni] == std::numeric_limits<float>::infinity())
                    touched.push_back(ni);
                dist[ni] = nd;
                pq.push({nd, nx, ny});
            }
        }
    }
}

} // namespace

std::vector<MacroLight> collect_macro_lights(const GameState& gs) {
    std::vector<MacroLight> out;
    const int w = gs.mapW;
    const int h = gs.mapH;

    const LandmarkDef& cityDef = landmark_def(LandmarkType::City);
    const LandmarkDef& villDef = landmark_def(LandmarkType::Village);
    const LandmarkDef& spireDef = landmark_def(LandmarkType::Spire);

    out.reserve(gs.settlements.size() + gs.villages.size() + gs.spires.size());

    // Inhabited landmarks: population-scaled warm hearth glow.
    for (const Settlement& s : gs.settlements)
        push_light(out, w, h, float(s.x), float(s.y), float(s.population),
                   cityDef.lightColor);
    for (const Village& v : gs.villages)
        push_light(out, w, h, float(v.x), float(v.y), float(v.population),
                   villDef.lightColor);

    // Fixed POIs: active spires emit their arcane tint at a synthetic strength.
    for (const Spire& sp : gs.spires)
        if (!sp.depleted)
            push_light(out, w, h, float(sp.x), float(sp.y), spireDef.lightPop,
                       spireDef.lightColor);

    return out;
}

void bake_light_field(int width, int height,
                      const std::vector<MacroLight>& lights,
                      std::vector<std::uint8_t>& out,
                      const FeatureLayer* features) {
    if (width <= 0 || height <= 0) {
        out.clear();
        return;
    }
    const std::size_t cells = std::size_t(width) * std::size_t(height);
    std::vector<float> acc(cells * 3, 0.0f);

    // With a feature layer, propagate each light through the terrain (increment
    // B). Without one — or with an empty layer — fall back to the exact radial
    // falloff (increment A), which is bit-for-bit identical to the original.
    if (features && features->width > 0 && features->height > 0) {
        std::vector<float> dist(cells, std::numeric_limits<float>::infinity());
        std::vector<std::size_t> touched;
        for (const MacroLight& L : lights) {
            accumulate_occluded(L, width, height, *features, acc, dist, touched);
            for (std::size_t t : touched)
                dist[t] = std::numeric_limits<float>::infinity();
            touched.clear();
        }
    } else {
        for (const MacroLight& L : lights)
            accumulate_radial(L, width, height, acc);
    }

    // Encode summed glow into RGBA8: clamp per channel to the TS ceiling, store
    // value/ceiling; the shader decodes with * kMacroGlowCeil.
    out.resize(cells * 4u);
    const float inv = 255.0f / kMacroGlowCeil;
    for (std::size_t i = 0; i < cells; ++i) {
        for (int c = 0; c < 3; ++c) {
            // Master gain scales the summed glow before the ceiling clamp, so a
            // single city core (acc ~1) lands well under saturation while dense
            // stacks still clamp gracefully at the ceiling.
            float g = acc[i * 3u + std::size_t(c)] * kMacroGlowGain;
            if (g > kMacroGlowCeil)
                g = kMacroGlowCeil;
            out[i * 4u + std::size_t(c)] = std::uint8_t(g * inv + 0.5f);
        }
        out[i * 4u + 3u] = 255u;
    }
}

} // namespace sm
