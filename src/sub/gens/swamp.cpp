#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/gens/kit/noise.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/gens/kit/wall.h"
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

void gen_swamp(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const int* nbTreeCount = in.nbTreeCount;
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    // Wilderness: no road stitching (see gen_open) — the neighbour feature
    // ring is not this module's business.
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}
} // namespace sm::sub
