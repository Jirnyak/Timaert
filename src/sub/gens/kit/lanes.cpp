#include "sub/gens/kit/lanes.h"

#include "sub/gens/kit/streets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sm::sub::kit {

namespace {

// Does this straight run meet masonry anywhere along it?
bool crosses_masonry(const SubworldMapData& out,
                     float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = std::max(1, int(std::ceil(std::sqrt(dx * dx + dy * dy))));
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int x = int(std::floor(x0 + dx * t));
        const int y = int(std::floor(y0 + dy * t));
        if (x < 0 || y < 0 || x >= kCellSize || y >= kCellSize) return true;
        if (out.tiles[std::size_t(y) * kCellSize + x] == TILE_WALL) return true;
    }
    return false;
}

// A man's width, the unit every lane is measured in (macro/npc.h).
constexpr float kBodyWidth = 1.1f;   // 2 × kNpcBodyRadiusDefault

// The served grid: one cell per this many tiles. Four is the plot scale — a
// quarter served at finer resolution than a house is a distinction nothing
// downstream can use.
constexpr int kServeStep = 4;
constexpr int kServeDim = kCellSize / kServeStep;

struct Tip {
    float x, y, dir;
    LaneRank rank;
    float sinceBranch;
    int   servedRun;     // consecutive steps over ground already served
};

} // namespace

float lane_half_width(LaneRank rank) {
    switch (rank) {
        case LaneRank::High:   return kBodyWidth * 4.0f * 0.5f;  // two carts
        case LaneRank::Street: return kBodyWidth * 3.0f * 0.5f;  // cart + man
        default:               return kBodyWidth * 2.0f * 0.5f;  // two men
    }
}

