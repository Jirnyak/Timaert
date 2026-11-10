#include "macro/tree_layer.h"

namespace sm {

void build_tree_layer(TreeLayer& layer, const TerrainData& terrain,
                      const std::uint8_t* forestMask,
                      std::size_t forestMaskCount) {
    const std::size_t n =
        world_cells_within_cap(terrain.width, terrain.height);
    if (n == 0u || terrain.rgba.size() < n * 4u) {
        tree_layer_reset(layer, 0, 0);
        return; // fail closed: empty layer, at() returns 0
    }
    tree_layer_reset(layer, terrain.width, terrain.height);

    const int W = terrain.width, H = terrain.height;
    const bool haveMask = forestMask != nullptr && forestMaskCount >= n;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(W) + std::size_t(x);
            // Вода — дверь карты, не маска A (M-109).
            if (terrain.is_water(std::uint32_t(i))) continue; // 0 деревьев
            // THE cell cascade (map_generator.h biome_at_cell) — the private
            // copy that lived here was canon-audit C5.
            const Biome biome = biome_at_cell(terrain, x, y);
            int forest = 0;
            if (haveMask) {
                // Сосед массива — шаг ИНДЕКСА (cell_step, ЗАКОН АДРЕСА);
                // здесь стояла лямбда с собственной парой свёрток координат.
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (forestMask[cell_step(std::uint32_t(i), dx, dy, W)]
                            != 0) ++forest;
            }
            layer.data[i] = derived_tree_count(biome, float(forest) / 9.0f);
        }
    }
}

void set_tree_count(TreeLayer& layer, int x, int y, int count) {
    if (!layer.has_complete_storage()) return;
    const int wx = FeatureLayer::wrap_coord(x, layer.width);
    const int wy = FeatureLayer::wrap_coord(y, layer.height);
    const std::size_t i = std::size_t(wy) * std::size_t(layer.width) + std::size_t(wx);
    if (i >= layer.cell_count()) return;
    if (count < 0) count = 0;
    if (count > kMaxTreesPerCell) count = kMaxTreesPerCell;
    if (layer.data[i] == std::uint16_t(count)) return;
    layer.data[i] = std::uint16_t(count);
    ++layer.revision;
}

bool restore_tree_counts(TreeLayer& layer,
                         std::span<const std::uint16_t> counts) {
    if (!layer.has_complete_storage()
        || counts.size() != layer.cell_count()) {
        return false;
    }
    bool changed = false;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        const std::uint16_t v = counts[i] > kMaxTreesPerCell
            ? std::uint16_t(kMaxTreesPerCell) : counts[i];
        if (layer.data[i] != v) { layer.data[i] = v; changed = true; }
    }
    if (changed) ++layer.revision;
    return true;
}

} // namespace sm
