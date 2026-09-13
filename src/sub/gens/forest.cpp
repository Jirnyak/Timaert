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

// TS forest.ts parity: coarse, globally-aligned glade scan. The glade
// centers are keyed to absolute tile coordinates so neighbouring forest
// cells agree on where clearings cross a seam.
static void carve_forest_glade(SubworldMapData& out, int lx, int ly, int radius) {
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy > r2) continue;
            const int px = lx + dx;
            const int py = ly + dy;
            if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
            const std::size_t idx = std::size_t(py) * kCellSize + px;
            if (out.tiles[idx] == TILE_TREE_DECOR || out.tiles[idx] == TILE_EMPTY) {
                out.tiles[idx] = TILE_GRASS;
            }
        }
    }

    std::size_t write = 0;
    for (std::size_t read = 0; read < out.structures.size(); ++read) {
        const Structure& s = out.structures[read];
        bool keep = true;
        if (s.kind == Structure::Tree) {
            const int tx = int(std::floor(s.x));
            const int ty = int(std::floor(s.y));
            const int dx = tx - lx;
            const int dy = ty - ly;
            keep = dx * dx + dy * dy > r2;
        }
        if (keep) {
            if (write != read) out.structures[write] = s;
            ++write;
        }
    }
    out.structures.resize(write);
}

static void scatter_forest_glades(const CellContext& ctx, SubworldMapData& out) {
    constexpr int kGladeStep = 48;
    constexpr float kGladeThreshold = 0.72f;
    constexpr int kGladeRadiusMin = 6;
    constexpr int kGladeRadiusMax = 16;

    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    const int startGX = gox + ((kGladeStep - (gox % kGladeStep)) % kGladeStep);
    const int startGY = goy + ((kGladeStep - (goy % kGladeStep)) % kGladeStep);
    const int endGX = gox + kCellSize;
    const int endGY = goy + kCellSize;

    for (int gy = startGY; gy < endGY; gy += kGladeStep) {
        for (int gx = startGX; gx < endGX; gx += kGladeStep) {
            const float n = smooth_noise01(float(gx) * 0.007f + 777.0f,
                                           float(gy) * 0.007f + 888.0f,
                                           ctx.seed);
            if (n < kGladeThreshold) continue;

            const float rn = noise01(gx * 5 + 99999, gy * 7 + 88888, ctx.seed);
            const int radius = kGladeRadiusMin
                + int(std::floor(rn * float(kGladeRadiusMax - kGladeRadiusMin + 1)));
            carve_forest_glade(out, gx - gox, gy - goy, radius);
        }
    }
}

void gen_forest(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
    scatter_forest_glades(ctx, out);
}
} // namespace sm::sub
