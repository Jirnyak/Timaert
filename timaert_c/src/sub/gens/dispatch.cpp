#include "sub/gens/dispatch.h"
#include "sub/base_generator.h"
#include "sub/gens/city_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

// Forward decls for per-mode generators (each defined below).
static void gen_open      (const CellContext&, SubworldMapData&);
static void gen_forest    (const CellContext&, SubworldMapData&);
static void gen_swamp     (const CellContext&, SubworldMapData&);
static void gen_village   (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_mountain  (const CellContext&, SubworldMapData&);
static void gen_water     (const CellContext&, SubworldMapData&);
static void gen_road      (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);

SubworldMode resolve_mode(const CellContext& ctx) {
    if (ctx.landmarkSettlementId >= 0) {
        return ctx.landmarkSize > 1500 ? SubworldMode::City : SubworldMode::Village;
    }
    if (ctx.feature == FT_Mountain) return SubworldMode::Mountain;
    if (ctx.feature == FT_Road)     return SubworldMode::Road;
    if (ctx.feature == FT_DirtRoad) return SubworldMode::Road;
    if (ctx.feature == FT_Tree)     return SubworldMode::Forest;
    if (ctx.biome == Biome::Water)  return SubworldMode::Water;
    if (ctx.biome == Biome::Swamp)  return SubworldMode::Swamp;
    return SubworldMode::Open;
}

void dispatch_generate(const CellContext& ctx, const float nbHeights[9],
                       const Biome nbBiome[9],
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out) {
    out.heightmap.clear();
    generate_heightmap(out.heightmap, kCellSize, nbHeights, nbBiome, nbFeature,
                       ctx.biome, ctx.seed,
                       ctx.cx * kCellSize, ctx.cy * kCellSize);
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.trav.assign (std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();
    out.waterLevel = biome_config(ctx.biome).waterLevel;

    switch (resolve_mode(ctx)) {
        case SubworldMode::Forest:    gen_forest   (ctx, out); break;
        case SubworldMode::City:      generate_city(ctx, nbFeature, out); break;
        case SubworldMode::Village:   gen_village  (ctx, nbFeature, out); break;
        case SubworldMode::Mountain:  gen_mountain (ctx, out); break;
        case SubworldMode::Water:     gen_water    (ctx, out); break;
        case SubworldMode::Swamp:     gen_swamp    (ctx, out); break;
        case SubworldMode::Road:      gen_road     (ctx, nbFeature, out); break;
        case SubworldMode::Ruin:
        case SubworldMode::Grassland:
        case SubworldMode::Open:
        default:                      gen_open     (ctx, out); break;
    }

    // TS-faithful post-pass: smooth heightmap under road / square tiles so
    // paths look like they were carved into the relief instead of riding
    // its bumps. No-op when the cell has no roads. Mirrors `smoothRoadHeights`
    // applied at the end of `BaseGenerator.generateHeightmap` in TS.
    smooth_road_heights(out.heightmap, out.tiles, kCellSize, kCellSize);
}

// ── Generators ────────────────────────────────────────────────

// Plain biome ground tiles + stitched cross-cell tree scatter.
static void gen_open(const CellContext& ctx, SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // fill_base_tiles already stamps decorative trees but with a local RNG
    // — overwrite with the global stitched scatter so neighbouring open
    // cells line up tree distributions seamlessly.
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

static void gen_forest(const CellContext& ctx, SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ true, /*clearRadius*/ 0, ctx.seed);
}

static void gen_swamp(const CellContext& ctx, SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        Biome::Swamp, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

struct VillagePoint { float x, y; };
struct VillageNode { float x, y; bool isMain; };
struct VillageEdge { int p1, p2; };
struct VillageHouse { int x, y, w, h; float rotation; };
struct VillageDir { float angle; int tx, ty; };
struct VillageWall {
    std::vector<VillagePoint> nodes;
    float avgRadius = 0.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float gateHalfArc = 0.0f;
    float gateAngles[8]{};
    int gateCount = 0;
};

struct VillageState {
    Rng rng;
    std::uint32_t seed;
    int centerX;
    int centerY;
    std::vector<VillageNode> nodes;
    std::vector<VillageEdge> edges;
    std::vector<VillageHouse> houses;
    std::vector<std::vector<VillagePoint>> roadPaths;
    VillageWall wall;
    bool hasWall = false;

    explicit VillageState(std::uint32_t s)
        : rng(s), seed(s), centerX(kCellSize / 2), centerY(kCellSize / 2) {
        if (rng.next_f01() > 0.5f) centerX += rng_int(-10, 10);
        if (rng.next_f01() > 0.5f) centerY += rng_int(-10, 10);
        nodes.reserve(5);
        edges.reserve(5);
        houses.reserve(64);
        roadPaths.reserve(5);
    }

    int rng_int(int lo, int hi) {
        return lo + int(rng.next_u32() % std::uint32_t(hi - lo + 1));
    }

    float rng_float(float lo, float hi) {
        return lo + rng.next_f01() * (hi - lo);
    }
};

static void village_set_tile(SubworldMapData& out, int x, int y, std::uint8_t tile) {
    if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) return;
    const std::size_t idx = std::size_t(y) * kCellSize + x;
    out.tiles[idx] = tile;
    out.trav[idx] = (tile == TILE_HOUSE || tile == TILE_WALL) ? 0 : 1;
}

static bool village_is_road_feature(std::uint8_t f) {
    return f == FT_Road || f == FT_DirtRoad;
}

static std::vector<VillageDir> village_default_dirs(VillageState& vs) {
    std::vector<VillageDir> dirs;
    dirs.reserve(4);
    dirs.push_back({vs.rng_float(-0.15f, 0.15f), kCellSize - 1, vs.centerY});
    dirs.push_back({3.14159265f + vs.rng_float(-0.15f, 0.15f), 0, vs.centerY});
    if (vs.rng.next_f01() < 0.65f) {
        dirs.push_back({1.57079633f + vs.rng_float(-0.15f, 0.15f), vs.centerX, kCellSize - 1});
        dirs.push_back({-1.57079633f + vs.rng_float(-0.15f, 0.15f), vs.centerX, 0});
    }
    return dirs;
}

static std::vector<VillageDir> village_dirs_from_neighbors(VillageState& vs,
                                                           const std::uint8_t nbFeature[9]) {
    std::vector<VillageDir> dirs;
    dirs.reserve(8);
    for (int d = 0; d < 8; ++d) {
        const int dx = kDirOffsets[d][0];
        const int dy = kDirOffsets[d][1];
        const int idx = (dy + 1) * 3 + (dx + 1);
        if (!village_is_road_feature(nbFeature[idx])) continue;
        const int tx = std::clamp(int(std::lround(float(vs.centerX) + float(dx) * float(kCellSize) * 0.49f)),
                                  1, kCellSize - 2);
        const int ty = std::clamp(int(std::lround(float(vs.centerY) + float(dy) * float(kCellSize) * 0.49f)),
                                  1, kCellSize - 2);
        dirs.push_back({std::atan2(float(dy), float(dx)), tx, ty});
    }
    return dirs.size() < 2 ? village_default_dirs(vs) : dirs;
}

static void village_mark_organic_road(SubworldMapData& out, VillageState& vs,
                                      int x1, int y1, int x2, int y2,
                                      float baseAngle) {
    const float dx = float(x2 - x1);
    const float dy = float(y2 - y1);
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1.0f) return;
    const int steps = int(std::ceil(dist));
    std::vector<VillagePoint> centerline;
    centerline.reserve(std::size_t(steps) + 1u);

    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        float x = float(x1) + dx * t;
        float y = float(y1) + dy * t;
        if (t > 0.08f && t < 0.92f) {
            const float waveAmp = 3.5f + vs.rng_float(-1.0f, 1.0f);
            const float perp = baseAngle + 1.57079633f;
            const float offset = std::sin(t * dist * 0.015f + float(vs.seed)) * waveAmp;
            x += std::cos(perp) * offset;
            y += std::sin(perp) * offset;
        }
        centerline.push_back({x, y});
        const int ix = int(std::floor(x));
        const int iy = int(std::floor(y));
        for (int ry = -1; ry <= 1; ++ry) {
            for (int rx = -1; rx <= 1; ++rx) {
                village_set_tile(out, ix + rx, iy + ry, TILE_ROAD);
            }
        }
    }
    vs.roadPaths.push_back(std::move(centerline));
}

