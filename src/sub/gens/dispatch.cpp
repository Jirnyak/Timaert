#include "sub/gens/dispatch.h"
#include "macro/tree_layer.h"
#include "sub/base_generator.h"
#include "sub/city_layout.h"
#include "core/rng.h"
#include "sub/height.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace sm::sub {

// Forward decls for per-mode generators (each defined below).
static void gen_open      (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_forest    (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_swamp     (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_city      (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_village   (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_mountain  (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_water     (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_road      (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_field     (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], const float nbFertility[9], SubworldMapData&);
static void gen_spire     (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void gen_ruin      (const CellContext&, const Biome nbBiome[9], const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData&);
static void scatter_forest_glades(const CellContext&, SubworldMapData&);
static void scatter_field_crops(const CellContext&, SubworldMapData&);
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
    // Features come before the biome base: what men built on the cell decides
    // what the cell IS underfoot, then the biome fills in the terrain, then
    // trees compose on top (a forested peak is a Mountain cell with scattered
    // trees, not Forest mode). Features never fight each other — the layer is
    // one byte per cell, a road OR a field, settled at macro stamp time
    // (peasants plough around the road, stamp_field_features).
    if (feature == FT_Road)     return SubworldMode::Road;
    if (feature == FT_DirtRoad) return SubworldMode::Road;
    if (feature == FT_Field)    return SubworldMode::Field;
    if (ctx.biome == Biome::Mountain) return SubworldMode::Mountain;
    if (ctx.biome == Biome::Water)    return SubworldMode::Water;
    if (ctx.biome == Biome::Swamp)    return SubworldMode::Swamp;
    // Forest is a COUNT class, not a feature: massif interiors (tree count ≥
    // half the golden max) generate as Forest; edges/ambience stay open with
    // their trees composed on top by the count-driven scatter.
    if (is_forest_cell(ctx.treeCount)) return SubworldMode::Forest;
    return SubworldMode::Grassland;
}

void dispatch_generate(const CellContext& ctx, const float nbHeights[9],
                       const Biome nbBiome[9],
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out,
                       const CellLandmarkKind* nbLandmark,
                       const int* nbTreeCount,
                       const float* nbFertility) {
    CellContext safeCtx = ctx;
    safeCtx.feature = FeatureLayer::decode(std::uint8_t(ctx.feature));
    std::uint8_t safeFeature[9]{};
    for (int i = 0; i < 9; ++i) {
        safeFeature[i] = std::uint8_t(FeatureLayer::decode(nbFeature[i]));
    }

    // Per-cell fertility ring for the field-plot module. Missing array or a
    // negative entry = "unknown" — fall back to the centre cell's own value
    // (synthetic/test contexts).
    float safeFertility[9];
    for (int i = 0; i < 9; ++i) {
        safeFertility[i] = (nbFertility && nbFertility[i] >= 0.0f)
            ? nbFertility[i] : ctx.fertility01;
    }

    // Per-cell tree counts (macro TreeLayer). A missing array or a negative
    // entry means "unknown" (synthetic/test contexts without the layer) and
    // falls back to the ring cell's biome ambience — bare contexts have no
    // massif mask, so the forest term is zero by definition.
    int safeTreeCount[9];
    for (int i = 0; i < 9; ++i) {
        if (nbTreeCount && nbTreeCount[i] >= 0) {
            safeTreeCount[i] = std::min(nbTreeCount[i], kMaxTreesPerCell);
        } else {
            safeTreeCount[i] = int(derived_tree_count(nbBiome[i], 0.0f));
        }
    }

    // Universal terrain flattening: one TerrainMod per neighbour, resolved
    // from its effective landmark + feature (roads/settlements calm the
    // terrain they stand on — see base_generator.h). Without neighbour
    // landmark data the centre cell still flattens itself.
    TerrainMod nbMods[9]{};
    for (int i = 0; i < 9; ++i) {
        const CellLandmarkKind lm = nbLandmark
            ? nbLandmark[i]
            : (i == 4 ? effective_landmark(safeCtx) : CellLandmarkKind::None);
        nbMods[i] = terrain_mod_for(lm, FeatureType(safeFeature[i]));
    }

    out.heightmap.clear();
    generate_heightmap(out.heightmap, kCellSize, nbHeights, nbBiome,
                       safeCtx.biome, safeCtx.seed,
                       safeCtx.cx * kCellSize, safeCtx.cy * kCellSize,
                       nbMods);
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.trav.assign (std::size_t(kCellSize) * kCellSize, 1);
    out.structures.clear();
    out.waterLevel = WATER_LEVEL;

    switch (resolve_mode(safeCtx)) {
        case SubworldMode::Forest:    gen_forest   (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::City:      gen_city     (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Village:   gen_village  (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Mountain:  gen_mountain (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Water:     gen_water    (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Swamp:     gen_swamp    (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Road:      gen_road     (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Field:     gen_field    (safeCtx, nbBiome, safeFeature, safeTreeCount, safeFertility, out); break;
        case SubworldMode::Spire:     gen_spire    (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Ruin:      gen_ruin     (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
        case SubworldMode::Grassland:
        case SubworldMode::Open:
        default:                      gen_open     (safeCtx, nbBiome, safeFeature, safeTreeCount, out); break;
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
    if (out.heightmap.size() != std::size_t(kCellSize) * kCellSize) return;

    constexpr float kShoreTop = WATER_LEVEL + 0.022f;
    constexpr int kShoreRadius = 6;
    const int w = kCellSize;
    const int hgt = kCellSize;

    std::vector<std::uint8_t> water(out.heightmap.size(), 0);
    std::vector<std::uint8_t> nearX(out.heightmap.size(), 0);
    std::vector<std::uint8_t> nearWater(out.heightmap.size(), 0);

    for (std::size_t i = 0; i < out.heightmap.size(); ++i) {
        water[i] = out.heightmap[i] < WATER_LEVEL ? 1u : 0u;
    }

    std::vector<int> prefix(std::size_t(std::max(w, hgt)) + 1u, 0);
    for (int y = 0; y < hgt; ++y) {
        const int row = y * w;
        prefix[0] = 0;
        for (int x = 0; x < w; ++x) {
            prefix[std::size_t(x + 1)] = prefix[std::size_t(x)]
                + int(water[std::size_t(row + x)]);
        }
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - kShoreRadius);
            const int x1 = std::min(w, x + kShoreRadius + 1);
            nearX[std::size_t(row + x)] =
                prefix[std::size_t(x1)] > prefix[std::size_t(x0)] ? 1u : 0u;
        }
    }
    for (int x = 0; x < w; ++x) {
        prefix[0] = 0;
        for (int y = 0; y < hgt; ++y) {
            prefix[std::size_t(y + 1)] = prefix[std::size_t(y)]
                + int(nearX[std::size_t(y) * w + x]);
        }
        for (int y = 0; y < hgt; ++y) {
            const int y0 = std::max(0, y - kShoreRadius);
            const int y1 = std::min(hgt, y + kShoreRadius + 1);
            nearWater[std::size_t(y) * w + x] =
                prefix[std::size_t(y1)] > prefix[std::size_t(y0)] ? 1u : 0u;
        }
    }

    for (std::size_t i = 0; i < out.heightmap.size(); ++i) {
        const std::uint8_t tile = out.tiles[i];
        if (preserves_authored_surface(tile)) continue;

        const float h = out.heightmap[i];
        if (water[i]) {
            out.tiles[i] = TILE_WATER;
        } else if (h < kShoreTop && nearWater[i]) {
            out.tiles[i] = TILE_SHORE;
        } else if (tile == TILE_WATER || tile == TILE_SHORE) {
            out.tiles[i] = TILE_GRASS;
        }
    }
}

// ── Generators ────────────────────────────────────────────────

// Plain biome ground tiles + stitched cross-cell tree scatter.
static void gen_open(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                     const int nbTreeCount[9],
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
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}

static void gen_forest(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                       const int nbTreeCount[9],
                       SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
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

static void gen_swamp(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                      const int nbTreeCount[9],
                      SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
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

// Flatten the heightmap under a building footprint to its mean elevation so the
// structure box — which the 3D renderer seats at a SINGLE centre height
// (`sample_height_m(s.x, s.y)`) — sits on level ground instead of poking through
// / floating above a tilted, noisy slope (the "towns on cliffs" report). This is
// a genuinely different operation from the road smoother: a road corridor stays
// harmonic (curvature-free but slope-following), while a building floor is dead
// level. Callers guarantee footprints are strictly cell-interior (≥2-tile margin
// from every edge), so the pad reads/writes only interior tiles and is therefore
// seam-IDENTICAL whether baked per-cell or inherited by the 3×3 composite (which
// just memcpy's the per-cell heightmaps). Using the footprint MEAN balances cut
// vs fill so the pad seats naturally in the relief (neither a mesa nor a pit).
static void flatten_footprint(SubworldMapData& out, int x, int y, int w, int h) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kCellSize, x + w);
    const int y1 = std::min(kCellSize, y + h);
    if (x1 <= x0 || y1 <= y0) return;
    double sum = 0.0;
    int cnt = 0;
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            sum += out.heightmap[std::size_t(yy) * kCellSize + xx];
            ++cnt;
        }
    }
    if (cnt == 0) return;
    const float level = float(sum / double(cnt));
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            out.heightmap[std::size_t(yy) * kCellSize + xx] = level;
        }
    }
}

// Oriented house: the universal placement primitive. Stamps the ROTATED
// footprint (every tile whose centre falls inside the yawed box, slightly
// fattened so the tile stamp fully covers the solid), flattens exactly those
// cells to their mean (same cut-vs-fill pad rationale as flatten_footprint),
// and emits ONE oriented Structure record — independent width/length/yaw, the
// silhouette the 3D pass draws and the collision index blocks with. Clearance
// is checked on the footprint's AABB plus a 1-tile ring (conservative for a
// rotated box, which only costs a few rejected attempts).
static bool add_house_obb(SubworldMapData& out, float cx, float cy,
                          float hx, float hy, float yaw, float height,
                          bool requireClear) {
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    const float ex = std::fabs(hx * cs) + std::fabs(hy * sn);
    const float ey = std::fabs(hx * sn) + std::fabs(hy * cs);
    const int x0 = int(std::floor(cx - ex));
    const int y0 = int(std::floor(cy - ey));
    const int x1 = int(std::ceil(cx + ex));
    const int y1 = int(std::ceil(cy + ey));
    if (x0 < 2 || y0 < 2 || x1 >= kCellSize - 2 || y1 >= kCellSize - 2) {
        return false;
    }
    if (requireClear) {
        for (int yy = y0 - 1; yy <= y1 + 1; ++yy) {
            for (int xx = x0 - 1; xx <= x1 + 1; ++xx) {
                const std::uint8_t t = out.tiles[std::size_t(yy) * kCellSize + xx];
                if (t == TILE_ROAD || t == TILE_HOUSE || t == TILE_WALL
                 || t == TILE_SQUARE || t == TILE_FIELD) {
                    return false;
                }
            }
        }
    }
    auto inside = [&](float px, float py) {
        const float dx = px - cx;
        const float dy = py - cy;
        const float lx = dx * cs + dy * sn;
        const float ly = -dx * sn + dy * cs;
        return std::fabs(lx) <= hx + 0.35f && std::fabs(ly) <= hy + 0.35f;
    };
    double sum = 0.0;
    int cnt = 0;
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            if (!inside(float(xx) + 0.5f, float(yy) + 0.5f)) continue;
            sum += out.heightmap[std::size_t(yy) * kCellSize + xx];
            ++cnt;
        }
    }
    if (cnt == 0) return false;
    const float level = float(sum / double(cnt));
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            if (!inside(float(xx) + 0.5f, float(yy) + 0.5f)) continue;
            const std::size_t idx = std::size_t(yy) * kCellSize + xx;
            out.tiles[idx] = TILE_HOUSE;
            out.trav[idx] = 0;
            out.heightmap[idx] = level;
        }
    }
    Structure s{};
    s.kind = Structure::House;
    s.x = cx;
    s.y = cy;
    s.radius = std::max(hx, hy);
    s.height = height;
    s.yaw = yaw;
    s.hx = hx;
    s.hy = hy;
    out.structures.push_back(s);
    return true;
}

static bool stamp_landmark_house(SubworldMapData& out, Rng& r,
                                 float cx, float cy, int w, int h,
                                 float height) {
    // The keep gets a modest random lean — enough to break the axis-aligned
    // grid look without turning the citadel diagonal to its own plaza.
    const float yaw = (r.next_f01() * 2.0f - 1.0f) * 0.35f;
    return add_house_obb(out, cx, cy, float(w) * 0.5f, float(h) * 0.5f,
                         yaw, height, /*requireClear=*/false);
}

static bool try_add_roadside_house(SubworldMapData& out, Rng& r,
                                   int center, float maxRadius,
                                   int minSize, int maxSize,
                                   float height) {
    const int radius = int(std::floor(maxRadius));
    if (radius <= 0) return false;
    const int span = radius * 2 + 1;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const int hxT = center + int(r.next_u32() % std::uint32_t(span)) - radius;
        const int hyT = center + int(r.next_u32() % std::uint32_t(span)) - radius;
        const int dx = hxT - center;
        const int dy = hyT - center;
        if (dx * dx + dy * dy > radius * radius) continue;
        if (!has_tile_near(out, hxT, hyT, 10, TILE_ROAD)) continue;
        // Independent continuous width / length and a free orientation: houses
        // stop being one quantised square. Width and length draw from the same
        // band, so proportions range from square to ~1:2 barns.
        const float sizeRange = float(std::max(0, maxSize - minSize));
        const float w = float(minSize) + r.next_f01() * sizeRange;
        const float h = float(minSize) + r.next_f01() * sizeRange;
        const float yaw = r.next_f01() * 3.14159265f;
        if (add_house_obb(out, float(hxT), float(hyT),
                          w * 0.5f, h * 0.5f, yaw, height,
                          /*requireClear=*/true)) {
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

// Radial-concentric city street plan (see sub/city_layout.h). Replaces the old
// centre-rooted starburst — which piled all road density (and therefore the
// road-gated houses) into the middle and fanned everything due east when no
// neighbour carried a road. Here the network is:
//   * AVENUES  — radial roads from the plaza (centre) to the rim, evenly spaced
//                over the full circle from a seed-derived base rotation, so a
//                city is symmetric in every direction regardless of neighbours;
//   * RINGS    — concentric ring roads (polygonal, subdivided so they read as
//                round) tying the avenues together into blocks and spreading
//                circumferential density that counteracts the radial pinch;
//   * STREETS  — short frontage streets fanning tangentially from every
//                avenue×ring node so houses line roads across the whole
//                footprint, not just downtown.
// Uses its OWN RNG seeded off `seed` (like gen_village's internal streets) so it
// never perturbs the shared r-stream driving keep / house / field / wall placement.
static void carve_city_streets(SubworldMapData& out,
                               int center, int wallR, int population,
                               std::uint32_t seed) {
    const CityLayout& L = city_layout();
    const float usableR = std::max(24.0f, float(wallR) - L.streetWallInset);
    const int avenues        = city_avenues(population);
    const int rings          = city_rings(population);
    const int streetsPerNode = city_streets_per_node(population);
    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kHalfPi = 1.57079632679f;

    Rng r(seed == 0u ? 1u : seed);
    const float baseRot = r.next_f01() * kTwoPi;  // cities don't all align

    // Radial avenues: plaza → rim.
    for (int a = 0; a < avenues; ++a) {
        const float ang = baseRot + (float(a) / float(avenues)) * kTwoPi;
        const int ex = int(std::floor(float(center) + std::cos(ang) * usableR));
        const int ey = int(std::floor(float(center) + std::sin(ang) * usableR));
        carve_organic_road(out, center, center, ex, ey,
                           seed + std::uint32_t(a * 131 + 17));
    }

    // Concentric ring roads: walk a subdivided polygon at each ring radius and
    // connect successive nodes with organic segments (approximates a circle;
    // more sides than avenues so a small city's ring still reads as round).
    const int ringSegments = std::max(avenues, 12);
    for (int ring = 0; ring < rings; ++ring) {
        const float rr = city_ring_radius(ring, rings, usableR);
        int px = 0, py = 0;
        for (int s = 0; s <= ringSegments; ++s) {
            const float ang = baseRot + (float(s) / float(ringSegments)) * kTwoPi;
            const int nx = int(std::floor(float(center) + std::cos(ang) * rr));
            const int ny = int(std::floor(float(center) + std::sin(ang) * rr));
            if (s > 0) {
                carve_organic_road(out, px, py, nx, ny,
                                   seed + std::uint32_t(ring * 977 + s * 37 + 5));
            }
            px = nx; py = ny;
        }
    }

    // Local frontage streets: from every avenue×ring node, fan a couple of short
    // streets roughly tangential (alternating sides) so house frontage spreads
    // across the footprint rather than only along the radials.
    for (int a = 0; a < avenues; ++a) {
        const float ang = baseRot + (float(a) / float(avenues)) * kTwoPi;
        for (int ring = 0; ring < rings; ++ring) {
            const float rr = city_ring_radius(ring, rings, usableR);
            const int nx = int(std::floor(float(center) + std::cos(ang) * rr));
            const int ny = int(std::floor(float(center) + std::sin(ang) * rr));
            for (int k = 0; k < streetsPerNode; ++k) {
                const float side = (k % 2 == 0) ? 1.0f : -1.0f;
                const float dir = ang + side * (kHalfPi * 0.7f + r.next_f01() * 0.6f);
                const float len = L.streetLenMin
                    + r.next_f01() * (L.streetLenMax - L.streetLenMin);
                const int ex = int(std::floor(float(nx) + std::cos(dir) * len));
                const int ey = int(std::floor(float(ny) + std::sin(dir) * len));
                const int dx = ex - center;
                const int dy = ey - center;
                if (ex < 12 || ey < 12 || ex >= kCellSize - 12 || ey >= kCellSize - 12)
                    continue;
                if (dx * dx + dy * dy > int(usableR * usableR)) continue;
                carve_organic_road(out, nx, ny, ex, ey,
                                   seed + std::uint32_t(a * 613 + ring * 71 + k * 17 + 3));
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
        // Ring noise amplitudes live in sub/city_layout.h: the citizen
        // populator derives the ring's worst INWARD excursion from them
        // (wall_inner_bound) so nobody is placed in a dip of the wall — i.e.
        // outside their own town. Same model for both rings; one authority.
        const float harmonic =
            std::sin(angle * 3.0f + phase1) * kSettlementWallRing.harmonic3Amp
          + std::sin(angle * 5.0f + phase2) * kSettlementWallRing.harmonic5Amp;
        const float jitter = (r.next_f01() * 2.0f - 1.0f) * radius * roughness
                           * kSettlementWallRing.jitterAmp;
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
    // ── Gates: LINEAR width, not an angular arc. The old ±0.16 rad midpoint
    // test scaled the opening with the ring radius (~64 tiles across at
    // wallR 200 — a breach, not a gate). A gate is now the fixed-width
    // corridor where a main-road axis crosses the ring: perpendicular
    // distance to the outbound axis ray under kGateHalfWidth tiles. ──
    constexpr float kGateHalfWidth = 4.0f;  // opening half-width, tiles
    // Half-thickness from the same ring model the populator reads, so its
    // "inside the walls" bound accounts for the real masonry (city_layout.h).
    constexpr float kWallHalfThick = kSettlementWallRing.halfThickness;
    constexpr float kWallPieceLen  = 8.0f;  // max oriented piece length —
                                            // short pieces drape over relief
    constexpr float kGateMaxSpan   = 18.0f; // wider holes are breaches: no arch
    constexpr float kGateClearM    = 5.0f;  // lintel underside above the road
    constexpr float kGateJambR     = 1.8f;  // round gate-jamb tower radius

    std::array<float, 8> gux{};
    std::array<float, 8> guy{};
    for (int g = 0; g < gates.count; ++g) {
        const float gdx = float(gates.dx[std::size_t(g)]);
        const float gdy = float(gates.dy[std::size_t(g)]);
        const float len = std::sqrt(gdx * gdx + gdy * gdy);
        gux[std::size_t(g)] = gdx / len;
        guy[std::size_t(g)] = gdy / len;
    }
    auto is_gate_pt = [&](float px, float py) {
        for (int g = 0; g < gates.count; ++g) {
            const float vx = px - cx;
            const float vy = py - cy;
            const float proj = vx * gux[std::size_t(g)] + vy * guy[std::size_t(g)];
            if (proj <= 0.0f) continue;
            if (std::fabs(vx * guy[std::size_t(g)] - vy * gux[std::size_t(g)])
                < kGateHalfWidth) {
                return true;
            }
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

    // Pass 1 — tile stamp (3×3 brush along every chord, unchanged idiom, but
    // the gate hole is the fixed-width corridor).
    for (int i = 0; i < segs; ++i) {
        const int next = (i + 1) % segs;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            const float sxF = x1 + dx * t;
            const float syF = y1 + dy * t;
            if (is_gate_pt(sxF, syF)) continue;  // leave the gate span unwalled
            const int x = int(std::floor(sxF));
            const int y = int(std::floor(syF));
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int px = x + ox;
                    const int py = y + oy;
                    if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                    const std::size_t idx = std::size_t(py) * kCellSize + px;
                    const std::uint8_t cur = out.tiles[idx];
                    // Never paint over roads/plaza/houses/fields so openings
                    // (any road crossing the ring) stay clear.
                    if (cur == TILE_ROAD || cur == TILE_SQUARE
                     || cur == TILE_HOUSE || cur == TILE_FIELD) continue;
                    out.tiles[idx] = TILE_WALL;
                    out.trav[idx] = 0;
                }
            }
        }
    }

    // Pass 2 — oriented wall bodies. Walk the WHOLE smoothed ring at ~1-tile
    // steps, classify every sample, then emit each contiguous wall run as
    // short ORIENTED boxes that follow the ring's curvature (yawed chords, no
    // more axis-aligned blob string), and each bounded opening as a real
    // gate: two round jamb towers plus a lintel lifted kGateClearM above the
    // road — bodies walk through beneath it, wall-walk defenders cross on top
    // (sub/collide.h z-layering). An opening is ANY road crossing the ring,
    // so an interior avenue punching an inner ring gets an honest gate too.
    enum class RingCls : std::uint8_t { Wall, Gate, Skip };
    struct RingSample { float x, y; RingCls cls; };
    std::vector<RingSample> ringPts;
    for (int i = 0; i < segs; ++i) {
        const int next = (i + 1) % segs;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist)));
        for (int s = 0; s < steps; ++s) {  // exclusive: next chord owns its start
            const float t = float(s) / float(steps);
            const float px = x1 + dx * t;
            const float py = y1 + dy * t;
            const int tx = std::clamp(int(std::floor(px)), 0, kCellSize - 1);
            const int ty = std::clamp(int(std::floor(py)), 0, kCellSize - 1);
            const std::uint8_t tile = out.tiles[std::size_t(ty) * kCellSize + tx];
            RingCls cls = RingCls::Wall;
            if (is_gate_pt(px, py) || tile == TILE_ROAD || tile == TILE_SQUARE) {
                cls = RingCls::Gate;
            } else if (tile == TILE_HOUSE || tile == TILE_FIELD) {
                cls = RingCls::Skip;
            }
            ringPts.push_back({px, py, cls});
        }
    }
    const int n = int(ringPts.size());
    int startIdx = 0;
    while (startIdx < n && ringPts[std::size_t(startIdx)].cls != RingCls::Wall) {
        ++startIdx;
    }
    if (n >= 4 && startIdx < n) {
        auto at = [&](int k) -> const RingSample& {
            return ringPts[std::size_t((startIdx + k) % n)];
        };
        auto emit_wall_run = [&](int a, int b) {  // run [a, b] in rotated index
            int piece0 = a;
            while (piece0 <= b) {
                int piece1 = std::min(b, piece0 + int(kWallPieceLen) - 1);
                const RingSample& p0 = at(piece0);
                const RingSample& p1 = at(piece1);
                const float ddx = p1.x - p0.x;
                const float ddy = p1.y - p0.y;
                const float len = std::sqrt(ddx * ddx + ddy * ddy);
                Structure w{};
                w.kind = Structure::Wall;
                w.x = (p0.x + p1.x) * 0.5f;
                w.y = (p0.y + p1.y) * 0.5f;
                w.yaw = std::atan2(ddy, ddx);
                w.hx = std::max(kWallHalfThick, len * 0.5f + 0.45f);
                w.hy = kWallHalfThick;
                w.radius = w.hx;
                w.height = height;
                out.structures.push_back(w);
                piece0 = piece1 + 1;
            }
        };
        auto emit_gate_run = [&](int a, int b) {
            const RingSample& g0 = at(a);
            const RingSample& g1 = at(b);
            const float ddx = g1.x - g0.x;
            const float ddy = g1.y - g0.y;
            const float span = std::sqrt(ddx * ddx + ddy * ddy);
            if (span < 2.0f || span > kGateMaxSpan) return;
            const float ux = ddx / span;
            const float uy = ddy / span;
            // Jamb towers just outside the opening, pulled back into the wall
            // ends; skipped if that spot is itself a street.
            for (int side = 0; side < 2; ++side) {
                const float dir = side == 0 ? -1.0f : 1.0f;
                const float jx = (side == 0 ? g0.x : g1.x) + dir * ux * kGateJambR * 0.5f;
                const float jy = (side == 0 ? g0.y : g1.y) + dir * uy * kGateJambR * 0.5f;
                if (tile_protected(jx, jy)) continue;
                Structure j{};
                j.kind = Structure::Wall;
                j.x = jx;
                j.y = jy;
                j.radius = kGateJambR;
                j.height = height + 2.0f;
                j.shape = Structure::Cylinder;
                out.structures.push_back(j);
            }
            // The lintel: a horizontal bar bridging the opening at gate-arch
            // height. zBase lifts its solid span clear of the roadway.
            Structure l{};
            l.kind = Structure::Wall;
            l.x = (g0.x + g1.x) * 0.5f;
            l.y = (g0.y + g1.y) * 0.5f;
            l.yaw = std::atan2(ddy, ddx);
            l.hx = span * 0.5f + 1.2f;
            l.hy = kWallHalfThick;
            l.radius = l.hx;
            l.zBase = kGateClearM;
            l.height = std::max(2.0f, height - kGateClearM);
            out.structures.push_back(l);
        };
        int runStart = 0;
        RingCls runCls = at(0).cls;
        for (int k = 1; k <= n; ++k) {
            const RingCls cls = (k == n) ? RingCls::Skip /*sentinel close*/
                                         : at(k).cls;
            if (k < n && cls == runCls) continue;
            if (runCls == RingCls::Wall) emit_wall_run(runStart, k - 1);
            else if (runCls == RingCls::Gate) emit_gate_run(runStart, k - 1);
            runStart = k;
            runCls = cls;
        }
    }

    // Towers at every other non-gate node: ROUND — chunky cylinders breaking
    // the box rhythm, taller than the curtain so the ring reads fortified.
    const float towerH = height * 1.6f;
    for (int i = 0; i < segs; i += 2) {
        const float nx = xs[std::size_t(i)];
        const float ny = ys[std::size_t(i)];
        if (is_gate_pt(nx, ny)) continue;
        if (tile_protected(nx, ny)) continue;
        Structure t{};
        t.kind = Structure::Wall;
        t.x = nx;
        t.y = ny;
        t.radius = 3.4f;
        t.height = towerH;
        t.shape = Structure::Cylinder;
        out.structures.push_back(t);
    }
}

static void gen_city(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                     const int nbTreeCount[9],
                     SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    clear_decor_tiles(out);

    Rng r(ctx.seed ^ 0xC1C1C1u);
    int center = kCellSize / 2;
    const int population = std::max(50, ctx.landmarkSize);
    // Footprint from the one settlement-layout authority (sub/city_layout.h) —
    // the same call the citizen populator makes, so walls and people can never
    // describe different towns.
    int wallR = city_wall_radius(population);
    const int ringCount = city_wall_rings(population);
    // Main roads align to neighbouring macro road cells when available.
    const RoadAxisSet axes = settlement_road_axes(nbFeature);
    carve_settlement_main_roads(out, ctx, axes, center, ctx.seed ^ 0x7711AAu);
    // Interior street plan: radial avenues + ring roads + frontage streets,
    // spread over the whole footprint (see sub/city_layout.h). Seeded off
    // ctx.seed so it does not perturb the r-stream below.
    carve_city_streets(out, center, wallR, population, ctx.seed ^ 0x51A7E11u);
    const int squareSize = std::clamp(5 + population / 5000, 5, 10);
    stamp_rect(out, center - squareSize / 2, center - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    const int keepBase = std::clamp(4 + population / 1500, 6, 16);
    const int keepW = keepBase + int(r.next_u32() % 3u);
    const int keepH = keepBase + int(r.next_u32() % 3u);
    const bool keepPlaced = stamp_landmark_house(
        out, r, float(center),
        float(center) - float(keepBase) * 0.5f - 2.0f,
        keepW, keepH, 10.0f);

    // Roadside blocks: bounded TS mycelium approximation without dynamic tip
    // queues. Count curve lives in sub/city_layout.h (single source of truth).
    const int houses = city_house_target(population);
    int placedHouses = keepPlaced ? 1 : 0;
    for (int attempt = 0; placedHouses < houses && attempt < houses * 48; ++attempt) {
        const int minSize = placedHouses < 16 ? 3 : 2;
        const int maxSize = placedHouses < 16 ? 5 : 4;
        if (try_add_roadside_house(out, r, center, city_house_radius(population),
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
        stamp_settlement_wall(out, r, city_ring_wall_radius(population, ring),
                              22 + ring * 8, city_wall_roughness(ring),
                              10.0f + float(ring) * 2.0f, axes);
    }

    // Trees outside the wall ring — TS-faithful stitch.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ wallR + 16, ctx.seed);
    // The city's field plots grow the same wheat as open farmland — one door.
    scatter_field_crops(ctx, out);
}

static void gen_village(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                        const int nbTreeCount[9],
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

    // Village footprint from the same authority the city uses and the citizen
    // populator reads (sub/city_layout.h).
    const float settleR = village_core_radius(population);
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
        if (try_add_roadside_house(out, r, center, settleR, 2, 3,
                                   4.0f + r.next_f01() * 2.5f)) {
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
    if (village_is_walled(population)) {
        const float wallR = village_wall_radius(population);
        stamp_settlement_wall(out, r, wallR,
                              std::max(10, 12 + int(wallR / 8.0f)),
                              kSettlementWallRing.villageRoughness, 6.0f, axes);
        clearR = wallR * 1.05f;
    }

    // Trees scatter outside the village core.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ int(clearR), ctx.seed);
    // The village's field plots grow the same wheat as open farmland.
    scatter_field_crops(ctx, out);
}

static bool preserves_mountain_surface(std::uint8_t tile) {
    return tile == TILE_WATER || tile == TILE_SHORE || tile == TILE_ROAD
        || tile == TILE_SQUARE || tile == TILE_HOUSE || tile == TILE_WALL
        || tile == TILE_FIELD;
}

static void stamp_mountain_rock(SubworldMapData& out, const CellContext& ctx) {
    if (out.tiles.size() != out.heightmap.size()) return;
    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    for (int y = 0; y < kCellSize; ++y) {
        const int ym = std::max(0, y - 1);
        const int yp = std::min(kCellSize - 1, y + 1);
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (preserves_mountain_surface(out.tiles[idx])) continue;
            const float h = out.heightmap[idx];
            if (h < WATER_LEVEL + 0.14f) continue;

            const int xm = std::max(0, x - 1);
            const int xp = std::min(kCellSize - 1, x + 1);
            const float hL = out.heightmap[std::size_t(y) * kCellSize + xm];
            const float hR = out.heightmap[std::size_t(y) * kCellSize + xp];
            const float hD = out.heightmap[std::size_t(ym) * kCellSize + x];
            const float hU = out.heightmap[std::size_t(yp) * kCellSize + x];
            const float slope = std::clamp(std::sqrt((hR - hL) * (hR - hL)
                                                   + (hU - hD) * (hU - hD)) * 18.0f,
                                           0.0f, 1.0f);
            const float high = std::clamp((h - 0.56f) / 0.42f, 0.0f, 1.0f);
            const float mass = smooth_noise01(float(gox + x) * 0.014f + 501.0f,
                                              float(goy + y) * 0.014f + 733.0f,
                                              ctx.seed ^ 0x4D54524Fu);
            const float patch = 0.10f + high * 0.22f + slope * 0.42f;
            if (mass < patch) {
                out.tiles[idx] = TILE_ROCK;
            }
        }
    }
}

static void gen_mountain(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                         const int nbTreeCount[9],
                         SubworldMapData& out) {
    // Lay biome ground; mountain material is stamped from the generated
    // mountain context, not guessed later from height as snow/grey.
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    clear_decor_tiles(out);
    stamp_mountain_rock(out, ctx);
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}

// Water cells: lay biome ground + run the universal tree scatter (water
// biome's treeDensity = 0 → no-op). Final water/land height clamping is a
// dispatch-level post-pass because later generators and road smoothing can
// still alter tile classes and heights.
static void gen_water(const CellContext& ctx, const Biome nbBiome[9],
                      const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // Reclassify the DRY margin (coastal blending lifts the near-land part of
    // a water cell above the plane) BEFORE scattering: fill_base_tiles floods
    // the whole cell with authored TILE_WATER, and the scatterer rightly
    // refuses authored tiles — so without this sync a coastal water cell
    // stayed 100% treeless with a hard cut at the land seam (owner report),
    // even though its dry ground now paints as the neighbour's biome. The
    // dispatch-end sync stays (idempotent; re-runs after road smoothing).
    sync_water_tiles_from_heightmap(out);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}

// Spire landmark: tower footprint plus scorch and crater tiles.
static void gen_spire(const CellContext& ctx, const Biome nbBiome[9],
                      const std::uint8_t nbFeature[9], const int nbTreeCount[9], SubworldMapData& out) {
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

    // The spire itself: one ROUND tower (a 96 m grey box read as a crate;
    // the cylinder shape finally makes it a spire).
    {
        Structure spire{};
        spire.kind = Structure::Wall;
        spire.x = float(cx);
        spire.y = float(cy);
        spire.radius = float(kTowerDiameter) * 0.5f;
        spire.height = float(kTowerHeight);
        spire.shape = Structure::Cylinder;
        out.structures.push_back(spire);
    }
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ kScorchRadius, ctx.seed);
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
        // Same ring-noise model as a live settlement's wall, from the one
        // authority in sub/city_layout.h — a ruin is a wall that lost its town,
        // not a second noise model.
        const float harmonic =
            std::sin(angle * 3.0f + phase1) * kSettlementWallRing.harmonic3Amp
          + std::sin(angle * 5.0f + phase2) * kSettlementWallRing.harmonic5Amp;
        const float jitter = (r.next_f01() * 2.0f - 1.0f) * radius * roughness
                           * kSettlementWallRing.jitterAmp;
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
            // Oriented rubble: the surviving stretch leans along its own
            // segment (80 % of the chord, so the ruin keeps honest gaps)
            // instead of one 2-tile axis-aligned crumb at the midpoint.
            const float mx = (xs[std::size_t(i)] + xs[std::size_t(next)]) * 0.5f;
            const float my = (ys[std::size_t(i)] + ys[std::size_t(next)]) * 0.5f;
            const float ddx = xs[std::size_t(next)] - xs[std::size_t(i)];
            const float ddy = ys[std::size_t(next)] - ys[std::size_t(i)];
            const float len = std::sqrt(ddx * ddx + ddy * ddy);
            Structure w{};
            w.kind = Structure::Wall;
            w.x = mx;
            w.y = my;
            w.yaw = std::atan2(ddy, ddx);
            w.hx = std::max(1.0f, len * 0.4f);
            w.hy = 1.0f;
            w.radius = w.hx;
            w.height = 5.0f;
            out.structures.push_back(w);
        }
    }
}

static void gen_ruin(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                     const int nbTreeCount[9],
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
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}

// Organic road raster used by roads and settlements. Endpoint damping keeps
// edge contacts deterministic while the interior bend gives TS-style shape.
// Rasterise one road leg as the old organic sine-wiggle segment. Endpoint
// damping (sin²(πt)) keeps the offset zero at BOTH ends, so legs chain
// cleanly and cross-cell edge anchors stay exact.
static void carve_road_leg(SubworldMapData& out,
                           int x1, int y1, int x2, int y2,
                           std::uint32_t worldSeed, float ampTiles) {
    constexpr int kStreetWidth = 2;       // 5-tile footprint
    const float fdx = float(x2 - x1);
    const float fdy = float(y2 - y1);
    const float dist = std::sqrt(fdx * fdx + fdy * fdy);
    if (dist < 1.0f) return;
    const float invDist = 1.0f / dist;
    const float nx = -fdy * invDist;   // rotated 90°
    const float ny =  fdx * invDist;
    constexpr float kPi   = 3.14159265f;
    constexpr float kFreq = 0.012f;      // gentle long-wavelength curve
    const float phase     = float(worldSeed & 0xfffffu) * 0.0001f;
    const int steps = int(std::ceil(dist));
    for (int i = 0; i <= steps; ++i) {
        const float t  = float(i) / float(steps);
        const float x0 = float(x1) + fdx * t;
        const float y0 = float(y1) + fdy * t;
        const float damp = std::sin(t * kPi);
        const float damp2 = damp * damp;                           // C¹ at ends
        const float off  = std::sin(t * dist * kFreq + phase) * ampTiles * damp2;
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

// Terrain-aware road centreline: a coarse A* on an 8-tile lattice whose step
// cost punishes GRADE quadratically, so the road hugs flat, low ground and
// climbs a massif in small switchbacks instead of charging the summit (owner
// ask — «серпантинами, по наиболее ровным местам»). Pure function of the
// heightmap with index tie-breaks ⇒ deterministic; the exact endpoints are
// kept, so cross-cell edge anchors and the road-parity invariants hold.
static void road_centreline(const SubworldMapData& out,
                            int x1, int y1, int x2, int y2,
                            std::vector<std::array<int, 2>>& pts) {
    pts.clear();
    const auto& hm = out.heightmap;
    if (hm.size() != std::size_t(kCellSize) * kCellSize) {
        pts.push_back({x1, y1});
        pts.push_back({x2, y2});
        return;
    }
    constexpr int S = 8;                           // lattice step, tiles
    int bx0 = std::min(x1, x2), bx1 = std::max(x1, x2);
    int by0 = std::min(y1, y2), by1 = std::max(y1, y2);
    const int mar = std::max(160, std::max(bx1 - bx0, by1 - by0) / 2);
    bx0 = std::max(0, bx0 - mar);
    by0 = std::max(0, by0 - mar);
    bx1 = std::min(kCellSize - 1, bx1 + mar);
    by1 = std::min(kCellSize - 1, by1 + mar);
    const int W = (bx1 - bx0) / S + 1;
    const int H = (by1 - by0) / S + 1;
    const auto nodeH = [&](int gx, int gy) {
        const int tx = std::min(kCellSize - 1, bx0 + gx * S);
        const int ty = std::min(kCellSize - 1, by0 + gy * S);
        return hm[std::size_t(ty) * kCellSize + tx];
    };
    const auto nodeIdx = [&](int gx, int gy) { return gy * W + gx; };
    const int sx = std::clamp((x1 - bx0) / S, 0, W - 1);
    const int sy = std::clamp((y1 - by0) / S, 0, H - 1);
    const int tx = std::clamp((x2 - bx0) / S, 0, W - 1);
    const int ty = std::clamp((y2 - by0) / S, 0, H - 1);

    const std::size_t n = std::size_t(W) * H;
    std::vector<float> dist(n, std::numeric_limits<float>::infinity());
    std::vector<std::int32_t> prev(n, -1);
    using QN = std::pair<float, std::int32_t>;
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;
    const auto heur = [&](int gx, int gy) {
        const float dx = float(gx - tx), dy = float(gy - ty);
        return std::sqrt(dx * dx + dy * dy) * float(S);
    };
    dist[std::size_t(nodeIdx(sx, sy))] = 0.0f;
    pq.push({heur(sx, sy), nodeIdx(sx, sy)});
    static const int kNX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kNY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float kNL[8] = {1.0f, 1.0f, 1.0f, 1.0f,
                                 1.41421356f, 1.41421356f,
                                 1.41421356f, 1.41421356f};
    // Grade in metres-per-metre; quadratic penalty makes a 26° direct climb
    // ~5× a flat detour and a 45° one ~19× — switchbacks win emergently.
    constexpr float kGradePen = 18.0f;
    const int goal = nodeIdx(tx, ty);
    while (!pq.empty()) {
        const QN cur = pq.top();
        pq.pop();
        const int ci = cur.second;
        if (ci == goal) break;
        const int cgx = ci % W, cgy = ci / W;
        const float cd = dist[std::size_t(ci)];
        if (cur.first - heur(cgx, cgy) > cd + 1e-4f) continue;  // stale
        const float ch = nodeH(cgx, cgy);
        for (int k = 0; k < 8; ++k) {
            const int ngx = cgx + kNX[k], ngy = cgy + kNY[k];
            if (ngx < 0 || ngy < 0 || ngx >= W || ngy >= H) continue;
            const float lenM = kNL[k] * float(S);
            const float grade = std::fabs(nodeH(ngx, ngy) - ch)
                              * kHeightScaleM / lenM;
            const float nd = cd + lenM * (1.0f + kGradePen * grade * grade);
            const std::size_t ni = std::size_t(nodeIdx(ngx, ngy));
            if (nd < dist[ni]) {
                dist[ni] = nd;
                prev[ni] = std::int32_t(ci);
                pq.push({nd + heur(ngx, ngy), int(ni)});
            }
        }
    }
    // Reconstruct lattice path (goal→start), emit exact endpoints around it.
    std::vector<std::array<int, 2>> rev;
    for (std::int32_t i = goal; i >= 0; i = prev[std::size_t(i)]) {
        rev.push_back({bx0 + (i % W) * S, by0 + (i / W) * S});
        if (i == nodeIdx(sx, sy)) break;
    }
    pts.push_back({x1, y1});
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) pts.push_back(*it);
    pts.push_back({x2, y2});
}

static void carve_organic_road(SubworldMapData& out,
                               int x1, int y1, int x2, int y2,
                               std::uint32_t worldSeed) {
    // Terrain-aware centreline first, then the organic wiggle per leg (small
    // amplitude — the A* path already curves where the terrain demands it).
    std::vector<std::array<int, 2>> pts;
    road_centreline(out, x1, y1, x2, y2, pts);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        carve_road_leg(out, pts[i][0], pts[i][1],
                       pts[i + 1][0], pts[i + 1][1],
                       worldSeed + std::uint32_t(i) * 0x9E37u, 2.0f);
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

static void gen_road(const CellContext& ctx, const Biome nbBiome[9],
                    const std::uint8_t nbFeature[9],
                     const int nbTreeCount[9],
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

    // Farm lanes (Field Inc F6): the road sprouts a smooth track toward
    // every neighbouring FIELD cell, aimed at the SYMMETRIC shared-edge
    // anchor — the exact point the field cell's own lane starts from on its
    // side — so the turn-off reads as one continuous lane across the seam.
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            if (xx == 1 && yy == 1) continue;
            if (FeatureLayer::decode(nbFeature[yy * 3 + xx]) != FT_Field)
                continue;
            int ax = 0, ay = 0;
            edge_anchor_target(ctx, xx - 1, yy - 1, ax, ay);
            carve_organic_road(out, cx, cy, ax, ay,
                               ctx.seed ^ 0x5a17f00du);
        }
    }

    // Vegetation around the road — biome-typical scatter, no urban clear.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}

// Crop stands on ploughed ground — ONE scatterer for every producer of
// TILE_FIELD (the FT_Field module below and the settlement field plots), so
// a village garden and open farmland grow the same wheat through the same
// door. Deterministic per GLOBAL lattice node (the glade-scan idiom): a
// coarse lattice with hash jitter, gated per node, planted only where the
// tile really is ploughed. A stand is a Structure::Crop — non-solid, drawn
// by the billboard pass (sprite row kCropSpriteRow), harvested through the
// same loot door as a tree (map_data.h kStructureKindRows).
static void scatter_field_crops(const CellContext& ctx, SubworldMapData& out) {
    constexpr int   kCropStepTiles = 8;     // lattice pitch (po2)
    // Node survival scales with the cell's FERTILITY (the same moisture
    // channel the field stamp scored by) — a lush river meadow stands thick,
    // a dry margin stands thin. The owner's law: crop density is a function
    // of the cell's own fertility, settlements included, no special case.
    constexpr float kCropGateMax   = 0.90f; // survival at fertility 1.0
    constexpr float kCropMinH      = 0.9f;  // stand height band, metres
    constexpr float kCropMaxH      = 1.5f;
    // Stand footprint as a fraction of its height — the billboard aspect the
    // renderer preserves (same law as trees: radius authored here once).
    constexpr float kCropWidthRatio = 0.45f;

    const float gate = kCropGateMax * std::clamp(ctx.fertility01, 0.0f, 1.0f);
    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    const int startGX = gox + ((kCropStepTiles - (gox % kCropStepTiles))
                               % kCropStepTiles);
    const int startGY = goy + ((kCropStepTiles - (goy % kCropStepTiles))
                               % kCropStepTiles);
    // The harvest scar (crop_count row): the sickle took this many stands
    // and regrowth has not returned them yet. The LAST `scar` stands in
    // lattice order stay down, so a harvested field comes back thinner and
    // returning to it never resurrects the wheat. Deterministic: same scar
    // → same survivors.
    int quota = -1;
    if (ctx.cropHarvested > 0) {
        int natural = 0;
        for (int gy = startGY; gy < goy + kCellSize; gy += kCropStepTiles) {
            for (int gx = startGX; gx < gox + kCellSize; gx += kCropStepTiles) {
                if (noise01(gx, gy, ctx.seed ^ 0xc50f5eedu) > gate) continue;
                const int jx = int(noise01(gx + 7, gy + 3, ctx.seed) * 7.0f) - 3;
                const int jy = int(noise01(gx + 1, gy + 9, ctx.seed) * 7.0f) - 3;
                const int x = gx - gox + jx;
                const int y = gy - goy + jy;
                if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
                if (out.tiles[std::size_t(y) * kCellSize + x] != TILE_FIELD)
                    continue;
                ++natural;
            }
        }
        quota = std::max(0, natural - ctx.cropHarvested);
    }

    for (int gy = startGY; gy < goy + kCellSize; gy += kCropStepTiles) {
        for (int gx = startGX; gx < gox + kCellSize; gx += kCropStepTiles) {
            if (quota == 0) return;
            if (noise01(gx, gy, ctx.seed ^ 0xc50f5eedu) > gate) continue;
            // Hash jitter inside the lattice cell so rows of wheat do not
            // stand on a visible grid.
            const int jx = int(noise01(gx + 7, gy + 3, ctx.seed) * 7.0f) - 3;
            const int jy = int(noise01(gx + 1, gy + 9, ctx.seed) * 7.0f) - 3;
            const int x = gx - gox + jx;
            const int y = gy - goy + jy;
            if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) continue;
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.tiles[idx] != TILE_FIELD) continue;
            const float h = kCropMinH
                + noise01(gx + 5, gy + 5, ctx.seed) * (kCropMaxH - kCropMinH);
            Structure s{};
            s.kind   = Structure::Crop;
            s.x      = float(x) + 0.5f;
            s.y      = float(y) + 0.5f;
            s.radius = h * kCropWidthRatio;
            s.height = h;
            out.structures.push_back(s);
            if (quota > 0) --quota;
        }
    }
}

// FT_Field — the ploughed farmland the map paints around villages
// (stamp_field_features on the wettest land cells). Underfoot it is a REAL
// peasant field system, an organic module like the roads and settlements:
//   · ONE organic ploughland MASSIF — low-frequency noise thresholded by
//     the local FERTILITY, both bilinearly blended over the 3×3 macro ring
//     (the tree-rate idiom), so the massif grows with the richness of the
//     land and flows CONTINUOUSLY across a seam between two field cells,
//     while fading to nothing toward neighbours nobody ploughed;
//   · the massif is PARTITIONED into parcels by grass BALKS (межи) that
//     run along and across the local FURLONG direction — a coarse global
//     direction field — so the quilt is quasi-regular strips near-by and
//     turns with the land far away, like real medieval field systems;
//   · SOME balks carry knee-high boulder walls (Structure::Fence — solid,
//     stone-flavoured boxes), broken at balk crossings and near tracks, so
//     the walls read as crofts' dry-stone rows with honest gaps, never a
//     perimeter around every plot;
//   · a neighbouring ROAD cell sprouts a smooth farm TRACK: the field cell
//     carves from the SYMMETRIC shared-edge anchor (the same point
//     gen_road's own spur aims at from the other side) into the fields;
//   · per tile the plough still refuses water and scarps, so the massif
//     follows gullies raggedly instead of painting over them.
// Furrow ORIENTATION stays per macro cell (the renderer's material grid,
// field_furrows_vertical — the map's own hash), exactly as the map draws
// one furrow direction per cell.

// The FURLONG FRAME at a global tile. The land is divided into furlong
// districts by a jittered-grid Voronoi; each district ploughs in ONE
// direction (its hash), parcel balks run parallel inside it, and the
// Voronoi boundary itself (F2−F1) is the wide lane where two differently
// turned quilts meet — the way real open-field systems are stitched.
// Pure (coords, world seed): every neighbouring cell derives the same
// frame, so balks, lanes and walls walk straight across seams.
struct FieldFrame {
    float du, dv;    // distance to the nearest along/across balk line
    float lane;      // distance to the district boundary lane (F2−F1)
    float theta;     // the district's furlong direction
    int   row, col;  // parcel indices (along/across) for gate hashes
    int   seg;       // wall segment index along a balk
    int   di, dj;    // district id (gate hashes stay per-district)
};

static FieldFrame field_frame_at(float gx, float gy,
                                 std::uint32_t worldSeed) {
    constexpr float kDistrictTiles = 560.0f;  // furlong quilt scale
    constexpr float kParcelAlong   = 96.0f;   // balk spacing along furrows
    constexpr float kParcelAcross  = 148.0f;  // ... and across
    constexpr float kBalkWarp      = 14.0f;   // balk wobble, tiles
    constexpr float kWallSegLen    = 96.0f;   // wall segment hash length
    const std::uint32_t salt = worldSeed ^ 0xf1e1d5a1u;

    const int bx = int(std::floor(gx / kDistrictTiles));
    const int by = int(std::floor(gy / kDistrictTiles));
    float best = 1e30f, second = 1e30f;
    float bcx = 0.0f, bcy = 0.0f;
    int bi = bx, bj = by;
    for (int j = by - 1; j <= by + 1; ++j) {
        for (int i = bx - 1; i <= bx + 1; ++i) {
            const float cx = (float(i) + 0.25f
                + 0.5f * noise01(i, j, salt ^ 0xd151u)) * kDistrictTiles;
            const float cy = (float(j) + 0.25f
                + 0.5f * noise01(i * 7 + 1, j * 3 + 5, salt ^ 0xd152u))
                * kDistrictTiles;
            const float d = (gx - cx) * (gx - cx) + (gy - cy) * (gy - cy);
            if (d < best) {
                second = best;
                best = d;
                bcx = cx; bcy = cy; bi = i; bj = j;
            } else if (d < second) {
                second = d;
            }
        }
    }

    FieldFrame f{};
    f.lane  = std::sqrt(second) - std::sqrt(best);
    f.theta = noise01(bi * 13 + 7, bj * 17 + 3, salt ^ 0xd153u)
            * 3.14159265f;
    f.di = bi;
    f.dj = bj;
    const float cs = std::cos(f.theta);
    const float sn = std::sin(f.theta);
    const float rx = gx - bcx;
    const float ry = gy - bcy;
    const float u = rx * cs + ry * sn
        + (smooth_noise01(gx * 0.02f, gy * 0.02f, salt ^ 0x1111u) - 0.5f)
          * kBalkWarp;
    const float v = -rx * sn + ry * cs
        + (smooth_noise01(gx * 0.02f, gy * 0.02f, salt ^ 0x2222u) - 0.5f)
          * kBalkWarp;
    const float pu = u - kParcelAlong * std::floor(u / kParcelAlong);
    const float pv = v - kParcelAcross * std::floor(v / kParcelAcross);
    f.du  = std::min(pu, kParcelAlong - pu);
    f.dv  = std::min(pv, kParcelAcross - pv);
    f.row = int(std::floor(u / kParcelAlong));
    f.col = int(std::floor(v / kParcelAcross));
    f.seg = int(std::floor(v / kWallSegLen));
    return f;
}

static void gen_field(const CellContext& ctx, const Biome nbBiome[9],
                      const std::uint8_t nbFeature[9],
                      const int nbTreeCount[9],
                      const float nbFertility[9],
                      SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();

    // ── The knobs of the field system (all data) ──
    // A PARCEL is ploughed as a WHOLE (owner: fields are parcels, not
    // stains): its (district,row,col) hash rolls against the smooth
    // fertility·fieldness threshold, so rich country quilts dense, poor
    // country keeps scattered strips, and the fieldness remap still kills
    // parcels on seams with un-ploughed ground while staying seamless
    // against another field cell.
    constexpr float kParcelBase = 0.18f;
    constexpr float kParcelFert = 0.50f;
    // Parcels: balk half-width; the spacing/warp/segment scales live in
    // field_frame_at, the ONE frame both sweeps read.
    constexpr float kBalkHalf         = 1.7f;
    constexpr float kDistrictLaneHalf = 2.6f;
    // Boulder walls: share of along-furlong balk segments that carry one,
    // gap kept clear at balk crossings and district lanes.
    constexpr float kWallShare    = 0.45f;
    constexpr float kWallCrossGap = 7.0f;
    // Per-tile plough gates — same idioms as everywhere.
    constexpr float kFieldWetTop   = WATER_LEVEL + 0.022f;
    constexpr float kFieldMaxSlope = 0.36f;

    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    const std::uint32_t salt = ctx.worldSeed ^ 0xf1e1d5a1u;

    // Ring values for the bilinear blends (the tree-rate idiom).
    float ringField[9];
    float ringFert[9];
    for (int i = 0; i < 9; ++i) {
        ringField[i] = FeatureLayer::decode(nbFeature[i]) == FT_Field
                           ? 1.0f : 0.0f;
        ringFert[i] = std::clamp(nbFertility[i], 0.0f, 1.0f);
    }
    const float invCS = 1.0f / float(kCellSize);
    auto ring_blend = [&](const float* ring, float gxf, float gyf) {
        const int x0 = std::clamp(int(std::floor(gxf - 0.5f)), 0, 2);
        const int y0 = std::clamp(int(std::floor(gyf - 0.5f)), 0, 2);
        const int x1 = std::min(2, x0 + 1);
        const int y1 = std::min(2, y0 + 1);
        const float fx = std::clamp((gxf - 0.5f) - float(x0), 0.0f, 1.0f);
        const float fy = std::clamp((gyf - 0.5f) - float(y0), 0.0f, 1.0f);
        return ring[y0 * 3 + x0] * (1.0f - fx) * (1.0f - fy)
             + ring[y0 * 3 + x1] * fx * (1.0f - fy)
             + ring[y1 * 3 + x0] * (1.0f - fx) * fy
             + ring[y1 * 3 + x1] * fx * fy;
    };

    for (int y = 0; y < kCellSize; ++y) {
        const float gyf = (float(y) + 0.5f) * invCS + 1.0f;
        for (int x = 0; x < kCellSize; ++x) {
            const float gxf = (float(x) + 0.5f) * invCS + 1.0f;
            // Fieldness: 1 deep inside ploughed country, 0 at a seam with
            // ground nobody ploughed (the 0.5 blend midpoint remaps to 0),
            // 1 straight across a field-field seam.
            // Sharpened like the material seam blend: full fieldness through
            // the interior, a ~cell/6 fade at a seam with un-ploughed ground,
            // still exactly 0 ON that seam and 1 across a field-field one.
            const float fieldW =
                std::clamp((ring_blend(ringField, gxf, gyf) - 0.5f) * 6.0f,
                           0.0f, 1.0f);
            if (fieldW <= 0.0f) continue;
            const float fertW = ring_blend(ringFert, gxf, gyf);

            const float gx = float(gox + x);
            const float gy = float(goy + y);
            // The furlong frame: parcel balks + the district boundary lane.
            const FieldFrame fr = field_frame_at(gx, gy, ctx.worldSeed);
            // Whole-parcel decision: one hash per (district,row,col).
            const float plough01 = noise01(fr.di * 733 + fr.row * 131,
                                           fr.dj * 419 + fr.col * 57,
                                           ctx.worldSeed ^ 0x9a3c11u);
            if (plough01 > (kParcelBase + kParcelFert * fertW) * fieldW)
                continue;
            const bool balk = fr.du < kBalkHalf || fr.dv < kBalkHalf
                           || fr.lane < kDistrictLaneHalf;

            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (out.heightmap[idx] < kFieldWetTop) continue;
            const int xm = std::max(0, x - 2);
            const int xp = std::min(kCellSize - 1, x + 2);
            const int ym = std::max(0, y - 2);
            const int yp = std::min(kCellSize - 1, y + 2);
            const float gxs =
                (out.heightmap[std::size_t(y) * kCellSize + xp]
               - out.heightmap[std::size_t(y) * kCellSize + xm])
                / float(std::max(1, xp - xm));
            const float gys =
                (out.heightmap[std::size_t(yp) * kCellSize + x]
               - out.heightmap[std::size_t(ym) * kCellSize + x])
                / float(std::max(1, yp - ym));
            const float slope = std::sqrt(gxs * gxs + gys * gys)
                              * kHeightScaleM;
            if (slope > kFieldMaxSlope) continue;
            if (!balk) out.tiles[idx] = TILE_FIELD;
        }
    }

    // ── Farm tracks: a smooth lane from every neighbouring road ──
    // Carved from the SYMMETRIC shared-edge anchor (the exact point the
    // road cell's own field spur aims at from its side) to the cell centre,
    // through whatever parcels lie on the way — tracks cross fields, that
    // is what tracks do. Carved BEFORE the walls so no wall is built where
    // carts roll.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (!is_road_feature(nbFeature[(dy + 1) * 3 + (dx + 1)])) continue;
            int ax = 0, ay = 0;
            edge_anchor_target(ctx, dx, dy, ax, ay);
            carve_organic_road(out, ax, ay, kCellSize / 2, kCellSize / 2,
                               ctx.seed ^ 0x5a17f00du);
        }
    }

    // ── Boulder walls on SOME along-furlong balks ──
    // A second sweep over the balk centrelines: a wall segment exists per
    // (parcel row, segment index) hash, keeps a gap at balk crossings and
    // near tracks, and is laid as a row of knee-high stone boxes — each
    // cell lays its own tiles' stones from global coords, so a wall walks
    // straight across a seam.
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            const std::uint8_t t = out.tiles[idx];
            if (t == TILE_FIELD || t == TILE_ROAD || t == TILE_WATER
                || t == TILE_SHORE) continue;
            const float gx = float(gox + x);
            const float gy = float(goy + y);
            // Stones sit only where the massif would have ploughed — the
            // balk is a line INSIDE the fields, not loose rubble on grass.
            const float gxf = (float(x) + 0.5f) * invCS + 1.0f;
            const float gyf = (float(y) + 0.5f) * invCS + 1.0f;
            // Sharpened like the material seam blend: full fieldness through
            // the interior, a ~cell/6 fade at a seam with un-ploughed ground,
            // still exactly 0 ON that seam and 1 across a field-field one.
            const float fieldW =
                std::clamp((ring_blend(ringField, gxf, gyf) - 0.5f) * 6.0f,
                           0.0f, 1.0f);
            if (fieldW <= 0.0f) continue;
            const float fertW = ring_blend(ringFert, gxf, gyf);
            const FieldFrame fr = field_frame_at(gx, gy, ctx.worldSeed);
            const float plough01 = noise01(fr.di * 733 + fr.row * 131,
                                           fr.dj * 419 + fr.col * 57,
                                           ctx.worldSeed ^ 0x9a3c11u);
            if (plough01 > (kParcelBase + kParcelFert * fertW) * fieldW)
                continue;
            // The wall rides the CENTRE of an along-furlong balk, stops
            // short of crossings and of the district lane (gaps carts and
            // cattle pass through), and only some (district, row, segment)
            // triples carry one at all.
            if (fr.du >= 0.8f || fr.dv < kWallCrossGap
                || fr.lane < kDistrictLaneHalf + kWallCrossGap) continue;
            if (noise01(fr.di * 911 + fr.row * 131,
                        fr.dj * 577 + fr.seg * 37,
                        ctx.worldSeed ^ 0xba1c5a1u) > kWallShare) continue;
            // Skip stones beside a carved track (3×3 look-around).
            bool nearRoad = false;
            for (int oy = -2; oy <= 2 && !nearRoad; ++oy) {
                for (int ox = -2; ox <= 2; ++ox) {
                    const int nx = x + ox, ny = y + oy;
                    if (nx < 0 || ny < 0 || nx >= kCellSize
                        || ny >= kCellSize) continue;
                    if (out.tiles[std::size_t(ny) * kCellSize + nx]
                        == TILE_ROAD) { nearRoad = true; break; }
                }
            }
            if (nearRoad) continue;
            // One stone per second tile keeps the row readable, not fused.
            if (((gox + x) + (goy + y)) & 1) continue;
            Structure s{};
            s.kind   = Structure::Fence;
            s.x      = float(x) + 0.5f;
            s.y      = float(y) + 0.5f;
            s.radius = 0.9f;
            s.height = 0.7f
                + noise01(gox + x, goy + y, salt ^ 0x0fe57u) * 0.5f;
            s.yaw    = fr.theta + 1.5707963f;   // long side along the balk
            s.hx     = 1.3f;
            s.hy     = 0.5f;
            out.structures.push_back(s);
        }
    }

    // Vegetation keeps to the balks and whatever ground the plough refused
    // — the scatter skips TILE_FIELD on its own — and the wheat stands on
    // the parcels.
    scatter_universal_trees(out, kCellSize, gox, goy,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
    scatter_field_crops(ctx, out);
}

} // namespace sm::sub
