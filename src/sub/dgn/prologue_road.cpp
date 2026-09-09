// Prologue road — the demo's opening pocket (CANON S17 «карман может быть и
// ОТКРЫТЫМ миром», S20 авторская начальная сцена): a road winding through
// forest under the honest sky. No door raised it and no exit leaves it — the
// scene ends by the plot (the witches), not by walking out.
//
// THE 3×3 TOROIDAL LAW of this module (owner, 2026-09-09: «в центре 3
// дороги, по краям лес — и заторить»): the scene is a BLOCK of 3×3 distinct
// cells — the road runs down the centre COLUMN through three road cells, the
// six flanking cells are unbroken forest — and the block tiles the plane.
// The engine wraps the window (DungeonKindRow::wrapWindow) by resolving
// every window cell to its block variant (x mod 3, y mod 3), so the walker
// meets the same stretch again three cells (~3 km) later, and what stands
// beside the road is real forest, not a mirrored road.
//
// Continuity is LAW, not stitching: the relief is a periodic function of
// GLOBAL block coordinates (integer wave numbers over 3·kCellSize, phases
// from the scene-wide seed), so any two adjacent cells — including across
// the block's wrap — continue each other exactly. The road's axis is a
// block-periodic swing that passes through cell centres at every cell
// boundary. Per-CELL variation (which trees, where) rides ctx.seed, which
// the resolver varies per variant.
//
// No ceiling is stamped: the sky is not a column, it is the absence of
// masonry (dungeons.md §3). No lid, no walls, no sea.
#include "sub/dgn/dispatch.h"
#include "sub/base_generator.h"

#include <cmath>

