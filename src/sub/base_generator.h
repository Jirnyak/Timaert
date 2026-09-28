// Universal subworld foundation — heightmap, BiomeConfig, grid primitives.
// Mirrors subworld/base-generator.ts.
#pragma once
#include <cstdint>
#include <vector>
#include "sub/map_data.h"

namespace sm::sub
{

    // THE PLANE ITSELF LIVES IN map_data.h — beside the two structs that own
    // it (`CellContext::seaLevel` carries it into generation,
    // `SubworldMapData::waterLevel` is the finished scene's answer). This file
    // holds the LAWS that consume it, and every one of them takes the plane as
    // an ARGUMENT rather than reading a constant.
    //
    // That is not style. The plane used to be spelled twice here — `WATER_LEVEL`
    // for the subworld's water surface and `kMacroSeaLevel` for "where macro
    // flips Water ↔ Land" — both hardcoded 0.40, both claiming to be
    // independent of the other. They never were: the remap below divides by one
    // and multiplies by the other, so they are the SAME number wearing two
    // names, and the macroworld's plane is an editor value the player moves
    // (`LayerParameters::seaLevel`, slider 0.10..0.80). Measured on the owner's
    // stand at 0.60: a water cell legally carries macroH up to 0.60, so
    // `t = macroH / 0.40` reached 1.5 (there was no upper clamp) and the water
    // BED came out at 1.5² × 0.40 = 0.90 while the visible plane still drew at
    // 0.40 — water buried under its own hills, «воды вообще не видно». Below
    // 0.40 the same arithmetic fails the other way and drowns the land. One
    // number, one argument, and both directions retire together.

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
    //
    // `seaLevel` is the SCENE'S plane (CellContext::seaLevel), inherited from
    // the macroworld's own — the law has no plane of its own to fall back on,
    // deliberately: a default here is how the two worlds drifted apart in the
    // first place.
    inline float skeleton_cell_height01(float macroH, bool isWater,
                                        bool isMountain, float seaLevel) {
        if (isWater) {
            // t = 1 at the shoreline, 0 in the deep; squared, so deep water
            // sits well below the plane. A REAL water cell's macroH runs
            // [0, seaLevel) — that is what makes it water (macro/map_generator.h
            // is_water) — so t stays inside [0,1) and the bed stays under the
            // surface without being told to.
            //
            // NO UPPER CLAMP, deliberately. A caller CAN hand this a height the
            // plane calls land while asserting the cell is water — a hand-built
            // fixture does exactly that — and then t exceeds 1 and the bed
            // climbs above the water. Clamping would hide that at the point of
            // READING instead of at the point of birth (AGENTS §5 п.3), and it
            // is not what the owner's 0.60 report needed: with the plane
            // INHERITED the case cannot arise from a real world at all. The
            // streaming placeholder used to clamp here and the generator did
            // not; they are one door now, and the door does not clamp.
            const float t = std::max(0.0f, macroH / seaLevel);
            return t * t * seaLevel;
        }
        if (isMountain) return 0.80f + macroH * 0.15f;
        const float landFloor = seaLevel + kLandMargin;
        const float landScale = (1.0f - landFloor) / (1.0f - seaLevel);
        return landFloor + (macroH - seaLevel) * landScale;
    }

    // The crest's per-cell jitter — hash noise of the cell's own PLACE and the
    // world seed. Declared here because the crest law above is inline and the
    // far world needs both; defined in base_generator.cpp beside the rest of
    // the noise stack.
    float crest_jitter01(int cellGX, int cellGY, std::uint32_t worldSeed);

    // THE cell-skeleton CREST law (normalised 0..1) — the peak a cell's ridges
    // aim at, and the twin of skeleton_cell_height01 above. Extracted from the
    // generator's own loop so there is exactly ONE of it: the far world builds
    // the same massifs the near world does, or the canon's «та же гора»
    // (S18.1) is a wish rather than a property.
    //
    // Every input is a property of the CELL'S PLACE — its macro height, whether
    // it is mountain, how many of its four neighbours are, and its wrapped
    // index — so two windows containing the same cell compute the same crest.
    // (They did not always: the jitter used to be seeded from whichever cell
    // was the window CENTRE, and massif borders stepped 8 m for it.)
    inline float skeleton_cell_peak01(float macroH, bool isWater,
                                      bool isMountain, int adjMountain,
                                      int cellGX, int cellGY,
                                      std::uint32_t worldSeed, float seaLevel) {
        const float jitter = crest_jitter01(cellGX, cellGY, worldSeed) - 0.5f;
        if (isMountain) {
            // Crest base from the skeleton law; jitter and the neighbour-massif
            // lift are the crest's own on top.
            return std::clamp(skeleton_cell_height01(macroH, false, true,
                                                    seaLevel)
                                  + float(adjMountain) * 0.02f
                                  + jitter * 0.045f,
                              0.80f, 1.04f);
        }
        // The crest floor is the plane plus a WIDTH: a non-mountain cell's
        // ridges aim at least this far above the water, whatever the water is.
        return std::clamp(skeleton_cell_height01(macroH, isWater, false,
                                                 seaLevel)
                              + 0.07f + float(adjMountain) * 0.015f
                              + jitter * 0.03f,
                          seaLevel + 0.10f, 1.05f);
    }

