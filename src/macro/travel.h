// Macro travel stamina: what one cell of the world costs to cross, and the act
// of paying for it. The cost formula and the exhaustion curve live in
// macro/movement_cost.h — shared with the subworld, which walks the same world
// in smaller steps.
#pragma once

#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/movement_cost.h"

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
OverloadCharge player_overload_charge(const CharacterSheet& sheet,
                                      const Inventory& inventory);

struct MacroTravelCost {
    Biome biome = Meadow;
    FeatureType feature = FT_None;
    // Terrain difficulty of the cell, and the stamina one crossing of it costs
    // (weight × kStaminaPerCell). FRACTIONAL: a meadow is 2 SP but tundra is
    // 2.5, and the carry between steps lives in the caller's TravelStamina.
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
bool macro_travel_cost_for_cell(const GameState& gs,
                                const TerrainData& terrain,
                                const FeatureLayer* features,
                                int x, int y,
                                MacroTravelCost& out,
                                const TreeLayer* treeLayer = nullptr,
                                // The cell being stepped FROM — prices the
                                // uphill climb half of the law (downhill and
                                // an originless first step are free).
                                int fromX = -1, int fromY = -1);

// Cross ONE macro cell: resolve its cost and pay it. `stamina` carries the
// fractional remainder between steps (runtime state, never serialised) — pass
// the SAME accumulator the subworld path uses, since it is one body walking.
// Returns false only when the terrain query fails; `out` receives the resolved
// cost either way.
bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    TravelStamina& stamina,
                                    MacroTravelCost* out = nullptr,
                                    const TreeLayer* treeLayer = nullptr,
                                    int fromX = -1, int fromY = -1);

} // namespace sm
