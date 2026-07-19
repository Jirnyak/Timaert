#include "sub/gens/dispatch.h"
#include "sub/base_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sm::sub {

// Forward decls for per-mode generators (each defined below).
static void gen_open      (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_forest    (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_swamp     (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_city      (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_village   (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_mountain  (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_water     (const CellContext&, SubworldMapData&);
static void gen_road      (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void gen_spire     (const CellContext&, SubworldMapData&);
static void gen_ruin      (const CellContext&, const std::uint8_t nbFeature[9], SubworldMapData&);
static void scatter_forest_glades(const CellContext&, SubworldMapData&);
static void carve_organic_road(SubworldMapData&, int, int, int, int, std::uint32_t);
static void edge_anchor_target(const CellContext&, int, int, int&, int&);
static bool is_road_feature(std::uint8_t);
static void sync_water_tiles_from_heightmap(SubworldMapData&);

SubworldMode resolve_mode(const CellContext& ctx) {
    const FeatureType feature = FeatureLayer::decode(std::uint8_t(ctx.feature));
    switch (ctx.landmarkKind) {
        case CellLandmarkKind::City:    return SubworldMode::City;
        case CellLandmarkKind::Village: return SubworldMode::Village;
        case CellLandmarkKind::Ruin:    return SubworldMode::Ruin;
        case CellLandmarkKind::Spire:   return SubworldMode::Spire;
        case CellLandmarkKind::None:    break;
    }
    if (ctx.landmarkSettlementId >= 0) return SubworldMode::City;
    if (feature == FT_Mountain) return SubworldMode::Mountain;
    if (feature == FT_Road)     return SubworldMode::Road;
    if (feature == FT_DirtRoad) return SubworldMode::Road;
    if (feature == FT_Tree)     return SubworldMode::Forest;
    if (ctx.biome == Biome::Water)  return SubworldMode::Water;
    if (ctx.biome == Biome::Swamp)  return SubworldMode::Swamp;
    return SubworldMode::Grassland;
}

void dispatch_generate(const CellContext& ctx, const float nbHeights[9],
                       const Biome nbBiome[9],
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out) {
    CellContext safeCtx = ctx;
    safeCtx.feature = FeatureLayer::decode(std::uint8_t(ctx.feature));
    std::uint8_t safeFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        safeFeature[i] = std::uint8_t(FeatureLayer::decode(nbFeature[i]));
    }

    out.heightmap.clear();
    generate_heightmap(out.heightmap, kCellSize, nbHeights, nbBiome, safeFeature,
                       safeCtx.biome, safeCtx.seed,
                       safeCtx.cx * kCellSize, safeCtx.cy * kCellSize);
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.trav.assign (std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();
    out.waterLevel = WATER_LEVEL;

    switch (resolve_mode(safeCtx)) {
        case SubworldMode::Forest:    gen_forest   (safeCtx, safeFeature, out); break;
        case SubworldMode::City:      gen_city     (safeCtx, safeFeature, out); break;
        case SubworldMode::Village:   gen_village  (safeCtx, safeFeature, out); break;
        case SubworldMode::Mountain:  gen_mountain (safeCtx, safeFeature, out); break;
        case SubworldMode::Water:     gen_water    (safeCtx, out); break;
        case SubworldMode::Swamp:     gen_swamp    (safeCtx, safeFeature, out); break;
        case SubworldMode::Road:      gen_road     (safeCtx, safeFeature, out); break;
        case SubworldMode::Spire:     gen_spire    (safeCtx, out); break;
        case SubworldMode::Ruin:      gen_ruin     (safeCtx, safeFeature, out); break;
        case SubworldMode::Grassland:
        case SubworldMode::Open:
        default:                      gen_open     (safeCtx, safeFeature, out); break;
    }

    // TS-faithful post-pass: smooth heightmap under road / square tiles so
    // paths look like they were carved into the relief instead of riding
    // its bumps. No-op when the cell has no roads. Mirrors `smoothRoadHeights`
    // applied at the end of `BaseGenerator.generateHeightmap` in TS.
    smooth_road_heights(out.heightmap, out.tiles, kCellSize, kCellSize);
    sync_water_tiles_from_heightmap(out);
}

static bool preserves_authored_surface(std::uint8_t tile) {
    return tile == TILE_ROAD
        || tile == TILE_HOUSE
        || tile == TILE_WALL
        || tile == TILE_FIELD
        || tile == TILE_SQUARE;
}

static void sync_water_tiles_from_heightmap(SubworldMapData& out) {
    if (out.tiles.size() != out.heightmap.size()) return;
    constexpr float kShoreTop = WATER_LEVEL + 0.05f;
    for (std::size_t i = 0; i < out.heightmap.size(); ++i) {
        const std::uint8_t tile = out.tiles[i];
        if (preserves_authored_surface(tile)) continue;

        const float h = out.heightmap[i];
        if (h < WATER_LEVEL) {
            out.tiles[i] = TILE_WATER;
        } else if (h < kShoreTop) {
            out.tiles[i] = TILE_SHORE;
        } else if (tile == TILE_WATER || tile == TILE_SHORE) {
            out.tiles[i] = TILE_GRASS;
        }
    }
}

// ── Generators ────────────────────────────────────────────────

// Plain biome ground tiles + stitched cross-cell tree scatter.
static void gen_open(const CellContext& ctx, const std::uint8_t nbFeature[9],
                     SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // Plain wilderness: no road stitching. TS only grows connecting roads when
    // the cell itself is road-like (road feature or landmark); a bare
    // grassland/open cell next to a road must stay road-free.
    (void)nbFeature;
    // fill_base_tiles already stamps decorative trees but with a local RNG
    // — overwrite with the global stitched scatter so neighbouring open
    // cells line up tree distributions seamlessly.
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

static void gen_forest(const CellContext& ctx, const std::uint8_t nbFeature[9],
                       SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ true, /*clearRadius*/ 0, ctx.seed);
    scatter_forest_glades(ctx, out);
}

// TS forest.ts parity: coarse, globally-aligned glade scan. The glade
// centers are keyed to absolute tile coordinates so neighbouring forest
// cells agree on where clearings cross a seam.
static float noise01(int x, int y, std::uint32_t seed) {
    return float(hash3(std::uint32_t(x), std::uint32_t(y), seed)) / 4294967295.0f;
}

static float smooth_noise01(float x, float y, std::uint32_t seed) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = noise01(ix,     iy,     seed);
    const float n10 = noise01(ix + 1, iy,     seed);
    const float n01 = noise01(ix,     iy + 1, seed);
    const float n11 = noise01(ix + 1, iy + 1, seed);
    return n00 * (1.0f - sx) * (1.0f - sy)
         + n10 * sx * (1.0f - sy)
         + n01 * (1.0f - sx) * sy
         + n11 * sx * sy;
}

static void carve_forest_glade(SubworldMapData& out, int lx, int ly, int radius) {
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > r2) continue;
            const int px = lx + dx;
            const int py = ly + dy;
            if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
            const std::size_t idx = std::size_t(py) * kCellSize + px;
            if (out.tiles[idx] == TILE_TREE_DECOR || out.tiles[idx] == TILE_EMPTY) {
                out.tiles[idx] = TILE_GRASS;
            }
        }
    }

    std::size_t write = 0;
    for (std::size_t read = 0; read < out.structures.size(); ++read) {
        const Structure& s = out.structures[read];
        bool keep = true;
        if (s.kind == Structure::Tree) {
            const int tx = int(std::floor(s.x));
            const int ty = int(std::floor(s.y));
            const int dx = tx - lx;
            const int dy = ty - ly;
            keep = dx * dx + dy * dy > r2;
        }
        if (keep) {
            if (write != read) out.structures[write] = s;
            ++write;
        }
    }
    out.structures.resize(write);
}

static void scatter_forest_glades(const CellContext& ctx, SubworldMapData& out) {
    constexpr int kGladeStep = 48;
    constexpr float kGladeThreshold = 0.72f;
    constexpr int kGladeRadiusMin = 6;
    constexpr int kGladeRadiusMax = 16;

    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    const int startGX = gox + ((kGladeStep - (gox % kGladeStep)) % kGladeStep);
    const int startGY = goy + ((kGladeStep - (goy % kGladeStep)) % kGladeStep);
    const int endGX = gox + kCellSize;
    const int endGY = goy + kCellSize;

    for (int gy = startGY; gy < endGY; gy += kGladeStep) {
        for (int gx = startGX; gx < endGX; gx += kGladeStep) {
            const float n = smooth_noise01(float(gx) * 0.007f + 777.0f,
                                           float(gy) * 0.007f + 888.0f,
                                           ctx.seed);
            if (n < kGladeThreshold) continue;

            const float rn = noise01(gx * 5 + 99999, gy * 7 + 88888, ctx.seed);
            const int radius = kGladeRadiusMin
                + int(std::floor(rn * float(kGladeRadiusMax - kGladeRadiusMin + 1)));
            carve_forest_glade(out, gx - gox, gy - goy, radius);
        }
    }
}

static void gen_swamp(const CellContext& ctx, const std::uint8_t nbFeature[9],
                      SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        Biome::Swamp, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

static void clear_decor_tiles(SubworldMapData& out) {
    for (std::uint8_t& t : out.tiles) {
        if (t == TILE_TREE_DECOR) t = TILE_GRASS;
    }
}

static void stamp_rect(SubworldMapData& out, int x, int y, int w, int h,
                       std::uint8_t tile, std::uint8_t trav) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kCellSize, x + w);
    const int y1 = std::min(kCellSize, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            const std::size_t idx = std::size_t(yy) * kCellSize + xx;
            out.tiles[idx] = tile;
            out.trav[idx] = trav;
        }
    }
}

static bool rect_clear_for_urban(const SubworldMapData& out, int x, int y,
                                 int w, int h) {
    if (x < 2 || y < 2 || x + w >= kCellSize - 2 || y + h >= kCellSize - 2) {
        return false;
    }
    for (int yy = y - 1; yy <= y + h; ++yy) {
        for (int xx = x - 1; xx <= x + w; ++xx) {
            const std::uint8_t t = out.tiles[std::size_t(yy) * kCellSize + xx];
            if (t == TILE_ROAD || t == TILE_HOUSE || t == TILE_WALL
             || t == TILE_SQUARE || t == TILE_FIELD) {
                return false;
            }
        }
    }
    return true;
}

static bool has_tile_near(const SubworldMapData& out, int x, int y,
                          int radius, std::uint8_t tile) {
    const int x0 = std::max(0, x - radius);
    const int y0 = std::max(0, y - radius);
    const int x1 = std::min(kCellSize - 1, x + radius);
    const int y1 = std::min(kCellSize - 1, y + radius);
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            if (out.tiles[std::size_t(yy) * kCellSize + xx] == tile) {
                return true;
            }
        }
    }
    return false;
}

