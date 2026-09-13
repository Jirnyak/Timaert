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

// An honest bridge — the gate-lintel mechanism writ long (owner, 2026-08-29:
// "like the walls' gates: over the water, arches beneath, not a solid dam").
// The span is a chain of level stone DECK chords lifted on zBase — the same
// lift a gate lintel uses, so sub/collide.h support_at carries a walker on
// the deck top and blocked_at stops a flyer rising through it — plus round
// PIERS descending to the bed at the chord joints. Between piers nothing is
// solid: those gaps ARE the arches; the water plane and anyone wading it
// pass under the deck freely. The deck grades from bank height to bank
// height (each chord level, the chain stepping less than a kerb), never
// dipping below the water plane + freeboard. All placement policy lives
// HERE, in the generator (CANON S17), not in the engine.
// How high the deck's top clears the water plane. FILE SCOPE because two laws
// read it: the span itself, and the BRIDGEHEAD test that decides where a lane
// may join it (a fork must land on the bank, not on the ramp — so "bank" is
// measured against the deck's own height, and a second copy of this number
// would silently drift the two apart).
constexpr float kBridgeFreeboardM = 3.0f;

static void add_bridge_segment(SubworldMapData& out, int ax, int ay, int bx, int by) {
    constexpr float kDeckChordTiles = 16.0f; // level piece length (walls: 8)
    constexpr float kDeckHalfWidth  = 2.0f;  // roadway, kerb to kerb 4 tiles
    constexpr float kDeckThickM     = 1.0f;  // the stone slab underfoot
    constexpr float kFreeboardM     = kBridgeFreeboardM;
    constexpr float kPierRadius     = 1.5f;  // round pier half-width, tiles
    constexpr float kRampGradeM     = 0.75f; // ramp fall per chord (< kStepUpM)
    constexpr float kPierFootingM   = 6.0f;  // pier sunk into the bed
    // THE ARCH (owner, 2026-08-30: «небольшая арочка тоже нужна мосту»). The
    // deck rises toward the middle of the water and comes back down to the
    // banks — the shape masonry takes when it spans, and the reason the
    // arches between the piers read as arches instead of a jetty's legs.
    // Its RISE is not authored, it is DERIVED from the crossing: half a metre
    // per chord of half-span, so a brook gets a nearly flat plank and a real
    // river gets a real arch, capped where a bridge stops being a bridge and
    // starts being a hill. The per-chord step of the profile below is
    // kCamberStepM·π/2 ≈ 0.79 m — under kStepUpM, so the arch is walked, never
    // jumped, which is the invariant the flat deck was built to win.
    constexpr float kCamberStepM    = 0.50f; // rise per chord of half-span
    constexpr float kCamberMaxM     = 6.0f;  // cap on the rise at midspan
    // Kerbs: two low stone rails along the roadway. Deliberately BELOW
    // kStepUpM — a parapet you can step over is scenery that never traps
    // anybody, and the bridge still reads as a bridge rather than a plank.
    constexpr float kKerbHalfWidth  = 0.35f;
    constexpr float kKerbHeightM    = 0.6f;
    const float dx = float(bx - ax);
    const float dy = float(by - ay);
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 2.0f) return;
    const float yaw = std::atan2(dy, dx);
    const auto& hm = out.heightmap;
    if (hm.size() != std::size_t(kCellSize) * kCellSize) return;
    // Ground in metres at distance d along the road line.
    const auto ground_m = [&](float d) -> float {
        const float t = std::clamp(d / length, 0.0f, 1.0f);
        const int tx = std::clamp(int(float(ax) + dx * t), 0, kCellSize - 1);
        const int ty = std::clamp(int(float(ay) + dy * t), 0, kCellSize - 1);
        return hm[std::size_t(ty) * kCellSize + tx] * kHeightScaleM;
    };
    // THE deck level: the water plane plus a freeboard, one world height for
    // every span of every cell — because the water it clears is one height
    // (owner, 2026-08-29: «у нас вода одной высоты, почему тогда высота моста
    // скачет»). Everything follows from that: the arms of a hub meet level,
    // two cells' spans meet level across the seam, and a walker crosses
    // without a single step. The deck is stated in WORLD metres (zWorld), so
    // no sampling of the bed can move it — the old bed-relative lift wobbled
    // by the difference between the generator's exact tile heights and the
    // renderer's 16-tile mesh, which is what built the flight of steps.
    const float deckTopM = kSeaLevelM + kFreeboardM;
    // Where the line runs over LAND the deck is not level but a RAMP falling
    // at a gentle grade, and it ends exactly where it meets the ground — no
    // fixed approach length, no lip to jump: whichever comes first, the bank
    // rising or the ramp descending, is where the walker steps off. On a
    // steep bank that is a couple of tiles; on a flat beach the ramp runs
    // until it lands.
    const int chords = std::max(1, int(std::ceil(length / kDeckChordTiles)));
    const float chordLen = length / float(chords);
    // Distance from d to the nearest wet sample along the line (in chords),
    // measured on the chord lattice so both directions fall the same way.
    std::vector<float> topAt(std::size_t(chords) + 1u, deckTopM);
    {
        std::vector<std::uint8_t> wet(std::size_t(chords) + 1u, 0u);
        bool anyWet = false;
        for (int i = 0; i <= chords; ++i) {
            wet[std::size_t(i)] =
                ground_m(float(i) * chordLen) < kSeaLevelM ? 1u : 0u;
            anyWet = anyWet || wet[std::size_t(i)] != 0u;
        }
        if (!anyWet) return;  // dry line: the road itself carries it
        // ONE chamfer distance transform, read both ways: for a DRY joint it
        // is how far the ramp has fallen from the water's edge, for a WET one
        // how far it has climbed toward midspan. Two sweeps each.
        const auto chord_distance_to = [&](bool toWet) {
            std::vector<int> dist(std::size_t(chords) + 1u, chords + 1);
            for (int i = 0; i <= chords; ++i) {
                if ((wet[std::size_t(i)] != 0u) == toWet) dist[std::size_t(i)] = 0;
                else if (i > 0) dist[std::size_t(i)] =
                    std::min(dist[std::size_t(i)], dist[std::size_t(i - 1)] + 1);
            }
            for (int i = chords - 1; i >= 0; --i) {
                dist[std::size_t(i)] =
                    std::min(dist[std::size_t(i)], dist[std::size_t(i + 1)] + 1);
            }
            return dist;
        };
        const std::vector<int> toWet = chord_distance_to(true);
        const std::vector<int> toDry = chord_distance_to(false);
        // Half-span in chords: the deepest a wet joint gets from either bank,
        // which is exactly where the arch peaks.
        // An arch needs TWO SHORES to spring from. When the line never leaves
        // the water — a cell whose crossing runs bank to bank in the
        // NEIGHBOURS, or a lake fixture — there is no dry joint to measure
        // from: the distance transform saturates, every wet joint reads as
        // "deepest", and the deck would rise UNIFORMLY by an arbitrary camber
        // that depends on how many chords the line happens to have. That is a
        // step of metres against the next cell's span, which is the exact
        // class of bug the world-levelled deck exists to prevent. So: no dry
        // joint, no arch — the deck stays at the world's level and meets its
        // neighbours there.
        int halfSpan = 0;
        bool anyDry = false;
        for (int i = 0; i <= chords; ++i) {
            if (wet[std::size_t(i)]) halfSpan = std::max(halfSpan, toDry[std::size_t(i)]);
            else anyDry = true;
        }
        const float camberM = (anyDry && halfSpan > 0)
            ? std::min(kCamberMaxM, kCamberStepM * float(halfSpan))
            : 0.0f;
        constexpr float kHalfPi = 1.57079633f;
        const auto arch_at = [&](int chordsFromBank) {
            const float r = halfSpan > 0
                ? float(chordsFromBank) / float(halfSpan) : 0.0f;
            return deckTopM + camberM * std::sin(kHalfPi * r);
        };
        // The ramp starts from the SPAN, not from the bare level: the first
        // wet chord is already up on the arch, so a ramp falling from the
        // level would open a gap of arch-rise PLUS ramp-fall at the shore —
        // two steps at one joint, and the walker is jumping again. Continuing
        // the deck downward means the only step anywhere is one grade.
        const float bankTopM = arch_at(1);
        for (int i = 0; i <= chords; ++i) {
            if (wet[std::size_t(i)]) {
                // Steepest at the banks, level at the crown: an arch.
                topAt[std::size_t(i)] = arch_at(toDry[std::size_t(i)]);
            } else {
                topAt[std::size_t(i)] =
                    bankTopM - kRampGradeM * float(toWet[std::size_t(i)]);
            }
        }
    }
    for (int i = 0; i < chords; ++i) {
        const float d0 = float(i) * chordLen;
        const float dm = d0 + chordLen * 0.5f;
        const float topM = (topAt[std::size_t(i)]
                            + topAt[std::size_t(i + 1)]) * 0.5f;
        // The ground already carries a walker at this height — the ramp has
        // landed, and stone laid past that point would be a wall, not a road.
        if (topM <= ground_m(dm) + 0.5f) continue;
        Structure deck{};
        deck.kind = Structure::Bridge;
        deck.x = float(ax) + dx * (dm / length);
        deck.y = float(ay) + dy * (dm / length);
        deck.yaw = yaw;
        deck.hx = chordLen * 0.5f + 0.45f;   // overlap, as walls do
        deck.hy = kDeckHalfWidth;
        deck.radius = deck.hx;
        deck.zWorld = true;
        deck.zBase = topM - kDeckThickM;     // absolute bottom of the slab
        deck.height = kDeckThickM;
        out.structures.push_back(deck);
        // Kerbs: the same chord, narrowed and pushed to either edge, standing
        // ON the deck it belongs to (world-levelled like everything here, so
        // a rail can never float off its own slab).
        for (int side = -1; side <= 1; side += 2) {
            const float off = float(side) * (kDeckHalfWidth - kKerbHalfWidth);
            Structure kerb = deck;
            kerb.kind = Structure::Kerb;
            kerb.x = deck.x - dy / length * off;
            kerb.y = deck.y + dx / length * off;
            kerb.hy = kKerbHalfWidth;
            kerb.zBase = topM;
            kerb.height = kKerbHeightM;
            out.structures.push_back(kerb);
        }
        // Pier under the joint behind this chord; its head hides inside the
        // slab so span and support never show a seam, and its footing is sunk
        // into the bed so no sampler disagreement can leave it hanging. The
        // first joint is the approach — no pier there.
        if (i == 0) continue;
        const float jTopM = topAt[std::size_t(i)] - kDeckThickM * 0.5f;
        const float jGroundM = ground_m(d0);
        if (jTopM <= jGroundM + 0.5f) continue;
        Structure pier{};
        pier.kind = Structure::Bridge;
        pier.x = float(ax) + dx * (d0 / length);
        pier.y = float(ay) + dy * (d0 / length);
        pier.radius = kPierRadius;
        pier.shape = Structure::Cylinder;
        pier.zWorld = true;
        pier.zBase = jGroundM - kPierFootingM;
        pier.height = jTopM - pier.zBase;
        out.structures.push_back(pier);
    }
}

