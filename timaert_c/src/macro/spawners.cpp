#include "macro/spawners.h"
#include "macro/pathfinding.h"
#include "macro/biomes.h"
#include "core/rng.h"
#include "core/torus.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace sm {

// ── TS-faithful tree spawner (game/tree-spawner.ts spawnTrees) ──
// Domain-warped multi-scale FBM forest patches with smoothstep density.
// Linear distribution looks like dandruff; this gives organic groves.
namespace {

inline float ihash01(std::int32_t x, std::int32_t y, std::int32_t sd) {
    std::uint32_t v = std::uint32_t(x * 374761 + y * 668265 + sd * 2246822);
    v = (v ^ (v >> 13)) * 1274126177u;
    v ^= v >> 16;
    return float(v) / 4294967296.0f;
}

inline float smoothNoise(float x, float y, std::int32_t sd) {
    int   ix = int(std::floor(x));
    int   iy = int(std::floor(y));
    float fx = x - float(ix);
    float fy = y - float(iy);
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float n00 = ihash01(ix,     iy,     sd);
    float n10 = ihash01(ix + 1, iy,     sd);
    float n01 = ihash01(ix,     iy + 1, sd);
    float n11 = ihash01(ix + 1, iy + 1, sd);
    float a = n00 + (n10 - n00) * sx;
    float b = n01 + (n11 - n01) * sx;
    return a + (b - a) * sy;
}

inline float fbm(float x, float y, std::int32_t sd, int octaves) {
    float value = 0.0f, amp = 1.0f, maxAmp = 0.0f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        value  += smoothNoise(x * freq, y * freq, sd + i * 100) * amp;
        maxAmp += amp;
        amp    *= 0.5f;
        freq   *= 2.0f;
    }
    return value / maxAmp;
}

} // namespace

std::vector<TreePoint> spawn_trees(const TerrainData& td, std::uint32_t seed,
                                   float /*densityIgnored*/) {
    const int mw = td.width;
    const int mh = td.height;
    std::vector<TreePoint> out;
    out.reserve(std::size_t(mw * mh) / 16);

    // Sea level matches map_generator default (TS defaultParameters.seaLevel = 0.40).
    constexpr float kSeaLevel = 0.40f;
    const std::int32_t sd = std::int32_t(seed);

    for (int y = 0; y < mh; ++y) {
        for (int x = 0; x < mw; ++x) {
            const std::size_t idx = std::size_t(y * mw + x);

            // Hard exclusion: water (mask channel A == 0).
            if (td.rgba[idx * 4 + 3] == 0) continue;

            const float h    = float(td.rgba[idx * 4 + 0]) / 255.0f;
            const std::uint8_t temp  = td.rgba[idx * 4 + 2];
            const std::uint8_t moist = td.rgba[idx * 4 + 1];

            // Shoreline buffer + mountain cap.
            if (h < kSeaLevel + 0.03f || h > 0.80f) continue;

            // Biome exclusion via TS 3×3 climate matrix
            // (row 0 col 0 = Tundra, row 0 col 2 = Snow, row 2 col 0 = Desert).
            const int tRow     = std::min(2, int(temp)  / 86);
            const int moistCol = std::min(2, int(moist) / 86);
            if ((tRow == 0 && moistCol != 1)
             || (tRow == 2 && moistCol == 0)) continue;

            // Organic noise — domain-warped multi-scale FBM.
            const float nx = float(x) / float(mw);
            const float ny = float(y) / float(mh);
            const float warpX = fbm(nx * 8.0f, ny * 8.0f, sd + 100, 3);
            const float warpY = fbm(nx * 8.0f, ny * 8.0f, sd + 200, 3);
            const float wnx = nx + (warpX - 0.5f) * 0.06f;
            const float wny = ny + (warpY - 0.5f) * 0.06f;

            const float large = fbm(wnx * 14.0f, wny * 14.0f, sd + 500, 4);
            const float med   = fbm(wnx * 35.0f, wny * 35.0f, sd + 600, 3);
            const float fine  = fbm(wnx * 70.0f, wny * 70.0f, sd + 700, 2);
            const float noise = large * 0.40f + med * 0.35f + fine * 0.25f;

            constexpr float t0 = 0.35f, t1 = 0.55f;
            const float clamped = std::clamp((noise - t0) / (t1 - t0), 0.0f, 1.0f);
            const float density = clamped * clamped * (3.0f - 2.0f * clamped);
            const float cellRand = ihash01(x, y, sd + 999);
            if (cellRand < density) out.push_back({x, y});
        }
    }
    return out;
}

