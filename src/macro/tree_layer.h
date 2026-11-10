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
//     through the registry (field_move → this grid).
#pragma once
#include "macro/resource_field.h"   // FieldCell — общая ширина клетки поля
#include "core/caps.h"              // kWorldCellCap — кап клеток мира
#include <cstdint>
#include <cstring>
#include <span>
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

// ПЛОСКИЙ МАССИВ ПОД КАПОМ КЛЕТОК МИРА (M-180, 2026-11-10): heap-члена у слоя
// нет. Здесь стоял `std::vector<std::uint16_t>`, и «построен ли слой»
// отвечала ДЛИНА ВЕКТОРА — второй носитель размера мира рядом с
// `width/height`. Теперь носитель один: слой жив, когда его стороны называют
// мир в пределах капа. Живые клетки — первые `cell_count()`; хвост до капа
// стоит нулём, его чистит `tree_layer_reset`. Структура весит 2 МиБ и на
// стеке не живёт — она лежит полем состояния мира
// (`GameState::treeLayer@src/macro/state.h`), а состояние рождается в куче;
// двери ниже ПИШУТ В НЕЁ, а не возвращают её значением.
struct TreeLayer {
    int width = 0, height = 0;
    // Runtime dirty counter — bumped on every mutation so the renderer knows
    // to refresh u_treeMap. Never serialized.
    std::uint32_t revision = 0;
    std::uint16_t data[kWorldCellCap] = {};

    std::size_t cell_count() const {
        return world_cells_within_cap(width, height);
    }
    bool has_complete_storage() const { return cell_count() > 0u; }
    // Живые клетки слоя — ровно мир, не кап (их и везёт сейв).
    std::span<const std::uint16_t> cells() const {
        return {data, cell_count()};
    }
    std::span<std::uint16_t> cells() { return {data, cell_count()}; }
    std::uint16_t at(int x, int y) const {
        if (!world_shape_ok(width, height) || cell_count() == 0u) return 0;
        return data[cell_of(x, y, width)];
    }
};
static_assert(sizeof(TreeLayer) == 12u + 2u * kWorldCellCap,
              "слой леса: 12 Б шапки + u16 на клетку капа, без хвоста");

// Родить ПУСТОЙ слой над миром w×h: стороны названы, весь кап обнулён.
// Мир вне капа (или с неположительной стороной) рождает слой без сторон —
// fail-closed, `at()` отвечает 0. Единственная дверь рождения: фикстура,
// которой нужен лес нужной плотности, зовёт её и пишет клетки через
// `cells()`.
// Чистится ВЕСЬ кап: хвост за миром обязан стоять нулём, иначе байты слоя
// помнили бы прошлый мир.
inline void tree_layer_reset(TreeLayer& layer, int width, int height) {
    const bool fits = world_cells_within_cap(width, height) > 0u;
    layer.width = fits ? width : 0;
    layer.height = fits ? height : 0;
    std::memset(layer.data, 0, sizeof(layer.data));
    ++layer.revision;
}

// Build the derived layer from terrain (biome classification: water mask,
// mountain elevation, climate matrix — same cascade as resolve_context) and
// the forest-massif mask (`forestMask`: width×height bytes, nonzero = the
// cell belongs to a spawn_trees massif; null/short = no massifs, ambience
// only). Deterministic; torus-wrapped. Пишет в `out` на месте: слой весит
// 2 МиБ, и возврат значением клал бы его на стек звонящего.
void build_tree_layer(TreeLayer& out, const TerrainData& terrain,
                      const std::uint8_t* forestMask,
                      std::size_t forestMaskCount);

// Set one cell's count: clamps to [0, kMaxTreesPerCell], writes the grid and
// bumps `revision`. This is the GRID's one poke door — gameplay never calls
// it directly, it mutates through field_move (the Trees carrier
// row), so the ledger and the renderer can never see different forests.
void set_tree_count(TreeLayer& layer, int x, int y, int count);

// (Дверь наката клеток из сейва стояла здесь и умерла 2026-11-10, ломоть 3
// сейв-эпика: единственный её звонящий — свёртка загрузки — получает лес
// полем состояния, а ряд Trees провода читает клетки прямо в слой
// (`write_world_fields@src/macro/world_fields.h` и её зеркало чтения).)

} // namespace sm
