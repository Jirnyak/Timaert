#include "sub/gens/gens.h"

// The tower is one object with two sides: this module stamps its exterior,
// sub/dgn/spire_tower the storeys behind it. Its dimensions are therefore a
// SHARED contract, not this module's numbers (sub/dgn/dispatch.h).
#include "sub/dgn/dispatch.h"

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

// Spire landmark: tower footprint plus scorch and crater tiles.
void gen_spire(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const int* nbTreeCount = in.nbTreeCount;
    constexpr int kScorchRadius = 90;
    constexpr int kCraterInner = 18;
    constexpr int kCraterRing = 22;

    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();

    // The tower axis — the exported placement (dgn/dispatch.h
    // kSpireTowerLocalCenter), so the engine reads where it stands.
    const int cx = int(kSpireTowerLocalCenter);
    const int cy = int(kSpireTowerLocalCenter);
    const int scorch2 = kScorchRadius * kScorchRadius;
    for (int y = cy - kScorchRadius; y <= cy + kScorchRadius; ++y) {
        if (y < 0 || y >= kCellSize) continue;
        for (int x = cx - kScorchRadius; x <= cx + kScorchRadius; ++x) {
            if (x < 0 || x >= kCellSize) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 > scorch2) continue;
            const float dist = std::sqrt(float(d2));
            const float falloff = 1.0f - dist / float(kScorchRadius);
            const float n = smooth_noise01(float(x) * 0.08f, float(y) * 0.08f, ctx.seed);
            if (n < falloff * 0.55f) {
                out.tiles[std::size_t(y) * kCellSize + x] = TILE_ROCK;
            }
        }
    }

    const int outer = kCraterInner + kCraterRing;
    const int inner2 = kCraterInner * kCraterInner;
    const int outer2 = outer * outer;
    for (int y = cy - outer; y <= cy + outer; ++y) {
        if (y < 0 || y >= kCellSize) continue;
        for (int x = cx - outer; x <= cx + outer; ++x) {
            if (x < 0 || x >= kCellSize) continue;
            const int dx = x - cx;
            const int dy = y - cy;
            const int d2 = dx * dx + dy * dy;
            if (d2 < inner2 || d2 > outer2) continue;
            const float n = smooth_noise01(float(x) * 0.18f + 200.0f,
                                           float(y) * 0.18f + 200.0f,
                                           ctx.seed);
            out.tiles[std::size_t(y) * kCellSize + x] = n < 0.55f ? TILE_ROCK : TILE_SQUARE;
        }
    }

    // ── The pedestal ────────────────────────────────────────────────────────
    // Everything the tower carries on its crown — the parapet on the rim, the
    // hatch beside the orb — stands OFF the axis, and a grounded structure is
    // seated by the terrain under ITSELF (sub/collide.h). On the damped but
    // still uneven ground a spire cell keeps (terrain_mod_for), that would
    // have rippled the rail around the crown by whatever the slope did across
    // 24 tiles. So the footprint is levelled to its own mean first — the
    // house's cut-vs-fill law, the tower's shape — and painted as the masonry
    // it is, which also keeps the road smoother (which touches road/square
    // paint only) off the pad.
    const float padR = kSpireTowerRadiusTiles + 2.0f;
    float groundM = ctx.macroHeight * kHeightScaleM;
    if (out.heightmap.size() == std::size_t(kCellSize) * kCellSize) {
        const int p = int(padR);
        double sum = 0.0;
        int cnt = 0;
        for (int y = std::max(0, cy - p); y <= std::min(kCellSize - 1, cy + p); ++y) {
            for (int x = std::max(0, cx - p); x <= std::min(kCellSize - 1, cx + p); ++x) {
                const int dx = x - cx, dy = y - cy;
                if (float(dx * dx + dy * dy) > padR * padR) continue;
                sum += out.heightmap[std::size_t(y) * kCellSize + x];
                ++cnt;
            }
        }
        if (cnt > 0) {
            const float level = float(sum / double(cnt));
            groundM = level * kHeightScaleM;
            for (int y = std::max(0, cy - p); y <= std::min(kCellSize - 1, cy + p); ++y) {
                for (int x = std::max(0, cx - p); x <= std::min(kCellSize - 1, cx + p); ++x) {
                    const int dx = x - cx, dy = y - cy;
                    const float d2 = float(dx * dx + dy * dy);
                    if (d2 > padR * padR) continue;
                    const std::size_t i = std::size_t(y) * kCellSize + x;
                    out.heightmap[i] = level;
                    if (d2 <= kSpireTowerRadiusTiles * kSpireTowerRadiusTiles) {
                        out.tiles[i] = TILE_WALL;
                        out.trav[i] = 0;
                    }
                }
            }
        }
    }
    // The crown, in absolute world metres. Stated once here and read by
    // everything standing on it, for the bridge's reason (map_data.h zWorld):
    // a height sampled per structure wobbles by the difference between the
    // generator's exact tile heights and the renderer's 16-tile mesh, and a
    // parapet that wobbles is a parapet with gaps in it.
    const float crownM = groundM + kSpireTowerHeightM;

    // The spire itself: one ROUND tower (the cylinder shape is what makes it
    // a spire and not a crate). Dimensions are the dungeon layer's shared
    // authority (dgn/dispatch.h) — the roof exit stands the player at this
    // exact crown. Its foot is buried one wall course deep, the bridge pier's
    // rule: nothing a sampler disagrees about can leave the tower hanging.
    {
        const float footingM = structure_min_height(Structure::Wall);
        Structure spire{};
        spire.kind = Structure::Wall;
        spire.x = float(cx);
        spire.y = float(cy);
        spire.radius = kSpireTowerRadiusTiles;
        spire.shape = Structure::Cylinder;
        spire.zWorld = true;
        spire.zBase = groundM - footingM;
        spire.height = kSpireTowerHeightM + footingM;
        out.structures.push_back(spire);
    }
    // The parapet: the same chord ring that seals the hall inside, laid on the
    // rim. Sixteen segments put the chord's sag at 0.21 tiles — far inside the
    // wall's own half-thickness, so the rail has no gaps — and each chord runs
    // a half-chord plus a wall's half so neighbours overlap. It stands ON the
    // crown (zWorld, one stated height for every segment), and it is what
    // makes leaving the crown a decision instead of an accident.
    {
        constexpr int kCrownParapetSegments = 16;
        const float wallHalf = structure_min_half_xy(Structure::Wall);
        const float ringR = kSpireTowerRadiusTiles - wallHalf;
        const float halfChord =
            ringR * std::sin(3.14159265f / float(kCrownParapetSegments))
            + wallHalf;
        for (int s = 0; s < kCrownParapetSegments; ++s) {
            const float a = (float(s) + 0.5f) * 2.0f * 3.14159265f
                          / float(kCrownParapetSegments);
            Structure seg{};
            seg.kind = Structure::Wall;
            seg.x = float(cx) + std::cos(a) * ringR;
            seg.y = float(cy) + std::sin(a) * ringR;
            seg.yaw = a + 3.14159265f / 2.0f;   // tangent to the circle
            seg.hx = halfChord;
            seg.hy = wallHalf;
            seg.radius = seg.hx;
            seg.zWorld = true;
            seg.zBase = crownM;
            seg.height = kSpireCrownParapetHeightM;
            out.structures.push_back(seg);
        }
    }
    // The way back in: the lid the top storey's ladder climbs to, from above.
    // Without it the crown was a one-way trip — take the orb, then jump 128 m
    // (owner, 2026-09-09). Its tag carries the tier, exactly as the gate at
    // the foot does, because both open the same tower.
    {
        float hxT = 0.0f, hyT = 0.0f;
        spire_crown_hatch_point(hxT, hyT);
        Structure hatch{};
        hatch.kind = Structure::SpireHatch;
        hatch.x = hxT;
        hatch.y = hyT;
        hatch.hx = structure_min_half_xy(Structure::SpireHatch);
        hatch.hy = hatch.hx;
        hatch.radius = hatch.hx;
        hatch.zWorld = true;
        hatch.zBase = crownM;
        hatch.height = structure_min_height(Structure::SpireHatch);
        hatch.tag = std::uint16_t(std::clamp(ctx.landmark.tier, 1, 5));
        out.structures.push_back(hatch);
    }
    // The gate on the south face — the CaveMouth pattern: a Door-verb prop
    // whose row opens the tower's own interior. tag carries the spell's
    // tier (its OWN context field since §42 — `size` is population for
    // every kind), which the dungeon reads as its storey count
    // (DungeonRef::ordinal).
    {
        Structure gate{};
        gate.kind = Structure::SpireGate;
        gate.x = float(cx);
        gate.y = float(cy) + kSpireTowerRadiusTiles;
        gate.hx = 0.75f; // a leaf 1.5 tiles across, the street-door width
        gate.hy = structure_min_half_xy(Structure::SpireGate);
        gate.radius = std::max(gate.hx, gate.hy);
        gate.height = structure_min_height(Structure::SpireGate);
        gate.tag = std::uint16_t(std::clamp(ctx.landmark.tier, 1, 5));
        out.structures.push_back(gate);
    }
    // The prize on the crown: the orb, seated on the tower top (zBase = the
    // shared tower height) — visible from the ground as a blue star, reached
    // through the climb. A CONSUMED spire raises no orb: the scene shows
    // what the macro world remembers (ctx.landmark.depleted).
    if (!ctx.landmark.depleted) {
        Structure orb{};
        orb.kind = Structure::SpireOrb;
        orb.shape = Structure::Cylinder;
        orb.x = float(cx);
        orb.y = float(cy);
        orb.radius = structure_min_half_xy(Structure::SpireOrb);
        orb.height = structure_min_height(Structure::SpireOrb);
        orb.zWorld = true;      // one crown, one stated height
        orb.zBase = crownM;
        out.structures.push_back(orb);
    }
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ kScorchRadius, ctx.seed);
}
} // namespace sm::sub