static bool add_house_rect(SubworldMapData& out, int x, int y, int w, int h,
                           float height) {
    if (!rect_clear_for_urban(out, x, y, w, h)) return false;
    stamp_rect(out, x, y, w, h, TILE_HOUSE, 0);
    out.structures.push_back(
        Structure{Structure::House, float(x) + float(w) * 0.5f,
                  float(y) + float(h) * 0.5f,
                  float(std::max(w, h)) * 0.5f, height});
    return true;
}

static bool stamp_landmark_house(SubworldMapData& out, int x, int y, int w, int h,
                                 float height) {
    if (x < 2 || y < 2 || x + w >= kCellSize - 2 || y + h >= kCellSize - 2) {
        return false;
    }
    stamp_rect(out, x, y, w, h, TILE_HOUSE, 0);
    out.structures.push_back(
        Structure{Structure::House, float(x) + float(w) * 0.5f,
                  float(y) + float(h) * 0.5f,
                  float(std::max(w, h)) * 0.5f, height});
    return true;
}

static bool try_add_roadside_house(SubworldMapData& out, Rng& r,
                                   int center, float maxRadius,
                                   int minSize, int maxSize,
                                   float height) {
    const int radius = int(std::floor(maxRadius));
    if (radius <= 0) return false;
    const int span = radius * 2 + 1;
    const int sizeRange = std::max(1, maxSize - minSize + 1);
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int hx = center + int(r.next_u32() % std::uint32_t(span)) - radius;
        const int hy = center + int(r.next_u32() % std::uint32_t(span)) - radius;
        const int dx = hx - center;
        const int dy = hy - center;
        if (dx * dx + dy * dy > radius * radius) continue;
        if (!has_tile_near(out, hx, hy, 10, TILE_ROAD)) continue;
        const int w = minSize + int(r.next_u32() % std::uint32_t(sizeRange));
        const int h = minSize + int(r.next_u32() % std::uint32_t(sizeRange));
        if (add_house_rect(out, hx - w / 2, hy - h / 2, w, h, height)) {
            return true;
        }
    }
    return false;
}

