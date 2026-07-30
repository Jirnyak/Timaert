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

    // Universal terrain flattening from macro features & landmarks
    // ----------------------------------------------------------------
    // One data row per macro-cell content class decides how much a cell's
    // terrain calms down so the authored content sits naturally:
    //   `damp`     0..1 — scales DOWN ridge weight, mountain noise and the
    //              biome-edge gradient boost for the whole cell (applied to
    //              the per-cell 3×3 tables BEFORE bilinear blending, so
    //              transitions into un-damped neighbours stay seamless).
    //   `plateauR` tiles — radius of a radial pull toward the cell-centre
    //              macro height (smoothstep falloff, full strength at the
    //              centre). Settlements build around their cell centre, so
    //              this gives walls/houses a natural table to stand on
    //              instead of hanging off a mountain face. 0 = no plateau.
    // Adding a new glowing/flattening content class is one row in
    // `terrain_mod_for` — no per-mode terrain code.
    struct TerrainMod
    {
        float damp     = 0.0f;
        float plateauR = 0.0f;
    };

    // Combined mod for a cell: max(damp), max(plateauR) of its landmark class
    // (City/Village/Ruin/Spire) and its feature (roads damp their whole cell —
    // the carved corridor then reads as a pass, not a cliff stair).
    TerrainMod terrain_mod_for(CellLandmarkKind landmark, FeatureType feature);

    // Relief uplift is derived from `nbBiome[9]` directly: mountain cells
    // (Biome::Mountain, elevation-classified) get a per-cell mountainScale of
    // 0.3 and ridgeWeight 1; neighbours of mountain cells get a gradient
    // 0.1 + 0.15 × adjMtn, all bilinearly blended. Features never affect
    // height — they scatter on top (trees) or carve tiles (roads).

    // Build a kCellSize² heightmap using neighbour-aware blending. `nbHeights`
    // is 9 macro heights in row-major order [NW, N, NE, W, C, E, SW, S, SE];
    // `nbBiome` is 9 matching biome enums (Biome::Mountain drives ridges).
    // Heights are remapped per-cell with the seaLevel/WATER_LEVEL split (water
    // cells get a squared deep-ocean curve, land cells get a linear lift) and
    // then bilinearly blended into a smooth manifold — this single pass
    // produces natural shorelines, river banks for single-cell water, and
    // gradients from plains to foothills to peaks. No post-clamping.
    // `nbMods` (optional, 9 entries) carries the per-neighbour TerrainMod —
    // road/settlement flattening resolved by the caller from macro context.
    // Null means "no macro content anywhere" (bare-terrain tests).
    void generate_heightmap(std::vector<float> &out,
                            int cellSize,
                            const float nbHeights[9],
                            const Biome nbBiome[9],
                            Biome biome,
                            std::uint32_t seed,
                            int globalOffsetX = 0,
                            int globalOffsetY = 0,
                            const TerrainMod *nbMods = nullptr);

    // Fill base tiles for a biome (open ground / forest scatter / desert).
    void fill_base_tiles(std::vector<std::uint8_t> &tiles, int cellSize,
                         Biome biome, std::uint32_t seed);

    // Mean survival of the FBM cluster gate in scatter_universal_trees —
    // calibrates macro tree COUNT → per-tile rate so the expected number of
    // PLACED trees over a full flat cell ≈ the cell's count. Measured by
    // tree_layer_test's calibration guard; retune there if the gate changes.
    constexpr float kTreeScatterYield = 0.58f;

    // Universal tree scatterer (evolved from the TS `scatterUniversalTrees`).
    // Walks the cell on ONE globally-aligned lattice keyed by
    // `(globalOffsetX, globalOffsetY)` so adjacent cells stitch their tree
    // distributions seamlessly. Tree density is 3×3-CONTEXTUAL and
    // COUNT-DRIVEN: each ring cell contributes a tree rate (trees/tile²)
    // derived from its macro tree count (`nbTreeCount`, macro/tree_layer.h —
    // the ONE per-cell scalar authority; 16384 = the golden densest forest),
    // bilinearly blended per node — forests deepen among forests, plains grow
    // a smooth опушка on their forest side, water (count 0) contributes none.
    // Placement uses the TS `terrainNoise` hash so identical global
    // coordinates always pick the same trees regardless of which cell
    // computed them. Skips tiles within `clearRadius` of the cell centre
    // (urban modes). Pushes a Structure::Tree per placed tree and stamps
    // TILE_TREE_DECOR — this is the ONE tree authority: every decor tile has
    // a real 3D tree, and felling one writes back to the macro count.
    void scatter_universal_trees(SubworldMapData &out,
                                 int cellSize,
                                 int globalOffsetX, int globalOffsetY,
                                 const Biome nbBiome[9],
                                 const int nbTreeCount[9],
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
