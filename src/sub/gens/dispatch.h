// Per-mode generators. Each is a self-contained free function. Adding a new
// generator = one function + one entry in dispatch_generate(). Mirrors
// subworld/{forest,grassland,city,village,...}.ts.
#pragma once
#include "sub/map_data.h"

namespace sm::sub {

// Resolve the SubworldMode from a CellContext (mirrors mode-resolver).
SubworldMode resolve_mode(const CellContext& ctx);

// Single entry point — dispatches to the right generator. `nbHeights` is
// 9 macro heights, `nbBiome` is 9 macro biomes, `nbFeature` is 9 macro
// features in row-major order for the 3×3 neighbourhood centred on this
// cell. Generators use them to blend heightmap, carve organic roads /
// dirt paths that line up across cell boundaries, etc.
// `nbLandmark` (optional, 9 entries) is the neighbours' EFFECTIVE landmark
// (map_data.h effective_landmark) — it drives the universal terrain
// flattening around settlements (base_generator.h TerrainMod). Null keeps
// the centre cell's own landmark (from `ctx`) and assumes bare neighbours,
// which is exact for every synthetic/test context that has no neighbours.
// `nbTreeCount` (optional, 9 entries) is the neighbours' macro tree count
// (macro/tree_layer.h) — THE per-cell tree-density target for the scatter.
// Null or a negative entry means "unknown": the count is re-derived from the
// ring cell's biome + the window's forest fraction (tests / bare contexts).
// `nbFertility` (optional, 9 entries) is the neighbours' fertility01 (the
// macro moisture channel) — the field-plot module gates and sizes each plot
// by its OWNING cell's fertility, so a plot straddling a seam is derived
// identically by both sides. Null or a negative entry falls back to the
// centre cell's own fertility01.
void dispatch_generate(const CellContext& ctx,
                       const float nbHeights[9],
                       const Biome nbBiome[9],
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out,
                       const LandmarkType* nbLandmark = nullptr,
                       const int* nbTreeCount = nullptr,
                       const float* nbFertility = nullptr);

} // namespace sm::sub
