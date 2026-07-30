// tree_layer_test — the per-cell tree-count layer (macro/tree_layer.h).
//
// Locks:
//   1. The derivation formula: water carries nothing, the golden constant
//      16384 (2^14) is hit EXACTLY by a fully forested 3×3 neighbourhood and
//      never exceeded, the forest term is monotone, biome bases match the
//      authored table.
//   2. build_tree_layer: biome cascade (water mask / mountain elevation /
//      climate), torus-wrapped forest neighbourhoods, fail-closed on bad
//      storage.
//   3. The mutation path: set_tree_count clamps to [0, 16384], records the
//      sparse override, bumps `revision`; apply_tree_overrides reproduces a
//      mutated layer on a freshly derived one (the load path).
//   4. Scatter calibration: scatter_universal_trees is COUNT-driven — over a
//      flat full cell the number of PLACED trees tracks the requested count
//      through kTreeScatterYield (the guard that keeps macro counts honest
//      against what the subworld actually grows), count 0 grows nothing, and
//      half the count grows roughly half the trees.
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <vector>

#include "macro/tree_layer.h"
#include "sub/base_generator.h"
#include "sub/map_data.h"

using namespace sm;
using namespace sm::sub;

static int gFailures = 0;
#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("[tree_layer] FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++gFailures; \
        } \
    } while (0)

static int count_trees(const SubworldMapData& out) {
    int n = 0;
    for (const auto& s : out.structures)
        if (s.kind == Structure::Tree) ++n;
    return n;
}

static int run_scatter(int count, std::uint32_t seed, SubworldMapData& out) {
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.heightmap.assign(std::size_t(kCellSize) * kCellSize, 0.5f); // flat, below treeline
    out.structures.clear();
    Biome nbBiome[9];
    int nbCount[9];
    for (int i = 0; i < 9; ++i) { nbBiome[i] = Biome::Meadow; nbCount[i] = count; }
    scatter_universal_trees(out, kCellSize, 0, 0, nbBiome, nbCount, 0, seed);
    return count_trees(out);
}

