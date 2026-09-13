#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/gens/kit/noise.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/kit/outline.h"
#include "sub/city_layout.h"
#include "core/rng.h"
#include "sub/height.h"
#include "macro/tree_layer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {
// The kit is this module's vocabulary — primitives, never a sibling module.
using namespace kit;

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
    float pu;        // raw position inside the along band (adjacency math)
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
    f.pu  = pu;
    f.du  = std::min(pu, kParcelAlong - pu);
    f.dv  = std::min(pv, kParcelAcross - pv);
    f.row = int(std::floor(u / kParcelAlong));
    f.col = int(std::floor(v / kParcelAcross));
    f.seg = int(std::floor(v / kWallSegLen));
    return f;
}

void gen_field(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;
    const float* nbFertility = in.nbFertility;
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
    constexpr float kWallShare    = 0.55f;
    constexpr float kWallCrossGap = 7.0f;
    // Per-tile plough gates — same idioms as everywhere; the wet gate is THE
    // shared kWetEdgeTop (base_generator.h), the shore law's own line.
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
            if (out.heightmap[idx] < kWetEdgeTop) continue;
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
            // The balk lies BETWEEN two parcels: a boulder wall stands if
            // EITHER flank is ploughed (walls edge every worked row — the
            // own-row-only test silently dropped half the walls along every
            // ploughed↔fallow border).
            const float gate = (kParcelBase + kParcelFert * fertW) * fieldW;
            const int rowB = fr.pu < 48.0f ? fr.row - 1 : fr.row + 1;
            const bool ploughedA =
                noise01(fr.di * 733 + fr.row * 131,
                        fr.dj * 419 + fr.col * 57,
                        ctx.worldSeed ^ 0x9a3c11u) <= gate;
            const bool ploughedB =
                noise01(fr.di * 733 + rowB * 131,
                        fr.dj * 419 + fr.col * 57,
                        ctx.worldSeed ^ 0x9a3c11u) <= gate;
            if (!ploughedA && !ploughedB) continue;
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
