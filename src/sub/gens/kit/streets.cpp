#include "sub/gens/kit/streets.h"

#include "core/rng.h"
#include "sub/height.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace sm::sub::kit {

RoadDirSet connected_road_dirs(const std::uint8_t nbFeature[9]) {
    RoadDirSet out{};
    for (int d = 0; d < 8; ++d) {
        const int dx = kDirOffsets[d][0];
        const int dy = kDirOffsets[d][1];
        const int idx = (dy + 1) * 3 + (dx + 1);
        if (!is_road_feature(nbFeature[idx])) continue;
        out.dx[std::size_t(out.count)] = dx;
        out.dy[std::size_t(out.count)] = dy;
        out.angle[std::size_t(out.count)] = std::atan2(float(dy), float(dx));
        ++out.count;
    }
    return out;
}

RoadAxisSet settlement_road_axes(const std::uint8_t nbFeature[9]) {
    RoadAxisSet out{};
    const RoadDirSet dirs = connected_road_dirs(nbFeature);
    for (int i = 0; i < dirs.count; ++i) {
        out.dx[std::size_t(out.count)] = dirs.dx[std::size_t(i)];
        out.dy[std::size_t(out.count)] = dirs.dy[std::size_t(i)];
        out.angle[std::size_t(out.count)] = dirs.angle[std::size_t(i)];
        ++out.count;
    }
    // Main roads reach ONLY edges that connect to a road / settlement
    // neighbour, always via the symmetric edge anchor so both sides of the seam
    // meet (identical to gen_road's neighbour-only carving). No cardinal
    // fallback -> a settlement never carves a road toward a road-less neighbour
    // ("into the void"); an unconnected settlement grows internal streets only.
    out.anchored = out.count > 0;
    return out;
}

void carve_landmark_anchor_roads(SubworldMapData& out,
                                        const CellContext& ctx,
                                        const RoadDirSet& dirs,
                                        int center,
                                        std::uint32_t seed) {
    for (int i = 0; i < dirs.count; ++i) {
        int tx, ty;
        edge_anchor_target(ctx, dirs.dx[std::size_t(i)], dirs.dy[std::size_t(i)], tx, ty);
        carve_organic_road(out, center, center, tx, ty,
                           seed + std::uint32_t(i * 197 + 31));
    }
}

void carve_settlement_main_roads(SubworldMapData& out,
                                        const CellContext& ctx,
                                        const RoadAxisSet& axes,
                                        int center,
                                        std::uint32_t seed) {
    for (int i = 0; i < axes.count; ++i) {
        int tx, ty;
        edge_anchor_target(ctx, axes.dx[std::size_t(i)], axes.dy[std::size_t(i)], tx, ty);
        carve_organic_road(out, center, center, tx, ty,
                           seed + std::uint32_t(i * 197 + 29));
    }
}
// Organic road raster used by roads and settlements. Endpoint damping keeps
// edge contacts deterministic while the interior bend gives TS-style shape.
// Rasterise one road leg as the old organic sine-wiggle segment. Endpoint
// damping (sin²(πt)) keeps the offset zero at BOTH ends, so legs chain
// cleanly and cross-cell edge anchors stay exact.
void carve_road_leg(SubworldMapData& out,
                           int x1, int y1, int x2, int y2,
                           std::uint32_t worldSeed, float ampTiles) {
    constexpr int kStreetWidth = 2;       // 5-tile footprint
    const float fdx = float(x2 - x1);
    const float fdy = float(y2 - y1);
    const float dist = std::sqrt(fdx * fdx + fdy * fdy);
    if (dist < 1.0f) return;
    const float invDist = 1.0f / dist;
    const float nx = -fdy * invDist;   // rotated 90°
    const float ny =  fdx * invDist;
    constexpr float kPi   = 3.14159265f;
    constexpr float kFreq = 0.012f;      // gentle long-wavelength curve
    const float phase     = float(worldSeed & 0xfffffu) * 0.0001f;
    const int steps = int(std::ceil(dist));
    for (int i = 0; i <= steps; ++i) {
        const float t  = float(i) / float(steps);
        const float x0 = float(x1) + fdx * t;
        const float y0 = float(y1) + fdy * t;
        const float damp = std::sin(t * kPi);
        const float damp2 = damp * damp;                           // C¹ at ends
        const float off  = std::sin(t * dist * kFreq + phase) * ampTiles * damp2;
        const int ix = int(std::floor(x0 + nx * off));
        const int iy = int(std::floor(y0 + ny * off));
        for (int dy = -kStreetWidth; dy <= kStreetWidth; ++dy) {
            for (int dx = -kStreetWidth; dx <= kStreetWidth; ++dx) {
                const int px = ix + dx;
                const int py = iy + dy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                const std::size_t idx = std::size_t(py) * kCellSize + px;
                const std::uint8_t cur = out.tiles[idx];
                // A lane does not cut through a house or through masonry. The
                // wall clause is what keeps a town's inner streets from
                // breaching the ring they were laid inside: the wall is raised
                // BEFORE the streets, and the only way through it is the gate
                // it left for the road that was there first.
                if (tile_is(cur, TILE_M_HOUSE | TILE_M_WALL)) continue;
                out.tiles[idx] = TILE_ROAD;
                out.trav [idx] = 1;
            }
        }
    }
}

