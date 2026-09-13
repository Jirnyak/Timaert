#include "sub/gens/village_palisade.h"

#include "sub/height.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sm::sub {

namespace {

// ── The timber's own dimensions, every one of them a fact about a tree ────
//
// A tile is a metre (vk_renderer_3d.cpp kTileMeters), so these read directly.

// The trunk the village fells, carries on two shoulders and drops into a pit:
// half a metre through, four metres standing. Both come from the prop table's
// row for this kind, because the row IS the log — nothing here restates it.
const float kLogR = structure_min_half_xy(Structure::Palisade);
const float kLogH = structure_min_height(Structure::Palisade);

// Shoulder to shoulder: the next trunk's centre is one trunk away. Any wider
// and it is a fence with gaps, any closer and the village felled trees it did
// not need.
const float kLogStep = kLogR * 2.0f;

// The gate's posts are the two heaviest trunks in the village — the pair it
// picked out and dragged whole, twice the girth of a wall log, because they
// carry the beam and the platform above it.
const float kPostR = kLogR * 2.0f;

// The beam laid across them: one more trunk, so its thickness is a trunk's.
const float kBeamThickM = kLogR * 2.0f;

// The watch platform on top of the frame: a man standing, plus a rail at his
// waist to lean on.
const float kWatchH = kBodyEyeM + 1.0f;

// How deep the gate head is, front to back — a plank walk a man can stand on
// and turn round on, which is a pace either side of him.
const float kWatchHalfDepth = 1.0f;

// Widest opening the village can span with one felled trunk. Beyond it there
// is no beam long enough, and the frame goes up without one — the same honest
// refusal the city's arch makes, in timber's terms: a tree's usable length.
const float kBeamMaxSpanM = 12.0f;

} // namespace

