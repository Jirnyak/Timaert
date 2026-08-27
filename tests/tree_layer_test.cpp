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
//   3. The grid's poke door: set_tree_count clamps to [0, 16384] and bumps
//      `revision` (the u_treeMap refresh driver); restore_tree_counts
//      reproduces a mutated layer on a freshly derived one (the v36 load
//      path — the save carries the living grid whole) and refuses a
//      size-mismatched grid without touching the layer.
//   4. Scatter calibration: scatter_universal_trees is COUNT-driven — over a
//      flat full cell the number of PLACED trees tracks the requested count
//      through kTreeScatterYield (the guard that keeps macro counts honest
//      against what the subworld actually grows), count 0 grows nothing, and
//      half the count grows roughly half the trees.
#include <cstdio>
#include <vector>

#include "check.h"
#include "macro/tree_layer.h"
#include "sub/base_generator.h"
#include "sub/map_data.h"

using namespace sm;
using namespace sm::sub;

namespace {

int count_trees(const SubworldMapData& out) {
    int n = 0;
    for (const auto& s : out.structures)
        if (s.kind == Structure::Tree) ++n;
    return n;
}

int run_scatter(int count, std::uint32_t seed, SubworldMapData& out) {
    out.tiles.assign(std::size_t(kCellSize) * kCellSize, std::uint8_t(TILE_GRASS));
    out.heightmap.assign(std::size_t(kCellSize) * kCellSize, 0.5f); // flat, below treeline
    out.structures.clear();
    Biome nbBiome[9];
    int nbCount[9];
    for (int i = 0; i < 9; ++i) { nbBiome[i] = Biome::Meadow; nbCount[i] = count; }
    scatter_universal_trees(out, kCellSize, 0, 0, nbBiome, nbCount, 0, seed);
    return count_trees(out);
}

void test_derivation_formula() {
    CHECK(derived_tree_count(Biome::Water, 1.0f) == 0,
          "water carries no trees");
    CHECK(derived_tree_count(Biome::Meadow, 1.0f) == kMaxTreesPerCell,
          "a full 3x3 massif hits the golden max exactly");
    CHECK(derived_tree_count(Biome::Meadow, 0.0f)
              == biome_base_tree_count(int(Biome::Meadow)),
          "no massif = the biome's ambient base (meadow)");
    CHECK(derived_tree_count(Biome::Desert, 0.0f)
              == biome_base_tree_count(int(Biome::Desert)),
          "no massif = the biome's ambient base (desert)");
    // Biome ambience must NEVER draw as forest on the map: every base sits
    // under the sprite coverage threshold (0.09 × 16384 ≈ 1475) AND under
    // the forest-class threshold — only massifs make forests.
    for (int b = 0; b < int(std::size(kBiomeBaseTreeCount)); ++b) {
        CHECK(biome_base_tree_count(b) < 1475,
              "biome ambience stays under the map-sprite threshold");
        CHECK(!is_forest_cell(biome_base_tree_count(b)),
              "biome ambience never classifies as forest");
    }
    CHECK(is_forest_cell(kForestClassTreeCount),
          "the forest-class threshold itself is forest");
    CHECK(!is_forest_cell(kForestClassTreeCount - 1),
          "one tree below the threshold is not forest");
    // Monotone in the forest fraction, never above the golden max.
    int prev = -1;
    for (int f = 0; f <= 9; ++f) {
        const int c = derived_tree_count(Biome::Steppe, float(f) / 9.0f);
        CHECK(c >= prev, "the forest term is monotone in the 3x3 fraction");
        CHECK(c <= kMaxTreesPerCell, "the count never exceeds the golden max");
        prev = c;
    }
    // A lone forest cell (frac 1/9) adds exactly 16384/9 over its base.
    CHECK(derived_tree_count(Biome::Meadow, 1.0f / 9.0f)
              == biome_base_tree_count(int(Biome::Meadow))
                  + int(float(kMaxTreesPerCell) / 9.0f + 0.5f),
          "a lone massif cell adds exactly one ninth of the max");
}

// A synthetic 8×8 world: row 0 is water (mask 0); cell (4,4) and its full
// ring carry the massif; cell (0,6) is forest at the torus edge.
TerrainData make_terrain() {
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
    return td;
}

std::vector<std::uint8_t> make_forest_mask() {
    std::vector<std::uint8_t> forestMask(64, 0);
    auto mark = [&](int x, int y) {
        forestMask[std::size_t(((y % 8) + 8) % 8) * 8
                   + std::size_t(((x % 8) + 8) % 8)] = 1;
    };
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            mark(4 + dx, 4 + dy);
    mark(0, 6);
    return forestMask;
}

void test_build_and_mutation() {
    const TerrainData td = make_terrain();
    const std::vector<std::uint8_t> forestMask = make_forest_mask();

    TreeLayer layer = build_tree_layer(td, forestMask.data(), forestMask.size());
    CHECK_OR_RETURN(layer.has_complete_storage(),
                    "the derived layer has full storage");
    CHECK(layer.at(2, 0) == 0, "the water row carries nothing");
    CHECK(layer.at(4, 4) == kMaxTreesPerCell,
          "9/9 forest = the golden max");
    CHECK(layer.at(3, 3) == derived_tree_count(Biome::Mountain, 4.0f / 9.0f),
          "the mountain cell derives through the biome cascade");
    // Ring cell (5,5): itself + 3 ring mates in ITS 3×3 → frac 4/9 over Meadow.
    CHECK(layer.at(5, 5) == derived_tree_count(Biome::Meadow, 4.0f / 9.0f),
          "a ring cell sees its own 3x3 fraction");
    // Torus: (7,6) neighbours the forest at (0,6) across the wrap.
    CHECK(layer.at(7, 6) == derived_tree_count(Biome::Meadow, 1.0f / 9.0f),
          "the massif neighbourhood wraps the torus");
    // Fail closed: truncated terrain → empty layer, at() reads 0.
    TerrainData bad;
    bad.width = 8; bad.height = 8;
    bad.rgba.assign(7, 0);
    TreeLayer badLayer = build_tree_layer(bad, forestMask.data(), forestMask.size());
    CHECK(!badLayer.has_complete_storage(),
          "truncated terrain fails closed to an empty layer");
    CHECK(badLayer.at(3, 3) == 0, "an empty layer reads 0 everywhere");

    // ── 3. The grid's poke door + the v36 load path ──
    const std::uint32_t rev0 = layer.revision;
    set_tree_count(layer, 4, 4, kMaxTreesPerCell - 5);
    CHECK(layer.at(4, 4) == kMaxTreesPerCell - 5, "the poke door writes");
    CHECK(layer.revision == rev0 + 1, "a real change bumps the revision");
    set_tree_count(layer, 4, 4, -37);
    CHECK(layer.at(4, 4) == 0, "a negative count clamps to zero");
    set_tree_count(layer, 4, 4, kMaxTreesPerCell * 3);
    CHECK(layer.at(4, 4) == kMaxTreesPerCell, "an excess count clamps to max");
    // Leave (4,4) at a value that DIFFERS from its virgin derivation (the
    // golden max): a restore fixture equal to the virgin grid cannot see a
    // disarmed restore — that blindness was caught live by this file's own
    // negative control.
    set_tree_count(layer, 4, 4, kMaxTreesPerCell - 5);
    set_tree_count(layer, 12, -2, 77);
    CHECK(layer.at(4, 6) == 77, "the write torus-wraps its coordinates");
    // Same value again: no revision churn.
    const std::uint32_t revStable = layer.revision;
    set_tree_count(layer, 4, 6, 77);
    CHECK(layer.revision == revStable,
          "writing the same value does not churn the revision");

    // Load path (v36): a fresh derived layer + the saved grid == the mutated
    // layer. This is the "felled forest must not resurrect" negative control
    // at the unit level — the save_roundtrip test holds it end to end.
    TreeLayer reloaded = build_tree_layer(td, forestMask.data(), forestMask.size());
    CHECK(reloaded.at(4, 4) == kMaxTreesPerCell,
          "the fresh derivation is virgin before the restore");
    CHECK_OR_RETURN(restore_tree_counts(reloaded, layer.data),
                    "restoring a matching grid succeeds");
    CHECK(reloaded.at(4, 4) == layer.at(4, 4),
          "the restored layer carries the felled cell, not the virgin one");
    CHECK(reloaded.at(4, 6) == 77, "the restored layer carries every mutation");
    // A size-mismatched grid (corrupt file past the version gate): refused,
    // layer untouched.
    const std::uint16_t before = reloaded.at(4, 4);
    std::vector<std::uint16_t> wrongSize(7, 3);
    CHECK(!restore_tree_counts(reloaded, wrongSize),
          "a size-mismatched grid is refused");
    CHECK(reloaded.at(4, 4) == before,
          "a refused restore leaves the layer untouched");
}

void test_scatter_calibration() {
    SubworldMapData out;
    const int placedFull = run_scatter(kMaxTreesPerCell, 12345u, out);
    const float ratioFull = float(placedFull) / float(kMaxTreesPerCell);
    std::printf("[tree_layer] scatter full: placed=%d target=%d ratio=%.3f\n",
                placedFull, kMaxTreesPerCell, double(ratioFull));
    CHECK(ratioFull > 0.80f && ratioFull < 1.20f,
          "a full cell grows roughly its requested count");
    // Every placed tree stamped its decor tile (the ONE-authority invariant).
    int decor = 0;
    for (auto t : out.tiles) if (t == TILE_TREE_DECOR) ++decor;
    CHECK(decor == placedFull, "every placed tree stamped its decor tile");

    const int placedZero = run_scatter(0, 12345u, out);
    CHECK(placedZero == 0, "count 0 grows nothing");

    const int placedHalf = run_scatter(kMaxTreesPerCell / 2, 777u, out);
    const float ratioHalf = float(placedHalf) / float(kMaxTreesPerCell / 2);
    std::printf("[tree_layer] scatter half: placed=%d target=%d ratio=%.3f\n",
                placedHalf, kMaxTreesPerCell / 2, double(ratioHalf));
    CHECK(ratioHalf > 0.80f && ratioHalf < 1.20f,
          "half the count grows roughly half the trees");
    CHECK(placedHalf < placedFull, "density scales with the count");
}

} // namespace

int main() {
    test_derivation_formula();
    test_build_and_mutation();
    test_scatter_calibration();
    return sm::test::report("tree_layer_test");
}