static void village_generate_square(SubworldMapData& out, VillageState& vs) {
    const int size = vs.rng_int(3, 6);
    const int x0 = vs.centerX - size / 2;
    const int y0 = vs.centerY - size / 2;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            village_set_tile(out, x0 + x, y0 + y, TILE_SQUARE);
}

static float village_settle_radius(int population) {
    return std::min(float(kCellSize) * 0.07f,
                    30.0f + std::sqrt(float(std::max(0, population))) * 3.0f);
}

static bool village_try_place_house(SubworldMapData& out, VillageState& vs,
                                    const VillageEdge& edge, float maxR) {
    const auto& n1 = vs.nodes[std::size_t(edge.p1)];
    const auto& n2 = vs.nodes[std::size_t(edge.p2)];
    const float ex = n2.x - n1.x;
    const float ey = n2.y - n1.y;
    const float len = std::sqrt(ex * ex + ey * ey);
    if (len < 0.1f) return false;
    const float px = -ey / len;
    const float py =  ex / len;

    for (int attempt = 0; attempt < 40; ++attempt) {
        const float t = vs.rng.next_f01();
        const float sx = n1.x + ex * t;
        const float sy = n1.y + ey * t;
        const float dx = sx - float(vs.centerX);
        const float dy = sy - float(vs.centerY);
        if (dx * dx + dy * dy > maxR * maxR) continue;

        const float side = vs.rng.next_f01() > 0.5f ? 1.0f : -1.0f;
        const int offset = 1 + vs.rng_int(1, 2);
        const int hx = int(std::floor(sx + px * float(offset) * side));
        const int hy = int(std::floor(sy + py * float(offset) * side));
        const int hw = vs.rng_int(2, 3);
        const int hh = vs.rng_int(2, 3);
        if (hx < 5 || hy < 5 || hx >= kCellSize - 5 || hy >= kCellSize - 5) continue;

        bool free = true;
        for (int y = hy; y < hy + hh && free; ++y) {
            for (int x = hx; x < hx + hw; ++x) {
                if (out.tiles[std::size_t(y) * kCellSize + x] != TILE_EMPTY) {
                    free = false;
                    break;
                }
            }
        }
        if (!free) continue;

        for (int y = hy; y < hy + hh; ++y)
            for (int x = hx; x < hx + hw; ++x)
                village_set_tile(out, x, y, TILE_HOUSE);

        const float rot = std::atan2(ey, ex) + vs.rng_float(-0.3f, 0.3f);
        vs.houses.push_back({hx, hy, hw, hh, rot});
        Structure s{Structure::House,
            float(hx) + float(hw) * 0.5f,
            float(hy) + float(hh) * 0.5f,
            float(std::max(hw, hh)) * 0.5f,
            2.5f};
        out.structures.push_back(s);
        return true;
    }
    return false;
}

