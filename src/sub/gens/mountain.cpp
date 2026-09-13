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


// Cave mouths: openings in the rock of a highland cell. Placement is the
// cell's OWN business (the self-contained generator rule), the interior behind
// each is not — the prop's table row says it opens a Cave, and the ordinal it
// carries names WHICH cave of this cell, exactly as a house door names a house.
//
// A mouth is rare on purpose: a cell holds at most a couple, so finding one is
// worth something. Whether a given cell has any at all is the cell's own coin,
// so caves cluster where the rock does rather than dotting every hill.
static void scatter_cave_mouths(SubworldMapData& out, const CellContext& ctx) {
    Rng r(ctx.seed ^ 0xCA5E0FFu);
    // Two thirds of highland cells keep no cave: the map should not become a
    // sieve, and an empty hillside is what makes the next one worth entering.
    if ((r.next_u32() % 3u) != 0u) return;
    const int wanted = 1 + int(r.next_u32() % 2u);
    // Ground the mouth against real rock: the mountain stamp painted TILE_ROCK
    // where the massif stands, so a mouth can only open where there IS stone.
    int placed = 0;
    for (int attempt = 0; attempt < 64 && placed < wanted; ++attempt) {
        const int x = 64 + int(r.next_u32() % std::uint32_t(kCellSize - 128));
        const int y = 64 + int(r.next_u32() % std::uint32_t(kCellSize - 128));
        if (out.tiles[std::size_t(y) * kCellSize + x] != TILE_ROCK) continue;
        Structure m{};
        m.kind = Structure::CaveMouth;
        m.x = float(x) + 0.5f;
        m.y = float(y) + 0.5f;
        // Mouths differ in size, and the size MATTERS: the cavern behind is
        // cut to this footprint (sub/dgn/cave.cpp), so a crack in the rock
        // opens on a burrow and a yawning gap opens on a hall. A constant
        // here would make every cave in the world the same size.
        m.hx = structure_min_half_xy(Structure::CaveMouth)
             * (0.75f + r.next_f01() * 1.25f);
        m.hy = m.hx * (0.4f + r.next_f01() * 0.3f);
        m.radius = m.hx;
        m.height = structure_min_height(Structure::CaveMouth);
        m.yaw = r.next_f01() * 3.14159265f;
        m.tag = std::uint16_t(placed);
        out.structures.push_back(m);
        ++placed;
    }
}
static bool preserves_mountain_surface(std::uint8_t tile) {
    return tile == TILE_WATER || tile == TILE_SHORE || tile == TILE_ROAD
        || tile == TILE_SQUARE || tile == TILE_HOUSE || tile == TILE_WALL
        || tile == TILE_FIELD;
}

static void stamp_mountain_rock(SubworldMapData& out, const CellContext& ctx) {
    if (out.tiles.size() != out.heightmap.size()) return;
    const int gox = ctx.cx * kCellSize;
    const int goy = ctx.cy * kCellSize;
    for (int y = 0; y < kCellSize; ++y) {
        const int ym = std::max(0, y - 1);
        const int yp = std::min(kCellSize - 1, y + 1);
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (preserves_mountain_surface(out.tiles[idx])) continue;
            const float h = out.heightmap[idx];
            if (h < WATER_LEVEL + 0.14f) continue;

            const int xm = std::max(0, x - 1);
            const int xp = std::min(kCellSize - 1, x + 1);
            const float hL = out.heightmap[std::size_t(y) * kCellSize + xm];
            const float hR = out.heightmap[std::size_t(y) * kCellSize + xp];
            const float hD = out.heightmap[std::size_t(ym) * kCellSize + x];
            const float hU = out.heightmap[std::size_t(yp) * kCellSize + x];
            const float slope = std::clamp(std::sqrt((hR - hL) * (hR - hL)
                                                   + (hU - hD) * (hU - hD)) * 18.0f,
                                           0.0f, 1.0f);
            const float high = std::clamp((h - 0.56f) / 0.42f, 0.0f, 1.0f);
            const float mass = smooth_noise01(float(gox + x) * 0.014f + 501.0f,
                                              float(goy + y) * 0.014f + 733.0f,
                                              ctx.seed ^ 0x4D54524Fu);
            const float patch = 0.10f + high * 0.22f + slope * 0.42f;
            if (mass < patch) {
                out.tiles[idx] = TILE_ROCK;
            }
        }
    }
}

void gen_mountain(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;
    // Lay biome ground; mountain material is stamped from the generated
    // mountain context, not guessed later from height as snow/grey.
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    (void)nbFeature;  // wilderness: no road stitching (see gen_open)
    out.structures.clear();
    clear_decor_tiles(out);
    stamp_mountain_rock(out, ctx);
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
    scatter_cave_mouths(out, ctx);
}
} // namespace sm::sub
