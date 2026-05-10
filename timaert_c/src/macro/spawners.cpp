#include "macro/spawners.h"
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

static std::size_t road_index(int w, int x, int y) {
    return std::size_t(y) * std::size_t(w) + std::size_t(x);
}

static int road_torus_delta(int from, int to, int size) {
    if (size <= 0) return 0;
    int d = to - from;
    if (d > size / 2) d -= size;
    else if (d < -size / 2) d += size;
    return d;
}

constexpr int kRoadSnapRadius = 3;
constexpr float kRoadDataRoadWidthNorm = 0.01f;
constexpr float kRoadDataCityRadiusXNorm = 0.02f;
constexpr float kRoadDataCityRadiusYNorm = 0.04f;
constexpr std::uint8_t kRoadDataRoadValue = 153; // TS roadFragmentShader intensity 0.6
constexpr std::uint8_t kRoadDataCityValue = 255; // TS roadFragmentShader intensity 1.0

static int road_round_js(float v) {
    return int(std::floor(v + 0.5f));
}

static int road_sign(int v) {
    return (v > 0) ? 1 : (v < 0 ? -1 : 0);
}

static void roaddata_max(std::vector<std::uint8_t>& roadData,
                         int w, int x, int y, std::uint8_t value) {
    std::uint8_t& cell = roadData[road_index(w, x, y)];
    if (value > cell) cell = value;
}

static void stamp_roaddata_disc(std::vector<std::uint8_t>& roadData,
                                int w, int h, int cx, int cy,
                                int radius, std::uint8_t value) {
    const int r2 = radius * radius;
    for (int oy = -radius; oy <= radius; ++oy) {
        for (int ox = -radius; ox <= radius; ++ox) {
            if (ox * ox + oy * oy > r2) continue;
            roaddata_max(roadData, w, wrapi(cx + ox, w), wrapi(cy + oy, h), value);
        }
    }
}

static void stamp_roaddata_ellipse(std::vector<std::uint8_t>& roadData,
                                   int w, int h, int cx, int cy,
                                   int rx, int ry, std::uint8_t value) {
    const std::int64_t rx2 = std::int64_t(rx) * rx;
    const std::int64_t ry2 = std::int64_t(ry) * ry;
    const std::int64_t limit = rx2 * ry2;
    for (int oy = -ry; oy <= ry; ++oy) {
        for (int ox = -rx; ox <= rx; ++ox) {
            const std::int64_t dx2 = std::int64_t(ox) * ox;
            const std::int64_t dy2 = std::int64_t(oy) * oy;
            if (dx2 * ry2 + dy2 * rx2 > limit) continue;
            roaddata_max(roadData, w, wrapi(cx + ox, w), wrapi(cy + oy, h), value);
        }
    }
}

static void stamp_roaddata_edge(std::vector<std::uint8_t>& roadData,
                                int w, int h,
                                int ax, int ay, int bx, int by,
                                int roadRadius) {
    const int dx = road_torus_delta(ax, bx, w);
    const int dy = road_torus_delta(ay, by, h);
    const int steps = std::max(std::abs(dx), std::abs(dy));
    if (steps == 0) {
        stamp_roaddata_disc(roadData, w, h, wrapi(ax, w), wrapi(ay, h),
                            roadRadius, kRoadDataRoadValue);
        return;
    }

    const float invSteps = 1.0f / float(steps);
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) * invSteps;
        const int x = wrapi(ax + road_round_js(float(dx) * t), w);
        const int y = wrapi(ay + road_round_js(float(dy) * t), h);
        stamp_roaddata_disc(roadData, w, h, x, y, roadRadius,
                            kRoadDataRoadValue);
    }
}

static std::vector<std::uint8_t> build_road_data(const Politik& P,
                                                 int w, int h) {
    if (w <= 0 || h <= 0) return {};
    std::vector<std::uint8_t> roadData(std::size_t(w) * h, 0);
    if (P.cities.empty()) return roadData;

    const int roadRadius = std::max(1,
        int(std::ceil(kRoadDataRoadWidthNorm * float(std::max(w, h)))));
    const int cityRadiusX = std::max(1,
        int(std::ceil(kRoadDataCityRadiusXNorm * float(w))));
    const int cityRadiusY = std::max(1,
        int(std::ceil(kRoadDataCityRadiusYNorm * float(h))));

    for (std::size_t i = 0; i < P.cities.size(); ++i) {
        for (int b : P.cities[i].connections) {
            if (b < 0 || std::size_t(b) <= i
                || std::size_t(b) >= P.cities.size()) {
                continue;
            }
            const City& a = P.cities[i];
            const City& B = P.cities[std::size_t(b)];
            stamp_roaddata_edge(roadData, w, h, a.x, a.y, B.x, B.y,
                                roadRadius);
        }
    }

    // TS draws cities after road quads in generateLayer2(), so city masks win.
    for (const City& c : P.cities) {
        stamp_roaddata_ellipse(roadData, w, h, wrapi(c.x, w), wrapi(c.y, h),
                               cityRadiusX, cityRadiusY, kRoadDataCityValue);
    }
    return roadData;
}

