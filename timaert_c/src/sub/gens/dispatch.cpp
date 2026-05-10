#include "sub/gens/dispatch.h"
#include "sub/base_generator.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

// Forward decls for per-mode generators (each defined below).
static void gen_open      (const CellContext&, SubworldMapData&);
static void gen_forest    (const CellContext&, SubworldMapData&);
static void gen_swamp     (const CellContext&, SubworldMapData&);
static void gen_city      (const CellContext&, SubworldMapData&);
static void gen_village   (const CellContext&, SubworldMapData&);
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
        case SubworldMode::City:      gen_city     (ctx, out); break;
        case SubworldMode::Village:   gen_village  (ctx, out); break;
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

static void gen_city(const CellContext& ctx, SubworldMapData& out) {
    Rng r(ctx.seed ^ 0xC1C1C1u);
    int center = kCellSize / 2;
    int wallR = 220;
    // Walls (ring of TILE_WALL)
    for (int a = 0; a < 360; ++a) {
        float t = a * 3.14159265f / 180.0f;
        int x = center + int(std::cos(t) * wallR);
        int y = center + int(std::sin(t) * wallR);
        if (x >= 0 && x < kCellSize && y >= 0 && y < kCellSize) {
            out.tiles[std::size_t(y) * kCellSize + x] = TILE_WALL;
            out.trav [std::size_t(y) * kCellSize + x] = 0;
            Structure s{Structure::Wall, float(x), float(y), 0.6f, 8.0f};
            out.structures.push_back(s);
        }
    }
    // Streets — radial 4-way main road + a few rings.
    for (int x = center - wallR; x <= center + wallR; ++x) {
        if (x >= 0 && x < kCellSize)
            out.tiles[std::size_t(center) * kCellSize + x] = TILE_ROAD;
    }
    for (int y = center - wallR; y <= center + wallR; ++y) {
        if (y >= 0 && y < kCellSize)
            out.tiles[std::size_t(y) * kCellSize + center] = TILE_ROAD;
    }
    // Houses scattered in the four quadrants.
    int houses = 30 + (ctx.landmarkSize / 200);
    for (int i = 0; i < houses; ++i) {
        int hx = center + int(r.next_u32() % std::uint32_t(wallR * 2)) - wallR;
        int hy = center + int(r.next_u32() % std::uint32_t(wallR * 2)) - wallR;
        if (hx < 0 || hy < 0 || hx >= kCellSize || hy >= kCellSize) continue;
        Structure s{Structure::House, float(hx), float(hy), 4.0f, 6.0f};
        out.structures.push_back(s);
        out.tiles[std::size_t(hy) * kCellSize + hx] = TILE_HOUSE;
        out.trav [std::size_t(hy) * kCellSize + hx] = 0;
    }
    // Trees outside the wall ring — TS-faithful stitch.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ wallR + 16, ctx.seed);
}

static void gen_village(const CellContext& ctx, SubworldMapData& out) {
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    Rng r(ctx.seed ^ 0xABCDEFu);
    int center = kCellSize / 2;
    int n = 5 + int(r.next_u32() % 6u);
    for (int i = 0; i < n; ++i) {
        int hx = center + int(r.next_u32() % 100u) - 50;
        int hy = center + int(r.next_u32() % 100u) - 50;
        Structure s{Structure::House, float(hx), float(hy), 3.5f, 5.0f};
        out.structures.push_back(s);
        if (hx >= 0 && hx < kCellSize && hy >= 0 && hy < kCellSize) {
            out.tiles[std::size_t(hy) * kCellSize + hx] = TILE_HOUSE;
            out.trav [std::size_t(hy) * kCellSize + hx] = 0;
        }
    }
    // Trees scatter outside the village core.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        ctx.biome, /*forest*/ false, /*clearRadius*/ 90, ctx.seed);
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
