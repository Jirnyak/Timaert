// Macro travel stamina: what one cell of the world costs to cross, and the act
// of paying for it. The cost formula and the exhaustion curve live in
// macro/movement_cost.h — shared with the subworld, which walks the same world
// in smaller steps.
#pragma once

#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/movement_cost.h"
#include "macro/items.h"
#include <cmath>

namespace sm {

struct GameState;
struct TerrainData;
struct TreeLayer;
struct CharacterSheet;
struct Inventory;

// The ONE spelling of the carry-overload surcharge: capacity from the sheet,
// carried weight from the bag, and the "any overload hurts" ceil (sub-1kg
// overload still costs 1 SP). Was copy-pasted in travel.cpp and app/main.cpp.
struct OverloadCharge {
    float overload = 0.0f;  // kg carried over capacity
    int   cost = 0;         // its integer SP surcharge
};
// THE overload law, for any back on the map. It was called
// `player_overload_charge` and only the player was ever charged by it, so a
// caravan hauling a ton of iron marched as briskly as an empty scout — the
// weight was a number in a panel, not a cost. Owner's ruling, 2026-08-27:
// «да, перегруз универсальный всем».
OverloadCharge overload_charge(const CharacterSheet& sheet,
                               const Inventory& inventory);

// The same law from the CACHED capacity a macro leader carries on his runtime
// (ecs::MacroNpcRuntime::carryCap), so a think prices its load without
// rebuilding a derived sheet it does not store. Inline: the macro AI is the
// caller, and it must not have to link the travel translation unit (and its
// terrain/feature world) to ask what a pack weighs.
inline OverloadCharge overload_charge_from_capacity(float capacityKg,
                                                    const Inventory& inventory) {
    const float carried = inventory_weight(inventory);
    const float overload = get_overload_penalty(carried, capacityKg);
    // Native CombatStats are integer POD. Preserve the "any overload hurts"
    // behaviour instead of silently truncating sub-1kg overload to zero.
    return {overload, overload > 0.0f ? int(std::ceil(overload)) : 0};
}

struct MacroTravelCost {
    Biome biome = Meadow;
    FeatureType feature = FT_None;
    // Terrain difficulty of the cell, and the stamina one crossing of it costs
    // (weight × kStaminaPerCell). FRACTIONAL: a meadow is 2 SP but tundra is
    // 2.5, and the carry between steps is the body's one signed spCarry.
    float weight = 0.0f;
    // The traveller's own discount on that ground (travel_skill_efficiency):
    // 1.0 for a novice, lower for a veteran. Reported so the UI can show WHY a
    // march is cheap, and so a test can pin progression.
    float efficiency = 1.0f;
    float cellCost = 0.0f;
    float overload = 0.0f;      // kg carried over capacity
    int   overloadCost = 0;     // its integer SP surcharge
    float totalCost = 0.0f;
};

// `treeLayer` (optional): forest-class cells (macro/tree_layer.h) drain like
// the old FT_Tree undergrowth; null = no forest drag (bare/test contexts).
// `bag` prices the OVERLOAD half of the march — the player's carried weight.
// It is passed in because his bag is an ordinary NpcInventory on his squad
// entity now (macro/player_entity.h); null = an unburdened walker, which is
// exactly what a bare test fixture is.
// `sheet` is the walker's — passed in, not dug out of GameState, because the
// caller owns WHICH sheet walks (phase 4: the player's callers hand the
// EFFECTIVE sheet through player_effective_sheet; a bare test fixture hands
// a base one, which is honestly a walker wearing nothing).
bool macro_travel_cost_for_cell(const CharacterSheet& sheet,
                                const Inventory* bag,
                                const TerrainData& terrain,
                                const FeatureLayer* features,
                                int x, int y,
                                MacroTravelCost& out,
                                const TreeLayer* treeLayer = nullptr,
                                // The cell being stepped FROM — prices the
                                // uphill climb half of the law (downhill and
                                // an originless first step are free).
                                int fromX = -1, int fromY = -1);

// Cross ONE macro cell: resolve its cost and pay it. `spCarry` is the body's
// ONE signed fractional carry (movement_cost.h settle_sp_carry) — the same
// field a macro squad keeps on its runtime, because the player is one. It
// carries the
// fractional remainder between steps (runtime state, never serialised) — pass
// the SAME accumulator the subworld path uses, since it is one body walking.
// Returns false only when the terrain query fails; `out` receives the resolved
// cost either way.
bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const CharacterSheet& sheet,
                                    const Inventory* bag,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    float& spCarry,
                                    MacroTravelCost* out = nullptr,
                                    const TreeLayer* treeLayer = nullptr,
                                    int fromX = -1, int fromY = -1);

} // namespace sm