static void village_grow(SubworldMapData& out, VillageState& vs, int population) {
    const int target = std::max(1, population / 5);
    const int maxIter = target * 50;
    const float maxR = village_settle_radius(population);
    int placed = 0;
    for (int iter = 0; placed < target && iter < maxIter; ++iter) {
        if (vs.edges.empty()) break;
        const auto& edge = vs.edges[std::size_t(vs.rng_int(0, int(vs.edges.size()) - 1))];
        if (village_try_place_house(out, vs, edge, maxR)) ++placed;
    }
}

static bool village_place_field(SubworldMapData& out, VillageState& vs,
                                float cx, float cy, float fw, float fh, float rot) {
    const int halfDiag = int(std::ceil(std::sqrt(fw * fw + fh * fh) * 0.5f)) + 2;
    const int minX = int(std::floor(cx)) - halfDiag;
    const int maxX = int(std::ceil (cx)) + halfDiag;
    const int minY = int(std::floor(cy)) - halfDiag;
    const int maxY = int(std::ceil (cy)) + halfDiag;
    if (minX < 2 || minY < 2 || maxX >= kCellSize - 2 || maxY >= kCellSize - 2) {
        return false;
    }

    const float co = std::cos(rot);
    const float si = std::sin(rot);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float lx = (float(x) - cx) * co + (float(y) - cy) * si;
            const float ly = -(float(x) - cx) * si + (float(y) - cy) * co;
            if (std::fabs(lx) <= fw * 0.5f && std::fabs(ly) <= fh * 0.5f) {
                const auto tile = out.tiles[std::size_t(y) * kCellSize + x];
                if (tile == TILE_HOUSE || tile == TILE_ROAD) return false;
            }
        }
    }
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float lx = (float(x) - cx) * co + (float(y) - cy) * si;
            const float ly = -(float(x) - cx) * si + (float(y) - cy) * co;
            if (std::fabs(lx) <= fw * 0.5f && std::fabs(ly) <= fh * 0.5f) {
                village_set_tile(out, x, y, TILE_FIELD);
            }
        }
    }
    return true;
}