static bool add_field_rect(SubworldMapData& out, int cx, int cy, int w, int h) {
    const int x = cx - w / 2;
    const int y = cy - h / 2;
    if (x < 2 || y < 2 || x + w >= kCellSize - 2 || y + h >= kCellSize - 2) {
        return false;
    }
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            const std::uint8_t t = out.tiles[std::size_t(yy) * kCellSize + xx];
            if (t == TILE_ROAD || t == TILE_HOUSE || t == TILE_WALL
             || t == TILE_SQUARE || t == TILE_WATER) {
                return false;
            }
        }
    }
    stamp_rect(out, x, y, w, h, TILE_FIELD, 1);
    return true;
}

struct RoadAxisSet {
    std::array<int, 8> dx{};
    std::array<int, 8> dy{};
    std::array<float, 8> angle{};
    int count = 0;
    bool anchored = false;
};

struct RoadDirSet {
    std::array<int, 8> dx{};
    std::array<int, 8> dy{};
    std::array<float, 8> angle{};
    int count = 0;
};

static RoadDirSet connected_road_dirs(const std::uint8_t nbFeature[9]) {
    RoadDirSet out{};
    for (int d = 0; d < 8; ++d) {
        const int dx = kDirOffsets[d][0];
        const int dy = kDirOffsets[d][1];
        const int idx = (dy + 1) * 3 + (dx + 1);
        if (!is_road_feature(nbFeature[idx])) continue;
        out.dx[std::size_t(out.count)] = dx;
        out.dy[std::size_t(out.count)] = dy;
        out.angle[std::size_t(out.count)] = std::atan2(float(dy), float(dx));
        ++out.count;
    }
    return out;
}

static RoadAxisSet settlement_road_axes(const std::uint8_t nbFeature[9]) {
    RoadAxisSet out{};
    const RoadDirSet dirs = connected_road_dirs(nbFeature);
    for (int i = 0; i < dirs.count; ++i) {
        out.dx[std::size_t(out.count)] = dirs.dx[std::size_t(i)];
        out.dy[std::size_t(out.count)] = dirs.dy[std::size_t(i)];
        out.angle[std::size_t(out.count)] = dirs.angle[std::size_t(i)];
        ++out.count;
    }
    // Main roads reach ONLY edges that connect to a road / settlement
    // neighbour, always via the symmetric edge anchor so both sides of the seam
    // meet (identical to gen_road's neighbour-only carving). No cardinal
    // fallback -> a settlement never carves a road toward a road-less neighbour
    // ("into the void"); an unconnected settlement grows internal streets only.
    out.anchored = out.count > 0;
    return out;
}

static void carve_landmark_anchor_roads(SubworldMapData& out,
                                        const CellContext& ctx,
                                        const RoadDirSet& dirs,
                                        int center,
                                        std::uint32_t seed) {
    for (int i = 0; i < dirs.count; ++i) {
        int tx, ty;
        edge_anchor_target(ctx, dirs.dx[std::size_t(i)], dirs.dy[std::size_t(i)], tx, ty);
        carve_organic_road(out, center, center, tx, ty,
                           seed + std::uint32_t(i * 197 + 31));
    }
}

static void carve_settlement_main_roads(SubworldMapData& out,
                                        const CellContext& ctx,
                                        const RoadAxisSet& axes,
                                        int center,
                                        std::uint32_t seed) {
    for (int i = 0; i < axes.count; ++i) {
        int tx, ty;
        edge_anchor_target(ctx, axes.dx[std::size_t(i)], axes.dy[std::size_t(i)], tx, ty);
        carve_organic_road(out, center, center, tx, ty,
                           seed + std::uint32_t(i * 197 + 29));
    }
}

