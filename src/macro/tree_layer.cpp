#include "macro/tree_layer.h"

namespace sm {

TreeLayer build_tree_layer(const TerrainData& terrain,
                           const std::uint8_t* forestMask,
                           std::size_t forestMaskCount) {
    TreeLayer layer;
    std::size_t n = 0;
    if (!FeatureLayer::cell_count_for(terrain.width, terrain.height, n)
        || terrain.rgba.size() < n * 4u) {
        return layer; // fail closed: empty layer, at() returns 0
    }
    layer.width = terrain.width;
    layer.height = terrain.height;
    layer.data.assign(n, 0);

    const int W = terrain.width, H = terrain.height;
    const bool haveMask = forestMask != nullptr && forestMaskCount >= n;
    auto massif_at = [&](int x, int y) -> bool {
        const int wx = FeatureLayer::wrap_coord(x, W);
        const int wy = FeatureLayer::wrap_coord(y, H);
        return forestMask[std::size_t(wy) * std::size_t(W) + std::size_t(wx)] != 0;
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(W) + std::size_t(x);
            const std::uint8_t mask = terrain.rgba[i * 4u + 3u];
            if (!mask) continue; // water cell: 0 trees
            // THE cell cascade (map_generator.h biome_at_cell) — the private
            // copy that lived here was canon-audit C5.
            const Biome biome = biome_at_cell(terrain, x, y);
            int forest = 0;
            if (haveMask) {
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (massif_at(x + dx, y + dy)) ++forest;
            }
            layer.data[i] = derived_tree_count(biome, float(forest) / 9.0f);
        }
    }
    return layer;
}

void set_tree_count(TreeLayer& layer, int x, int y, int count) {
    if (!layer.has_complete_storage()) return;
    const int wx = FeatureLayer::wrap_coord(x, layer.width);
    const int wy = FeatureLayer::wrap_coord(y, layer.height);
    const std::size_t i = std::size_t(wy) * std::size_t(layer.width) + std::size_t(wx);
    if (i >= layer.data.size()) return;
    if (count < 0) count = 0;
    if (count > kMaxTreesPerCell) count = kMaxTreesPerCell;
    if (layer.data[i] == std::uint16_t(count)) return;
    layer.data[i] = std::uint16_t(count);
    ++layer.revision;
}

bool restore_tree_counts(TreeLayer& layer,
                         const std::vector<std::uint16_t>& counts) {
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
