#include "sub/gens/kit/growth.h"

#include "sub/base_generator.h"
#include "sub/city_layout.h"
#include "sub/gens/kit/streets.h"
#include "sub/height.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace sm::sub::kit {

namespace {

// The growth lattice. Four tiles is the coarsest step that still resolves a
// house plot (2–5 tiles), and the outline it feeds is sampled at 64 bearings,
// so nothing finer would survive to the wall anyway. Coarse matters: this runs
// once per settlement cell on the generation path.
constexpr int kStep = 4;

// Ground within a block of a road is worth TWICE ordinary ground. This single
// number is why a town is long rather than round: doubling the value of
// roadside ground means a place will happily stretch twice as far along a
// tract before it spreads sideways. A block is the frontage-street band the
// street plan already works in (sub/city_layout.h streetLenMin), so "near the
// road" means the same distance to the growth as it does to the streets.
constexpr float kRoadWorth = 2.0f;

} // namespace

Outline grow_outline(const SubworldMapData& out, float cx, float cy,
                     float coreRadius, float targetArea, float maxRadius) {
    Outline shape = Outline::disk(cx, cy, coreRadius);
    const auto& hm = out.heightmap;
    if (hm.size() != std::size_t(kCellSize) * kCellSize) return shape;
    if (maxRadius <= coreRadius) return shape;

    // Lattice window: the reach, clipped to the cell.
    const int x0 = std::max(0, int(cx - maxRadius));
    const int y0 = std::max(0, int(cy - maxRadius));
    const int x1 = std::min(kCellSize - 1, int(cx + maxRadius));
    const int y1 = std::min(kCellSize - 1, int(cy + maxRadius));
    const int W = (x1 - x0) / kStep + 1;
    const int H = (y1 - y0) / kStep + 1;
    if (W < 3 || H < 3) return shape;

    const auto tile_of = [&](int gx, int gy) {
        const int tx = std::min(kCellSize - 1, x0 + gx * kStep);
        const int ty = std::min(kCellSize - 1, y0 + gy * kStep);
        return std::size_t(ty) * kCellSize + tx;
    };
    const auto node_pos = [&](int gx, int gy) {
        return std::array<float, 2>{float(x0 + gx * kStep), float(y0 + gy * kStep)};
    };

    // ── The price of each parcel of ground ────────────────────────────────
    const std::size_t n = std::size_t(W) * H;
    std::vector<float> price(n, 1.0f);
    std::vector<char> forbidden(n, 0);
    std::vector<float> roadDist(n, std::numeric_limits<float>::infinity());

    for (int gy = 0; gy < H; ++gy) {
        for (int gx = 0; gx < W; ++gx) {
            const std::size_t i = std::size_t(gy) * W + gx;
            const std::size_t t = tile_of(gx, gy);

            // Wet ground is not built on. The mason refuses exactly what the
            // plough refuses (base_generator.h kWetEdgeTop), which is what puts
            // a town on the bank rather than in the water.
            if (hm[t] < kWetEdgeTop) { forbidden[i] = 1; continue; }

            // Slope, in metres per metre, priced by the one law (streets.h).
            const int tx = std::min(kCellSize - 2, std::max(1, x0 + gx * kStep));
            const int ty = std::min(kCellSize - 2, std::max(1, y0 + gy * kStep));
            const float dh = (hm[std::size_t(ty) * kCellSize + tx + 1]
                            - hm[std::size_t(ty) * kCellSize + tx - 1]);
            const float dv = (hm[std::size_t(ty + 1) * kCellSize + tx]
                            - hm[std::size_t(ty - 1) * kCellSize + tx]);
            const float grade = std::sqrt(dh * dh + dv * dv) * kHeightScaleM * 0.5f;
            price[i] = 1.0f + kGradePenalty * grade * grade;

            if (tile_is(out.tiles[t], kTilePaved)) roadDist[i] = 0.0f;
        }
    }

    // Distance to the nearest lane, by two chamfer sweeps over the lattice —
    // O(N), and exact enough at four-tile resolution for a discount band.
    const float kOrtho = float(kStep);
    const float kDiag = float(kStep) * 1.41421356f;
    auto relax = [&](std::size_t i, std::size_t j, float w) {
        if (roadDist[j] + w < roadDist[i]) roadDist[i] = roadDist[j] + w;
    };
    for (int gy = 0; gy < H; ++gy) {
        for (int gx = 0; gx < W; ++gx) {
            const std::size_t i = std::size_t(gy) * W + gx;
            if (gx > 0)            relax(i, i - 1, kOrtho);
            if (gy > 0)            relax(i, i - std::size_t(W), kOrtho);
            if (gx > 0 && gy > 0)  relax(i, i - std::size_t(W) - 1, kDiag);
            if (gx + 1 < W && gy > 0) relax(i, i - std::size_t(W) + 1, kDiag);
        }
    }
    for (int gy = H - 1; gy >= 0; --gy) {
        for (int gx = W - 1; gx >= 0; --gx) {
            const std::size_t i = std::size_t(gy) * W + gx;
            if (gx + 1 < W)            relax(i, i + 1, kOrtho);
            if (gy + 1 < H)            relax(i, i + std::size_t(W), kOrtho);
            if (gx + 1 < W && gy + 1 < H) relax(i, i + std::size_t(W) + 1, kDiag);
            if (gx > 0 && gy + 1 < H)  relax(i, i + std::size_t(W) - 1, kDiag);
        }
    }

    // The discount. Full worth on the lane itself, fading to nothing one block
    // out — beyond a block from a road, ground is ordinary ground.
    const float block = city_layout().streetLenMin;
    for (std::size_t i = 0; i < n; ++i) {
        if (forbidden[i]) continue;
        const float near = std::max(0.0f, 1.0f - roadDist[i] / block);
        price[i] /= 1.0f + (kRoadWorth - 1.0f) * near;
    }

    // ── Take the cheapest ground until the place is big enough ────────────
    // A cheapest-first flood from the core: every parcel's cost is the cost of
    // reaching it, so the town grows as one connected body and always along
    // its easiest ground, rather than by each bearing racing outward alone.
    std::vector<float> cost(n, std::numeric_limits<float>::infinity());
    std::vector<char> taken(n, 0);
    using QN = std::pair<float, std::int32_t>;
    std::priority_queue<QN, std::vector<QN>, std::greater<QN>> pq;

    const float coreR2 = coreRadius * coreRadius;
    float area = 0.0f;
    const float parcel = float(kStep) * float(kStep);
    for (int gy = 0; gy < H; ++gy) {
        for (int gx = 0; gx < W; ++gx) {
            const std::size_t i = std::size_t(gy) * W + gx;
            const auto p = node_pos(gx, gy);
            const float dx = p[0] - cx, dy = p[1] - cy;
            if (dx * dx + dy * dy > coreR2) continue;
            // The core is held regardless of what the ground says — it is the
            // guarantee everyone else relies on.
            cost[i] = 0.0f;
            taken[i] = 1;
            area += parcel;
            pq.push({0.0f, std::int32_t(i)});
        }
    }

    static const int kNX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int kNY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float kNL[8] = {1.0f, 1.0f, 1.0f, 1.0f,
                                 1.41421356f, 1.41421356f,
                                 1.41421356f, 1.41421356f};
    while (!pq.empty() && area < targetArea) {
        const QN cur = pq.top();
        pq.pop();
        const int ci = cur.second;
        if (cur.first > cost[std::size_t(ci)] + 1e-4f) continue;   // stale
        const int cgx = ci % W, cgy = ci / W;
        for (int k = 0; k < 8; ++k) {
            const int ngx = cgx + kNX[k], ngy = cgy + kNY[k];
            if (ngx < 0 || ngy < 0 || ngx >= W || ngy >= H) continue;
            const std::size_t ni = std::size_t(ngy) * W + ngx;
            if (forbidden[ni]) continue;
            const auto p = node_pos(ngx, ngy);
            const float dx = p[0] - cx, dy = p[1] - cy;
            if (dx * dx + dy * dy > maxRadius * maxRadius) continue;
            const float nd = cost[std::size_t(ci)]
                           + kNL[k] * float(kStep) * price[ni];
            if (nd >= cost[ni]) continue;
            cost[ni] = nd;
            if (!taken[ni]) {
                taken[ni] = 1;
                area += parcel;
                if (area >= targetArea) break;
            }
            pq.push({nd, std::int32_t(ni)});
        }
    }

    // ── Read the shape back as a radius per bearing ───────────────────────
    // Along each bearing, walk out from the heart until the ground stops being
    // the town's. Stopping at the FIRST parcel it does not hold keeps the
    // outline star-shaped about the heart, which is what makes "inside" a
    // question everyone downstream can answer in constant time — and is what a
    // walled town is anyway: you can walk from the square to any of its wall
    // without leaving it.
    for (int b = 0; b < Outline::kBearings; ++b) {
        const float ang = float(b) * Outline::kTwoPi / float(Outline::kBearings);
        const float ca = std::cos(ang), sa = std::sin(ang);
        float reach = coreRadius;
        for (float d = coreRadius; d <= maxRadius; d += float(kStep) * 0.5f) {
            const int gx = (int(cx + ca * d) - x0) / kStep;
            const int gy = (int(cy + sa * d) - y0) / kStep;
            if (gx < 0 || gy < 0 || gx >= W || gy >= H) break;
            if (!taken[std::size_t(gy) * W + gx]) break;
            reach = d;
        }
        shape.r[std::size_t(b)] = reach;
    }

    // Two 1-2-1 passes, the same smoothing the wall ring uses: the lattice is
    // four tiles coarse, and without this the outline steps in and out by a
    // parcel between neighbouring bearings and the curtain reads as sawtooth.
    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, Outline::kBearings> next = shape.r;
        for (int i = 0; i < Outline::kBearings; ++i) {
            const std::size_t p = std::size_t((i + Outline::kBearings - 1) % Outline::kBearings);
            const std::size_t c = std::size_t(i);
            const std::size_t q = std::size_t((i + 1) % Outline::kBearings);
            next[c] = (shape.r[p] + shape.r[c] * 2.0f + shape.r[q]) * 0.25f;
        }
        shape.r = next;
    }
    // Smoothing may have pulled a bearing under the guarantee; restore it.
    for (float& r : shape.r) r = std::max(r, coreRadius);
    return shape;
}

} // namespace sm::sub::kit
