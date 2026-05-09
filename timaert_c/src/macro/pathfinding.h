// A* pathfinding over a torus cost grid. Mirrors pathfinding.ts verbatim.
#pragma once
#include "macro/map_generator.h"  // TerrainData
#include "macro/features.h"
#include <cstdint>
#include <vector>

namespace sm {

struct PathPoint { int x, y; };

// Pre-computed SP-weight grid (1.0 .. 10.0 per cell). Mirrors PathCostData.
struct PathCostData {
    int                width  = 0;
    int                height = 0;
    std::vector<float> costGrid;
};

struct PathResult {
    std::vector<PathPoint> path;
    bool                   found = false;
};

// Build cost grid from terrain master texture + feature layer.
// Mirrors movement-cost.ts buildCostGrid.
PathCostData build_cost_grid(const TerrainData& td,
                             const FeatureLayer* features = nullptr,
                             float seaLevel = 0.40f);

// 8-direction A* with octile heuristic + edge cost = costGrid[dest] * stepLen.
// Indexed binary min-heap (matches TS MinHeap by-key dedup).
// `maxSteps <= 0` means "unlimited" — capped internally at the cell count
// (every cell visited at most once, so termination is guaranteed). The
// previous default (50000) silently failed on large maps (4096² has 16M
// cells; far paths exhaust the budget long before reaching the goal),
// which manifested as "click on far cell, no trajectory drawn".
PathResult find_path(const PathCostData& data,
                     int sx, int sy, int gx, int gy,
                     int maxSteps = 0);

} // namespace sm