static void carve_city_branch_streets(SubworldMapData& out, Rng& r,
                                      int center, int wallR,
                                      int population, const RoadAxisSet& axes,
                                      std::uint32_t seed) {
    const int branchCount = std::min(72, std::max(16, population / 180));
    const float maxR = std::max(24.0f, float(wallR) - 14.0f);
    for (int i = 0; i < branchCount; ++i) {
        const float axis = axes.angle[std::size_t(i % std::max(1, axes.count))];
        const float startDist = 24.0f + r.next_f01() * std::max(1.0f, maxR * 0.45f);
        const float side = (r.next_u32() & 1u) ? 1.0f : -1.0f;
        const float angle = axis + side * (0.55f + r.next_f01() * 0.55f)
            + (r.next_f01() - 0.5f) * 0.30f;
        const float len = 28.0f + r.next_f01() * 72.0f;
        const int sx = int(std::floor(float(center) + std::cos(axis) * startDist));
        const int sy = int(std::floor(float(center) + std::sin(axis) * startDist));
        const int ex = int(std::floor(float(sx) + std::cos(angle) * len));
        const int ey = int(std::floor(float(sy) + std::sin(angle) * len));
        const int dx = ex - center;
        const int dy = ey - center;
        if (ex < 12 || ey < 12 || ex >= kCellSize - 12 || ey >= kCellSize - 12) continue;
        if (dx * dx + dy * dy > int(maxR * maxR)) continue;
        carve_organic_road(out, sx, sy, ex, ey, seed + std::uint32_t(i * 131 + 17));

        if ((i % 3) == 0) {
            const float split = angle + side * (-0.75f + r.next_f01() * 1.5f);
            const float slen = 18.0f + r.next_f01() * 44.0f;
            const int bx = int(std::floor(float(ex) + std::cos(split) * slen));
            const int by = int(std::floor(float(ey) + std::sin(split) * slen));
            const int bdx = bx - center;
            const int bdy = by - center;
            if (bx >= 12 && by >= 12 && bx < kCellSize - 12 && by < kCellSize - 12
                && bdx * bdx + bdy * bdy <= int(maxR * maxR)) {
                carve_organic_road(out, ex, ey, bx, by,
                                   seed + std::uint32_t(i * 131 + 71));
            }
        }
    }
}

static void field_road_junction(int center, int wallR, int fx, int fy,
                                int& jx, int& jy) {
    const int dx = fx - center;
    const int dy = fy - center;
    const int gateR = std::min(kCellSize / 2 - 4, wallR + 6);
    if (std::abs(dx) >= std::abs(dy)) {
        jx = center + (dx >= 0 ? gateR : -gateR);
        jy = center;
    } else {
        jx = center;
        jy = center + (dy >= 0 ? gateR : -gateR);
    }
    jx = std::clamp(jx, 1, kCellSize - 2);
    jy = std::clamp(jy, 1, kCellSize - 2);
}

static void stamp_settlement_wall(SubworldMapData& out, Rng& r,
                                  float radius, int segments,
                                  float roughness, float height,
                                  const RoadAxisSet& gates) {
    constexpr float kPi = 3.14159265f;
    constexpr float kTwoPi = kPi * 2.0f;
    std::array<float, 48> xs{};
    std::array<float, 48> ys{};
    const int segs = std::clamp(segments, 10, int(xs.size()));
    const float cx = float(kCellSize / 2);
    const float cy = float(kCellSize / 2);
    const float phase1 = r.next_f01() * kTwoPi;
    const float phase2 = r.next_f01() * kTwoPi;
    for (int i = 0; i < segs; ++i) {
        const float angle = float(i) * kTwoPi / float(segs);
        const float harmonic = std::sin(angle * 3.0f + phase1) * 0.32f
                             + std::sin(angle * 5.0f + phase2) * 0.18f;
        const float jitter = (r.next_f01() * 2.0f - 1.0f) * radius * roughness * 0.18f;
        const float rr = radius + radius * roughness * harmonic + jitter;
        xs[std::size_t(i)] = cx + std::cos(angle) * rr;
        ys[std::size_t(i)] = cy + std::sin(angle) * rr;
    }
    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, 48> nx = xs;
        std::array<float, 48> ny = ys;
        for (int i = 0; i < segs; ++i) {
            const int prev = (i + segs - 1) % segs;
            const int next = (i + 1) % segs;
            nx[std::size_t(i)] = (xs[std::size_t(prev)] + xs[std::size_t(i)] * 2.0f
                                + xs[std::size_t(next)]) * 0.25f;
            ny[std::size_t(i)] = (ys[std::size_t(prev)] + ys[std::size_t(i)] * 2.0f
                                + ys[std::size_t(next)]) * 0.25f;
        }
        xs = nx;
        ys = ny;
    }
    // Gate spans: a wall arc whose midpoint angle (from the centre) lines up
    // with a main-road axis is left open so the road passes through. This is
    // TS's angle-based gate test — far better than the old "suppress the whole
    // segment if any road tile is nearby", which deleted most of the wall.
    constexpr float kGateHalfArc = 0.16f;  // ~9° each side of a road axis
    auto is_gate_angle = [&](float ang) {
        for (int g = 0; g < gates.count; ++g) {
            float d = ang - gates.angle[std::size_t(g)];
            while (d >  kPi) d -= kTwoPi;
            while (d < -kPi) d += kTwoPi;
            if (std::fabs(d) < kGateHalfArc) return true;
        }
        return false;
    };

    auto tile_protected = [&](float fx, float fy) {
        const int tx = std::clamp(int(std::floor(fx)), 0, kCellSize - 1);
        const int ty = std::clamp(int(std::floor(fy)), 0, kCellSize - 1);
        const std::uint8_t t = out.tiles[std::size_t(ty) * kCellSize + tx];
        return t == TILE_ROAD || t == TILE_SQUARE
            || t == TILE_HOUSE || t == TILE_FIELD;
    };

    for (int i = 0; i < segs; ++i) {
        const int next = (i + 1) % segs;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float x2 = xs[std::size_t(next)];
        const float y2 = ys[std::size_t(next)];
        const float mx = (x1 + x2) * 0.5f;
        const float my = (y1 + y2) * 0.5f;
        const bool gate = is_gate_angle(std::atan2(my - cy, mx - cx));

        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            const int x = int(std::floor(x1 + dx * t));
            const int y = int(std::floor(y1 + dy * t));
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int px = x + ox;
                    const int py = y + oy;
                    if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                    const std::size_t idx = std::size_t(py) * kCellSize + px;
                    const std::uint8_t cur = out.tiles[idx];
                    // Never paint over roads/plaza/houses/fields so the gate
                    // opening (where a road crosses the ring) stays clear.
                    if (cur == TILE_ROAD || cur == TILE_SQUARE
                     || cur == TILE_HOUSE || cur == TILE_FIELD) continue;
                    if (gate) continue;  // leave the gate span unwalled
                    out.tiles[idx] = TILE_WALL;
                    out.trav[idx] = 0;
                }
            }
        }
        // Continuous wall: emit overlapping wall boxes along the WHOLE span,
        // not just one tiny box at the midpoint. A single 1.4-radius box per
        // ~25-tile segment rendered as invisible specks in 3D; walking the
        // span with closely-spaced overlapping boxes makes the ring read as a
        // solid stone wall (the TILE_WALL stamp above is already continuous).
        if (!gate) {
            constexpr float kWallBoxSpacing = 3.0f;   // tiles between boxes
            constexpr float kWallBoxRadius  = 2.2f;   // 4.4-wide → overlaps
            const int boxes = std::max(1, int(dist / kWallBoxSpacing));
            for (int b = 0; b <= boxes; ++b) {
                const float t   = float(b) / float(boxes);
                const float bxp = x1 + dx * t;
                const float byp = y1 + dy * t;
                if (tile_protected(bxp, byp)) continue;  // keep gate openings clear
                out.structures.push_back(
                    Structure{Structure::Wall, bxp, byp, kWallBoxRadius, height});
            }
        }
    }

    // Towers at every other non-gate node (corners of the ring). Rendered as
    // chunkier, taller stone boxes so the perimeter reads as a fortified wall
    // with towers rather than a flat fence.
    const float towerH = height * 1.6f;
    for (int i = 0; i < segs; i += 2) {
        const float nx = xs[std::size_t(i)];
        const float ny = ys[std::size_t(i)];
        if (is_gate_angle(std::atan2(ny - cy, nx - cx))) continue;
        if (tile_protected(nx, ny)) continue;
        out.structures.push_back(
            Structure{Structure::Wall, nx, ny, 3.4f, towerH});
    }
}