static void village_generate_fields(SubworldMapData& out, VillageState& vs, int population) {
    const float innerR = village_settle_radius(population) + 5.0f;
    const float outerR = innerR + std::min(float(kCellSize) * 0.08f,
                                           30.0f + float(population) * 0.3f);
    const int fieldCount = std::max(2, population / 20);
    int placed = 0;
    for (int attempts = 0; placed < fieldCount && attempts < fieldCount * 25; ++attempts) {
        const float a = vs.rng_float(0.0f, 6.2831853f);
        const float r = vs.rng_float(innerR, outerR);
        const float cx = float(vs.centerX) + std::cos(a) * r;
        const float cy = float(vs.centerY) + std::sin(a) * r;
        if (village_place_field(out, vs, cx, cy,
                                vs.rng_float(8.0f, 20.0f),
                                vs.rng_float(6.0f, 16.0f),
                                vs.rng_float(-0.4f, 0.4f))) {
            ++placed;
        }
    }
}

static float village_segment_intersection_angle(const VillagePoint& a1,
                                                const VillagePoint& a2,
                                                const VillagePoint& b1,
                                                const VillagePoint& b2,
                                                float cx, float cy,
                                                bool& hit) {
    const float dx1 = a2.x - a1.x;
    const float dy1 = a2.y - a1.y;
    const float dx2 = b2.x - b1.x;
    const float dy2 = b2.y - b1.y;
    const float denom = dx1 * dy2 - dy1 * dx2;
    if (std::fabs(denom) < 0.001f) { hit = false; return 0.0f; }
    const float t = ((b1.x - a1.x) * dy2 - (b1.y - a1.y) * dx2) / denom;
    const float u = ((b1.x - a1.x) * dy1 - (b1.y - a1.y) * dx1) / denom;
    hit = t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
    if (!hit) return 0.0f;
    const float ix = a1.x + t * dx1;
    const float iy = a1.y + t * dy1;
    return std::atan2(iy - cy, ix - cx);
}

