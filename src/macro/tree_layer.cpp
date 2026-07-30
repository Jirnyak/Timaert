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
            const float h = float(terrain.rgba[i * 4u + 0u]) / 255.0f;
            const float m = float(terrain.rgba[i * 4u + 1u]) / 255.0f;
            const float t = float(terrain.rgba[i * 4u + 2u]) / 255.0f;
            // Mask decides Water, elevation decides Mountain, climate the rest —
            // the same cascade as resolve_context / biome_at.
            const Biome biome = h >= kMountainBiomeLevel
                ? Biome::Mountain : biome_from_climate(t, m);
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

void set_tree_count(TreeLayer& layer, TreeOverrides& overrides,
                    int x, int y, int count) {
    if (!layer.has_complete_storage()) return;
    const int wx = FeatureLayer::wrap_coord(x, layer.width);
    const int wy = FeatureLayer::wrap_coord(y, layer.height);
    const std::size_t i = std::size_t(wy) * std::size_t(layer.width) + std::size_t(wx);
    if (i >= layer.data.size()) return;
    if (count < 0) count = 0;
    if (count > kMaxTreesPerCell) count = kMaxTreesPerCell;
    if (layer.data[i] == std::uint16_t(count)) return;
    layer.data[i] = std::uint16_t(count);
    overrides[std::uint32_t(i)] = std::uint16_t(count);
    ++layer.revision;
}

void apply_tree_overrides(TreeLayer& layer, const TreeOverrides& overrides) {
    if (!layer.has_complete_storage() || overrides.empty()) return;
    const std::size_t n = layer.cell_count();
    bool changed = false;
    for (const auto& [idx, count] : overrides) {
        if (std::size_t(idx) >= n) continue; // stale save vs smaller map: skip
        const std::uint16_t v = count > kMaxTreesPerCell
            ? std::uint16_t(kMaxTreesPerCell) : count;
        if (layer.data[idx] != v) { layer.data[idx] = v; changed = true; }
    }
    if (changed) ++layer.revision;
}

} // namespace sm
