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

struct RoadTraceStats {
    int cityCount = 0;
    int attemptedEdges = 0;
    int keptEdges = 0;
    int prunedEdges = 0;
    int boundedEdges = 0;
    int fallbackEdges = 0;
    int expansions = 0;
    int edgeExpansionCapHits = 0;
    int wholeExpansionCapHits = 0;
};

std::vector<TreePoint> spawn_trees(const TerrainData& td, std::uint32_t seed,
                                   float density);

// Boot-safe road tracing between connected cities. Uses bounded reusable
// scratch routing plus a strict dry Bresenham fallback, then prunes failed
// links from both city endpoints.
std::vector<std::uint8_t> trace_roads(const TerrainData& td,
                                      Politik& politik,
                                      RoadTraceStats* stats = nullptr);

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
