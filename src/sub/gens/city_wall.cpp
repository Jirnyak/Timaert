#include "sub/gens/city_wall.h"

#include "sub/city_layout.h"
#include "sub/height.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sm::sub {

using kit::Outline;
using kit::WallGate;

namespace {

// ── The masonry's own dimensions, all derived ─────────────────────────────

// Half-thickness from the same ring model the populator reads, so its "inside
// the walls" bound accounts for the real masonry (sub/city_layout.h).
constexpr float kWallHalfThick = kSettlementWallRing.halfThickness;

// Longest straight piece of curtain. Short pieces drape over relief; long ones
// bridge a dip and hang in the air at one end.
constexpr float kWallPieceLen = 8.0f;

// A tower reads as a tower only when it stands proud of the curtain on BOTH
// faces by at least the curtain's own thickness — otherwise it is a lump in
// the wall. Radius = half-thickness × 3 gives exactly that.
constexpr float kTowerRadius = kWallHalfThick * 3.0f;

// Widest opening that still gets an arch. Beyond it the span is a BREACH and
// a lintel would be a bridge, not a gate — but the jambs still go in, because
// the curtain does end there and the ends must be finished.
constexpr float kGateMaxSpan = 18.0f;

// The gate jamb: the tower that finishes a curtain end. Same stone, same rule.
constexpr float kGateJambR = kTowerRadius * 0.5f;

} // namespace