// A CROSSING cell lays its roads by a law of its own, and only it does — the
// dry-land road module above is untouched (owner, 2026-08-30: «главное не
// сломать дороги, которые идеально работают»).
//
// The problem it answers: the subworld calls a neighbour connected when that
// neighbour merely CARRIES a road byte — it cannot see whether a path
// actually runs between them. On land that is harmless and rather good: lanes
// merge into highways on their own. Over water it put the junction in the
// middle of the river — a road passing along the far bank dragged an arm out
// onto the span, and the bridge forked (owner's screenshot).
//
// The macro law is what makes a clean answer possible: a bridge is only ever
// laid SQUARE-ON, one cardinal axis in and out (macro/pathfinding.h
// waterCrossAxes). So the real crossing is the AXIS whose two cardinal
// neighbours both carry road, and everything else beside it is a road going
// past. Hence: span the axis straight, and let every other arm meet it at the
// nearest DRY point of that line — the bridgehead. The fork lands on the
// bank, before the bridge, which is what a bridge looks like.
static void gen_bridge_crossing(const CellContext& ctx,
                                const std::uint8_t nbFeature[9],
                                SubworldMapData& out) {
    const int cx = kCellSize / 2;
    const int cy = kCellSize / 2;
    const auto road_at = [&](int dx, int dy) {
        return is_road_feature(nbFeature[(dy + 1) * 3 + (dx + 1)]);
    };
    // A span is straight: masonry does not meander.
    const auto carve = [&](int x1, int y1, int x2, int y2, std::uint32_t seed) {
        carve_road_leg(out, x1, y1, x2, y2, seed, 0.0f);
    };

    // ── The crossings themselves: one per through axis (normally exactly
    // one; a pond bridged both ways honestly gets two). ──────────────────
    struct Leg { int x1, y1, x2, y2; };
    Leg legs[2];
    int nLegs = 0;
    std::uint8_t spanned = 0;   // which cardinal arms the spans consumed
    constexpr int kCardinal[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int axis = 0; axis < 2; ++axis) {
        const int adx = axis == 0 ? 1 : 0;
        const int ady = axis == 0 ? 0 : 1;
        if (!road_at(-adx, -ady) || !road_at(adx, ady)) continue;
        int x1, y1, x2, y2;
        edge_anchor_target(ctx, -adx, -ady, x1, y1);
        edge_anchor_target(ctx, adx, ady, x2, y2);
        legs[nLegs++] = {x1, y1, x2, y2};
        for (int k = 0; k < 4; ++k) {
            if (std::abs(kCardinal[k][0]) == adx
                && std::abs(kCardinal[k][1]) == ady) {
                spanned = std::uint8_t(spanned | (1u << k));
            }
        }
    }
    // Degenerate crossing (a dead end, or neighbours only on one side): fall
    // back to the plain terminus — anchor to centre — so a cell always joins
    // whatever touches it rather than silently going road-free.
    if (nLegs == 0) {
        for (int yy = 0; yy < 3; ++yy) {
            for (int xx = 0; xx < 3; ++xx) {
                if (xx == 1 && yy == 1) continue;
                if (!is_road_feature(nbFeature[yy * 3 + xx])) continue;
                int ex, ey;
                edge_anchor_target(ctx, xx - 1, yy - 1, ex, ey);
                carve(ex, ey, cx, cy, ctx.seed);
                add_bridge_segment(out, ex, ey, cx, cy);
            }
        }
        return;
    }
    // ── THE BRIDGEHEADS: where the road stops being a road ───────────────
    // A crossing is not one thing, it is three: a road down to the water, the
    // SPAN, and a road away from it. Only the middle one is masonry, and only
    // masonry is straight — the owner saw the difference in play (2026-08-30:
    // «у моста дороги становятся идеально прямыми, и это бросается в глаза»),
    // because the whole line from edge to edge was being ruled straight while
    // every road around it wandered.
    //
    // A bridgehead is found by walking the line from its own END INWARD, to
    // the last point still standing on the BANK. Searching the other way (the
    // span point nearest an arm) pinned junctions against the cell edge as
    // right-angled Ts on the seam. Working from the shore outward puts them
    // wherever the bank actually is — close to the river on a steep shore,
    // further inland on a shallow one, and neither is a number anybody typed.
    //
    // "Bank" is measured against the DECK's own height, not the waterline, so
    // the two meet by construction: the span begins exactly where the ground
    // has risen to it, which is why no ramp or step is needed at the joint.
    const float kBankTop01 = (kSeaLevelM + kBridgeFreeboardM) / kHeightScaleM;
    if (out.heightmap.size() != std::size_t(kCellSize) * kCellSize) return;
    const auto height01 = [&](int px, int py) {
        return out.heightmap[std::size_t(py) * kCellSize + px];
    };
    // Walk leg `i` inward from one of its ends; return the last point on the
    // bank. Falls back to that end itself when the line is wet from the very
    // edge (an open-water crossing), which makes the approach a no-op and the
    // span the whole line — exactly the old behaviour, where it belongs.
    const auto bank_point = [&](int i, bool fromStart, int& outX, int& outY) {
        const float lx = float(legs[i].x2 - legs[i].x1);
        const float ly = float(legs[i].y2 - legs[i].y1);
        const float len = std::sqrt(lx * lx + ly * ly);
        outX = fromStart ? legs[i].x1 : legs[i].x2;
        outY = fromStart ? legs[i].y1 : legs[i].y2;
        if (len < 1.0f) return false;
        bool any = false;
        for (float d = 0.0f; d <= len; d += 4.0f) {
            const float t = (fromStart ? d : len - d) / len;
            const int px = std::clamp(int(float(legs[i].x1) + lx * t),
                                      0, kCellSize - 1);
            const int py = std::clamp(int(float(legs[i].y1) + ly * t),
                                      0, kCellSize - 1);
            if (height01(px, py) < kBankTop01) break;   // the bank ends here
            outX = px;
            outY = py;
            any = true;
        }
        return any;
    };

    // Lay each crossing: an ORGANIC road to the near bridgehead, the straight
    // span between the bridgeheads, an organic road on from the far one.
    int headX[2][2]{}, headY[2][2]{};   // [leg][end]
    for (int i = 0; i < nLegs; ++i) {
        const bool haveA = bank_point(i, true,  headX[i][0], headY[i][0]);
        const bool haveB = bank_point(i, false, headX[i][1], headY[i][1]);
        const std::uint32_t seed = ctx.seed + std::uint32_t(i) * 17u;
        // The approaches follow the ground like any other road (terrain A* +
        // the organic wiggle); they are skipped when the bank IS the anchor.
        if (haveA) {
            carve_organic_road(out, legs[i].x1, legs[i].y1,
                               headX[i][0], headY[i][0], seed ^ 0xA71u);
        }
        if (haveB) {
            carve_organic_road(out, headX[i][1], headY[i][1],
                               legs[i].x2, legs[i].y2, seed ^ 0xB72u);
        }
        // The span itself — masonry, and masonry does not meander.
        carve(headX[i][0], headY[i][0], headX[i][1], headY[i][1], seed);
        add_bridge_segment(out, headX[i][0], headY[i][0],
                           headX[i][1], headY[i][1]);
    }

    // ── Everything else joins a BRIDGEHEAD: a lane may run up to the bridge,
    // never onto the middle of it. ───────────────────────────────────────
    const auto bridgehead = [&](int fromX, int fromY, int& outX, int& outY) {
        bool found = false;
        float best = -1.0f;
        for (int i = 0; i < nLegs; ++i) {
            for (int e = 0; e < 2; ++e) {
                const float ddx = float(headX[i][e] - fromX);
                const float ddy = float(headY[i][e] - fromY);
                const float d2 = ddx * ddx + ddy * ddy;
                if (!found || d2 < best) {
                    found = true;
                    best = d2;
                    outX = headX[i][e];
                    outY = headY[i][e];
                }
            }
        }
        return found;
    };
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            if (xx == 1 && yy == 1) continue;
            if (!is_road_feature(nbFeature[yy * 3 + xx])) continue;
            const int adx = xx - 1;
            const int ady = yy - 1;
            bool consumed = false;
            for (int k = 0; k < 4; ++k) {
                if ((spanned & (1u << k)) && kCardinal[k][0] == adx
                    && kCardinal[k][1] == ady) {
                    consumed = true;
                }
            }
            if (consumed) continue;   // this arm IS one end of a span
            int ex, ey;
            edge_anchor_target(ctx, adx, ady, ex, ey);
            int hx, hy;
            // Always answerable now: a bridgehead falls back to the span's own
            // anchor when the crossing runs over open water, so an arm can
            // never be dropped and leave a road-bearing neighbour joined to
            // nothing (the degenerate crossing above makes the same promise).
            if (!bridgehead(ex, ey, hx, hy)) continue;
            // Straight is for MASONRY. This lane runs on the bank, so it is an
            // ordinary road — terrain-followed and wandering, ending exactly
            // on the bridgehead. That is the difference between a road meeting
            // a bridge and two rulers crossing at a right angle.
            carve_organic_road(out, ex, ey, hx, hy, ctx.seed ^ 0x8B1D6Eu);
        }
    }
}