static void bresenham_torus(std::vector<std::uint8_t>& mask,
                            int w, int h, int x0, int y0, int x1, int y1) {
    // Approach: convert to non-toroidal direction first.
    int dx = x1 - x0; if (dx >  w / 2) dx -= w; else if (dx < -w / 2) dx += w;
    int dy = y1 - y0; if (dy >  h / 2) dy -= h; else if (dy < -h / 2) dy += h;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    dx = std::abs(dx); dy = std::abs(dy);
    int err = dx - dy;
    int cx = x0, cy = y0;
    int steps = dx + dy + 8;
    for (int i = 0; i < steps; ++i) {
        mask[std::size_t(wrapi(cy, h)) * w + wrapi(cx, w)] = 255;
        if (cx == x1 && cy == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 <  dx) { err += dx; cy += sy; }
    }
}

// Natural road network: A* between connected city pairs over a road-aware
// cost grid. Water cells are heavily penalised (50.0×) so paths only cross
// water as a last resort; if the chosen path still includes water cells,
// the connection is pruned so downstream consumers (NPC AI, trade) don't
// see phantom edges. Each successful trace lowers the cost of stamped
// cells, encouraging subsequent edges to branch off existing roads.
std::vector<std::uint8_t> trace_roads(const TerrainData& td, Politik& P) {
    const int W = td.width, H = td.height;
    std::vector<std::uint8_t> mask(std::size_t(W) * H, 0);
    if (P.cities.empty()) return mask;

    // Build a road-specific cost grid (independent of FeatureLayer; trees
    // and mountains are derived from terrain alone since the FeatureLayer
    // is built *after* roads).
    constexpr float kSeaLevel = 0.40f;
    const std::uint8_t sl8 = std::uint8_t(kSeaLevel * 255.0f);
    const float kRoadShare   = 0.30f;   // existing-road cell cost (cheap → reuse)
    const float kLand        = 1.00f;
    const float kMountain    = 5.00f;   // h > 0.78 → mountain peak
    const float kWaterReject = 50.00f;  // water cell — A* avoids unless no choice
    PathCostData cg;
    cg.width = W; cg.height = H;
    cg.costGrid.assign(std::size_t(W) * H, kLand);
    for (int i = 0; i < W * H; ++i) {
        std::uint8_t h = td.rgba[std::size_t(i) * 4 + 0];
        if (h < sl8)            cg.costGrid[std::size_t(i)] = kWaterReject;
        else if (h > 200)       cg.costGrid[std::size_t(i)] = kMountain;
    }

    // Mark city cells as cheap so A* anchors snap cleanly.
    for (const City& c : P.cities)
        cg.costGrid[std::size_t(wrapi(c.y, H)) * W + wrapi(c.x, W)] = kRoadShare;

    // Pruning bookkeeping — collect pairs to drop after we've examined every edge.
    std::vector<std::pair<int,int>> dropPairs;

    for (std::size_t i = 0; i < P.cities.size(); ++i) {
        for (int b : P.cities[i].connections) {
            if (b < 0 || std::size_t(b) <= i) continue;  // unique pairs only
            const City& a = P.cities[i];
            const City& B = P.cities[std::size_t(b)];

            PathResult pr = find_path(cg, a.x, a.y, B.x, B.y, /*maxSteps=*/200000);

            // Reject the edge if A* either failed or had to swim through water.
            bool crossesWater = false;
            if (pr.found) {
                for (const PathPoint& p : pr.path) {
                    if (cg.costGrid[std::size_t(p.y) * W + p.x] >= kWaterReject - 0.01f) {
                        crossesWater = true; break;
                    }
                }
            }
            if (!pr.found || crossesWater) {
                dropPairs.push_back({int(i), b});
                continue;
            }

            // Stamp + lower cost so subsequent edges may branch off (road sharing).
            for (const PathPoint& p : pr.path) {
                std::size_t k = std::size_t(p.y) * W + p.x;
                mask[k] = 255;
                cg.costGrid[k] = kRoadShare;
            }
        }
    }

    // Apply pruning — symmetric removal from both endpoints.
    auto strip = [&](int from, int to) {
        for (int& c : P.cities[std::size_t(from)].connections)
            if (c == to) { c = -1; break; }
        // compact -1 holes to the back so iteration still works.
        int* arr = P.cities[std::size_t(from)].connections;
        constexpr int N = sizeof(P.cities[std::size_t(from)].connections)
                        / sizeof(P.cities[std::size_t(from)].connections[0]);
        int w = 0;
        for (int r = 0; r < N; ++r) if (arr[r] != -1) arr[w++] = arr[r];
        for (; w < N; ++w) arr[w] = -1;
    };
    for (auto [x, y] : dropPairs) { strip(x, y); strip(y, x); }

    return mask;
}