    // THE MOUNTAIN SILHOUETTE — and THE far world's, because it is the same
    // function (CANON S18.1: «дальний рельеф не имеет права быть похожим шумом,
    // он обязан быть той же функцией, усечённой»).
    //
    // `coarseOnly` drops the fine octave the near ground carries — a ~6 m crag
    // grain at a ~120-tile wavelength, which at any distance where the far
    // world is drawn has been under a pixel for kilometres. That is the canon's
    // law of distance made literal: DETAIL IS REMOVED, never substituted. What
    // survives is the massif: the same warp, the same two crest octaves, the
    // same compression, the same period closing on the same world.
    //
    // `h` is the manifold the ridges rise out of; `macroH` / `peakTarget` /
    // `ridgeWeight` are the cell columns blended at this tile; `gx, gy` are
    // WORLD TILE coordinates (wrapped by the caller — the noise closes on
    // `worldTiles` and a tile is its place).
    // `seaLevel` floors the valley between the ridges: a massif's basin may sit
    // lower than the plain around it, never below the water.
    float mountain_ridges01(float h, int gx, int gy, float macroH,
                            float peakTarget, float ridgeWeight,
                            float worldTiles, bool coarseOnly, float seaLevel);

    // THE GROUND'S OWN DETAIL, and the law of WHICH octaves a mesh may carry.
    //
    // The near generator lays two octaves over its macro manifold: λ≈125 and
    // λ≈50 tiles. Dropping both from the far world was the mistake that made
    // its lowland a billiard table — at three kilometres a 125 m feature
    // spans 2.4° of screen, which is not detail, it is the ground.
    //
    // What decides is not a flag but NYQUIST: an octave whose wavelength is
    // shorter than twice the mesh's spacing cannot be drawn by that mesh at
    // all — sampling it only aliases. So `minWavelengthTiles` = 2 × step, and
    // the law reads: carry every octave the mesh can represent, drop exactly
    // those it cannot. That is «деталь УБИРАЕТСЯ, а не подменяется» with a
    // measure attached, and it is also why the finished far world is RINGS:
    // halving the step doubles the octaves it may carry, so the detail comes
    // back as you approach instead of being switched on.
    float terrain_detail01(int gx, int gy, float worldTiles,
                           float minWavelengthTiles);