int stamp_city_wall(SubworldMapData& out, const Outline& outline,
                    const CurtainStyle& style, WallGate* gates,
                    int maxGates, const Outline* clip) {
    // A point this ring is not allowed to build on: beyond the boundary it
    // shares with (see the header).
    auto clipped = [&](float x, float y) {
        return clip != nullptr && !clip->contains(x, y, 0.0f);
    };
    constexpr int kNodes = Outline::kBearings;
    std::array<float, kNodes> xs{};
    std::array<float, kNodes> ys{};
    for (int i = 0; i < kNodes; ++i) {
        const float angle = float(i) * Outline::kTwoPi / float(kNodes);
        xs[std::size_t(i)] = outline.cx + std::cos(angle) * outline.r[std::size_t(i)];
        ys[std::size_t(i)] = outline.cy + std::sin(angle) * outline.r[std::size_t(i)];
    }

    auto tile_protected = [&](float fx, float fy) {
        const int tx = std::clamp(int(std::floor(fx)), 0, kCellSize - 1);
        const int ty = std::clamp(int(std::floor(fy)), 0, kCellSize - 1);
        const std::uint8_t t = out.tiles[std::size_t(ty) * kCellSize + tx];
        // Built ground MINUS masonry: the ring yields to a lane, a plaza, a
        // house and a plough — but not to its own courses, or a ring could
        // never close over the stretch it just laid.
        return tile_is(t, kTileBuilt & ~TILE_M_WALL);
    };

    // ── Pass 1 — tile stamp: a 3×3 brush along every chord. ──
    for (int i = 0; i < kNodes; ++i) {
        const int next = (i + 1) % kNodes;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            if (clipped(x1 + dx * t, y1 + dy * t)) continue;
            const int x = int(std::floor(x1 + dx * t));
            const int y = int(std::floor(y1 + dy * t));
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int px = x + ox;
                    const int py = y + oy;
                    if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                    const std::size_t idx = std::size_t(py) * kCellSize + px;
                    // Never paint over a lane or a plaza: those tiles ARE the
                    // openings, and a gate that gets walled shut is a town
                    // with no way in.
                    if (tile_is(out.tiles[idx], kTileBuilt & ~TILE_M_WALL)) continue;
                    out.tiles[idx] = TILE_WALL;
                    out.trav[idx] = 0;
                }
            }
        }
    }

    // ── Pass 2 — oriented bodies. Walk the whole ring at ~1-tile steps,
    // classify every sample, then emit each contiguous run: a wall run as
    // short ORIENTED chords following the curvature, an opening as a real
    // gate (two round jambs plus a lintel lifted clear of the roadway —
    // bodies walk through beneath it, defenders cross on top). ──
    enum class RingCls : std::uint8_t { Wall, Gate, Shared };
    struct RingSample { float x, y; RingCls cls; };
    std::vector<RingSample> ringPts;
    for (int i = 0; i < kNodes; ++i) {
        const int next = (i + 1) % kNodes;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist)));
        for (int s = 0; s < steps; ++s) {  // exclusive: next chord owns its start
            const float t = float(s) / float(steps);
            const float px = x1 + dx * t;
            const float py = y1 + dy * t;
            const int tx = std::clamp(int(std::floor(px)), 0, kCellSize - 1);
            const int ty = std::clamp(int(std::floor(py)), 0, kCellSize - 1);
            const std::uint8_t tile = out.tiles[std::size_t(ty) * kCellSize + tx];
            // THE gate rule: the ring opens where a road already runs. Nothing
            // else opens it, and every opening is therefore a road. A SHARED
            // stretch is neither: the boundary wall stands there already.
            ringPts.push_back({px, py,
                clipped(px, py)             ? RingCls::Shared :
                tile_is(tile, kTilePaved)   ? RingCls::Gate   : RingCls::Wall});
        }
    }

    int gateCount = 0;
    const int n = int(ringPts.size());
    int startIdx = 0;
    while (startIdx < n && ringPts[std::size_t(startIdx)].cls != RingCls::Wall) {
        ++startIdx;
    }
    if (n >= 4 && startIdx < n) {
        auto at = [&](int k) -> const RingSample& {
            return ringPts[std::size_t((startIdx + k) % n)];
        };
        auto emit_wall_run = [&](int a, int b) {  // run [a, b] in rotated index
            int piece0 = a;
            while (piece0 <= b) {
                int piece1 = std::min(b, piece0 + int(kWallPieceLen) - 1);
                const RingSample& p0 = at(piece0);
                const RingSample& p1 = at(piece1);
                const float ddx = p1.x - p0.x;
                const float ddy = p1.y - p0.y;
                const float len = std::sqrt(ddx * ddx + ddy * ddy);
                Structure w{};
                w.kind = Structure::Wall;
                w.x = (p0.x + p1.x) * 0.5f;
                w.y = (p0.y + p1.y) * 0.5f;
                w.yaw = std::atan2(ddy, ddx);
                w.hx = std::max(kWallHalfThick, len * 0.5f + 0.45f);
                w.hy = kWallHalfThick;
                w.radius = w.hx;
                w.height = style.height;
                out.structures.push_back(w);
                piece0 = piece1 + 1;
            }
        };
        auto emit_gate_run = [&](int a, int b) {
            const RingSample& g0 = at(a);
            const RingSample& g1 = at(b);
            const float ddx = g1.x - g0.x;
            const float ddy = g1.y - g0.y;
            const float span = std::sqrt(ddx * ddx + ddy * ddy);
            if (span < 2.0f) return;   // a scratch of paving, not a way through
            const float ux = ddx / span;
            const float uy = ddy / span;
            const float mx = (g0.x + g1.x) * 0.5f;
            const float my = (g0.y + g1.y) * 0.5f;
            // Jambs finish the curtain ends, pulled back into them; skipped if
            // that spot is itself roadway.
            for (int side = 0; side < 2; ++side) {
                const float dir = side == 0 ? -1.0f : 1.0f;
                const float jx = (side == 0 ? g0.x : g1.x) + dir * ux * kGateJambR * 0.5f;
                const float jy = (side == 0 ? g0.y : g1.y) + dir * uy * kGateJambR * 0.5f;
                if (tile_protected(jx, jy)) continue;
                Structure j{};
                j.kind = Structure::Wall;
                j.x = jx;
                j.y = jy;
                j.radius = kGateJambR;
                j.height = style.height + kBodyEyeM;   // a jamb overlooks its own gate
                j.shape = Structure::Cylinder;
                out.structures.push_back(j);
            }
            // The lintel: a bar bridging the opening at arch height. zBase
            // lifts its solid span clear of the roadway. A breach too wide to
            // arch keeps its jambs and goes without.
            if (span <= kGateMaxSpan) {
                Structure l{};
                l.kind = Structure::Wall;
                l.x = mx;
                l.y = my;
                l.yaw = std::atan2(ddy, ddx);
                l.hx = span * 0.5f + 1.2f;
                l.hy = kWallHalfThick;
                l.radius = l.hx;
                // The PROMISE, plainly stated: this much air over the way
                // through. What it has to be measured FROM is not known yet —
                // the ground under a gateway is still being cut by the road
                // smoothing that runs after every generator — so the seat is
                // settled by seat_lifted_spans (sub/height.h) once the map is final.
                l.zBase = kGateClearM;
                l.height = std::max(2.0f, style.height - kGateClearM);
                out.structures.push_back(l);
            }
            if (gates != nullptr && gateCount < maxGates) {
                gates[gateCount] = {mx, my,
                    std::atan2(my - outline.cy, mx - outline.cx), span};
            }
            ++gateCount;
        };
        int runStart = 0;
        RingCls runCls = at(0).cls;
        for (int k = 1; k <= n; ++k) {
            // The sentinel closes the final run: index 0 is a Wall sample by
            // construction (startIdx sought one), so ending on Gate forces the
            // last opening to be emitted.
            const RingCls cls = (k == n) ? RingCls::Wall : at(k).cls;
            if (k < n && cls == runCls) continue;
            if (runCls == RingCls::Wall)      emit_wall_run(runStart, k - 1);
            else if (runCls == RingCls::Gate) emit_gate_run(runStart, k - 1);
            // Shared: nothing to build — the other wall is already there.
            runStart = k;
            runCls = cls;
        }
    }

    // ── Pass 3 — towers WHERE THE RING TURNS. ──
    // Not "every other node": that was a spacing nobody could derive, and it
    // scaled with the node count rather than with the wall. A tower flanks a
    // corner — so a tower stands at each local maximum of the outline's turn,
    // which is parameter-free, self-tuning with the shape, and puts towers on
    // the salients of an organically-grown town rather than on a lattice.
    if (style.towers) {
        // A tower is a STOREY above the curtain, not a bump on it: its floor
        // is the curtain walk's ceiling, so it rises by one full course of the
        // same wall module the curtain is built from. Clearing a defender's
        // eyes (kBodyEyeM, 1.7 m on a 12 m curtain) is the minimum that makes
        // it a tower at all, and it was tried first — at fourteen per cent it
        // is invisible from the ground, which is the same as not building it.
        const float towerH = style.height + structure_min_height(Structure::Wall);
        std::array<float, kNodes> turn{};
        for (int i = 0; i < kNodes; ++i) {
            const int p = (i + kNodes - 1) % kNodes;
            const int q = (i + 1) % kNodes;
            const float ax = xs[std::size_t(i)] - xs[std::size_t(p)];
            const float ay = ys[std::size_t(i)] - ys[std::size_t(p)];
            const float bx = xs[std::size_t(q)] - xs[std::size_t(i)];
            const float by = ys[std::size_t(q)] - ys[std::size_t(i)];
            const float la = std::sqrt(ax * ax + ay * ay);
            const float lb = std::sqrt(bx * bx + by * by);
            if (la <= 0.0f || lb <= 0.0f) continue;
            // |sin| of the turn between successive chords.
            turn[std::size_t(i)] = std::fabs(ax * by - ay * bx) / (la * lb);
        }
        for (int i = 0; i < kNodes; ++i) {
            const float t = turn[std::size_t(i)];
            if (t <= turn[std::size_t((i + kNodes - 1) % kNodes)]) continue;
            if (t <= turn[std::size_t((i + 1) % kNodes)]) continue;
            const float nx = xs[std::size_t(i)];
            const float ny = ys[std::size_t(i)];
            if (clipped(nx, ny)) continue;          // that side is not ours
            if (tile_protected(nx, ny)) continue;   // never in a gateway
            Structure tw{};
            tw.kind = Structure::Wall;
            tw.x = nx;
            tw.y = ny;
            tw.radius = kTowerRadius;
            tw.height = towerH;
            tw.shape = Structure::Cylinder;
            out.structures.push_back(tw);
        }
    }
    return gateCount;
}

} // namespace sm::sub