static void gen_city(const CellContext& ctx, const std::uint8_t nbFeature[9],
                     SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    clear_decor_tiles(out);

    Rng r(ctx.seed ^ 0xC1C1C1u);
    int center = kCellSize / 2;
    const int population = std::max(50, ctx.landmarkSize);
    int wallR = int(std::min(360.0f,
        std::max(90.0f, 70.0f + std::sqrt(float(population)) * 3.0f)));
    const int ringCount = 1
        + (population >= 2000 ? 1 : 0)
        + (population >= 5000 ? 1 : 0)
        + (population >= 10000 ? 1 : 0)
        + (population >= 20000 ? 1 : 0);
    // Main roads align to neighbouring macro road cells when available.
    const RoadAxisSet axes = settlement_road_axes(nbFeature);
    carve_settlement_main_roads(out, ctx, axes, center, ctx.seed ^ 0x7711AAu);
    carve_city_branch_streets(out, r, center, wallR, population, axes,
                              ctx.seed ^ 0x51A7E11u);
    const int squareSize = std::clamp(5 + population / 5000, 5, 10);
    stamp_rect(out, center - squareSize / 2, center - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    const int keepBase = std::clamp(4 + population / 1500, 6, 16);
    const int keepW = keepBase + int(r.next_u32() % 3u);
    const int keepH = keepBase + int(r.next_u32() % 3u);
    const bool keepPlaced = stamp_landmark_house(out, center - keepW / 2,
                                                 center - keepH / 2 - keepBase / 2 - 2,
                                                 keepW, keepH, 10.0f);

    // Roadside blocks: bounded TS mycelium approximation without dynamic tip queues.
    const int houses = std::min(380,
        std::max(20, int(std::pow(float(population), 0.8f))));
    int placedHouses = keepPlaced ? 1 : 0;
    for (int attempt = 0; placedHouses < houses && attempt < houses * 48; ++attempt) {
        const int minSize = placedHouses < 16 ? 3 : 2;
        const int maxSize = placedHouses < 16 ? 5 : 4;
        if (try_add_roadside_house(out, r, center, float(wallR) - 10.0f,
                                   minSize, maxSize, 5.0f + r.next_f01() * 4.0f)) {
            ++placedHouses;
        }
    }
    stamp_rect(out, center - squareSize / 2, center - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    const int targetFields = std::min(80, std::max(6, population / 50));
    std::array<int, 80> fieldX{};
    std::array<int, 80> fieldY{};
    const float outerWallR = float(wallR) + float(ringCount - 1) * 8.0f;
    int fields = 0;
    for (int attempt = 0; fields < targetFields && attempt < targetFields * 18; ++attempt) {
        const float a = r.next_f01() * 6.2831853f;
        const float outer = float(kCellSize) * 0.48f;
        const float d = outerWallR * 1.08f
            + r.next_f01() * std::max(1.0f, outer - outerWallR * 1.08f);
        const int fw = 8 + int(r.next_u32() % 15u);
        const int fh = 6 + int(r.next_u32() % 13u);
        const int fx = int(std::floor(float(center) + std::cos(a) * d));
        const int fy = int(std::floor(float(center) + std::sin(a) * d));
        if (add_field_rect(out, fx, fy, fw, fh)) {
            fieldX[std::size_t(fields)] = fx;
            fieldY[std::size_t(fields)] = fy;
            ++fields;
        }
    }
    const int fieldRoads = std::min(fields, std::max(2, population / 1500));
    for (int i = 0; i < fieldRoads; ++i) {
        const int idx = (i * fields) / std::max(1, fieldRoads);
        int jx, jy;
        field_road_junction(center, int(std::ceil(outerWallR)),
                            fieldX[std::size_t(idx)], fieldY[std::size_t(idx)],
                            jx, jy);
        carve_organic_road(out, jx, jy, fieldX[std::size_t(idx)], fieldY[std::size_t(idx)],
                           ctx.seed + std::uint32_t(0xF00Du + i * 97));
    }

    // Stamp walls after every road cut so gate tiles and structure records agree.
    for (int ring = 0; ring < ringCount; ++ring) {
        const float fraction = float(ring + 1) / float(ringCount);
        stamp_settlement_wall(out, r, float(wallR) * fraction + float(ring) * 8.0f,
                              22 + ring * 8, 0.12f + float(ring) * 0.025f,
                              10.0f + float(ring) * 2.0f, axes);
    }

    // Trees outside the wall ring — TS-faithful stitch.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ wallR + 16, ctx.seed);
}

static void gen_village(const CellContext& ctx, const std::uint8_t nbFeature[9],
                        SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    clear_decor_tiles(out);
    Rng r(ctx.seed ^ 0xABCDEFu);
    int center = kCellSize / 2;
    const int population = std::max(10, ctx.landmarkSize);
    const RoadAxisSet axes = settlement_road_axes(nbFeature);
    carve_settlement_main_roads(out, ctx, axes, center, ctx.seed ^ 0xA115EEDu);
    const int squareSize = 3 + int(r.next_u32() % 4u);
    stamp_rect(out, center - squareSize / 2, center - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    const float settleR = std::min(float(kCellSize) * 0.07f,
                                   30.0f + std::sqrt(float(population)) * 3.0f);
    // Internal village streets: short radials from the centre so houses have
    // roads to line even when no main road reaches a neighbour. Bounded to the
    // village core (never reaches a cell edge -> never a road "into the void").
    // Deterministic from the cell seed so it does not perturb the r-stream that
    // drives field / wall placement.
    {
        const int spokes = std::clamp(4 + population / 40, 4, 8);
        const float reach = std::max(20.0f, settleR * 0.95f);
        const float base = float(ctx.seed & 0xFFFFu) / 65535.0f * 6.2831853f;
        for (int i = 0; i < spokes; ++i) {
            const float a = base + (float(i) / float(spokes)) * 6.2831853f;
            const int ex = int(std::floor(float(center) + std::cos(a) * reach));
            const int ey = int(std::floor(float(center) + std::sin(a) * reach));
            carve_organic_road(out, center, center, ex, ey,
                               ctx.seed + std::uint32_t(0x51EE7u + i * 41));
        }
    }
    const int houses = std::min(120, std::max(1, population / 5));
    int placedHouses = 0;
    for (int attempt = 0; placedHouses < houses && attempt < houses * 64; ++attempt) {
        if (try_add_roadside_house(out, r, center, settleR, 2, 3, 5.0f)) {
            ++placedHouses;
        }
    }

    const float fieldInner = settleR + 5.0f;
    const float fieldOuter = fieldInner
        + std::min(float(kCellSize) * 0.08f, 30.0f + float(population) * 0.3f);
    stamp_rect(out, center - squareSize / 2, center - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);
    const int targetFields = std::min(40, std::max(2, population / 20));
    std::array<int, 40> fieldX{};
    std::array<int, 40> fieldY{};
    int fields = 0;
    for (int attempt = 0; fields < targetFields && attempt < targetFields * 25; ++attempt) {
        const float a = r.next_f01() * 6.2831853f;
        const float d = fieldInner + r.next_f01() * std::max(1.0f, fieldOuter - fieldInner);
        const int fw = 8 + int(r.next_u32() % 13u);
        const int fh = 6 + int(r.next_u32() % 11u);
        const int fx = int(std::floor(float(center) + std::cos(a) * d));
        const int fy = int(std::floor(float(center) + std::sin(a) * d));
        if (add_field_rect(out, fx, fy, fw, fh)) {
            fieldX[std::size_t(fields)] = fx;
            fieldY[std::size_t(fields)] = fy;
            ++fields;
        }
    }
    const int farmRoads = std::min(fields, std::max(1, population / 80));
    for (int i = 0; i < farmRoads; ++i) {
        const int idx = (i * fields) / std::max(1, farmRoads);
        carve_organic_road(out, center, center,
                           fieldX[std::size_t(idx)], fieldY[std::size_t(idx)],
                           ctx.seed + std::uint32_t(0xBEEFu + i * 53));
    }
    stamp_rect(out, center - squareSize / 2, center - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    float clearR = std::max(90.0f, settleR + 12.0f);
    if (population >= 50) {
        const float wallR = std::max(15.0f, settleR + 6.0f);
        stamp_settlement_wall(out, r, wallR,
                              std::max(10, 12 + int(wallR / 8.0f)),
                              0.12f, 6.0f, axes);
        clearR = wallR * 1.05f;
    }

    // Trees scatter outside the village core.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ int(clearR), ctx.seed);
}

static void gen_mountain(const CellContext& ctx, const std::uint8_t nbFeature[9],
                         SubworldMapData& out) {
    // Lay biome ground; the heightmap (mountain feature amp + ridge
    // multifractal) does the actual mountain shaping. Slope-driven rock
    // and snow overlay in the terrain shader exposes rock on steep faces.
    // Sparse trees from the universal scatter — biome density already low.
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

// Water cells: lay biome ground + run the universal tree scatter (water
// biome's treeDensity = 0 → no-op). Final water/land height clamping is a
// dispatch-level post-pass because later generators and road smoothing can
// still alter tile classes and heights.
static void gen_water(const CellContext& ctx, SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

// Spire landmark: tower footprint plus scorch and crater tiles.
static void gen_spire(const CellContext& ctx, SubworldMapData& out) {
    constexpr int kTowerDiameter = 14;
    constexpr int kTowerHeight = 96;
    constexpr int kScorchRadius = 90;
    constexpr int kCraterInner = 18;
    constexpr int kCraterRing = 22;

    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();

    const int cx = kCellSize / 2;
    const int cy = kCellSize / 2;
    const int scorch2 = kScorchRadius * kScorchRadius;
    for (int y = cy - kScorchRadius; y <= cy + kScorchRadius; ++y) {
        if (y < 0 || y >= kCellSize) continue;
        for (int x = cx - kScorchRadius; x <= cx + kScorchRadius; ++x) {
            if (x < 0 || x >= kCellSize) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 > scorch2) continue;
            const float dist = std::sqrt(float(d2));
            const float falloff = 1.0f - dist / float(kScorchRadius);
            const float n = smooth_noise01(float(x) * 0.08f, float(y) * 0.08f, ctx.seed);
            if (n < falloff * 0.55f) {
                out.tiles[std::size_t(y) * kCellSize + x] = TILE_ROCK;
            }
        }
    }

    const int outer = kCraterInner + kCraterRing;
    const int inner2 = kCraterInner * kCraterInner;
    const int outer2 = outer * outer;
    for (int y = cy - outer; y <= cy + outer; ++y) {
        if (y < 0 || y >= kCellSize) continue;
        for (int x = cx - outer; x <= cx + outer; ++x) {
            if (x < 0 || x >= kCellSize) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 < inner2 || d2 > outer2) continue;
            const float n = smooth_noise01(float(x) * 0.18f + 200.0f,
                                           float(y) * 0.18f + 200.0f,
                                           ctx.seed);
            out.tiles[std::size_t(y) * kCellSize + x] = n < 0.55f ? TILE_ROCK : TILE_SQUARE;
        }
    }

    out.structures.push_back(
        Structure{Structure::Wall, float(cx), float(cy),
                  float(kTowerDiameter) * 0.5f, float(kTowerHeight)});
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ kScorchRadius, ctx.seed);
}

static bool stamp_ruin_wall_line(SubworldMapData& out,
                                 float x1, float y1, float x2, float y2) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
    bool segmentSuppressed = false;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int x = int(std::floor(x1 + dx * t));
        const int y = int(std::floor(y1 + dy * t));
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                const int px = x + ox;
                const int py = y + oy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                const std::size_t idx = std::size_t(py) * kCellSize + px;
                if (out.tiles[idx] == TILE_ROAD || out.tiles[idx] == TILE_SQUARE
                 || out.tiles[idx] == TILE_HOUSE || out.tiles[idx] == TILE_FIELD) {
                    segmentSuppressed = true;
                    continue;
                }
                out.tiles[idx] = TILE_WALL;
                out.trav[idx] = 0;
            }
        }
    }
    return segmentSuppressed;
}

