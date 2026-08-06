// Subworld tree sprite atlas constants and species dispatch.
//
// Atlas layout: kVariants columns × kTypes rows of kTileSize² RGBA8 tiles.
// Tree type rows (matching macro temperature dispatch):
//   0 = OAK      (temperate broadleaf)
//   1 = CHERRY   (sakura)
//   2 = BIRCH    (cool conifer/birch)
//   3 = AUTUMN   (warm broadleaf)
//   4 = PINE     (cold conifer)
//   5 = WILLOW   (warm temperate)
//   6 = JUNGLE   (tropics)
//
// `tree_type_for(biome, hash)` mirrors the macro temperature → tp dispatch
// using the biome enum (no temperature texture in subworld), so trees in
// each subworld cell match the species shown on the macro map.
#pragma once
#include <cstdint>
#include "macro/biomes.h"

namespace sm::sub {

struct TreeAtlas {
    static constexpr int kTileSize = 64;
    static constexpr int kTypes    = 7;
    static constexpr int kVariants = 8;
};

// Pick a tree species index (row in the atlas) appropriate for the macro
// temperature. `hash` is a per-instance 0..1 noise sample so adjacent trees
// vary. Mirrors TS BaseSubworldGenerator.temperatureToTreeType().
int tree_type_for_temperature(float temperature, float hash);

// Fallback for callers that only have a biome.
int tree_type_for(Biome b, float hash);

// ── Metric build of a tree ─────────────────────────────────────────────────
// A tree's SIZE is authored in two halves, each in exactly one place:
//
//   * the PLACE decides the individual's base height in metres — the biome
//     height band in base_generator.cpp's BiomeConfig, rolled per tree and
//     stored in Structure::height (a tundra dwarf and a rainforest giant are
//     the same species law under a different sky);
//   * the SPECIES scales it — the table below, which lives here because the
//     species itself is only resolved where the atlas row is picked (the
//     renderer, from the 3×3-blended macro temperature).
//
// Everything that draws or measures a tree goes through tree_billboard(), so
// the lit pass, the shadow caster and the smokes can never disagree on how
// tall a tree is or how deep it is seated.

// Crown half-width as a fraction of the tree's height. Keeps the historical
// billboard aspect (a quad 0.625 as wide as it is tall) — the per-species
// silhouette is drawn INSIDE that quad by shaders/tree_sprite.glsl, so the
// crown shape is the art's business, not the quad's.
//
// Read by the SCATTERER only: it turns a rolled height into the record's
// footprint (Structure::radius) once, and everything downstream reads that
// footprint back. Nothing re-derives a width from a height.
inline constexpr float kTreeCrownRatio = 0.3125f;

// How deep the billboard's base is buried below its terrain seat, as a
// fraction of the tree's height. One sprite row is 1/16 (0.0625) of the quad
// and is the drawn ground-contact shadow, so a hair under one row hides the
// seam without swallowing the trunk. (This used to be a fraction of the
// METRIC height applied to a quad sized from the RADIUS — which buried up to
// half of every slim tree.)
inline constexpr float kTreeSeatSinkFrac = 0.05f;

struct TreeBillboard {
    float heightM;    // full quad height = the tree's height, metres
    float halfWidthM; // quad half-width, metres
    float sinkM;      // metres to lower the base below its terrain seat
};

// Per-species height multiplier applied to the place's base height.
float tree_species_height_scale(int species);

// The one sizing law: the record's own metres (Structure::height / ::radius)
// scaled by the species. BOTH extents take the same factor, so the aspect the
// scatterer authored is preserved and neither field is decorative.
TreeBillboard tree_billboard(float baseHeightM, float baseRadiusM, int species);

} // namespace sm::sub
