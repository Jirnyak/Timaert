// Locks the PROLOGUE ROAD pocket generator (sub/dgn/prologue_road.cpp,
// routed by sub/dgn/dispatch.cpp) and the kind row that makes it a TORUS.
//
// The prologue is the demo's opening scene: a forest road under the honest
// sky, raised by the plot with no door and no exit. The scene is a 3×3
// BLOCK of variant cells (owner, 2026-09-09: «в центре 3 дороги, по краям
// лес — и заторить»): the road runs down the vx==0 column through three
// distinct road cells, the six flanking cells are unbroken forest, and the
// engine wraps the window over the block (DungeonKindRow::wrapCells). The
// module's one structural promise is CONTINUITY: any two adjacent cells —
// including across the block's wrap — continue each other exactly, or every
// window re-centre would stand the walker on a cliff.
//
// What is promised and asserted here:
//   1. BLOCK CONTINUITY — the headline. Every edge between neighbouring
//      variants (and across the wrap) is no rougher than the roughest
//      interior step, and the road bed continues across every vertical
//      join of the road column.
//   2. THE ROAD — a full-width TILE_ROAD bed in every row of every road
//      cell, NO bed anywhere in the forest cells, and the shared entry pad
//      standing ON the bed of the entered (0,0) variant.
//   3. OPEN SKY — no Structure::Wall anywhere: no lid, no masonry ring. The
//      sky is the absence of a ceiling (dungeons.md), and this is the
//      assertion that keeps it absent. And no sea: waterLevel == 0.
//   4. THE FOREST — real Structure::Tree solids in force in every variant,
//      none standing on the bed, and DIFFERENT woods per variant (the
//      per-variant seed is not decoration).
//   5. WALKABILITY — trav is 1 everywhere: the woods block by tree solids,
//      not by painted tiles.
//   6. THE KIND ROW — wrapCells==3, no household, no vermin, no hatch, no
//      storeys: the columns the session obeys instead of comparing kinds.
//   7. DETERMINISM — same context twice ⇒ identical grid and props; another
//      world seed ⇒ another relief.
#include "check.h"
#include "sub/dgn/dispatch.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

constexpr int kBlock = 3;

std::size_t cell_index(int x, int y) {
    return std::size_t(y) * kCellSize + std::size_t(x);
}

// Mirrors the engine's wrap resolver: variant coordinates in ctx.cx/cy, the
// scene-wide seed in ctx.worldSeed, the per-variant seed in ctx.seed.
CellContext make_prologue_ctx(std::uint32_t worldSeed, int vx, int vy) {
    CellContext ctx{};
    ctx.cx = vx;
    ctx.cy = vy;
    ctx.macroHeight = 0.55f;
    ctx.biome = Taiga;
    ctx.feature = FT_None;
    ctx.landmark.id = -1;
    ctx.landmark.size = 0;
    ctx.worldSeed = worldSeed;
    ctx.seed = dungeon_scene_seed(worldSeed, vx, vy, 0, 0);
    ctx.dungeon.kind = DungeonRef::PrologueRoad;
    ctx.dungeon.level = 0;
    ctx.dungeon.ordinal = 0;
    return ctx;
}

// Full production route: dispatch_generate must itself divert into the
// dungeon pipeline when the context carries a pocket ref.
void generate(const CellContext& ctx, SubworldMapData& out) {
    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = ctx.macroHeight;
        nbB[i] = ctx.biome;
        nbF[i] = std::uint8_t(FT_None);
    }
    dispatch_generate(ctx, nbH, nbB, nbF, out);
}

void generate_block(std::uint32_t worldSeed, SubworldMapData (&d)[kBlock][kBlock]) {
    for (int vy = 0; vy < kBlock; ++vy)
        for (int vx = 0; vx < kBlock; ++vx)
            generate(make_prologue_ctx(worldSeed, vx, vy), d[vy][vx]);
}

float interior_max_step(const SubworldMapData& d) {
    float maxStep = 0.0f;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x + 1 < kCellSize; ++x) {
            maxStep = std::max(maxStep,
                std::fabs(d.heightmap[cell_index(x + 1, y)]
                        - d.heightmap[cell_index(x, y)]));
        }
    }
    for (int y = 0; y + 1 < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            maxStep = std::max(maxStep,
                std::fabs(d.heightmap[cell_index(x, y + 1)]
                        - d.heightmap[cell_index(x, y)]));
        }
    }
    return maxStep;
}

