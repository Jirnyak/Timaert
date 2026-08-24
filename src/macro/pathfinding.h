// A* pathfinding over a torus cost grid. Mirrors pathfinding.ts.
#pragma once

#include "macro/features.h"
#include "macro/movement_cost.h" // kClimbSpWeight — the edge half of the law
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
        std::vector<float> costGrid;       // the CELL half: bed + canopy
        std::vector<std::uint8_t> water;   // 1 = open water cell
        // The height byte per cell (terrain R), baked beside the weights so
        // every walker prices the EDGE half of the law — the uphill climb —
        // without reaching back into the terrain (movement_cost.h).
        std::vector<std::uint8_t> height8;

        // The edge-climb term between two cells of this grid, by index:
        // kClimbSpWeight × max(0, Δh01), downhill free. Zero when heights
        // are not baked (the silent legal contribution).
        float climb(std::size_t fromIdx, std::size_t toIdx) const {
            if (height8.size() != costGrid.size()) return 0.0f;
            const int dh = int(height8[toIdx]) - int(height8[fromIdx]);
            return dh > 0 ? kClimbSpWeight * (float(dh) / 255.0f) : 0.0f;
        }
    };

    struct PathResult
    {
        std::vector<PathPoint> path;
        bool found = false;
    };

    // Build cost grid from terrain master texture + feature layer + the
    // tree-count layer (forest-class cells drag like the old FT_Tree).
    // Mirrors movement-cost.ts buildCostGrid.
    // Water rides the terrain's baked land mask (map_generator.h
    // biome_at_cell) — the old float seaLevel parameter re-derived it by
    // threshold and could disagree with the mask at the coast.
    PathCostData build_cost_grid(const TerrainData &td,
                                 const FeatureLayer *features = nullptr,
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
