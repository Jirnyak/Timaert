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
void dispatch_generate(const CellContext& ctx,
                       const float nbHeights[9],
                       const Biome nbBiome[9],
                       const std::uint8_t nbFeature[9],
                       SubworldMapData& out);

} // namespace sm::sub