    // THE FAR WORLD'S GROUND, in normalised height. It is the near generator
    // with its detail removed and nothing added: the macro manifold the cells
    // blend into, plus the massif that rises out of it at its coarse octaves.
    //
    // What is deliberately ABSENT is every term that is detail by nature —
    // the multi-octave ground noise, dunes, swamp dips, the settlement
    // plateau. None of them is visible at the ranges this draws, and a far
    // world that carried them would be paying to render what the air has
    // already eaten.
    //
    // The caller supplies the cell columns already blended at this tile (the
    // same bilinear over the four nearest cell CENTRES the near generator
    // does), because a far mesh samples a coarse cell grid once and reads many
    // tiles out of it — asking per tile would re-derive the same nine cells
    // for every vertex.
    inline float far_height01(int gx, int gy, float macroH01, float peak01,
                              float ridgeWeight, float worldTiles,
                              float seaLevel,
                              float gradient01 = 0.0f,
                              float heightScale = 0.0f,
                              float mtnScale = 0.0f,
                              float minWavelengthTiles = 0.0f) {
        // The manifold, plus every octave of ground the mesh can carry. The
        // relief term is the near generator's own: macroH² concentrates the
        // ground's own variation on high land and keeps lowlands calm, and the
        // biome-edge gradient lifts it where two kinds of land meet.
        float h = macroH01;
        if (minWavelengthTiles > 0.0f && heightScale > 0.0f
            && mtnScale > 0.0f) {
            const float noise = terrain_detail01(gx, gy, worldTiles,
                                                 minWavelengthTiles);
            const float relief = macroH01 * macroH01 + gradient01;
            h += (noise - 0.5f) * relief * heightScale * mtnScale;
        }
        if (ridgeWeight <= 0.01f) return std::clamp(h, 0.0f, 2.0f);
        return std::clamp(mountain_ridges01(h, gx, gy, macroH01, peak01,
                                            ridgeWeight, worldTiles,
                                            /*coarseOnly=*/true, seaLevel),
                          0.0f, 2.0f);
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
        // NO `waterLevel` COLUMN. All eleven rows held the same number, and
        // ELEVEN COPIES OF ONE NUMBER ARE NOT A COLUMN (DOD p.9): it had zero
        // readers — the scene's plane comes from `SubworldMapData::waterLevel`,
        // a dungeon's from its own kind row. A biome does not get to decide
        // where the sea is; the world does.
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
    TerrainMod terrain_mod_for(LandmarkType landmark, FeatureType feature);

    // Relief uplift is derived from `nbBiome[9]` directly: mountain cells
    // (Biome::Mountain, elevation-classified) get a per-cell mountainScale of
    // 0.15 and ridgeWeight 1; neighbours of mountain cells get a gradient
    // 0.1 + 0.1 × adjMtn, all bilinearly blended. Features never affect
    // height — they scatter on top (trees) or carve tiles (roads).
    // (The 0.3 / 0.15 this used to quote were the numbers before the mountain
    // seam was flattened, 2026-09-16 — and the far world mirrors these very
    // columns, so a stale number here is a stale number on the horizon.)

    // Build a kCellSize² heightmap using neighbour-aware blending. `nbHeights`
    // is 9 macro heights in row-major order [NW, N, NE, W, C, E, SW, S, SE];
    // `nbBiome` is 9 matching biome enums (Biome::Mountain drives ridges).
    // Heights are remapped per-cell about `seaLevel` — THE SCENE'S PLANE,
    // inherited from the macroworld (CellContext::seaLevel), never a constant of
    // this layer (water cells get a squared deep-ocean curve, land cells get a
    // linear lift) and
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
                            // THE CONTEXT IS ONE RING WIDER THAN THE CELLS IT
                            // BUILDS, and it has to be. The generator asks each
                            // of its nine cells "how many of YOUR four
                            // neighbours are mountain" — a question about that
                            // cell's own place — and a 3×3 cannot answer it for
                            // its own rim: a rim cell cannot see outward, so it
                            // undercounts, and its crest target becomes a
                            // function of WHICH WINDOW is asking. Measured at
                            // the foot of a massif, where it bites hardest:
                            // 8.3-9.7 m of step across a shared border, 1.3-3.4×
                            // the ground's own relief there, against 0.4-1.0×
                            // along the body of the same ridge.
                            //
                            // 25 entries, row-major, the SAME grid one ring
                            // wider: index (1+cy)*5 + (1+cx) is the 3×3 cell
                            // i = cy*3 + cx. Null is legal and means "no world
                            // around this fixture" — then the rim undercounts
                            // exactly as it always did, which is honest for a
                            // lone cell that has no neighbours to miss.
                            const Biome* nbBiome5,
                            Biome biome,
                            std::uint32_t seed,
                            int globalOffsetX,
                            int globalOffsetY,
                            // THE SCENE'S SEA PLANE, normalised — inherited from
                            // the macroworld through CellContext::seaLevel.
                            //
                            // No default, and IN THIS POSITION on purpose. A
                            // default is how the subworld came to remap about
                            // its own 0.40 while the map's plane sat elsewhere.
                            // The position is the other half of the guard:
                            // sitting before a POINTER, an omitted plane cannot
                            // be satisfied by anything, so every stale
                            // positional call fails to COMPILE. Put between
                            // `biome` and `seed` it was silently satisfied by
                            // the seed itself — the call built, the world
                            // remapped about 12345, and only a downstream mesh
                            // test noticed (caught exactly that way,
                            // 2026-09-27).
                            float seaLevel,
                            const TerrainMod *nbMods = nullptr,
                            // The world's width in CELLS. Every global-coordinate
                            // noise below closes on it, so the ground meets
                            // itself at the world's edge instead of stepping
                            // (CANON.md S1). 0 = a lone cell with no world
                            // around it (fixtures): do not wrap.
                            int worldCellsX = 0,
                            // THE WORLD seed, not the centre cell's hash above.
                            // The per-cell crest jitter is a property of the
                            // CELL, so every neighbour must derive it the same
                            // — which is only possible from the world seed plus
                            // the cell's own place (CellContext::worldSeed
                            // exists for exactly this, and the field-plot
                            // lattice already travels on it). 0 is a legal
                            // deterministic value for a fixture; what is NOT
                            // legal is deriving it from whoever is asking.
                            std::uint32_t worldSeed = 0);

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