static void build_ruin_wall(SubworldMapData& out,
                            Rng& r,
                            float radius,
                            int segments,
                            float roughness) {
    constexpr float kPi = 3.14159265f;
    constexpr float kTwoPi = kPi * 2.0f;
    std::array<float, 16> xs{};
    std::array<float, 16> ys{};
    const float cx = float(kCellSize / 2);
    const float cy = float(kCellSize / 2);
    const float phase1 = r.next_f01() * kTwoPi;
    const float phase2 = r.next_f01() * kTwoPi;
    for (int i = 0; i < segments; ++i) {
        const float angle = float(i) * kTwoPi / float(segments);
        const float harmonic = std::sin(angle * 3.0f + phase1) * 0.32f
                             + std::sin(angle * 5.0f + phase2) * 0.18f;
        const float jitter = (r.next_f01() * 2.0f - 1.0f) * radius * roughness * 0.18f;
        const float rr = radius + radius * roughness * harmonic + jitter;
        xs[std::size_t(i)] = cx + std::cos(angle) * rr;
        ys[std::size_t(i)] = cy + std::sin(angle) * rr;
    }
    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, 16> nx = xs;
        std::array<float, 16> ny = ys;
        for (int i = 0; i < segments; ++i) {
            const int prev = (i + segments - 1) % segments;
            const int next = (i + 1) % segments;
            nx[std::size_t(i)] = (xs[std::size_t(prev)] + xs[std::size_t(i)] * 2.0f
                                + xs[std::size_t(next)]) * 0.25f;
            ny[std::size_t(i)] = (ys[std::size_t(prev)] + ys[std::size_t(i)] * 2.0f
                                + ys[std::size_t(next)]) * 0.25f;
        }
        xs = nx;
        ys = ny;
    }
    for (int i = 0; i < segments; ++i) {
        const int next = (i + 1) % segments;
        if ((r.next_u32() % 5u) == 0u) continue;
        const bool suppressed = stamp_ruin_wall_line(
            out, xs[std::size_t(i)], ys[std::size_t(i)],
            xs[std::size_t(next)], ys[std::size_t(next)]);
        if (!suppressed) {
            const float mx = (xs[std::size_t(i)] + xs[std::size_t(next)]) * 0.5f;
            const float my = (ys[std::size_t(i)] + ys[std::size_t(next)]) * 0.5f;
            out.structures.push_back(Structure{Structure::Wall, mx, my, 1.0f, 5.0f});
        }
    }
}

