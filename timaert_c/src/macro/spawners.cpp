#include "macro/spawners.h"
#include "macro/biomes.h"
#include "core/rng.h"
#include "core/torus.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

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

template <class Fn>
static bool walk_bresenham_torus(int w, int h, int x0, int y0, int x1, int y1,
                                 Fn fn) {
    // Approach: convert to non-toroidal direction first.
    int dx = x1 - x0; if (dx >  w / 2) dx -= w; else if (dx < -w / 2) dx += w;
    int dy = y1 - y0; if (dy >  h / 2) dy -= h; else if (dy < -h / 2) dy += h;
    const int tx = x0 + dx;
    const int ty = y0 + dy;
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    dx = std::abs(dx); dy = std::abs(dy);
    int err = dx - dy;
    int cx = x0, cy = y0;
    int steps = dx + dy + 8;
    for (int i = 0; i < steps; ++i) {
        if (!fn(wrapi(cx, w), wrapi(cy, h))) return false;
        if (cx == tx && cy == ty) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 <  dx) { err += dx; cy += sy; }
    }
    return true;
}

static void bresenham_torus(std::vector<std::uint8_t>& mask,
                            int w, int h, int x0, int y0, int x1, int y1) {
    (void)walk_bresenham_torus(w, h, x0, y0, x1, y1,
        [&](int x, int y) {
            mask[std::size_t(y) * w + x] = 255;
            return true;
        });
}

struct RoadLineCheck {
    int cells = 0;
    int water = 0;
    int mountain = 0;
};

struct RoadNode {
    int x = 0;
    int y = 0;
    float g = 0.0f;
    float f = 0.0f;
};

struct RoadNodeGreater {
    bool operator()(const RoadNode& a, const RoadNode& b) const {
        return a.f > b.f;
    }
};

struct RoadRouterScratch {
    int w = 0;
    int h = 0;
    std::uint16_t mark = 1;
    std::vector<std::uint16_t> seen;
    std::vector<float> gScore;
    std::vector<int> parent;
    std::vector<RoadNode> heap;

    void init(int nw, int nh, int heapReserve) {
        if (w != nw || h != nh) {
            w = nw;
            h = nh;
            const std::size_t cells = std::size_t(w) * std::size_t(h);
            seen.assign(cells, 0);
            gScore.assign(cells, 0.0f);
            parent.assign(cells, -1);
            mark = 1;
        }
        if (int(heap.capacity()) < heapReserve) {
            heap.reserve(std::size_t(heapReserve));
        }
    }

    void next_mark() {
        ++mark;
        if (mark == 0) {
            std::fill(seen.begin(), seen.end(), std::uint16_t(0));
            mark = 1;
        }
        heap.clear();
    }
};

struct RoadRouteResult {
    bool found = false;
    bool edgeCapHit = false;
    bool wholeCapHit = false;
    int expansions = 0;
};

constexpr int kRoadPerEdgeExpansionCap = 4096;
constexpr int kRoadWholePassExpansionCap = 500000;
constexpr int kRoadHeapReserve = kRoadPerEdgeExpansionCap * 4;
constexpr int kRoadMaxFallbackCells = 384;
constexpr int kRoadMaxFallbackMountainPermille = 350;
constexpr std::uint8_t kRoadMountainHeight8 = 200;

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

static bool road_is_land(const TerrainData& td, int w, int x, int y,
                         std::uint8_t seaLevel8) {
    if (td.rgba.empty()) return true;
    const std::size_t i = road_index(w, x, y) * 4;
    return td.rgba[i + 3] != 0 && td.rgba[i + 0] >= seaLevel8;
}

static bool road_is_mountain(const TerrainData& td, int w, int x, int y) {
    if (td.rgba.empty()) return false;
    return td.rgba[road_index(w, x, y) * 4 + 0] >= kRoadMountainHeight8;
}

static RoadLineCheck measure_land_bresenham(const TerrainData& td,
                                            int w, int h,
                                            int x0, int y0, int x1, int y1,
                                            std::uint8_t seaLevel8) {
    RoadLineCheck out;
    (void)walk_bresenham_torus(w, h, x0, y0, x1, y1,
        [&](int x, int y) {
            ++out.cells;
            if (!road_is_land(td, w, x, y, seaLevel8)) ++out.water;
            if (road_is_mountain(td, w, x, y)) ++out.mountain;
            return true;
        });
    return out;
}

static void stamp_land_bresenham_fallback(std::vector<std::uint8_t>& mask,
                                          int w, int h,
                                          int x0, int y0, int x1, int y1) {
    bresenham_torus(mask, w, h, x0, y0, x1, y1);
}

