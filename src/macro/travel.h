// Macro travel stamina drain. Mirrors GameScreen.svelte drainPlayerSpForCell.
#pragma once

#include "macro/biomes.h"
#include "macro/features.h"

namespace sm {

struct GameState;
struct TerrainData;

struct MacroTravelCost {
    Biome biome = Meadow;
    FeatureType feature = FT_None;
    int cellCost = 0;
    float overload = 0.0f;
    int overloadCost = 0;
    int totalCost = 0;
};

bool macro_travel_cost_for_cell(const GameState& gs,
                                const TerrainData& terrain,
                                const FeatureLayer* features,
                                int x, int y,
                                MacroTravelCost& out);

bool drain_player_sp_for_macro_cell(GameState& gs,
                                    const TerrainData& terrain,
                                    const FeatureLayer* features,
                                    int x, int y,
                                    MacroTravelCost* out = nullptr);

} // namespace sm
