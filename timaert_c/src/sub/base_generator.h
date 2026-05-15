// Universal subworld foundation — heightmap, BiomeConfig, grid primitives.
// Mirrors subworld/base-generator.ts.
#pragma once
#include <cstdint>
#include <vector>
#include "sub/map_data.h"

namespace sm::sub
{

    // Single source of truth for sea level / subworld water plane.
    // ----------------------------------------------------------------
    // `WATER_LEVEL`  Heightmap value of the water surface. Used by:
    //   - `generate_heightmap` to remap macro heights (water cells map to
    //     [0, WATER_LEVEL] via squared deep-ocean curve; land cells map
    //     to [WATER_LEVEL + kLandMargin, 1.0]).
    //   - `renderer_3d` for structure-cull threshold.
    //   - `engine` for the visible water plane (`kVisualWaterLevel`).
    // Anything that touches "the water surface" reads this constant.
    //
    // `kMacroSeaLevel`  Fixed at 0.40 to match the macroworld generator's
    //   seaLevel (`map_generator.cpp`, TS `defaultParameters.seaLevel`).
    //   This is where biome assignment flips Water ↔ Land in macro space
    //   and is independent of the subworld water plane.
    //
    // `kLandMargin`  Minimum elevation of a land cell's shoreline above
    //   WATER_LEVEL in the remap. Without it, just-above-sea land maps
    //   to exactly WATER_LEVEL, and bilinear blend with adjacent water
    //   cells (whose remap can reach 0) drags the corner below the
    //   plane → submerged shores and submerged pure-land cells. TS
    //   hides this with a single-cell render; the C++ port renders the
    //   full 3×3 grid (12 internal seams) so the margin is required.
    //   Kept small (0.02) for a gentle, natural beach lift, not a
    //   dramatic cliff.
    constexpr float WATER_LEVEL    = 0.40f;
    constexpr float kMacroSeaLevel = 0.40f;
    constexpr float kLandMargin    = 0.02f;

    struct BiomeConfig
    {
        float treeDensity;
        int treeStep;
        float treeMinSize, treeMaxSize;
        float heightScale;
        float waterLevel;
        bool swampPools;
        bool duneNoise;
    };

    const BiomeConfig &biome_config(Biome b);

    // Per-feature subworld height amplifier removed — TS-faithful generator
    // now derives mountain/relief uplift from `nbBiome[9]` + `nbFeature[9]`
    // directly (see generate_heightmap below). Mountain feature cells get a
    // per-cell mountainScale of 0.3, neighbours of mountain cells get a
    // gradient 0.1 + 0.15 × adjMtn, all bilinearly blended.

    // Build a kCellSize² heightmap using neighbour-aware blending. `nbHeights`
    // is 9 macro heights in row-major order [NW, N, NE, W, C, E, SW, S, SE];
    // `nbBiome` is 9 matching biome enums; `nbFeature` is 9 matching feature
    // types (so mountain feature cells can drive ridge generation). Heights
    // are remapped per-cell with the seaLevel/WATER_LEVEL split (water cells
    // get a squared deep-ocean curve, land cells get a linear lift) and then
    // bilinearly blended into a smooth manifold — this single pass produces
    // natural shorelines, river banks for single-cell water, and gradients
    // from plains to foothills to peaks. No post-clamping.
    void generate_heightmap(std::vector<float> &out,
                            int cellSize,
                            const float nbHeights[9],
                            const Biome nbBiome[9],
                            const std::uint8_t nbFeature[9],
                            Biome biome,
                            std::uint32_t seed,
                            int globalOffsetX = 0,
                            int globalOffsetY = 0);

    // Fill base tiles for a biome (open ground / forest scatter / desert).
    void fill_base_tiles(std::vector<std::uint8_t> &tiles, int cellSize,
                         Biome biome, std::uint32_t seed);

    // Universal tree scatterer — TS port of `scatterUniversalTrees` in
    // base-generator.ts. Walks the cell on a globally-aligned grid keyed by
    // `(globalOffsetX, globalOffsetY)` so adjacent cells stitch their tree
    // distributions seamlessly. Density modulated by biome config + FBM cluster
    // noise; placement decision uses the TS `terrainNoise` hash so identical
    // global coordinates always pick the same trees regardless of which cell
    // computed them. Boosts density when `forestBoost` is true (Forest mode or
    // forest neighbour). Skips tiles within `clearRadius` of the cell centre
    // (used by urban / settlement modes). Pushes a Structure::Tree per placed
    // tree and stamps TILE_TREE_DECOR into the grid.
    void scatter_universal_trees(SubworldMapData &out,
                                 int cellSize,
                                 int globalOffsetX, int globalOffsetY,
                                 Biome biome,
                                 bool forestBoost,
                                 int clearRadius,
                                 std::uint32_t seed);

    // Smooth the heightmap under road / square tiles so paths read as carved
    // into the terrain. The implementation uses a sparse road-index pass, a
    // wide box average, an iterative Laplacian pass, and one shoulder blend.
    // No effect when the cell has no road/square tiles.
    void smooth_road_heights(std::vector<float>& hm,
                             const std::vector<std::uint8_t>& tiles,
                             int width, int height);

    // Same road smoother when the caller already owns a sorted list of
    // road/square tile indices. Used by async seam smoothing to avoid copying
    // and rescanning the full composite tile grid.
    void smooth_road_heights_indexed(std::vector<float>& hm,
                                     const std::vector<std::int32_t>& roadIdx,
                                     int width, int height);

} // namespace sm::sub
