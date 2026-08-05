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
    out.weight = cell_sp_weight(out.biome, out.feature, forest);
    // A trained traveller spends less on the same ground (the `travel` skill's
    // documented -2%/rank, applied here for the first time).
    out.efficiency = travel_skill_efficiency(gs.player.sheet.skills);
    out.cellCost = travel_stamina_cost(out.weight, /*cells*/1.0f, 0,
                                       out.efficiency);

    const OverloadCharge oc =
        player_overload_charge(gs.player.sheet, gs.player.inventory);
    out.overload = oc.overload;
    out.overloadCost = oc.cost;
    out.totalCost = travel_stamina_cost(out.weight, 1.0f, out.overloadCost,
                                        out.efficiency);
    return true;
}

bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    TravelStamina& stamina,
                                    MacroTravelCost* out,
                                    const TreeLayer* treeLayer) {
    MacroTravelCost cost;
    if (!macro_travel_cost_for_cell(gs, terrain, features, x, y, cost,
                                    treeLayer)) {
        return false;
    }

    // One act, one place: accumulate the fractional cost, charge whole SP, and
    // let the exhaustion curve bill the body for whatever stamina could not pay.
    spend_travel_stamina(gs.player.combatStats, stamina, cost.totalCost);

    if (out) {
        *out = cost;
    }
    return true;
}

OverloadCharge player_overload_charge(const CharacterSheet& sheet,
                                      const Inventory& inventory) {
    const float capacity = get_carry_capacity(sheet.attributes, sheet.skills);
    const float carried = inventory_weight(inventory);
    const float overload = get_overload_penalty(carried, capacity);
    // Native CombatStats are integer POD. Preserve the "any overload hurts"
    // behaviour instead of silently truncating sub-1kg overload to zero.
    return {overload, overload > 0.0f ? int(std::ceil(overload)) : 0};
}

} // namespace sm