void carve_lane(SubworldMapData& out, float x0, float y0, float x1, float y1,
                float halfWidth) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.5f) return;
    const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
    const int w = std::max(0, int(std::floor(halfWidth)));
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const int ix = int(std::floor(x0 + dx * t));
        const int iy = int(std::floor(y0 + dy * t));
        for (int oy = -w; oy <= w; ++oy) {
            for (int ox = -w; ox <= w; ++ox) {
                if (float(ox * ox + oy * oy) > halfWidth * halfWidth + 0.5f) continue;
                const int px = ix + ox, py = iy + oy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                const std::size_t idx = std::size_t(py) * kCellSize + px;
                // A lane does not cut a house or masonry (see carve_road_leg).
                if (tile_is(out.tiles[idx], TILE_M_HOUSE | TILE_M_WALL)) continue;
                out.tiles[idx] = TILE_ROAD;
                out.trav [idx] = 1;
            }
        }
    }
}

// Terrain-aware road centreline: a coarse A* on an 8-tile lattice whose step
// cost punishes GRADE quadratically, so the road hugs flat, low ground and
// climbs a massif in small switchbacks instead of charging the summit (owner
// ask — «серпантинами, по наиболее ровным местам»). Pure function of the
// heightmap with index tie-breaks ⇒ deterministic; the exact endpoints are
// kept, so cross-cell edge anchors and the road-parity invariants hold.
void road_centreline(const SubworldMapData& out,
                            int x1, int y1, int x2, int y2,
                            std::vector<std::array<int, 2>>& pts) {
    pts.clear();
    const auto& hm = out.heightmap;
    if (hm.size() != std::size_t(kCellSize) * kCellSize) {
        pts.push_back({x1, y1});
        pts.push_back({x2, y2});
        return;
    }
    constexpr int S = 8;                           // lattice step, tiles
    int bx0 = std::min(x1, x2), bx1 = std::max(x1, x2);
    int by0 = std::min(y1, y2), by1 = std::max(y1, y2);
    const int mar = std::max(160, std::max(bx1 - bx0, by1 - by0) / 2);
    bx0 = std::max(0, bx0 - mar);
    by0 = std::max(0, by0 - mar);
    bx1 = std::min(kCellSize - 1, bx1 + mar);
    by1 = std::min(kCellSize - 1, by1 + mar);
    const int W = (bx1 - bx0) / S + 1;
    const int H = (by1 - by0) / S + 1;
    const auto nodeH = [&](int gx, int gy) {
        const int tx = std::min(kCellSize - 1, bx0 + gx * S);
        const int ty = std::min(kCellSize - 1, by0 + gy * S);
        return hm[std::size_t(ty) * kCellSize + tx];
    };
    const auto nodeIdx = [&](int gx, int gy) { return gy * W + gx; };
    const int sx = std::clamp((x1 - bx0) / S, 0, W - 1);
    const int sy = std::clamp((y1 - by0) / S, 0, H - 1);
    const int tx = std::clamp((x2 - bx0) / S, 0, W - 1);
    const int ty = std::clamp((y2 - by0) / S, 0, H - 1);

    const std::size_t n = std::size_t(W) * H;
    std::vector<float> dist(n, std::numeric_limits<float>::infinity());
    std::vector<std::int32_t> prev(n, -1);
    using QN = std::pair<float, std::int32_t>;
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;
    const auto heur = [&](int gx, int gy) {
        const float dx = float(gx - tx), dy = float(gy - ty);
        return std::sqrt(dx * dx + dy * dy) * float(S);
    };
    dist[std::size_t(nodeIdx(sx, sy))] = 0.0f;
    pq.push({heur(sx, sy), nodeIdx(sx, sy)});
    static const int kNX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kNY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float kNL[8] = {1.0f, 1.0f, 1.0f, 1.0f,
                                 1.41421356f, 1.41421356f,
                                 1.41421356f, 1.41421356f};
    // Grade in metres-per-metre, priced by the one law (streets.h).
    const int goal = nodeIdx(tx, ty);
    while (!pq.empty()) {
        const QN cur = pq.top();
        pq.pop();
        const int ci = cur.second;
        if (ci == goal) break;
        const int cgx = ci % W, cgy = ci / W;
        const float cd = dist[std::size_t(ci)];
        if (cur.first - heur(cgx, cgy) > cd + 1e-4f) continue;  // stale
        const float ch = nodeH(cgx, cgy);
        for (int k = 0; k < 8; ++k) {
            const int ngx = cgx + kNX[k], ngy = cgy + kNY[k];
            if (ngx < 0 || ngy < 0 || ngx >= W || ngy >= H) continue;
            const float lenM = kNL[k] * float(S);
            const float grade = std::fabs(nodeH(ngx, ngy) - ch)
                              * kHeightScaleM / lenM;
            const float nd = cd + lenM * (1.0f + kGradePenalty * grade * grade);
            const std::size_t ni = std::size_t(nodeIdx(ngx, ngy));
            if (nd < dist[ni]) {
                dist[ni] = nd;
                prev[ni] = std::int32_t(ci);
                pq.push({nd + heur(ngx, ngy), int(ni)});
            }
        }
    }
    // Reconstruct lattice path (goal→start), emit exact endpoints around it.
    std::vector<std::array<int, 2>> rev;
    for (std::int32_t i = goal; i >= 0; i = prev[std::size_t(i)]) {
        rev.push_back({bx0 + (i % W) * S, by0 + (i / W) * S});
        if (i == nodeIdx(sx, sy)) break;
    }
    pts.push_back({x1, y1});
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) pts.push_back(*it);
    pts.push_back({x2, y2});
}

