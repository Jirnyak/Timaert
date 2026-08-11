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

    // THE cell-skeleton height law (normalised 0..1): the deterministic
    // per-cell column the generator bilinears into its macro manifold BEFORE
    // any noise or ridges (base_generator.cpp remapped[] / peakHeight[]).
    // Shared with the shadow-march apron (vk_renderer_3d upload): terrain
    // beyond the loaded window is represented by this skeleton at macro-cell
    // resolution, so out-of-window massifs keep casting into the window and
    // the march never pops at a recenter. Mountains answer with their CREST
    // base (the peak law minus its per-cell jitter/neighbour terms, unclamped
    // — the generator adds those on top): the crest is what decides "does
    // that ridge block the sun", and it is the level real in-window ridges
    // reach, so a massif sliding from apron to window changes its cast
    // shadow least.
    inline float skeleton_cell_height01(float macroH, bool isWater,
                                        bool isMountain) {
        if (isWater) {
            const float t = std::max(0.0f, macroH / kMacroSeaLevel);
            return t * t * WATER_LEVEL;
        }
        if (isMountain) return 0.80f + macroH * 0.15f;
        constexpr float kLandFloor = WATER_LEVEL + kLandMargin;
        const float landScale = (1.0f - kLandFloor) / (1.0f - kMacroSeaLevel);
        return kLandFloor + (macroH - kMacroSeaLevel) * landScale;
    }

    struct BiomeConfig
    {
        float treeDensity;
        int treeStep;
        // Height band of a mature tree in METRES, before the per-species
        // scale (sub/tree_atlas.h). The band belongs to the PLACE: the same
        // pine is a 7 m tundra scrub and a 20 m taiga mast. Rolled per tree
        // and stored verbatim in Structure::height.
        float treeMinHeightM, treeMaxHeightM;
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
    // A terrain post-process has TWO radii and they are not the same number.
    // Confusing them makes a caller either wrong or slow:
    //
    //   INPUT reach  — how far away a height can INFLUENCE the result. Pass 1
    //     is a box average of radius 12; pass 2's 80 Laplacian iterations carry
    //     information roughly one tile per iteration along the road chain. This
    //     is what an apron would have to be sized by if this pass ever moved
    //     into per-cell generation (problems.md #14) — and it is finite, which
    //     is why that move is possible at all.
    //
    //   OUTPUT reach — how far from a road tile a height can actually CHANGE.
    //     Every pass writes road tiles only; pass 3 additionally writes their
    //     8-neighbour shoulder. One tile. This is what decides which cells a
    //     finished run has dirtied, and using the input reach here would mark
    //     seven cells where one was touched.
    inline constexpr int kRoadSmoothInputReach = 12 + 80;
    inline constexpr int kRoadSmoothOutputReach = 1;

    void smooth_road_heights_indexed(std::vector<float>& hm,
                                     const std::vector<std::int32_t>& roadIdx,
                                     int width, int height);

} // namespace sm::sub