// TS-faithful port of generateDirtRoads (game/dirt-road-spawner.ts).
// Spiral search up to 60 tiles → torus-aware lerp trace; skip cells already
// on a road; skip water cells (isLand); stamp 255 elsewhere.
std::vector<std::uint8_t> trace_dirt_roads(int mapW, int mapH,
    const std::vector<std::uint8_t>& roadMask,
    const std::vector<int>& vx, const std::vector<int>& vy,
    const std::uint8_t* landMaskA /* may be nullptr */) {
    std::vector<std::uint8_t> dirt(std::size_t(mapW) * mapH, 0);
    auto isLand = [&](int x, int y) {
        if (!landMaskA) return true;
        return landMaskA[(std::size_t(y) * mapW + x) * 4 + 3] != 0;
    };
    for (std::size_t i = 0; i < vx.size(); ++i) {
        const int cx = vx[i], cy = vy[i];
        // Skip if village is already on a road.
        if (roadMask[std::size_t(cy) * mapW + cx]) continue;

        // Spiral scan to nearest road cell (radius up to 60, TS parity).
        int fx = -1, fy = -1;
        bool found = false;
        for (int r = 1; r <= 60 && !found; ++r) {
            for (int dy = -r; dy <= r && !found; ++dy) {
                for (int dx = -r; dx <= r && !found; ++dx) {
                    if (std::abs(dx) != r && std::abs(dy) != r) continue;
                    int nx = wrapi(cx + dx, mapW);
                    int ny = wrapi(cy + dy, mapH);
                    if (roadMask[std::size_t(ny) * mapW + nx]) {
                        fx = nx; fy = ny; found = true;
                    }
                }
            }
        }
        if (!found) continue;

        // Torus shortest-path delta.
        int dx = fx - cx;
        int dy = fy - cy;
        if (std::abs(dx) > mapW / 2) dx += dx > 0 ? -mapW : mapW;
        if (std::abs(dy) > mapH / 2) dy += dy > 0 ? -mapH : mapH;
        int steps = std::max(std::abs(dx), std::abs(dy));
        if (steps == 0) continue;

        for (int s = 0; s <= steps; ++s) {
            float t = float(s) / float(steps);
            int x = wrapi(cx + int(std::lround(dx * t)), mapW);
            int y = wrapi(cy + int(std::lround(dy * t)), mapH);
            std::size_t idx = std::size_t(y) * mapW + x;
            if (roadMask[idx]) continue;          // don't overwrite roads
            if (!isLand(x, y)) continue;          // skip water/ice
            dirt[idx] = 255;
        }
    }
    return dirt;
}

FeatureLayer build_feature_layer(const TerrainData& td,
                                 const std::vector<TreePoint>& trees,
                                 float mountainThreshold,
                                 const std::vector<std::uint8_t>& roadMask,
                                 const std::vector<std::uint8_t>* dirtMask) {
    FeatureLayer fl;
    fl.resize(td.width, td.height);
    int total = td.width * td.height;
    std::uint8_t mt = std::uint8_t(mountainThreshold * 255.0f);
    // Sea level matches map_generator default (TS defaultParameters.seaLevel = 0.40).
    constexpr std::uint8_t kSeaLvl8 = std::uint8_t(0.40f * 255.0f);
    auto is_water = [&](int idx) {
        return td.rgba[std::size_t(idx) * 4 + 0] < kSeaLvl8;
    };
    for (int i = 0; i < total; ++i) {
        if (is_water(i)) continue;
        if (td.rgba[std::size_t(i) * 4 + 0] >= mt) fl.data[std::size_t(i)] = FT_Mountain;
    }
    for (auto& t : trees) {
        int idx = t.y * td.width + t.x;
        if (idx < 0 || idx >= total || is_water(idx)) continue;
        fl.set(t.x, t.y, FT_Tree);
    }
    if (dirtMask) {
        for (int i = 0; i < total; ++i)
            if ((*dirtMask)[std::size_t(i)] && !is_water(i)) fl.data[std::size_t(i)] = FT_DirtRoad;
    }
    for (int i = 0; i < total; ++i)
        if (roadMask[std::size_t(i)] && !is_water(i)) fl.data[std::size_t(i)] = FT_Road;
    return fl;
}

} // namespace sm