LaneNet grow_lanes(SubworldMapData& out, const Outline& area, float wallInset,
                   const float* sourceX, const float* sourceY, int sourceCount,
                   float heartX, float heartY,
                   const LanePlan& plan, Rng& r) {
    LaneNet net;
    std::vector<std::uint8_t> served(std::size_t(kServeDim) * kServeDim, 0u);

    auto mark_served = [&](float x, float y) {
        const int rad = std::max(1, int(plan.serveRadius) / kServeStep);
        const int gx = int(x) / kServeStep;
        const int gy = int(y) / kServeStep;
        for (int oy = -rad; oy <= rad; ++oy) {
            for (int ox = -rad; ox <= rad; ++ox) {
                const int px = gx + ox, py = gy + oy;
                if (px < 0 || py < 0 || px >= kServeDim || py >= kServeDim) continue;
                served[std::size_t(py) * kServeDim + px] = 1u;
            }
        }
    };
    auto is_served = [&](float x, float y) {
        const int gx = int(x) / kServeStep;
        const int gy = int(y) / kServeStep;
        if (gx < 0 || gy < 0 || gx >= kServeDim || gy >= kServeDim) return true;
        return served[std::size_t(gy) * kServeDim + gx] != 0u;
    };
    // Is there already paving within the merge radius — i.e. is this tip about
    // to run alongside a lane it should simply join?
    auto paving_near = [&](float x, float y) {
        const int rad = int(plan.mergeRadius);
        const int cx = int(x), cy = int(y);
        for (int oy = -rad; oy <= rad; oy += 2) {
            for (int ox = -rad; ox <= rad; ox += 2) {
                const int px = cx + ox, py = cy + oy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                if (tile_is(out.tiles[std::size_t(py) * kCellSize + px], kTilePaved)) {
                    return true;
                }
            }
        }
        return false;
    };
    auto inside = [&](float x, float y) {
        return area.contains(x, y, wallInset);
    };
    // THE law a street obeys about masonry: it does not cross it. The way
    // through a wall is the gate the road that was there first left in it.
    //
    // Without this a lane simply stopped being painted on the wall's tiles and
    // carried on beyond them, so from the air a street ran straight "under" an
    // inner wall with no gate — exactly what the owner reported (2026-09-13).
    // The lane was never crossing the wall; it was pretending the wall was not
    // there, which looks the same and is worse.
    auto crosses_wall = [&](float x0, float y0, float x1, float y1) {
        return crosses_masonry(out, x0, y0, x1, y1);
    };

    float laid = 0.0f;      // total lane length so far
    auto lay = [&](float x0, float y0, float x1, float y1, LaneRank rank) {
        carve_lane(out, x0, y0, x1, y1, lane_half_width(rank));
        net.segs.push_back({x0, y0, x1, y1, rank});
        const float dx = x1 - x0, dy = y1 - y0;
        laid += std::sqrt(dx * dx + dy * dy);
        mark_served((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        mark_served(x1, y1);
    };
    auto enough = [&]() {
        return laid >= plan.frontageTiles || int(net.segs.size()) >= plan.maxSegs;
    };

    // ── The trunks already exist ──────────────────────────────────────────
    // A town's trunk road is the TRACT: the macro map put a road on this cell,
    // the module carved it gate-to-heart before the wall was raised, and the
    // wall opened on it. Laying a second lane down the same line would give
    // every gate a twin — the parallel-duplicate problem in a new costume. So
    // the trunks are not laid here; they are WALKED, seeding the side streets
    // that branch off them, exactly as a hypha branches off its own stem.
    std::vector<Tip> tips;
    for (int i = 0; i < sourceCount; ++i) {
        const float sx = sourceX[i], sy = sourceY[i];
        const float dx = heartX - sx, dy = heartY - sy;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < plan.branchEvery) continue;
        const float ux = dx / len, uy = dy / len;
        const float dir = std::atan2(dy, dx);
        for (float along = plan.branchEvery; along < len; along += plan.branchEvery) {
            const float x = sx + ux * along;
            const float y = sy + uy * along;
            mark_served(x, y);
            const float side = (r.next_u32() & 1u) ? 1.0f : -1.0f;
            tips.push_back({x, y, dir + side * 1.5707963f,
                            LaneRank::Street, 0.0f, 0});
            tips.push_back({x, y, dir - side * 1.5707963f,
                            LaneRank::Street, 0.0f, 0});
        }
    }
    // …and the streets that leave the market itself. A town with one gate
    // would otherwise grow entirely on one side of its own square.
    for (int i = 0; i < plan.heartSpokes; ++i) {
        const float a = (float(i) / float(std::max(1, plan.heartSpokes)))
                      * 6.28318530718f + r.next_f01() * 0.4f;
        const float sx = heartX + std::cos(a) * plan.heartRadius;
        const float sy = heartY + std::sin(a) * plan.heartRadius;
        // The mouth of the street is joined to the square by its own first
        // stretch, so the two are one network in tiles and not merely adjacent.
        lay(heartX, heartY, sx, sy, LaneRank::High);
        tips.push_back({sx, sy, a, LaneRank::High, 0.0f, 0});
    }

    // ── The branches, generation by generation ────────────────────────────
    // When the tips run out before the town is served, the network COLONISES:
    // it finds ground no lane reaches and grows to it from the nearest lane
    // end. This is the half of the model that branching alone does not give —
    // a hypha grows TOWARD food, and a street's food is somewhere unserved to
    // reach. Without it the coverage was luck, and on a town with one gate the
    // luck ran out: houses piled into one sector five times over (caught by
    // city_distribution_test, which measures exactly that).
    auto colonise_into = [&](std::vector<Tip>& into) -> bool {
        // Somewhere inside the town that no lane serves.
        int bestGX = -1, bestGY = -1;
        std::uint32_t seen = 0;
        for (int gy = 0; gy < kServeDim; ++gy) {
            for (int gx = 0; gx < kServeDim; ++gx) {
                if (served[std::size_t(gy) * kServeDim + gx]) continue;
                const float wx = float(gx * kServeStep + kServeStep / 2);
                const float wy = float(gy * kServeStep + kServeStep / 2);
                if (!inside(wx, wy)) continue;
                // Reservoir sampling: one pass, no allocation, deterministic
                // in the stream.
                ++seen;
                if (r.next_u32() % seen == 0u) { bestGX = gx; bestGY = gy; }
            }
        }
        if (bestGX < 0) return false;
        const float tx = float(bestGX * kServeStep + kServeStep / 2);
        const float ty = float(bestGY * kServeStep + kServeStep / 2);
        // …and the lane end nearest to it, which is where the new street
        // leaves from. A street that started anywhere else would not connect.
        float bx = heartX, by = heartY, best = 1e18f;
        LaneRank rank = LaneRank::Street;
        for (const LaneSeg& sg : net.segs) {
            const float dx = sg.x1 - tx, dy = sg.y1 - ty;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best) {
                best = d2; bx = sg.x1; by = sg.y1;
                rank = sg.rank == LaneRank::High ? LaneRank::Street : sg.rank;
            }
        }
        into.push_back({bx, by, std::atan2(ty - by, tx - bx), rank, 0.0f, 0});
        return true;
    };

    // Colonisation is not a fallback for when branching runs dry — it is half
    // the growth, interleaved with it. Every time a tip dies, the next one is
    // aimed at ground nothing reaches, so the network stays balanced WHILE it
    // grows instead of being balanced only if the frontage target happens to
    // outlast the branching.
    // The two growths ALTERNATE. Branch tips alone spend the whole frontage
    // budget along the trunks they sprang from — a hundred of them queue up
    // before the first hole-seeker is ever reached, and the town comes out
    // built along its roads and empty between them (min/max sector 0.14,
    // caught by city_distribution_test). One of each, turn about, and the
    // network fills as it grows.
    std::size_t head = 0;
    std::size_t holeHead = 0;
    std::vector<Tip> holeTips;
    bool wantHole = false;
    while (!enough()) {
        Tip t{};
        bool got = false;
        for (int tryBoth = 0; tryBoth < 2 && !got; ++tryBoth) {
            const bool takeHole = wantHole != (tryBoth == 1);
            if (takeHole) {
                if (holeHead >= holeTips.size()) colonise_into(holeTips);
                if (holeHead < holeTips.size()) { t = holeTips[holeHead++]; got = true; }
            } else if (head < tips.size()) {
                t = tips[head++];
                got = true;
            }
        }
        if (!got) break;
        wantHole = !wantHole;
        for (int step = 0; step < 256; ++step) {
            if (enough()) break;
            t.dir += (r.next_f01() * 2.0f - 1.0f) * plan.wanderRad;
            const float nx = t.x + std::cos(t.dir) * plan.stepTiles;
            const float ny = t.y + std::sin(t.dir) * plan.stepTiles;
            if (!inside(nx, ny)) break;                    // the wall stops it
            if (crosses_wall(t.x, t.y, nx, ny)) break;     // …and so does masonry

            // Both questions below are asked AHEAD of the tip, past its own
            // trail. A lane serves the ground beside it, so a tip that asked
            // about the ground under its feet found its own work there and
            // stopped after four steps — the network came out as stubs. What
            // it needs to know is whether the ground it is HEADING FOR is
            // already somebody else's.
            const float ca = std::cos(t.dir), sa = std::sin(t.dir);
            const float mergeAheadX = t.x + ca * plan.stepTiles * 2.0f;
            const float mergeAheadY = t.y + sa * plan.stepTiles * 2.0f;
            const float serveAheadX = t.x + ca * (plan.serveRadius + plan.stepTiles);
            const float serveAheadY = t.y + sa * (plan.serveRadius + plan.stepTiles);

            // Anastomosis: a tip that reaches another lane JOINS it. One last
            // segment to make the junction, and this tip is done — which is
            // what turns a tree of dead ends into a network of blocks.
            if (step > 0 && paving_near(mergeAheadX, mergeAheadY)) {
                lay(t.x, t.y, nx, ny, t.rank);
                break;
            }
            // Ground already served needs no second lane. A few steps of
            // tolerance, so a tip may cross a served strip to reach beyond it.
            t.servedRun = is_served(serveAheadX, serveAheadY) ? t.servedRun + 1 : 0;
            if (t.servedRun > 3) break;

            lay(t.x, t.y, nx, ny, t.rank);
            t.x = nx; t.y = ny;
            t.sinceBranch += plan.stepTiles;
            if (t.sinceBranch >= plan.branchEvery
                && t.rank != LaneRank::Alley) {
                t.sinceBranch = 0.0f;
                const float side = (r.next_u32() & 1u) ? 1.0f : -1.0f;
                tips.push_back({t.x, t.y, t.dir + side * 1.5707963f,
                                LaneRank(std::uint8_t(t.rank) + 1), 0.0f, 0});
            }
        }
    }
    return net;
}

void carve_pomerium(SubworldMapData& out, const Outline& area, float inset,
                    LaneNet& net) {
    const float half = lane_half_width(LaneRank::Alley);
    float px = 0.0f, py = 0.0f;
    for (int i = 0; i <= Outline::kBearings; ++i) {
        const int b = i % Outline::kBearings;
        const float ang = float(b) * Outline::kTwoPi / float(Outline::kBearings);
        const float rr = std::max(1.0f, area.r[std::size_t(b)] - inset);
        const float nx = area.cx + std::cos(ang) * rr;
        const float ny = area.cy + std::sin(ang) * rr;
        // The wall lane is interrupted where the castle backs into the
        // curtain — it does not tunnel through the enceinte, it stops at it.
        if (i > 0 && !crosses_masonry(out, px, py, nx, ny)) {
            carve_lane(out, px, py, nx, ny, half);
            net.segs.push_back({px, py, nx, ny, LaneRank::Alley});
        }
        px = nx; py = ny;
    }
}

} // namespace sm::sub::kit
