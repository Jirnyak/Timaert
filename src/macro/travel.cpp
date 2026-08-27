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
                                const Inventory* bag,
                                const TerrainData& terrain,
                                const FeatureLayer* features,
                                int x, int y,
                                MacroTravelCost& out,
                                const TreeLayer* treeLayer,
                                int fromX, int fromY) {
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

    // THE cell cascade (map_generator.h biome_at_cell): mask-decided water,
    // elevation-decided mountain, climate the rest — one answer everywhere.
    out.biome = biome_at_cell(terrain, wx, wy);
    out.feature = features && features->covers(terrain.width, terrain.height)
        ? features->at(wx, wy)
        : FT_None;
    const float density = treeLayer && treeLayer->has_complete_storage()
        ? float(treeLayer->at(wx, wy)) / float(kMaxTreesPerCell)
        : 0.0f;
    out.weight = cell_sp_weight(out.biome, out.feature, density);
    // The climb half of the law (movement_cost.h): stepping INTO this cell
    // from `fromX,fromY` pays the uphill difference; downhill and a first
    // step with no origin (-1) are free.
    if (fromX >= 0 && fromY >= 0) {
        const int fx = FeatureLayer::wrap_coord(fromX, terrain.width);
        const int fy = FeatureLayer::wrap_coord(fromY, terrain.height);
        const int dh = int(terrain.height_at(wx, wy))
                     - int(terrain.height_at(fx, fy));
        if (dh > 0) out.weight += kClimbSpWeight * (float(dh) / 255.0f);
    }
    // A trained traveller spends less on the same ground (the `travel` skill's
    // documented -2%/rank, applied here for the first time).
    out.efficiency = travel_skill_efficiency(gs.player.sheet.skills);
    out.cellCost = travel_stamina_cost(out.weight, /*cells*/1.0f, 0,
                                       out.efficiency);

    const OverloadCharge oc =
        overload_charge(gs.player.sheet, bag ? *bag : Inventory{});
    out.overload = oc.overload;
    out.overloadCost = oc.cost;
    out.totalCost = travel_stamina_cost(out.weight, 1.0f, out.overloadCost,
                                        out.efficiency);
    return true;
}

bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const Inventory* bag,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    TravelStamina& stamina,
                                    MacroTravelCost* out,
                                    const TreeLayer* treeLayer,
                                    int fromX, int fromY) {
    MacroTravelCost cost;
    if (!macro_travel_cost_for_cell(gs, bag, terrain, features, x, y, cost,
                                    treeLayer, fromX, fromY)) {
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

OverloadCharge overload_charge(const CharacterSheet& sheet,
                               const Inventory& inventory) {
    return overload_charge_from_capacity(
        get_carry_capacity(sheet.attributes, sheet.skills), inventory);
}

} // namespace sm