static void trace_ts_corridor_road(std::vector<std::uint8_t>& mask,
                                   const std::vector<std::uint8_t>& roadData,
                                   int w, int h,
                                   int ax, int ay, int bx, int by) {
    const int dx = road_torus_delta(ax, bx, w);
    const int dy = road_torus_delta(ay, by, h);
    const int steps = std::max(std::abs(dx), std::abs(dy));
    if (steps == 0) return;

    int curX = wrapi(ax, w);
    int curY = wrapi(ay, h);
    mask[road_index(w, curX, curY)] = 255;

    const int endX = wrapi(bx, w);
    const int endY = wrapi(by, h);

    const float invSteps = 1.0f / float(steps);
    for (int i = 1; i <= steps; ++i) {
        const float t = float(i) * invSteps;
        const int baseX = wrapi(ax + road_round_js(float(dx) * t), w);
        const int baseY = wrapi(ay + road_round_js(float(dy) * t), h);
        const int endDist = std::min(i, steps - i);
        const int radius = std::max(1, std::min(endDist, kRoadSnapRadius));

        int bestX = curX;
        int bestY = curY;
        int bestValue = -1;

        // Mirror road-network.ts: pick the 8-neighbour with highest roadData
        // value while constrained to the Bresenham guide leash.
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                const int nx = wrapi(curX + ox, w);
                const int ny = wrapi(curY + oy, h);
                const int ddx = road_torus_delta(baseX, nx, w);
                const int ddy = road_torus_delta(baseY, ny, h);
                if (std::abs(ddx) > radius || std::abs(ddy) > radius) continue;

                const int value = roadData[road_index(w, nx, ny)];
                if (value > bestValue) {
                    bestValue = value;
                    bestX = nx;
                    bestY = ny;
                }
            }
        }

        if (bestValue < 0) {
            const int ddx = road_torus_delta(curX, baseX, w);
            const int ddy = road_torus_delta(curY, baseY, h);
            bestX = wrapi(curX + road_sign(ddx), w);
            bestY = wrapi(curY + road_sign(ddy), h);
        }

        mask[road_index(w, bestX, bestY)] = 255;
        curX = bestX;
        curY = bestY;
    }

    const int gx = road_torus_delta(curX, endX, w);
    const int gy = road_torus_delta(curY, endY, h);
    const int gapSteps = std::max(std::abs(gx), std::abs(gy));
    const float invGapSteps = gapSteps > 0 ? 1.0f / float(gapSteps) : 0.0f;
    for (int j = 1; j <= gapSteps; ++j) {
        const float t = float(j) * invGapSteps;
        const int fx = wrapi(curX + road_round_js(float(gx) * t), w);
        const int fy = wrapi(curY + road_round_js(float(gy) * t), h);
        mask[road_index(w, fx, fy)] = 255;
    }
}

// Boot-time road network. Road generation is one-time feature stamping, not
// runtime pathfinding. Do not prune Politik connections here: TS keeps the
// generated connectivity graph intact and movement weights handle traversal.
std::vector<std::uint8_t> trace_roads(const TerrainData& td, Politik& P,
                                      RoadTraceStats* stats) {
    const int W = td.width, H = td.height;
    RoadTraceStats localStats;
    localStats.cityCount = int(P.cities.size());
    if (W <= 0 || H <= 0) {
        if (stats) *stats = localStats;
        return {};
    }
    std::vector<std::uint8_t> mask(std::size_t(W) * H, 0);
    if (P.cities.empty()) {
        if (stats) *stats = localStats;
        return mask;
    }
    const std::vector<std::uint8_t> roadData = build_road_data(P, W, H);

    for (std::size_t i = 0; i < P.cities.size(); ++i) {
        for (int b : P.cities[i].connections) {
            if (b < 0 || std::size_t(b) <= i
                || std::size_t(b) >= P.cities.size()) {
                continue;
            }
            const City& a = P.cities[i];
            const City& B = P.cities[std::size_t(b)];
            ++localStats.attemptedEdges;

            trace_ts_corridor_road(mask, roadData, W, H, a.x, a.y, B.x, B.y);
            ++localStats.keptEdges;
        }
    }

    if (stats) *stats = localStats;
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
