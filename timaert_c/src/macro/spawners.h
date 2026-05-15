// CPU placement of trees, mountains, dirt roads + road-network tracing.
// Mirrors tree-spawner / mountain-spawner / dirt-road-spawner / road-network.
#pragma once
#include <cstddef>
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
    int componentPrunedEdges = 0;
    int expansions = 0;
};

std::vector<TreePoint> spawn_trees(const TerrainData& td, std::uint32_t seed,
                                   float density,
                                   float seaLevel = 0.40f);

// One-time road tracing between connected cities. Deliberately keeps the
// native terrain-cost A* baseline instead of TS corridor snapping. Cross-island
// pairs are component-pruned; same-island pairs use generation-tagged whole-map
// A* and treat rejected water cells as blocked road terrain.
std::vector<std::uint8_t> trace_roads(const TerrainData& td,
                                      Politik& politik,
                                      RoadTraceStats* stats = nullptr,
                                      float seaLevel = 0.40f);

// Dirt-road BFS from villages to nearest road. Fails closed on invalid map
// dimensions, short road masks, or mismatched village coordinate arrays.
// `landMaskA` is an optional terrain RGBA pointer; when a non-zero byte count
// is supplied it must cover width * height * 4. The alpha channel acts as the
// land/water test.
std::vector<std::uint8_t> trace_dirt_roads(int mapW, int mapH,
    const std::vector<std::uint8_t>& roadMask,
    const std::vector<int>& villageX, const std::vector<int>& villageY,
    const std::uint8_t* landMaskA = nullptr,
    std::size_t landMaskByteCount = 0u);

// Build the FeatureLayer from terrain + spawns + roads.
FeatureLayer build_feature_layer(const TerrainData& td,
                                 const std::vector<TreePoint>& trees,
                                 float mountainThreshold,
                                 const std::vector<std::uint8_t>& roadMask,
                                 const std::vector<std::uint8_t>* dirtMask,
                                 float seaLevel = 0.40f);

} // namespace sm
