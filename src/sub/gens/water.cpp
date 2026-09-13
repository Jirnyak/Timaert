#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/gens/kit/noise.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/kit/outline.h"
#include "sub/city_layout.h"
#include "core/rng.h"
#include "sub/height.h"
#include "macro/tree_layer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {
// The kit is this module's vocabulary — primitives, never a sibling module.
using namespace kit;

// Water cells: lay biome ground + run the universal tree scatter (water
// biome's treeDensity = 0 → no-op). Final water/land height clamping is a
// dispatch-level post-pass because later generators and road smoothing can
// still alter tile classes and heights.
void gen_water(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const int* nbTreeCount = in.nbTreeCount;
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // Reclassify the DRY margin (coastal blending lifts the near-land part of
    // a water cell above the plane) BEFORE scattering: fill_base_tiles floods
    // the whole cell with authored TILE_WATER, and the scatterer rightly
    // refuses authored tiles — so without this sync a coastal water cell
    // stayed 100% treeless with a hard cut at the land seam (owner report),
    // even though its dry ground now paints as the neighbour's biome. The
    // dispatch-end sync stays (idempotent; re-runs after road smoothing).
    sync_water_tiles_from_heightmap(out);
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}
} // namespace sm::sub
