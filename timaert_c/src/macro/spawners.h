// CPU placement of trees, mountains, dirt roads + road-network tracing.
// Mirrors tree-spawner / mountain-spawner / dirt-road-spawner / road-network.
#pragma once
#include <cstdint>
#include <vector>
#include "macro/map_generator.h"
#include "macro/features.h"
#include "macro/politik.h"

namespace sm {

struct TreePoint { int x, y; };

std::vector<TreePoint> spawn_trees(const TerrainData& td, std::uint32_t seed,
                                   float density);

// Bresenham road tracing between connected cities.
// Build the natural road network. Runs A* between every connected city pair
// over a road-aware cost grid (water effectively impassable, mountain high,
// existing roads cheap → encourages branching/sharing). Connections that
// have no land path are pruned from `politik.cities[*].connections` so
// downstream consumers (NPC AI, trade) don't think they exist.
std::vector<std::uint8_t> trace_roads(const TerrainData& td,
                                      Politik& politik);

// Dirt-road BFS from villages to nearest road. `landMaskA` is an optional
// pointer to the terrain RGBA buffer; when supplied the alpha channel acts
// as the land/water test (0 = water → skip).
std::vector<std::uint8_t> trace_dirt_roads(int mapW, int mapH,
    const std::vector<std::uint8_t>& roadMask,
    const std::vector<int>& villageX, const std::vector<int>& villageY,
    const std::uint8_t* landMaskA = nullptr);

// Build the FeatureLayer from terrain + spawns + roads.
FeatureLayer build_feature_layer(const TerrainData& td,
                                 const std::vector<TreePoint>& trees,
                                 float mountainThreshold,
                                 const std::vector<std::uint8_t>& roadMask,
                                 const std::vector<std::uint8_t>* dirtMask);

} // namespace sm
