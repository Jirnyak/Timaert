// Per-cell tree count — the ONE authority for forests. A forest is not a
// feature (features are man-made: roads); a forest is a NUMBER: how many
// trees the macro cell carries. The golden constant is kMaxTreesPerCell =
// 16384 (2^14): the densest possible forest, a massif cell with all 8
// neighbours in the massif.
//
// The layer is DERIVED at worldgen: the organic forest-massif mask (the
// domain-warped FBM of spawn_trees — the natural лесные массивы) contributes
// 16384 × (massif cells in the 3×3 / 9), smooth by construction (the 3×3
// fraction is a box filter over the binary mask); each biome adds a small
// AMBIENT base, deliberately kept below the map-sprite threshold so the map
// draws massifs, not biome carpets. Regenerated from the seed on every boot,
// NOT serialized wholesale; what persists is the sparse override map in
// GameState (`treeOverrides`): only cells mutated by play (felled trees,
// future woodcutters) — derive-don't-store plus a mutation overlay.
//
// Consumers (everything forests used to do through FT_Tree now reads this):
//   - macro.frag `u_treeMap` (count/16384 as R8) — the map sprite density.
//   - subworld `scatter_universal_trees` — the count IS the tree density
//     target for cell generation (bilinearly blended across the 3×3 ring,
//     so borders stay smooth).
//   - forest-CLASS decisions (subworld Forest mode, forest fauna table,
//     tooltip label) — `is_forest_cell(count)`.
//   - continuous scaling (macro path cost, night-glow canopy occlusion,
//     danger-zone forest boost) — count / 16384.
//   - felling a tree in the subworld decrements the owning cell's count
//     through set_tree_count (micro → macro writeback).
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"

namespace sm {

// 2^14 — the densest forest cell (a massif cell with 8 massif neighbours).
constexpr int kMaxTreesPerCell = 16384;

// Half the golden max (2^13): the forest-CLASS threshold. A cell at or above
// it *behaves* as forest wherever a binary decision is still needed —
// subworld Forest mode, the forest fauna table, the map tooltip. Massif
// interiors (frac ≥ 5/9) qualify; edges and biome ambience do not.
constexpr int kForestClassTreeCount = 8192;
inline bool is_forest_cell(int treeCount) {
    return treeCount >= kForestClassTreeCount;
}

// Sparse persisted mutations: cell index (y*width+x) → current count.
using TreeOverrides = std::unordered_map<std::uint32_t, std::uint16_t>;

// Ambient trees a biome carries OUTSIDE any forest massif — scattered lone
// trees, not woodland. Deliberately capped under ~1475 (= 0.09 × 16384, the
// map-sprite coverage threshold) so biome ambience NEVER draws as forest on
// the map: the map shows the organic massifs, the ambience only thickens the
// subworld ground scatter. Relative order preserves biome character (taiga >
// swamp > meadow > … > desert).
inline constexpr std::uint16_t kBiomeBaseTreeCount[11] = {
    200,    // Tundra
    1400,   // Taiga
    300,    // Snow
    800,    // Valley
    600,    // Meadow
    1300,   // Swamp
    40,     // Desert
    250,    // Steppe
    1450,   // Tropics
    0,      // Water
    250,    // Mountain
};

// The derivation formula: biome ambient base + the massif term,
// 16384 × (massif cells in the 3×3 / 9), clamped to the golden max.
// `forestFrac9` ∈ [0,1]. Water carries nothing.
inline std::uint16_t derived_tree_count(Biome biome, float forestFrac9) {
    if (biome == Biome::Water) return 0;
    const int b = int(biome);
    const int base = (b >= 0 && b < 11) ? int(kBiomeBaseTreeCount[b]) : 0;
    int c = base + int(float(kMaxTreesPerCell) * forestFrac9 + 0.5f);
    if (c > kMaxTreesPerCell) c = kMaxTreesPerCell;
    if (c < 0) c = 0;
    return std::uint16_t(c);
}

struct TreeLayer {
    int width = 0, height = 0;
    std::vector<std::uint16_t> data;
    // Runtime dirty counter — bumped on every mutation so the renderer knows
    // to refresh u_treeMap. Never serialized.
    std::uint32_t revision = 0;

    std::size_t cell_count() const {
        std::size_t n = 0;
        return FeatureLayer::cell_count_for(width, height, n) ? n : 0u;
    }
    bool has_complete_storage() const {
        const std::size_t n = cell_count();
        return n > 0u && data.size() >= n;
    }
    std::uint16_t at(int x, int y) const {
        if (width <= 0 || height <= 0 || data.empty()) return 0;
        const int wx = FeatureLayer::wrap_coord(x, width);
        const int wy = FeatureLayer::wrap_coord(y, height);
        const std::size_t i = std::size_t(wy) * std::size_t(width) + std::size_t(wx);
        return i < data.size() ? data[i] : std::uint16_t(0);
    }
};

// Build the derived layer from terrain (biome classification: water mask,
// mountain elevation, climate matrix — same cascade as resolve_context) and
// the forest-massif mask (`forestMask`: width×height bytes, nonzero = the
// cell belongs to a spawn_trees massif; null/short = no massifs, ambience
// only). Deterministic; torus-wrapped.
TreeLayer build_tree_layer(const TerrainData& terrain,
                           const std::uint8_t* forestMask,
                           std::size_t forestMaskCount);

// Set one cell's count: clamps to [0, kMaxTreesPerCell], writes the layer,
// records the sparse override and bumps `revision`. This is THE mutation path
// (felled trees, future woodcutters) — never poke `data` directly.
void set_tree_count(TreeLayer& layer, TreeOverrides& overrides,
                    int x, int y, int count);

// Re-apply persisted overrides onto a freshly derived layer (load path).
void apply_tree_overrides(TreeLayer& layer, const TreeOverrides& overrides);

} // namespace sm