static float road_segment_distance_sq(float px, float py, float ex, float ey) {
    const float lenSq = ex * ex + ey * ey;
    if (lenSq <= 0.0001f) return px * px + py * py;
    const float t = std::clamp((px * ex + py * ey) / lenSq, 0.0f, 1.0f);
    const float qx = ex * t;
    const float qy = ey * t;
    const float dx = px - qx;
    const float dy = py - qy;
    return dx * dx + dy * dy;
}

static float road_octile_heuristic(int w, int h, int x, int y, int tx, int ty) {
    const int dx = std::abs(road_torus_delta(x, tx, w));
    const int dy = std::abs(road_torus_delta(y, ty, h));
    const int mn = std::min(dx, dy);
    const int mx = std::max(dx, dy);
    return float(mx) + 0.41421356f * float(mn);
}

static bool road_heap_push(RoadRouterScratch& scratch, const RoadNode& node) {
    if (scratch.heap.size() >= scratch.heap.capacity()) return false;
    scratch.heap.push_back(node);
    std::push_heap(scratch.heap.begin(), scratch.heap.end(), RoadNodeGreater{});
    return true;
}

static RoadNode road_heap_pop(RoadRouterScratch& scratch) {
    std::pop_heap(scratch.heap.begin(), scratch.heap.end(), RoadNodeGreater{});
    RoadNode node = scratch.heap.back();
    scratch.heap.pop_back();
    return node;
}

static RoadRouteResult route_bounded_road(const TerrainData& td,
                                          std::vector<std::uint8_t>& mask,
                                          RoadRouterScratch& scratch,
                                          int sx, int sy, int tx, int ty,
                                          std::uint8_t seaLevel8,
                                          int& wholeExpansionBudget) {
    RoadRouteResult result;
    const int W = td.width;
    const int H = td.height;
    if (wholeExpansionBudget <= 0) {
        result.wholeCapHit = true;
        return result;
    }
    if (!road_is_land(td, W, sx, sy, seaLevel8)
        || !road_is_land(td, W, tx, ty, seaLevel8)) {
        return result;
    }

    const int ex = road_torus_delta(sx, tx, W);
    const int ey = road_torus_delta(sy, ty, H);
    const float edgeLen = std::sqrt(float(ex * ex + ey * ey));
    const float corridor = std::clamp(edgeLen * 0.20f, 8.0f, 48.0f);
    const float corridorSq = corridor * corridor;
    const int startIdx = int(road_index(W, sx, sy));
    const int targetIdx = int(road_index(W, tx, ty));

    scratch.next_mark();
    scratch.seen[std::size_t(startIdx)] = scratch.mark;
    scratch.gScore[std::size_t(startIdx)] = 0.0f;
    scratch.parent[std::size_t(startIdx)] = -1;
    if (!road_heap_push(scratch, {sx, sy, 0.0f,
                                  road_octile_heuristic(W, H, sx, sy, tx, ty)})) {
        result.edgeCapHit = true;
        return result;
    }

    bool heapCapHit = false;
    static constexpr int kDirs[8][2] = {
        { 1, 0}, {-1, 0}, {0,  1}, {0, -1},
        { 1, 1}, { 1,-1}, {-1, 1}, {-1,-1}
    };

    while (!scratch.heap.empty()) {
        if (result.expansions >= kRoadPerEdgeExpansionCap) {
            result.edgeCapHit = true;
            break;
        }
        if (wholeExpansionBudget <= 0) {
            result.wholeCapHit = true;
            break;
        }

        const RoadNode cur = road_heap_pop(scratch);
        const int curIdx = int(road_index(W, cur.x, cur.y));
        if (cur.g != scratch.gScore[std::size_t(curIdx)]) continue;

        if (curIdx == targetIdx) {
            result.found = true;
            break;
        }

        ++result.expansions;
        --wholeExpansionBudget;

        for (const auto& d : kDirs) {
            const int nx = wrapi(cur.x + d[0], W);
            const int ny = wrapi(cur.y + d[1], H);
            if (!road_is_land(td, W, nx, ny, seaLevel8)) continue;

            const int lx = road_torus_delta(sx, nx, W);
            const int ly = road_torus_delta(sy, ny, H);
            if (road_segment_distance_sq(float(lx), float(ly),
                                         float(ex), float(ey)) > corridorSq) {
                continue;
            }

            const int nIdx = int(road_index(W, nx, ny));
            const float diagonal = (d[0] != 0 && d[1] != 0) ? 1.41421356f : 1.0f;
            float terrainCost = road_is_mountain(td, W, nx, ny) ? 6.0f : 1.0f;
            if (mask[std::size_t(nIdx)] != 0) terrainCost *= 0.35f;
            const float nextG = cur.g + diagonal * terrainCost;

            const bool firstVisit = scratch.seen[std::size_t(nIdx)] != scratch.mark;
            if (firstVisit || nextG + 0.001f < scratch.gScore[std::size_t(nIdx)]) {
                scratch.seen[std::size_t(nIdx)] = scratch.mark;
                scratch.gScore[std::size_t(nIdx)] = nextG;
                scratch.parent[std::size_t(nIdx)] = curIdx;
                const float hScore = road_octile_heuristic(W, H, nx, ny, tx, ty);
                if (!road_heap_push(scratch, {nx, ny, nextG, nextG + hScore})) {
                    heapCapHit = true;
                }
            }
        }
    }

    if (!result.found) {
        if (heapCapHit) result.edgeCapHit = true;
        return result;
    }

    int cur = targetIdx;
    int guard = 0;
    const int maxGuard = W * H;
    while (cur >= 0 && cur != startIdx && guard < maxGuard) {
        cur = scratch.parent[std::size_t(cur)];
        ++guard;
    }
    if (cur != startIdx) {
        result.found = false;
        return result;
    }
    cur = targetIdx;
    guard = 0;
    while (cur >= 0 && cur != startIdx && guard < maxGuard) {
        mask[std::size_t(cur)] = 255;
        cur = scratch.parent[std::size_t(cur)];
        ++guard;
    }
    mask[std::size_t(startIdx)] = 255;
    return result;
}

