// Macro travel stamina drain. Mirrors GameScreen.svelte drainPlayerSpForCell.
#pragma once

#include "macro/biomes.h"
#include "macro/features.h"

namespace sm {

struct GameState;
struct TerrainData;
struct TreeLayer;

struct MacroTravelCost {
    Biome biome = Meadow;
    FeatureType feature = FT_None;
    int cellCost = 0;
    float overload = 0.0f;
    int overloadCost = 0;
    int totalCost = 0;
};

// `treeLayer` (optional): forest-class cells (macro/tree_layer.h) drain like
// the old FT_Tree undergrowth; null = no forest drag (bare/test contexts).
bool macro_travel_cost_for_cell(const GameState& gs,
                                const TerrainData& terrain,
                                const FeatureLayer* features,
                                int x, int y,
                                MacroTravelCost& out,
                                const TreeLayer* treeLayer = nullptr);

bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    MacroTravelCost* out = nullptr,
                                    const TreeLayer* treeLayer = nullptr);

} // namespace sm
