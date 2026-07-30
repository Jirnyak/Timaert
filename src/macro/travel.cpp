#include "macro/travel.h"

#include "macro/attributes.h"
#include "macro/items.h"
#include "macro/map_generator.h"
#include "macro/movement_cost.h"
#include "macro/state.h"
#include "macro/tree_layer.h"

#include <cmath>
#include <cstddef>

namespace sm {

bool macro_travel_cost_for_cell(const GameState& gs,
                                const TerrainData& terrain,
                                const FeatureLayer* features,
                                int x, int y,
                                MacroTravelCost& out,
                                const TreeLayer* treeLayer) {
    out = MacroTravelCost{};
    if (!terrain.has_rgba_storage() || terrain.width <= 0 || terrain.height <= 0) {
        return false;
    }

    const int wx = FeatureLayer::wrap_coord(x, terrain.width);
    const int wy = FeatureLayer::wrap_coord(y, terrain.height);
    const std::size_t idx =
        std::size_t(wy) * std::size_t(terrain.width) + std::size_t(wx);
    const std::size_t src = idx * 4u;
    if (src + 3u >= terrain.rgba.size()) {
        return false;
    }

    const float height = float(terrain.rgba[src + 0u]) / 255.0f;
    const float moisture = float(terrain.rgba[src + 1u]) / 255.0f;
    const float temperature = float(terrain.rgba[src + 2u]) / 255.0f;
    // Mountains are an elevation-classified biome (like Water), not a feature;
    // biome_at applies the Water / Mountain / climate cascade in one place.
    out.biome = biome_at(temperature, moisture, height,
                         gs.mapParams.seaLevel, kMountainBiomeLevel);
    out.feature = features && features->covers(terrain.width, terrain.height)
        ? features->at(wx, wy)
        : FT_None;
    const bool forest = treeLayer && treeLayer->has_complete_storage()
        && is_forest_cell(int(treeLayer->at(wx, wy)));
    out.cellCost = cell_sp_cost(out.biome, out.feature, forest);

    const float capacity = get_carry_capacity(gs.player.sheet.attributes,
                                              gs.player.sheet.skills);
    const float weight = inventory_weight(gs.player.inventory);
    out.overload = get_overload_penalty(weight, capacity);
    // Native CombatStats are integer POD. Preserve the TS "any overload hurts"
    // behaviour instead of silently truncating sub-1kg overload to zero.
    out.overloadCost = out.overload > 0.0f
        ? int(std::ceil(out.overload))
        : 0;
    out.totalCost = out.cellCost + out.overloadCost;
    return true;
}

bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    MacroTravelCost* out,
                                    const TreeLayer* treeLayer) {
    MacroTravelCost cost;
    if (!macro_travel_cost_for_cell(gs, terrain, features, x, y, cost,
                                    treeLayer)) {
        return false;
    }

    auto& cs = gs.player.combatStats;
    cs.currentSp -= cost.totalCost;
    if (cs.currentSp < 0) {
        cs.currentHp += cs.currentSp;
    }

    if (out) {
        *out = cost;
    }
    return true;
}

} // namespace sm