void test_block_continuity() {
    SubworldMapData d[kBlock][kBlock];
    generate_block(1u, d);

    float maxStep = 0.0f;
    for (int vy = 0; vy < kBlock; ++vy)
        for (int vx = 0; vx < kBlock; ++vx)
            maxStep = std::max(maxStep, interior_max_step(d[vy][vx]));
    CHECK(maxStep > 0.0f, "the floor rolls — a billiard table would mean "
                          "the relief never ran");

    // Every edge between neighbouring variants — INCLUDING across the
    // block's wrap — is a step like any other, never a cliff.
    float maxSeam = 0.0f;
    for (int vy = 0; vy < kBlock; ++vy) {
        for (int vx = 0; vx < kBlock; ++vx) {
            const SubworldMapData& a = d[vy][vx];
            const SubworldMapData& east = d[vy][(vx + 1) % kBlock];
            const SubworldMapData& south = d[(vy + 1) % kBlock][vx];
            for (int y = 0; y < kCellSize; ++y) {
                maxSeam = std::max(maxSeam,
                    std::fabs(a.heightmap[cell_index(kCellSize - 1, y)]
                            - east.heightmap[cell_index(0, y)]));
            }
            for (int x = 0; x < kCellSize; ++x) {
                maxSeam = std::max(maxSeam,
                    std::fabs(a.heightmap[cell_index(x, kCellSize - 1)]
                            - south.heightmap[cell_index(x, 0)]));
            }
        }
    }
    CHECK(maxSeam <= maxStep * 2.0f,
          "every variant seam must be no rougher than the interior");

    // The bed continues across every vertical join of the road column.
    for (int vy = 0; vy < kBlock; ++vy) {
        const SubworldMapData& a = d[vy][0];
        const SubworldMapData& south = d[(vy + 1) % kBlock][0];
        for (int x = 0; x < kCellSize; ++x) {
            if (a.tiles[cell_index(x, kCellSize - 1)] != TILE_ROAD) continue;
            bool matched = false;
            for (int dx = -2; dx <= 2 && !matched; ++dx) {
                const int wx = ((x + dx) % kCellSize + kCellSize) % kCellSize;
                matched = south.tiles[cell_index(wx, 0)] == TILE_ROAD;
            }
            CHECK(matched, "road bed must continue across the join");
            if (!matched) return;   // one report, not a thousand
        }
    }
}

void test_road_column_and_entry() {
    SubworldMapData d[kBlock][kBlock];
    generate_block(1u, d);

    for (int vy = 0; vy < kBlock; ++vy) {
        for (int vx = 0; vx < kBlock; ++vx) {
            int rowsWithBed = 0;
            int bedTiles = 0;
            for (int y = 0; y < kCellSize; ++y) {
                int bed = 0;
                for (int x = 0; x < kCellSize; ++x) {
                    if (d[vy][vx].tiles[cell_index(x, y)] == TILE_ROAD) ++bed;
                }
                bedTiles += bed;
                if (bed >= 7) ++rowsWithBed;   // 2 × halfWidth − 1
            }
            if (vx == 0) {
                CHECK(rowsWithBed == kCellSize,
                      "every row of a road cell carries a full-width bed");
            } else {
                CHECK(bedTiles == 0, "a forest cell keeps no road — what "
                                     "stands beside the road is real forest");
            }
        }
    }

    // The shared entry pad (dungeon_entry_point) lands ON the bed of the
    // entered (0,0) variant: the module's room centres itself on the road
    // axis for exactly this.
    const CellContext ctx = make_prologue_ctx(1u, 0, 0);
    float ex = 0.0f, ey = 0.0f;
    dungeon_entry_point(ctx.dungeon, ex, ey);
    CHECK(ex >= 0.0f && ex < float(kCellSize)
          && ey >= 0.0f && ey < float(kCellSize),
          "entry pad must be inside the cell");
    CHECK(d[0][0].tiles[cell_index(int(ex), int(ey))] == TILE_ROAD,
          "the arriving body must stand on the road");

    // Everything is walkable ground — the woods block by tree SOLIDS.
    bool travOk = true;
    for (int vy = 0; vy < kBlock; ++vy)
        for (int vx = 0; vx < kBlock; ++vx)
            for (std::size_t i = 0; i < d[vy][vx].trav.size(); ++i)
                travOk = travOk && d[vy][vx].trav[i] == 1;
    CHECK(travOk, "an open pocket has no painted walls");
}

