// A* pathfinding over a torus cost grid. Mirrors pathfinding.ts.
#pragma once

#include "macro/features.h"
#include "macro/map_generator.h" // TerrainData

#include <cstdint>
#include <vector>

namespace sm
{

    struct TreeLayer;

    struct PathPoint
    {
        int x, y;
    };

    // Pre-computed SP-weight grid (1.0 .. 10.0 per cell). Mirrors PathCostData.
    // `water` marks the cells whose weight IS the water weight, as data rather
    // than a magic-number comparison: the macro march needs to know where a
    // body cannot make camp (Session 21 — no Resting at sea), and reading the
    // biome back out of a float would be the two-tables drift in miniature.
    struct PathCostData
    {
        int width = 0;
        int height = 0;
        std::vector<float> costGrid;
        std::vector<std::uint8_t> water;   // 1 = open water cell
    };

    struct PathResult
    {
        std::vector<PathPoint> path;
        bool found = false;
    };

    // Build cost grid from terrain master texture + feature layer + the
    // tree-count layer (forest-class cells drag like the old FT_Tree).
    // Mirrors movement-cost.ts buildCostGrid.
    PathCostData build_cost_grid(const TerrainData &td,
                                 const FeatureLayer *features = nullptr,
                                 float seaLevel = 0.40f,
                                 const TreeLayer *treeLayer = nullptr);

    // 8-direction A* with octile heuristic + edge cost = costGrid[dest] * stepLen.
    // Indexed binary min-heap (matches TS MinHeap by-key dedup).
    // `maxSteps <= 0` (default) means "unlimited" — capped internally at the cell
    // count (every cell visited at most once, so termination is guaranteed). The
    // previous TS-mirrored 50_000 default silently failed on large maps (a 1024²
    // grid has ~1M cells; far paths exhaust that budget long before reaching the
    // goal). Native build prefers a hard correctness floor over a TS-shaped cap.
    PathResult find_path(const PathCostData &data,
                         int sx, int sy, int gx, int gy,
                         int maxSteps = 0);

} // namespace sm
