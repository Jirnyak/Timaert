// Per-cell tree count — the ONE scalar authority for how many trees a macro
// cell carries. The golden constant is kMaxTreesPerCell = 16384 (2^14): the
// densest possible forest, a FT_Tree cell with all 8 neighbours forested.
//
// The layer is DERIVED at worldgen (biome base + forest-feature neighbourhood,
// smooth by construction — the 3×3 fraction is a box filter over the binary
// forest mask), so it is regenerated from the seed on every boot and is NOT
// serialized wholesale. What persists is the sparse override map in GameState
// (`treeOverrides`): only cells whose count was mutated by play (felled trees,
// future woodcutters) — the same "derive, don't store" rule every other layer
// follows, extended with a mutation overlay.
//
// Consumers:
//   - macro.frag `u_treeMap` (count/16384 as R8) — the map sprite density.
//   - subworld `scatter_universal_trees` — the count IS the tree density
//     target for cell generation (bilinearly blended across the 3×3 ring,
//     so borders stay smooth).
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

// 2^14 — the densest forest cell (FT_Tree with 8 forested neighbours).
constexpr int kMaxTreesPerCell = 16384;

// Sparse persisted mutations: cell index (y*width+x) → current count.
using TreeOverrides = std::unordered_map<std::uint32_t, std::uint16_t>;

// Ambient trees a biome carries with NO forest feature anywhere near. Values
// preserve the relative pre-layer biome densities (BiomeConfig treeDensity /
// treeStep² × cell area × scatter yield), capped by the golden constant —
// only Tropics hits the cap: a jungle IS the densest forest.
inline constexpr std::uint16_t kBiomeBaseTreeCount[11] = {
    320,    // Tundra
    14000,  // Taiga
    630,    // Snow
    1970,   // Valley
    1380,   // Meadow
    5600,   // Swamp
    40,     // Desert
    450,    // Steppe
    16384,  // Tropics
    0,      // Water
    350,    // Mountain
};

// The derivation formula: biome ambient base + the forest-feature term,
// 16384 × (forested cells in the 3×3 / 9), clamped to the golden max.
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
// the feature layer (forest 3×3 fraction). Deterministic; torus-wrapped.
TreeLayer build_tree_layer(const TerrainData& terrain, const FeatureLayer& features);

// Set one cell's count: clamps to [0, kMaxTreesPerCell], writes the layer,
// records the sparse override and bumps `revision`. This is THE mutation path
// (felled trees, future woodcutters) — never poke `data` directly.
void set_tree_count(TreeLayer& layer, TreeOverrides& overrides,
                    int x, int y, int count);

// Re-apply persisted overrides onto a freshly derived layer (load path).
void apply_tree_overrides(TreeLayer& layer, const TreeOverrides& overrides);

} // namespace sm