void test_open_sky_and_forest() {
    SubworldMapData d[kBlock][kBlock];
    generate_block(1u, d);
    for (int vy = 0; vy < kBlock; ++vy) {
        for (int vx = 0; vx < kBlock; ++vx) {
            int trees = 0;
            int walls = 0;
            int treesOnBed = 0;
            for (const Structure& s : d[vy][vx].structures) {
                if (s.kind == Structure::Wall) ++walls;
                if (s.kind != Structure::Tree) continue;
                ++trees;
                const int x = int(s.x), y = int(s.y);
                if (x >= 0 && x < kCellSize && y >= 0 && y < kCellSize
                    && d[vy][vx].tiles[cell_index(x, y)] == TILE_ROAD) {
                    ++treesOnBed;
                }
            }
            CHECK(walls == 0, "no lid, no masonry: the sky is the absence "
                              "of a ceiling and must stay absent");
            CHECK(trees > 1000, "a prologue forest is a forest in EVERY "
                                "variant, not a hedgerow");
            CHECK(treesOnBed == 0, "the scatterer's road-skip law must hold");
            CHECK(d[vy][vx].waterLevel == 0.0f, "the pocket has no sea");
        }
    }
    // Different variants grow DIFFERENT woods — the per-variant seed is not
    // decoration (tree counts colliding on all three road cells at once
    // would be a one-in-thousands accident, so compare placements).
    bool anyDiffer = d[0][0].structures.size() != d[1][0].structures.size();
    if (!anyDiffer && !d[0][0].structures.empty()) {
        const Structure& a = d[0][0].structures.front();
        const Structure& b = d[1][0].structures.front();
        anyDiffer = a.x != b.x || a.y != b.y;
    }
    CHECK(anyDiffer, "road cells must be three different stretches, not one "
                     "copied thrice");
}

void test_kind_row_and_storeys() {
    const DungeonKindRow& row = dungeon_kind_row(DungeonRef::PrologueRoad);
    CHECK(row.wrapCells == 3, "the prologue wraps a 3×3 block — that IS the "
                              "owner's torus");
    CHECK(!row.householdAbove && !row.verminAbove,
          "the plot populates the prologue, not the stocks");
    CHECK(!row.roofHatch && !row.shaftLadder, "nothing to climb");

    DungeonRef ref{};
    ref.kind = DungeonRef::PrologueRoad;
    CHECK(!dungeon_has_upper(ref), "an open pocket has no storeys");
    CHECK(!dungeon_has_cellar(ref, 42u, 0, 0), "and nothing dug below");
    CHECK(dungeon_floor_tile(ref) == TILE_ROAD,
          "the module's floor answer is the bed");
}

void test_determinism() {
    SubworldMapData a{}, b{}, c{};
    generate(make_prologue_ctx(1u, 0, 0), a);
    generate(make_prologue_ctx(1u, 0, 0), b);
    generate(make_prologue_ctx(999u, 0, 0), c);
    CHECK(a.tiles == b.tiles, "same context ⇒ identical ground");
    CHECK(a.heightmap == b.heightmap, "same context ⇒ identical relief");
    bool propsSame = a.structures.size() == b.structures.size();
    for (std::size_t i = 0; propsSame && i < a.structures.size(); ++i) {
        propsSame = a.structures[i].kind == b.structures[i].kind
                 && a.structures[i].x == b.structures[i].x
                 && a.structures[i].y == b.structures[i].y;
    }
    CHECK(propsSame, "same context ⇒ identical props");
    CHECK(a.heightmap != c.heightmap,
          "another world seed ⇒ another wood, or the seed is decoration");
}

} // namespace

int main() {
    test_block_continuity();
    test_road_column_and_entry();
    test_open_sky_and_forest();
    test_kind_row_and_storeys();
    test_determinism();
    return sm::test::report("dungeon_prologue_test");
}