static void gen_ruin(const CellContext& ctx, const std::uint8_t nbFeature[9],
                     SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    Rng r(ctx.seed ^ 0xA8A1A11u);
    const int center = kCellSize / 2;
    const RoadDirSet dirs = connected_road_dirs(nbFeature);
    carve_landmark_anchor_roads(out, ctx, dirs, center, ctx.seed ^ 0xA8A1EADu);

    const int difficulty = std::max(1, ctx.landmarkSize);
    const int rings = std::max(1, std::min(3, (difficulty + 2) / 3));
    for (int ring = 0; ring < rings; ++ring) {
        const float radius = float(kCellSize) * (0.12f + float(ring) * 0.10f);
        const int segments = 10 + int(r.next_u32() % 7u);
        build_ruin_wall(out, r, radius, segments, 0.25f);
    }

    const int squareSize = 5 + int(r.next_u32() % 5u);
    const int sx = center - squareSize / 2;
    const int sy = center - squareSize / 2;
    for (int y = 0; y < squareSize; ++y) {
        for (int x = 0; x < squareSize; ++x) {
            if (r.next_f01() < 0.8f) {
                out.tiles[std::size_t(sy + y) * kCellSize + sx + x] = TILE_SQUARE;
            }
        }
    }

    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

// Organic road raster used by roads and settlements. Endpoint damping keeps
// edge contacts deterministic while the interior bend gives TS-style shape.
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
                if (cur == TILE_HOUSE) continue;
                out.tiles[idx] = TILE_ROAD;
                out.trav [idx] = 1;
            }
        }
    }
}