int main() {
    // ── 1. Derivation formula ──
    CHECK(derived_tree_count(Biome::Water, 1.0f) == 0);
    CHECK(derived_tree_count(Biome::Meadow, 1.0f) == kMaxTreesPerCell);
    CHECK(derived_tree_count(Biome::Tropics, 0.0f) == kMaxTreesPerCell);
    CHECK(derived_tree_count(Biome::Meadow, 0.0f)
           == kBiomeBaseTreeCount[int(Biome::Meadow)]);
    CHECK(derived_tree_count(Biome::Desert, 0.0f)
           == kBiomeBaseTreeCount[int(Biome::Desert)]);
    // Monotone in the forest fraction, never above the golden max.
    int prev = -1;
    for (int f = 0; f <= 9; ++f) {
        const int c = derived_tree_count(Biome::Steppe, float(f) / 9.0f);
        CHECK(c >= prev);
        CHECK(c <= kMaxTreesPerCell);
        prev = c;
    }
    // A lone forest cell (frac 1/9) adds exactly 16384/9 over its base.
    CHECK(derived_tree_count(Biome::Meadow, 1.0f / 9.0f)
           == kBiomeBaseTreeCount[int(Biome::Meadow)]
              + int(float(kMaxTreesPerCell) / 9.0f + 0.5f));
    std::printf("[tree_layer] formula ok\n");

    // ── 2. build_tree_layer on a synthetic 8×8 world ──
    // Row 0 is water (mask 0); cell (4,4) and its full ring carry FT_Tree;
    // cell (0,4) is forest at the torus edge (neighbours wrap to x=7).
    TerrainData td;
    td.width = 8; td.height = 8;
    td.rgba.assign(8 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const std::size_t i = std::size_t(y) * 8 + x;
            td.rgba[i * 4 + 0] = 128;              // height 0.50 → land biome
            td.rgba[i * 4 + 1] = 128;              // moisture → Meadow column
            td.rgba[i * 4 + 2] = 128;              // temperature → Meadow row
            td.rgba[i * 4 + 3] = (y == 0) ? 0 : 255; // row 0 = water
        }
    td.rgba[((3 * 8) + 3) * 4 + 0] = 250;          // (3,3): mountain elevation
    FeatureLayer fl;
    fl.resize(8, 8);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            fl.set(4 + dx, 4 + dy, FT_Tree);
    fl.set(0, 6, FT_Tree);

    TreeLayer layer = build_tree_layer(td, fl);
    CHECK(layer.has_complete_storage());
    CHECK(layer.at(2, 0) == 0);                       // water row carries nothing
    CHECK(layer.at(4, 4) == kMaxTreesPerCell);        // 9/9 forest = the golden max
    CHECK(layer.at(3, 3) == derived_tree_count(Biome::Mountain, 4.0f / 9.0f));
    // Ring cell (5,5): itself + 3 ring mates in ITS 3×3 → frac 4/9 over Meadow.
    CHECK(layer.at(5, 5) == derived_tree_count(Biome::Meadow, 4.0f / 9.0f));
    // Torus: (7,6) neighbours the forest at (0,6) across the wrap.
    CHECK(layer.at(7, 6) == derived_tree_count(Biome::Meadow, 1.0f / 9.0f));
    // Fail closed: truncated terrain → empty layer, at() reads 0.
    TerrainData bad;
    bad.width = 8; bad.height = 8;
    bad.rgba.assign(7, 0);
    TreeLayer badLayer = build_tree_layer(bad, fl);
    CHECK(!badLayer.has_complete_storage());
    CHECK(badLayer.at(3, 3) == 0);
    std::printf("[tree_layer] build ok\n");

    // ── 3. Mutation path (fell-tree writeback) + load re-apply ──
    TreeOverrides ov;
    const std::uint32_t rev0 = layer.revision;
    set_tree_count(layer, ov, 4, 4, kMaxTreesPerCell - 5);
    CHECK(layer.at(4, 4) == kMaxTreesPerCell - 5);
    CHECK(layer.revision == rev0 + 1);
    CHECK(ov.size() == 1);
    set_tree_count(layer, ov, 4, 4, -37);              // clamps to 0
    CHECK(layer.at(4, 4) == 0);
    set_tree_count(layer, ov, 4, 4, kMaxTreesPerCell * 3); // clamps to max
    CHECK(layer.at(4, 4) == kMaxTreesPerCell);
    set_tree_count(layer, ov, 12, -2, 77);             // torus-wrapped write
    CHECK(layer.at(4, 6) == 77);
    CHECK(ov.size() == 2);
    // Same value again: no revision churn, no duplicate entries.
    const std::uint32_t revStable = layer.revision;
    set_tree_count(layer, ov, 4, 6, 77);
    CHECK(layer.revision == revStable);
    // Load path: fresh derived layer + overrides == the mutated layer.
    TreeLayer reloaded = build_tree_layer(td, fl);
    apply_tree_overrides(reloaded, ov);
    CHECK(reloaded.at(4, 4) == layer.at(4, 4));
    CHECK(reloaded.at(4, 6) == 77);
    // Stale override beyond the map: skipped, no crash.
    TreeOverrides stale;
    stale[9999999u] = 5;
    apply_tree_overrides(reloaded, stale);
    std::printf("[tree_layer] mutation+reload ok\n");

    // ── 4. Scatter calibration: placed ≈ requested count ──
    SubworldMapData out;
    const int placedFull = run_scatter(kMaxTreesPerCell, 12345u, out);
    const float ratioFull = float(placedFull) / float(kMaxTreesPerCell);
    std::printf("[tree_layer] scatter full: placed=%d target=%d ratio=%.3f\n",
                placedFull, kMaxTreesPerCell, double(ratioFull));
    CHECK(ratioFull > 0.80f && ratioFull < 1.20f);
    // Every placed tree stamped its decor tile (the ONE-authority invariant).
    int decor = 0;
    for (auto t : out.tiles) if (t == TILE_TREE_DECOR) ++decor;
    CHECK(decor == placedFull);

    const int placedZero = run_scatter(0, 12345u, out);
    CHECK(placedZero == 0);

    const int placedHalf = run_scatter(kMaxTreesPerCell / 2, 777u, out);
    const float ratioHalf = float(placedHalf) / float(kMaxTreesPerCell / 2);
    std::printf("[tree_layer] scatter half: placed=%d target=%d ratio=%.3f\n",
                placedHalf, kMaxTreesPerCell / 2, double(ratioHalf));
    CHECK(ratioHalf > 0.80f && ratioHalf < 1.20f);
    // Density scales with the count: half the count grows FEWER trees.
    CHECK(placedHalf < placedFull);
    std::printf("[tree_layer] scatter calibration ok "
                "(kTreeScatterYield=%.2f)\n", double(kTreeScatterYield));

    if (gFailures) {
        std::printf("[tree_layer] %d FAILURES\n", gFailures);
        return 1;
    }
    std::printf("[tree_layer] ALL PASS\n");
    return 0;
}