static bool village_is_gate_angle(const VillageWall& wall, float angle) {
    for (int i = 0; i < wall.gateCount; ++i) {
        if (angular_distance(angle, wall.gateAngles[i]) <= wall.gateHalfArc) {
            return true;
        }
    }
    return false;
}

static void village_build_palisade(SubworldMapData& out, VillageState& vs) {
    float maxHouseDist = 0.0f;
    for (const auto& h : vs.houses) {
        const float dx = float(h.x) + float(h.w) * 0.5f - float(vs.centerX);
        const float dy = float(h.y) + float(h.h) * 0.5f - float(vs.centerY);
        maxHouseDist = std::max(maxHouseDist, std::sqrt(dx * dx + dy * dy));
    }
    const float radius = std::max(15.0f, maxHouseDist + 6.0f);
    const int segments = std::max(10, int(std::floor(12.0f + radius / 8.0f)));

    VillageWall wall{};
    wall.nodes.reserve(std::size_t(segments));
    wall.centerX = float(vs.centerX);
    wall.centerY = float(vs.centerY);
    const float phase1 = vs.rng_float(0.0f, 6.2831853f);
    const float phase2 = vs.rng_float(0.0f, 6.2831853f);
    for (int i = 0; i < segments; ++i) {
        const float a = float(i) * 6.2831853f / float(segments);
        const float harmonic = std::sin(a * 3.0f + phase1) * 0.32f
                             + std::sin(a * 5.0f + phase2) * 0.18f;
        const float jitter = vs.rng_float(-1.0f, 1.0f) * radius * 0.12f * 0.18f;
        const float r = radius + radius * 0.12f * harmonic + jitter;
        wall.nodes.push_back({wall.centerX + std::cos(a) * r,
                              wall.centerY + std::sin(a) * r});
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < segments; ++i) {
            const auto p = wall.nodes[std::size_t((i - 1 + segments) % segments)];
            const auto c = wall.nodes[std::size_t(i)];
            const auto n = wall.nodes[std::size_t((i + 1) % segments)];
            wall.nodes[std::size_t(i)] = {
                (p.x + c.x * 2.0f + n.x) * 0.25f,
                (p.y + c.y * 2.0f + n.y) * 0.25f};
        }
    }
    float sumR = 0.0f;
    for (const auto& p : wall.nodes) {
        const float dx = p.x - wall.centerX;
        const float dy = p.y - wall.centerY;
        sumR += std::sqrt(dx * dx + dy * dy);
    }
    wall.avgRadius = sumR / float(std::max(1, segments));

    for (const auto& path : vs.roadPaths) {
        for (std::size_t p = 0; p + 1 < path.size(); ++p) {
            for (int w = 0; w < segments && wall.gateCount < 8; ++w) {
                bool hit = false;
                float angle = village_segment_intersection_angle(
                    path[p], path[p + 1],
                    wall.nodes[std::size_t(w)],
                    wall.nodes[std::size_t((w + 1) % segments)],
                    wall.centerX, wall.centerY, hit);
                if (hit) wall.gateAngles[wall.gateCount++] = angle;
            }
        }
    }
    if (wall.gateCount == 0) {
        wall.gateAngles[0] = 0.0f;
        wall.gateAngles[1] = 3.14159265f;
        wall.gateAngles[2] = 1.57079633f;
        wall.gateAngles[3] = -1.57079633f;
        wall.gateCount = 4;
    }
    wall.gateHalfArc = std::max(0.05f, 8.0f / std::max(1.0f, wall.avgRadius));

    int wallStep = 0;
    for (int i = 0; i < segments; ++i) {
        const auto a = wall.nodes[std::size_t(i)];
        const auto b = wall.nodes[std::size_t((i + 1) % segments)];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const int steps = std::max(1, int(std::ceil(std::sqrt(dx * dx + dy * dy) * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            const float x = a.x + dx * t;
            const float y = a.y + dy * t;
            const float angle = std::atan2(y - wall.centerY, x - wall.centerX);
            if (village_is_gate_angle(wall, angle)) continue;
            const int ix = int(std::floor(x));
            const int iy = int(std::floor(y));
            village_set_tile(out, ix, iy, TILE_WALL);
            if ((wallStep++ & 7) == 0) {
                out.structures.push_back({Structure::Wall, float(ix) + 0.5f, float(iy) + 0.5f, 0.6f, 3.0f});
            }
        }
    }
    vs.wall = std::move(wall);
    vs.hasWall = true;
}

static bool village_inside_wall(const VillageWall& wall, float px, float py) {
    const float dx = px - wall.centerX;
    const float dy = py - wall.centerY;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d <= wall.avgRadius * 0.72f) return true;
    if (d >= wall.avgRadius * 1.35f) return false;

    bool inside = false;
    const int n = int(wall.nodes.size());
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const auto a = wall.nodes[std::size_t(i)];
        const auto b = wall.nodes[std::size_t(j)];
        if ((a.y > py) != (b.y > py)
            && px < (b.x - a.x) * (py - a.y) / ((b.y - a.y) == 0.0f ? 0.00001f : (b.y - a.y)) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

static void village_fill_inner_gaps(SubworldMapData& out, VillageState& vs) {
    const float threshold = vs.hasWall ? vs.wall.avgRadius * 0.8f : float(kCellSize) * 0.12f;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] != TILE_EMPTY) continue;
            const bool inside = vs.hasWall && village_inside_wall(vs.wall, float(x) + 0.5f, float(y) + 0.5f);
            if (inside) {
                out.tiles[idx] = vs.rng.next_f01() < 0.5f ? std::uint8_t(TILE_GRASS)
                                                           : std::uint8_t(TILE_SQUARE);
            } else {
                const float dx = float(x - vs.centerX);
                const float dy = float(y - vs.centerY);
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > threshold) out.tiles[idx] = TILE_GRASS;
            }
        }
    }
}