namespace sm::sub {

namespace {

constexpr float kTau = 6.28318530718f;

// The block: wrapCells×wrapCells variant cells, road down the vx==0 column.
// The resolver anchors the entered window's centre on variant (0, 0). ONE
// authority for the period — the kind row's own column, the same number the
// engine wraps window coordinates by.
inline int block_cells() {
    return int(dungeon_kind_row(DungeonRef::PrologueRoad).wrapCells);
}

// The road's bed half-width in tiles. Wide enough for a cart and an ambush
// around it, narrow enough to read as a forest track, not a highway.
constexpr float kRoadHalfWidth = 4.0f;

// How far the road swings off the column's axis (tiles), over ONE period =
// the whole 3-cell block — a long lazy bend, not a corridor.
constexpr float kRoadSwingTiles = 48.0f;

// Rolling forest floor, in normalised height. Three integer-frequency
// harmonics over the BLOCK period: gentle enough that the road needs no
// carving pass, alive enough that the ground is not a billiard table.
constexpr float kReliefAmp = 0.0035f;

// The forest's density on the macro count scale (16384 = the golden densest
// forest, macro/tree_layer.h). Thick woods with a readable corridor.
constexpr int kTreeCount = 11000;

inline int wrap_variant(int v) {
    const int n = block_cells();
    return (v % n + n) % n;
}

// The road's centre x for a GLOBAL row (block tiles) — periodic over the
// block by construction. Shared by the generator (stamps the bed) and the
// room (seats the entry pad ON the bed).
inline float road_axis_x(float gy) {
    const float period = float(block_cells() * kCellSize);
    return float(kCellSize) * 0.5f
         + kRoadSwingTiles * std::sin(kTau * gy / period);
}

} // namespace

DungeonRoom dungeon_prologue_road_room(const DungeonRef& ref) {
    (void)ref;
    // The "room" the shared rules address, in the ENTERED (centre, variant
    // 0,0) cell: centred on the road's bed at the southern stretch, so
    // dungeon_entry_point (room.cy + room.hy − 2) lands the arriving body ON
    // the road. The half-extents span the whole cell — an open pocket has no
    // smaller rectangle worth naming.
    DungeonRoom room;
    room.hx = float(kCellSize) * 0.5f - 2.0f;
    room.hy = float(kCellSize) * 0.5f - 2.0f;
    room.cy = float(kCellSize) * 0.5f;
    room.cx = road_axis_x(room.cy + room.hy - 2.0f);
    return room;
}

void gen_dungeon_prologue_road(const CellContext& ctx, SubworldMapData& out) {
    const int W = kCellSize;
    const std::size_t n = std::size_t(W) * W;
    // WHICH cell of the block this is — the resolver hands the variant in
    // ctx.cx/cy (virtual block coordinates); wrap defensively so a test can
    // pass any integers.
    const int vx = wrap_variant(ctx.cx);
    const int vy = wrap_variant(ctx.cy);
    const bool roadCell = vx == 0;

    // Ground: open forest floor, everything walkable — trees are SOLIDS in
    // the structure array, not tiles, so the woods block by physics.
    out.tiles.assign(n, std::uint8_t(TILE_GRASS));
    out.trav.assign(n, 1);
    out.structures.clear();
    out.waterLevel = 0.0f;   // below any floor: the pocket has no sea

    // Rolling floor from integer-frequency harmonics over the BLOCK —
    // periodic across the block's wrap and continuous between neighbouring
    // cells by construction. Phases roll from the SCENE-WIDE seed
    // (ctx.worldSeed), never the per-cell one: a relief seam between two
    // variants would be a cliff every kilometre.
    const float base = skeleton_cell_height01(ctx.macroHeight,
                                              /*isWater=*/false,
                                              /*isMountain=*/false);
    const float period = float(block_cells() * W);
    const std::uint32_t s = ctx.worldSeed * 2654435761u;
    const float p1 = float(s & 0xFFu) * (kTau / 256.0f);
    const float p2 = float((s >> 8) & 0xFFu) * (kTau / 256.0f);
    const float p3 = float((s >> 16) & 0xFFu) * (kTau / 256.0f);
    out.heightmap.assign(n, base);
    for (int y = 0; y < W; ++y) {
        const float gy = kTau * float(vy * W + y) / period;
        for (int x = 0; x < W; ++x) {
            const float gx = kTau * float(vx * W + x) / period;
            const float relief =
                std::sin(9.0f * gx + p1) * std::sin(6.0f * gy + p2)
                + 0.5f * std::sin(15.0f * gx - 9.0f * gy + p3)
                + 0.25f * std::sin(27.0f * gx + 21.0f * gy + p1);
            out.heightmap[std::size_t(y) * W + x] =
                base + kReliefAmp * relief;
        }
    }

    // The bed — road cells only: TILE_ROAD along the block-periodic axis,
    // LEVEL ACROSS its width (each row of the bed takes the axis height) so
    // the cart-track reads as worn ground, with a one-tile shoulder blended
    // half-way.
    if (roadCell) {
        for (int y = 0; y < W; ++y) {
            const float axis = road_axis_x(float(vy * W + y));
            const float hAxis =
                out.heightmap[std::size_t(y) * W
                              + std::size_t(std::clamp(int(axis), 0, W - 1))];
            const int x0 = int(axis - kRoadHalfWidth - 1.0f);
            const int x1 = int(axis + kRoadHalfWidth + 1.0f);
            for (int x = x0; x <= x1; ++x) {
                const int wx = (x % W + W) % W;
                const std::size_t i = std::size_t(y) * W + wx;
                const float d = std::fabs(float(x) - axis);
                if (d <= kRoadHalfWidth) {
                    out.tiles[i] = std::uint8_t(TILE_ROAD);
                    out.heightmap[i] = hAxis;
                } else {
                    out.heightmap[i] = 0.5f * (out.heightmap[i] + hAxis);
                }
            }
        }
    }

    // The forest, by the ONE tree authority, on the block's own global
    // lattice — each variant grows ITS OWN wood (ctx.seed varies per
    // variant), the lattice offsets keep the distributions distinct, and
    // the scatterer skips the road bed on its own (it plants on bare
    // ground only).
    Biome nbBiome[9];
    int nbTreeCount[9];
    for (int i = 0; i < 9; ++i) {
        nbBiome[i] = Biome::Taiga;
        nbTreeCount[i] = kTreeCount;
    }
    scatter_universal_trees(out, W, vx * W, vy * W,
                            nbBiome, nbTreeCount, /*clearRadius=*/0,
                            ctx.seed);
}

} // namespace sm::sub