static void strip_city_connection(City& city, int target) {
    for (int& c : city.connections) {
        if (c == target) {
            c = -1;
            break;
        }
    }

    int w = 0;
    for (int c : city.connections) {
        if (c != -1) city.connections[w++] = c;
    }
    for (; w < int(sizeof(city.connections) / sizeof(city.connections[0])); ++w) {
        city.connections[w] = -1;
    }
}

// Boot-time road network. The router uses reusable W*H scratch once for the
// whole pass, not per edge. Bounded A* is preferred; the named land-Bresenham
// fallback is only accepted when the direct line is short, dry, and not mostly
// mountain. Failed links are pruned symmetrically from politics connections.
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

    constexpr float kSeaLevel = 0.40f;
    const std::uint8_t sl8 = std::uint8_t(kSeaLevel * 255.0f);
    int wholeExpansionBudget = kRoadWholePassExpansionCap;
    RoadRouterScratch scratch;
    scratch.init(W, H, kRoadHeapReserve);

    // Pruning bookkeeping: collect pairs to drop after every edge is examined.
    std::vector<std::pair<int,int>> dropPairs;

    for (std::size_t i = 0; i < P.cities.size(); ++i) {
        for (int b : P.cities[i].connections) {
            if (b < 0 || std::size_t(b) <= i
                || std::size_t(b) >= P.cities.size()) {
                continue;
            }
            const City& a = P.cities[i];
            const City& B = P.cities[std::size_t(b)];
            ++localStats.attemptedEdges;

            RoadRouteResult routed = route_bounded_road(td, mask, scratch,
                                                        a.x, a.y, B.x, B.y,
                                                        sl8,
                                                        wholeExpansionBudget);
            localStats.expansions += routed.expansions;
            if (routed.edgeCapHit) ++localStats.edgeExpansionCapHits;
            if (routed.wholeCapHit) ++localStats.wholeExpansionCapHits;
            if (routed.found) {
                ++localStats.keptEdges;
                ++localStats.boundedEdges;
                continue;
            }

            const RoadLineCheck line = measure_land_bresenham(td, W, H,
                                                              a.x, a.y,
                                                              B.x, B.y,
                                                              sl8);
            const bool mountainOk =
                line.cells > 0
                && line.mountain * 1000 <= line.cells * kRoadMaxFallbackMountainPermille;
            const bool fallbackOk =
                line.cells > 0
                && line.cells <= kRoadMaxFallbackCells
                && line.water == 0
                && mountainOk;
            if (!fallbackOk) {
                dropPairs.push_back({int(i), b});
                ++localStats.prunedEdges;
                continue;
            }

            stamp_land_bresenham_fallback(mask, W, H, a.x, a.y, B.x, B.y);
            ++localStats.keptEdges;
            ++localStats.fallbackEdges;
        }
    }

    // Apply pruning — symmetric removal from both endpoints.
    for (auto [x, y] : dropPairs) {
        strip_city_connection(P.cities[std::size_t(x)], y);
        strip_city_connection(P.cities[std::size_t(y)], x);
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