// Endpoint on the cell edge that matches the neighbour's matching point —
// midpoint of the shared edge for orthogonal neighbours, the shared
// corner for diagonals. Symmetric: both cells compute the same point.
static std::uint32_t symmetric_edge_seed(const CellContext& ctx, int dx, int dy) {
    const std::int64_t a = std::int64_t(ctx.cx) * 100003 + std::int64_t(ctx.cy);
    const std::int64_t b = std::int64_t(ctx.cx + dx) * 100003
                         + std::int64_t(ctx.cy + dy);
    const std::int64_t lo = std::min(a, b);
    const std::int64_t hi = std::max(a, b);
    const std::uint32_t seed =
        std::uint32_t(lo * 374761393ll) ^ std::uint32_t(hi * 1274126177ll);
    return seed == 0u ? 1u : seed;
}

static void edge_anchor_target(const CellContext& ctx, int dx, int dy,
                               int& ox, int& oy) {
    Rng r(symmetric_edge_seed(ctx, dx, dy));
    const int edgePos = int(std::floor(float(kCellSize)
        * (0.35f + r.next_f01() * 0.3f)));
    if (dx == 0) {
        ox = std::clamp(edgePos, 1, kCellSize - 2);
        oy = dy < 0 ? 1 : kCellSize - 2;
    } else if (dy == 0) {
        ox = dx > 0 ? kCellSize - 2 : 1;
        oy = std::clamp(edgePos, 1, kCellSize - 2);
    } else {
        ox = dx > 0 ? kCellSize - 2 : 1;
        oy = dy > 0 ? kCellSize - 2 : 1;
    }
}

static bool is_road_feature(std::uint8_t f) {
    return f == FT_Road || f == FT_DirtRoad;
}

static void add_bridge_segment(SubworldMapData& out, int ax, int ay, int bx, int by) {
    constexpr float kBridgeDeckHeight = 3.0f;
    const float dx = float(bx - ax);
    const float dy = float(by - ay);
    const float length = std::sqrt(dx * dx + dy * dy);
    out.structures.push_back(
        Structure{Structure::Bridge,
                  (float(ax) + float(bx)) * 0.5f,
                  (float(ay) + float(by)) * 0.5f,
                  length * 0.5f,
                  kBridgeDeckHeight});
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
        // Isolated road cell (no road-bearing neighbour). TS carves nothing
        // here. A visible stub produced scattered orphan road fragments all
        // over the map, so leave the cell road-free.
    } else if (nConnected == 1) {
        // Single connection — TS road-generator carves from the edge anchor
        // to the cell CENTRE and stops (a natural road terminus). Carving on
        // to the opposite edge instead pushed roads into neighbour cells that
        // carry no road feature, producing the fragmented-seam look.
        int ax, ay;
        edge_anchor_target(ctx, connectedDx[0], connectedDy[0], ax, ay);
        carve_organic_road(out, ax, ay, cx, cy, ctx.seed);
    } else if (nConnected == 2) {
        // Through-road: connect the two endpoints directly.
        int ax, ay, bx, by;
        edge_anchor_target(ctx, connectedDx[0], connectedDy[0], ax, ay);
        edge_anchor_target(ctx, connectedDx[1], connectedDy[1], bx, by);
        carve_organic_road(out, ax, ay, bx, by, ctx.seed);
    } else {
        // 3+ directions: hub at centre, radial arms to each neighbour.
        for (int i = 0; i < nConnected; ++i) {
            int ex, ey;
            edge_anchor_target(ctx, connectedDx[i], connectedDy[i], ex, ey);
            carve_organic_road(out, cx, cy, ex, ey, ctx.seed + std::uint32_t(i) * 17u);
        }
    }

    if (ctx.biome == Biome::Water) {
        if (nConnected == 1) {
            int ax, ay;
            edge_anchor_target(ctx, connectedDx[0], connectedDy[0], ax, ay);
            add_bridge_segment(out, ax, ay, cx, cy);
        } else if (nConnected == 2) {
            int ax, ay, bx, by;
            edge_anchor_target(ctx, connectedDx[0], connectedDy[0], ax, ay);
            edge_anchor_target(ctx, connectedDx[1], connectedDy[1], bx, by);
            add_bridge_segment(out, ax, ay, bx, by);
        } else if (nConnected > 2) {
            for (int i = 0; i < nConnected; ++i) {
                int ex, ey;
                edge_anchor_target(ctx, connectedDx[i], connectedDy[i], ex, ey);
                add_bridge_segment(out, cx, cy, ex, ey);
            }
        }
    }

    // Vegetation around the road — biome-typical scatter, no urban clear.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 0, ctx.seed);
}

} // namespace sm::sub
