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

static bool stamp_ruin_wall_line(SubworldMapData& out,
                                 float x1, float y1, float x2, float y2) {
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
    bool segmentSuppressed = false;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int x = int(std::floor(x1 + dx * t));
        const int y = int(std::floor(y1 + dy * t));
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                const int px = x + ox;
                const int py = y + oy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                const std::size_t idx = std::size_t(py) * kCellSize + px;
                if (out.tiles[idx] == TILE_ROAD || out.tiles[idx] == TILE_SQUARE
                 || out.tiles[idx] == TILE_HOUSE || out.tiles[idx] == TILE_FIELD) {
                    segmentSuppressed = true;
                    continue;
                }
                out.tiles[idx] = TILE_WALL;
                out.trav[idx] = 0;
            }
        }
    }
    return segmentSuppressed;
}

static void build_ruin_wall(SubworldMapData& out,
                            Rng& r,
                            float radius,
                            int segments,
                            float roughness) {
    constexpr float kPi = 3.14159265f;
    constexpr float kTwoPi = kPi * 2.0f;
    std::array<float, 16> xs{};
    std::array<float, 16> ys{};
    const float cx = float(kCellSize / 2);
    const float cy = float(kCellSize / 2);
    const float phase1 = r.next_f01() * kTwoPi;
    const float phase2 = r.next_f01() * kTwoPi;
    for (int i = 0; i < segments; ++i) {
        const float angle = float(i) * kTwoPi / float(segments);
        // Same ring-noise model as a live settlement's wall, from the one
        // authority in sub/city_layout.h — a ruin is a wall that lost its town,
        // not a second noise model.
        const float harmonic =
            std::sin(angle * 3.0f + phase1) * kSettlementWallRing.harmonic3Amp
          + std::sin(angle * 5.0f + phase2) * kSettlementWallRing.harmonic5Amp;
        const float jitter = (r.next_f01() * 2.0f - 1.0f) * radius * roughness
                           * kSettlementWallRing.jitterAmp;
        const float rr = radius + radius * roughness * harmonic + jitter;
        xs[std::size_t(i)] = cx + std::cos(angle) * rr;
        ys[std::size_t(i)] = cy + std::sin(angle) * rr;
    }
    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, 16> nx = xs;
        std::array<float, 16> ny = ys;
        for (int i = 0; i < segments; ++i) {
            const int prev = (i + segments - 1) % segments;
            const int next = (i + 1) % segments;
            nx[std::size_t(i)] = (xs[std::size_t(prev)] + xs[std::size_t(i)] * 2.0f
                                + xs[std::size_t(next)]) * 0.25f;
            ny[std::size_t(i)] = (ys[std::size_t(prev)] + ys[std::size_t(i)] * 2.0f
                                + ys[std::size_t(next)]) * 0.25f;
        }
        xs = nx;
        ys = ny;
    }
    for (int i = 0; i < segments; ++i) {
        const int next = (i + 1) % segments;
        if ((r.next_u32() % 5u) == 0u) continue;
        const bool suppressed = stamp_ruin_wall_line(
            out, xs[std::size_t(i)], ys[std::size_t(i)],
            xs[std::size_t(next)], ys[std::size_t(next)]);
        if (!suppressed) {
            // Oriented rubble: the surviving stretch leans along its own
            // segment (80 % of the chord, so the ruin keeps honest gaps)
            // instead of one 2-tile axis-aligned crumb at the midpoint.
            const float mx = (xs[std::size_t(i)] + xs[std::size_t(next)]) * 0.5f;
            const float my = (ys[std::size_t(i)] + ys[std::size_t(next)]) * 0.5f;
            const float ddx = xs[std::size_t(next)] - xs[std::size_t(i)];
            const float ddy = ys[std::size_t(next)] - ys[std::size_t(i)];
            const float len = std::sqrt(ddx * ddx + ddy * ddy);
            Structure w{};
            w.kind = Structure::Wall;
            w.x = mx;
            w.y = my;
            w.yaw = std::atan2(ddy, ddx);
            w.hx = std::max(1.0f, len * 0.4f);
            w.hy = 1.0f;
            w.radius = w.hx;
            w.height = 5.0f;
            out.structures.push_back(w);
        }
    }
}

void gen_ruin(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    Rng r(ctx.seed ^ 0xA8A1A11u);
    const int center = kCellSize / 2;
    const RoadDirSet dirs = connected_road_dirs(nbFeature);
    carve_landmark_anchor_roads(out, ctx, dirs, center, ctx.seed ^ 0xA8A1EADu);

    const int difficulty = std::max(1, ctx.landmark.size);
    const int rings = std::max(1, std::min(3, (difficulty + 2) / 3));
    for (int ring = 0; ring < rings; ++ring) {
        const float radius = float(kCellSize) * (0.12f + float(ring) * 0.10f);
        const int segments = 10 + int(r.next_u32() % 7u);
        build_ruin_wall(out, r, radius, segments, 0.25f);
    }

    const int squareSize = 5 + int(r.next_u32() % 5u);
    const int sx = center - squareSize / 2;
    const int sy = center - squareSize / 2;
    for (int y = 0; y < squareSize; ++y) {
        for (int x = 0; x < squareSize; ++x) {
            if (r.next_f01() < 0.8f) {
                out.tiles[std::size_t(sy + y) * kCellSize + sx + x] = TILE_SQUARE;
            }
        }
    }

    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}
} // namespace sm::sub
