// The open-air generator modules — one per SubworldMode, one TU each.
//
// A module is SELF-CONTAINED: it owns its content, its numbers and its own
// composition, and it never includes another module. What it may lean on is
// the layer BELOW it — the base terrain generator, and `gens/kit/`, the shared
// primitives (tiles, streets, plots, props, wall). That layering is the whole
// architecture here: siblings are independent, the floor is shared.
//
// This header is only the registry's view of them. It exists so dispatch can
// hold a table instead of a hand-written switch, and so a new kind (castle,
// camp, graveyard) is one TU plus one row — never an edit in four places.
#pragma once
#include "sub/map_data.h"

namespace sm::sub {

// Everything a module is handed. The neighbour rings are the 3×3 macro
// neighbourhood in row-major order, centre at index 4; they are pre-sanitised
// by dispatch, so a module may read them without checking for absence.
//
// One struct rather than five parameters because the list grew twice (tree
// counts, then fertility) and each growth edited eleven signatures and their
// eleven forward declarations. A sixth signal is now a field.
struct GenInput {
    const CellContext&   ctx;
    const Biome*         nbBiome;      // [9] macro biome
    const std::uint8_t*  nbFeature;    // [9] macro feature, already decoded
    const int*           nbTreeCount;  // [9] macro tree count, 0..kMaxTreesPerCell
    const float*         nbFertility;  // [9] macro moisture channel, 0..1
};

// The modules. Declared in SubworldMode order.
void gen_open    (const GenInput& in, SubworldMapData& out);
void gen_city    (const GenInput& in, SubworldMapData& out);
void gen_village (const GenInput& in, SubworldMapData& out);
void gen_forest  (const GenInput& in, SubworldMapData& out);
void gen_mountain(const GenInput& in, SubworldMapData& out);
void gen_swamp   (const GenInput& in, SubworldMapData& out);
void gen_ruin    (const GenInput& in, SubworldMapData& out);
void gen_water   (const GenInput& in, SubworldMapData& out);
void gen_road    (const GenInput& in, SubworldMapData& out);
void gen_spire   (const GenInput& in, SubworldMapData& out);
void gen_field   (const GenInput& in, SubworldMapData& out);

} // namespace sm::sub
