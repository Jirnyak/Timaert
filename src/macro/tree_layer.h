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
// draws massifs, not biome carpets. That derivation is the field's INITIAL
// CONDITION only: the forest is the Trees carrier row of the resource-field
// registry (macro/resource_field.h) and lives from there — felling spends
// it, growth thickens it past its virgin state. The save carries the grid
// whole (a living field is not derivable from seed + scars).
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
//     through the registry (resource_field_apply → this grid).
#pragma once
#include "macro/resource_field.h"   // FieldCell — общая ширина клетки поля
#include <cstdint>
#include <vector>
// THE TABLE HALF OF THE FOREST LIVES IN THE NEUTRAL LAYER (§11, 2026-10-03).
// The ceiling, the class line, the biome ambient and the derivation formula
// have no world in them, and a census found NINE files under src/sub/ holding
// an include of THIS header while taking nothing from it, plus three taking
// only those constants. A table both worlds may read is a shared constant,
// not a channel — so it moved, and what stays here is the living GRID.
#include "tables/forest.h"
#include "tables/biomes.h"
#include "macro/features.h"
#include "macro/map_generator.h"

namespace sm {

// Лес уже живёт в uint16 своим носителем, но потолок закрепляется ТЕМ ЖЕ
// числом, что и у ресурсных полей: в день, когда лес переедет в общий штабель
// (наряд M-91), ширина не должна оказаться сюрпризом. Сторож остаётся ЗДЕСЬ,
// а не уезжает с константой: `kMaxFieldUnitsPerCell` — число макро-слоя, и
// tables/ его не видит по построению.
static_assert(kMaxTreesPerCell <= kMaxFieldUnitsPerCell,
              "потолок леса переросл клетку поля");

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
        if (!world_shape_ok(width, height) || data.empty()) return 0;
        const std::size_t i = cell_of(x, y, width);
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

// Set one cell's count: clamps to [0, kMaxTreesPerCell], writes the grid and
// bumps `revision`. This is the GRID's one poke door — gameplay never calls
// it directly, it mutates through resource_field_apply (the Trees carrier
// row), so the ledger and the renderer can never see different forests.
void set_tree_count(TreeLayer& layer, int x, int y, int count);

// Load path: overwrite the grid with the save's counts (the save carries the
// living field whole). Refuses a size mismatch — the version gate makes that
// unreachable short of a corrupt file; virgin derivation then stands.
bool restore_tree_counts(TreeLayer& layer,
                         const std::vector<std::uint16_t>& counts);

} // namespace sm