static void gen_village(const CellContext& ctx, const std::uint8_t nbFeature[9],
                        SubworldMapData& out) {
    // TS VillageGenerator starts from TILE_EMPTY and fills biome ground only
    // after roads, houses, fields and palisade have claimed their cells.
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_EMPTY));
    out.trav.assign (std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();

    VillageState vs(ctx.seed ^ 0xABCDEFu);
    vs.nodes.push_back({float(vs.centerX), float(vs.centerY), true});
    auto dirs = village_dirs_from_neighbors(vs, nbFeature);
    for (const auto& d : dirs) {
        const int nodeId = int(vs.nodes.size());
        vs.nodes.push_back({float(d.tx), float(d.ty), true});
        vs.edges.push_back({0, nodeId});
        village_mark_organic_road(out, vs, vs.centerX, vs.centerY, d.tx, d.ty, d.angle);
    }

    const int population = std::max(ctx.landmarkSize, 10);
    village_generate_square(out, vs);
    village_grow(out, vs, population);
    village_generate_fields(out, vs, population);
    if (population >= 50) village_build_palisade(out, vs);
    village_fill_inner_gaps(out, vs);

    for (auto& t : out.tiles) {
        if (t == TILE_EMPTY) t = TILE_GRASS;
    }
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false,
        vs.hasWall ? int(vs.wall.avgRadius * 1.05f) : int(float(kCellSize) * 0.15f),
        ctx.seed);
}