int stamp_palisade(SubworldMapData& out, const kit::Outline& outline,
                   kit::WallGate* gates, int maxGates) {
    constexpr int kNodes = kit::Outline::kBearings;
    std::array<float, kNodes> xs{};
    std::array<float, kNodes> ys{};
    for (int i = 0; i < kNodes; ++i) {
        const float angle = float(i) * kit::Outline::kTwoPi / float(kNodes);
        xs[std::size_t(i)] = outline.cx + std::cos(angle) * outline.r[std::size_t(i)];
        ys[std::size_t(i)] = outline.cy + std::sin(angle) * outline.r[std::size_t(i)];
    }

    auto tile_protected = [&](float fx, float fy) {
        const int tx = std::clamp(int(std::floor(fx)), 0, kCellSize - 1);
        const int ty = std::clamp(int(std::floor(fy)), 0, kCellSize - 1);
        const std::uint8_t t = out.tiles[std::size_t(ty) * kCellSize + tx];
        // Ground another step already decided — a lane, the green, a cottage,
        // a ploughed strip. The stockade yields to all of them, and only to
        // its own line does it not.
        return tile_is(t, kTileBuilt & ~TILE_M_WALL);
    };

    // ── Pass 1 — the line on the ground. ──
    // A connected band, because "is this ring closed" is asked of the TILES
    // (city_wall_integrity_test walks them), and a band one tile wide breaks
    // wherever the ring steps diagonally.
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
            const int x = int(std::floor(x1 + dx * t));
            const int y = int(std::floor(y1 + dy * t));
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int px = x + ox;
                    const int py = y + oy;
                    if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                    const std::size_t idx = std::size_t(py) * kCellSize + px;
                    // Never close over the way in: those tiles ARE the gateway.
                    if (tile_is(out.tiles[idx], kTileBuilt & ~TILE_M_WALL)) continue;
                    out.tiles[idx] = TILE_WALL;
                    out.trav[idx] = 0;
                }
            }
        }
    }

    // ── Pass 2 — the trunks, and the frames where the road comes through. ──
    // Same rule as the city's masonry, for the same reason: a wall OPENS where
    // it finds a road already under itself, so a gateway is always a gateway
    // onto a real road and never an ornament facing a meadow.
    enum class Cls : std::uint8_t { Wall, Gate };
    struct Sample { float x, y; Cls cls; };
    std::vector<Sample> ring;
    for (int i = 0; i < kNodes; ++i) {
        const int next = (i + 1) % kNodes;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        // Sample at the trunk spacing: every sample either becomes a log or
        // belongs to an opening, so nothing between two logs goes unasked.
        const int steps = std::max(1, int(std::ceil(dist / kLogStep)));
        for (int s = 0; s < steps; ++s) {   // exclusive: the next chord owns its start
            const float t = float(s) / float(steps);
            const float px = x1 + dx * t;
            const float py = y1 + dy * t;
            const int tx = std::clamp(int(std::floor(px)), 0, kCellSize - 1);
            const int ty = std::clamp(int(std::floor(py)), 0, kCellSize - 1);
            const std::uint8_t tile = out.tiles[std::size_t(ty) * kCellSize + tx];
            ring.push_back({px, py,
                tile_is(tile, kTilePaved) ? Cls::Gate : Cls::Wall});
        }
    }

    int gateCount = 0;
    const int n = int(ring.size());
    if (n < 4) return 0;
    int startIdx = 0;
    while (startIdx < n && ring[std::size_t(startIdx)].cls != Cls::Wall) ++startIdx;
    if (startIdx >= n) return 0;          // all gateway: no ring to raise
    auto at = [&](int k) -> const Sample& {
        return ring[std::size_t((startIdx + k) % n)];
    };

    auto drive_log = [&](float x, float y, float radius, float height) {
        Structure l{};
        l.kind = Structure::Palisade;
        l.x = x;
        l.y = y;
        l.radius = radius;
        l.height = height;
        l.shape = Structure::Cylinder;    // a log is round because a log is round
        out.structures.push_back(l);
    };

    auto emit_run = [&](int a, int b) {
        for (int k = a; k <= b; ++k) drive_log(at(k).x, at(k).y, kLogR, kLogH);
    };

    auto emit_gateway = [&](int a, int b) {
        const Sample& g0 = at(a);
        const Sample& g1 = at(b);
        const float ddx = g1.x - g0.x;
        const float ddy = g1.y - g0.y;
        const float span = std::sqrt(ddx * ddx + ddy * ddy);
        if (span < 2.0f) return;          // a scuff of track, not a way through
        const float ux = ddx / span;
        const float uy = ddy / span;
        const float mx = (g0.x + g1.x) * 0.5f;
        const float my = (g0.y + g1.y) * 0.5f;

        // THE FRAME STANDS PROUD. The clear a gateway owes a rider is more
        // than this wall is tall, so the posts run past the line of trunks and
        // the head rides above them. A stockade's gate being the tallest thing
        // in the village is not an accident of these numbers — it is what the
        // numbers mean.
        //
        // AND IT HAS TWO LEGS. The masonry primitive this module replaced
        // simply SKIPPED a jamb whose spot came out on the roadway, which for
        // a stone arch merely loses a buttress — but a timber frame with one
        // post is a beam jutting into the air over the road, and that is what
        // the first cut of this module drew. A post does not stand in the
        // roadway and it does not go missing either: it stands at the EDGE of
        // it, so walk outward along the wall's own line until the ground is
        // the village's to build on.
        const float postH = kGateClearM + kBeamThickM;
        float px[2], py[2];
        for (int side = 0; side < 2; ++side) {
            const float dir = side == 0 ? -1.0f : 1.0f;
            const float ex = side == 0 ? g0.x : g1.x;
            const float ey = side == 0 ? g0.y : g1.y;
            px[side] = ex + dir * ux * kPostR;
            py[side] = ey + dir * uy * kPostR;
            // Out to a cart's width at most: further than that the opening is
            // not a gateway but a stretch of missing wall, and the frame would
            // be spanning ground the ring never meant to close.
            for (float step = kPostR; step <= span; step += kPostR) {
                if (!tile_protected(px[side], py[side])) break;
                px[side] = ex + dir * ux * step;
                py[side] = ey + dir * uy * step;
            }
            drive_log(px[side], py[side], kPostR, postH);
        }

        // THE GATE HEAD: the beam and the watch on it are ONE body, not two
        // stacked ones. Physically they are one piece of carpentry — the
        // crosspiece with a plank walk decked over it — and structurally it
        // matters, because a lifted span is settled onto the worst ground
        // under its OWN footprint (sub/height.h seat_lifted_spans). Two bodies
        // with two footprints get two answers, and the walk parts company with
        // the beam it is supposed to be nailed to. One body cannot.
        // The head is carried BY THE POSTS, so it is measured from them — it
        // reaches from one to the other and no further. Measuring it from the
        // opening instead is how a beam comes to end in mid-air beside the leg
        // that was supposed to hold it.
        const float legSpan = std::sqrt((px[1] - px[0]) * (px[1] - px[0])
                                      + (py[1] - py[0]) * (py[1] - py[0]));
        if (legSpan <= kBeamMaxSpanM) {
            Structure head{};
            head.kind = Structure::Palisade;
            head.x = (px[0] + px[1]) * 0.5f;
            head.y = (py[0] + py[1]) * 0.5f;
            head.yaw = std::atan2(py[1] - py[0], px[1] - px[0]);
            head.hx = legSpan * 0.5f + kPostR;
            head.hy = kWatchHalfDepth;
            head.radius = head.hx;
            head.zBase = kGateClearM;
            head.height = kBeamThickM + kWatchH;
            out.structures.push_back(head);
        }

        if (gates != nullptr && gateCount < maxGates) {
            gates[gateCount] = {mx, my,
                std::atan2(my - outline.cy, mx - outline.cx), span};
        }
        ++gateCount;
    };

    int runStart = 0;
    Cls runCls = at(0).cls;
    for (int k = 1; k <= n; ++k) {
        // The sentinel closes the last run: index 0 is a Wall sample by
        // construction, so a ring ending on a gateway still frames it.
        const Cls cls = (k == n) ? Cls::Wall : at(k).cls;
        if (k < n && cls == runCls) continue;
        if (runCls == Cls::Wall) emit_run(runStart, k - 1);
        else                     emit_gateway(runStart, k - 1);
        runStart = k;
        runCls = cls;
    }
    return gateCount;
}

} // namespace sm::sub