void carve_organic_road(SubworldMapData& out,
                               int x1, int y1, int x2, int y2,
                               std::uint32_t worldSeed) {
    // Terrain-aware centreline first, then the organic wiggle per leg (small
    // amplitude — the A* path already curves where the terrain demands it).
    std::vector<std::array<int, 2>> pts;
    road_centreline(out, x1, y1, x2, y2, pts);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        carve_road_leg(out, pts[i][0], pts[i][1],
                       pts[i + 1][0], pts[i + 1][1],
                       worldSeed + std::uint32_t(i) * 0x9E37u, 2.0f);
    }
}

// Endpoint on the cell edge that matches the neighbour's matching point —
// midpoint of the shared edge for orthogonal neighbours, the shared
// corner for diagonals. Symmetric: both cells compute the same point.
static std::uint32_t symmetric_edge_seed(const CellContext& ctx, int dx, int dy) {
    const std::int64_t a = std::int64_t(ctx.cx) * 100003 + std::int64_t(ctx.cy);
    const std::int64_t b = std::int64_t(ctx.cx + dx) * 100003
                         + std::int64_t(ctx.cy + dy);
    const std::int64_t lo = std::min(a, b);
    const std::int64_t hi = std::max(a, b);
    const std::uint32_t seed =
        std::uint32_t(lo * 374761393ll) ^ std::uint32_t(hi * 1274126177ll);
    return seed == 0u ? 1u : seed;
}

void edge_anchor_target(const CellContext& ctx, int dx, int dy,
                               int& ox, int& oy) {
    Rng r(symmetric_edge_seed(ctx, dx, dy));
    const int edgePos = int(std::floor(float(kCellSize)
        * (0.35f + r.next_f01() * 0.3f)));
    if (dx == 0) {
        ox = std::clamp(edgePos, 1, kCellSize - 2);
        oy = dy < 0 ? 1 : kCellSize - 2;
    } else if (dy == 0) {
        ox = dx > 0 ? kCellSize - 2 : 1;
        oy = std::clamp(edgePos, 1, kCellSize - 2);
    } else {
        ox = dx > 0 ? kCellSize - 2 : 1;
        oy = dy > 0 ? kCellSize - 2 : 1;
    }
}

bool is_road_feature(std::uint8_t f) {
    // A bridge is a road for connectivity: the banks' roads aim their edge
    // anchors at it, and its own carve line runs bank to bank. Wooden or
    // stone — the crossing connects either way (v72).
    return f == FT_Road || f == FT_DirtRoad || f == FT_Bridge
        || f == FT_WoodBridge;
}
} // namespace sm::sub::kit