static void gen_mountain(const CellContext& ctx, SubworldMapData& out) {
    // Lay biome ground; the heightmap (mountain feature amp + ridge
    // multifractal) does the actual mountain shaping. Slope-driven rock
    // and snow overlay in the terrain shader exposes rock on steep faces.
    // Sparse trees from the universal scatter — biome density already low.
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

// Water cells — TS-faithful: lay biome ground + run the universal tree
// scatter (water biome's treeDensity = 0 → no-op). The macro heightmap
// pulls water cells well below WATER_LEVEL so the global water plane
// covers them naturally; the renderer never needs a TILE_WATER fill.
//
// We additionally force the water cell's bed to sit firmly below the
// global water plane (WATER_LEVEL = 0.40). The bilinear blend in
// generate_heightmap mixes neighbouring land heights into the water
// cell near the shore, and the per-cell detail fBM adds another
// 0..heightScale on top — either of which can lift the bed above
// WATER_LEVEL and produce "hills standing where water should be".
// Clamping after generation guarantees the water plane always covers
// the bed; shore cells remain land-side cells whose own bilinear pulls
// down naturally toward this clamped neighbour.
static void gen_water(const CellContext& ctx, SubworldMapData& out) {
    // No post-clamp: generate_heightmap already remaps water cells with
    // the squared deep-ocean curve (h ≈ 0 deep, ≤ WATER_LEVEL at the
    // shoreline), and bilinear blend with land neighbours produces the
    // natural beach / river-bank slope. Just fill base tiles + tree
    // scatter (which trivially places nothing in pure water).
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

// ── Road carving ──────────────────────────────────────────────
//
// TS-faithful port of `markOrganicMainRoad` + `RoadGenerator.carveRoads`.
// For every neighbour cell that also carries a road feature, carve an
// organic curved street from this cell's centre to the midpoint of that
// edge (or to the corner for diagonals). The endpoint is deterministic
// — both this cell and the neighbour use the same midpoint, so road
// segments line up exactly across the cell boundary.
//
// Width is a single fixed constant so every cell carves the same
// footprint — critical for visually continuous roads across the 3x3
// seam. We use *straight* segments now: the previous sin-wave looked
// like a wobbly stripe and any per-cell randomness in amplitude shows
// up as a kink at the boundary. Current macroworld road geometry is
// produced by `trace_roads` as budgeted torus A* plus dry/short Bresenham
// fallback; the subworld carve renders the resulting edge anchor as a
// straight local road segment.
static void carve_organic_road(SubworldMapData& out,
                               int x1, int y1, int x2, int y2,
                               std::uint32_t worldSeed) {
    constexpr int kStreetWidth = 2;       // 5-tile footprint
    const float fdx = float(x2 - x1);
    const float fdy = float(y2 - y1);
    const float dist = std::sqrt(fdx * fdx + fdy * fdy);
    if (dist < 1.0f) return;
    // Perpendicular direction for organic offset.
    const float invDist = 1.0f / dist;
    const float nx = -fdy * invDist;   // rotated 90°
    const float ny =  fdx * invDist;
    // Wave deterministic from worldSeed only — both neighbour cells running
    // this carve see the same seed, and endpoint damping (sin(πt)) drives
    // offset to zero at t=0 and t=1, so segments meet cleanly at the edge.
    constexpr float kPi    = 3.14159265f;
    constexpr float kFreq  = 0.012f;     // gentle long-wavelength curve
    constexpr float kAmp   = 4.0f;       // tiles
    const float phase      = float(worldSeed & 0xfffffu) * 0.0001f;
    const int steps = int(std::ceil(dist));
    for (int i = 0; i <= steps; ++i) {
        const float t  = float(i) / float(steps);
        const float x0 = float(x1) + fdx * t;
        const float y0 = float(y1) + fdy * t;
        const float damp = std::sin(t * kPi);
        const float damp2 = damp * damp;                           // C¹ at ends
        const float off  = std::sin(t * dist * kFreq + phase) * kAmp * damp2;
        const int ix = int(std::floor(x0 + nx * off));
        const int iy = int(std::floor(y0 + ny * off));
        for (int dy = -kStreetWidth; dy <= kStreetWidth; ++dy) {
            for (int dx = -kStreetWidth; dx <= kStreetWidth; ++dx) {
                const int px = ix + dx;
                const int py = iy + dy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                const std::size_t idx = std::size_t(py) * kCellSize + px;
                const std::uint8_t cur = out.tiles[idx];
                if (cur == TILE_WALL || cur == TILE_HOUSE) continue;
                out.tiles[idx] = TILE_ROAD;
                out.trav [idx] = 1;
            }
        }
    }
}

// Endpoint on the cell edge that matches the neighbour's matching point —
// midpoint of the shared edge for orthogonal neighbours, the shared
// corner for diagonals. Symmetric: both cells compute the same point.
static void edge_target(int dx, int dy, int& ox, int& oy) {
    const int half = kCellSize / 2;
    if (dx ==  0 && dy == -1) { ox = half;          oy = 0;             return; } // N
    if (dx ==  1 && dy == -1) { ox = kCellSize - 1; oy = 0;             return; } // NE
    if (dx ==  1 && dy ==  0) { ox = kCellSize - 1; oy = half;          return; } // E
    if (dx ==  1 && dy ==  1) { ox = kCellSize - 1; oy = kCellSize - 1; return; } // SE
    if (dx ==  0 && dy ==  1) { ox = half;          oy = kCellSize - 1; return; } // S
    if (dx == -1 && dy ==  1) { ox = 0;             oy = kCellSize - 1; return; } // SW
    if (dx == -1 && dy ==  0) { ox = 0;             oy = half;          return; } // W
    if (dx == -1 && dy == -1) { ox = 0;             oy = 0;             return; } // NW
    ox = half; oy = half;
}

static bool is_road_feature(std::uint8_t f) {
    return f == FT_Road || f == FT_DirtRoad;
}

static void gen_road(const CellContext& ctx, const std::uint8_t nbFeature[9],
                     SubworldMapData& out) {
    // Lay biome ground first so the road sits on real grass / steppe / etc.
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();

    // Collect road-bearing neighbours (skip the centre, idx 4).
    int connectedDx[8], connectedDy[8], nConnected = 0;
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            if (xx == 1 && yy == 1) continue;
            if (!is_road_feature(nbFeature[yy * 3 + xx])) continue;
            connectedDx[nConnected] = xx - 1;
            connectedDy[nConnected] = yy - 1;
            ++nConnected;
        }
    }

    const int cx = kCellSize / 2;
    const int cy = kCellSize / 2;

    if (nConnected == 0) {
        // Isolated road cell — short N-S stub so something is visible.
        carve_organic_road(out, cx, 0, cx, kCellSize - 1, ctx.seed);
    } else if (nConnected == 1) {
        // Single connection — carve all the way through to the opposite
        // edge instead of dead-ending at centre. Without this, the road
        // visibly stops mid-cell when the macro path bends and one end
        // doesn't have a neighbour-road; the next cell's road then
        // appears to start from nowhere, producing the seam artifact.
        int ax, ay;
        edge_target(connectedDx[0], connectedDy[0], ax, ay);
        const int bx = kCellSize - 1 - ax;
        const int by = kCellSize - 1 - ay;
        carve_organic_road(out, ax, ay, bx, by, ctx.seed);
    } else if (nConnected == 2) {
        // Through-road: connect the two endpoints directly.
        int ax, ay, bx, by;
        edge_target(connectedDx[0], connectedDy[0], ax, ay);
        edge_target(connectedDx[1], connectedDy[1], bx, by);
        carve_organic_road(out, ax, ay, bx, by, ctx.seed);
    } else {
        // 3+ directions: hub at centre, radial arms to each neighbour.
        for (int i = 0; i < nConnected; ++i) {
            int ex, ey;
            edge_target(connectedDx[i], connectedDy[i], ex, ey);
            carve_organic_road(out, cx, cy, ex, ey, ctx.seed + std::uint32_t(i) * 17u);
        }
    }

    // Vegetation around the road — biome-typical scatter, no urban clear.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

} // namespace sm::sub