void gen_road(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;
    // Lay biome ground first so the road sits on real grass / steppe / etc.
    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();

    // Collect road-bearing neighbours (skip the centre, idx 4).
    int connectedDx[8], connectedDy[8], nConnected = 0;
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            if (xx == 1 && yy == 1) continue;
            if (!is_road_feature(nbFeature[yy * 3 + xx])) continue;
            connectedDx[nConnected] = xx - 1;
            connectedDy[nConnected] = yy - 1;
            ++nConnected;
        }
    }

    const int cx = kCellSize / 2;
    const int cy = kCellSize / 2;

    // A CROSSING is engineered, and that is the difference between a road and
    // a bridge: on dry land the line follows the ground (terrain A* + organic
    // wiggle — serpentines are the relief's own law), but a span is built to
    // a straight edge and its junctions belong on the bank. That whole law
    // lives in its own module above; the dry road below is exactly what it
    // always was.
    const bool spanning = ctx.biome == Biome::Water;
    if (spanning) {
        gen_bridge_crossing(ctx, nbFeature, out);
        // Under the arches the river is the river. The carved line laid road
        // tiles across the whole crossing, and the submerged half of them was
        // a second road lying on the bed — visible through the water beside
        // its own bridge. The DECK is the road over water; the tiles carry it
        // only where the ramp has landed on dry ground.
        if (out.heightmap.size() == out.tiles.size()) {
            for (std::size_t i = 0; i < out.tiles.size(); ++i) {
                if (out.tiles[i] == TILE_ROAD && out.heightmap[i] < WATER_LEVEL) {
                    out.tiles[i] = TILE_WATER;
                }
            }
        }
        scatter_universal_trees(out, kCellSize,
            ctx.cx * kCellSize, ctx.cy * kCellSize,
            nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
        return;
    }
    const auto carve = [&](int x1, int y1, int x2, int y2, std::uint32_t seed) {
        carve_organic_road(out, x1, y1, x2, y2, seed);
    };

    if (nConnected == 0) {
        // Isolated road cell (no road-bearing neighbour). TS carves nothing
        // here. A visible stub produced scattered orphan road fragments all
        // over the map, so leave the cell road-free.
    } else if (nConnected == 1) {
        // Single connection — TS road-generator carves from the edge anchor
        // to the cell CENTRE and stops (a natural road terminus). Carving on
        // to the opposite edge instead pushed roads into neighbour cells that
        // carry no road feature, producing the fragmented-seam look.
        int ax, ay;
        edge_anchor_target(ctx, connectedDx[0], connectedDy[0], ax, ay);
        carve(ax, ay, cx, cy, ctx.seed);
    } else if (nConnected == 2) {
        // Through-road: connect the two endpoints directly.
        int ax, ay, bx, by;
        edge_anchor_target(ctx, connectedDx[0], connectedDy[0], ax, ay);
        edge_anchor_target(ctx, connectedDx[1], connectedDy[1], bx, by);
        carve(ax, ay, bx, by, ctx.seed);
    } else {
        // 3+ directions: hub at centre, radial arms to each neighbour.
        for (int i = 0; i < nConnected; ++i) {
            int ex, ey;
            edge_anchor_target(ctx, connectedDx[i], connectedDy[i], ex, ey);
            carve(cx, cy, ex, ey, ctx.seed + std::uint32_t(i) * 17u);
        }
    }

    // Farm lanes (Field Inc F6): the road sprouts a smooth track toward
    // every neighbouring FIELD cell, aimed at the SYMMETRIC shared-edge
    // anchor — the exact point the field cell's own lane starts from on its
    // side — so the turn-off reads as one continuous lane across the seam.
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            if (xx == 1 && yy == 1) continue;
            if (FeatureLayer::decode(nbFeature[yy * 3 + xx]) != FT_Field)
                continue;
            int ax = 0, ay = 0;
            edge_anchor_target(ctx, xx - 1, yy - 1, ax, ay);
            carve_organic_road(out, cx, cy, ax, ay,
                               ctx.seed ^ 0x5a17f00du);
        }
    }

    // Vegetation around the road — biome-typical scatter, no urban clear.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount, /*clearRadius*/ 0, ctx.seed);
}
} // namespace sm::sub
