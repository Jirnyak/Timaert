// Subworld spatial hash — uniform grid for fast radius queries.
//
// TS-faithful port of `subworld/spatial-hash.ts`. Used by combat / AI to
// answer the universal question:
//   "Which live npc/player entities are within R units of (x, y)?"
//
// Build O(N), query O(K) with K = entities in the queried radius.
// Cell size matches TS (`CELL = 64` world units) so each cell holds a
// handful of NPCs.
//
// Storage is data-orientated: a single contiguous `std::vector<Entry>`
// indexed by per-bucket {begin, end} ranges (no per-bucket heap allocation
// in the hot path). Build cost is two passes (count → scatter) — branch-
// free and cache friendly.
//
// Header-only POD; no exceptions, no virtuals.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>
#include <entt/entt.hpp>
#include "ecs/components.h"

namespace sm {

constexpr float kSpatialHashCell = 64.0f;   // matches TS `CELL`

struct SpatialHash {
    struct Entry { entt::entity ent; float x, y; };
    struct Bucket { std::uint32_t begin, end; };

    float cell  = kSpatialHashCell;
    int   cols  = 1, rows = 1;
    std::vector<Bucket> buckets;             // size = cols * rows
    std::vector<Entry>  entries;             // contiguous, scattered by bucket

    inline int bucket_index(float x, float y) const noexcept {
        const int cx = std::clamp(int(std::floor(x / cell)), 0, cols - 1);
        const int cy = std::clamp(int(std::floor(y / cell)), 0, rows - 1);
        return cy * cols + cx;
    }
};

// Build over every alive subworld NPC/player in `reg`. Caller passes the
// world bounds (subworld renders inside [0..w]×[0..h]).
//
// Iterates `reg.view<ecs::Position, ecs::Health>(entt::exclude<ecs::Dead>)`
// and includes any entity carrying `ecs::SubworldTag` or `ecs::PlayerTag`.
inline void build_spatial_hash(SpatialHash& h, const entt::registry& reg,
                               float worldW, float worldH) {
    h.cell = kSpatialHashCell;
    h.cols = std::max(1, int(std::ceil(worldW / h.cell)));
    h.rows = std::max(1, int(std::ceil(worldH / h.cell)));
    const std::size_t nb = std::size_t(h.cols) * std::size_t(h.rows);
    h.buckets.assign(nb, {0u, 0u});
    h.entries.clear();

    auto view = reg.view<const ecs::Position, const ecs::Health>(
        entt::exclude<ecs::Dead>);

    // Pass 1 — count per bucket via end (will be turned into prefix sums).
    std::size_t total = 0;
    for (auto e : view) {
        if (!reg.any_of<ecs::SubworldTag, ecs::PlayerTag>(e)) continue;
        const auto& hp = view.template get<const ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        const auto& p  = view.template get<const ecs::Position>(e);
        ++h.buckets[std::size_t(h.bucket_index(p.x, p.y))].end;
        ++total;
    }
    // Prefix sum → begin/end ranges into one contiguous entries array.
    std::uint32_t acc = 0;
    for (auto& b : h.buckets) {
        const std::uint32_t cnt = b.end;
        b.begin = acc;
        acc += cnt;
        b.end = b.begin;                         // reset for scatter cursor
    }
    h.entries.resize(total);

    // Pass 2 — scatter entries into their bucket slot.
    for (auto e : view) {
        if (!reg.any_of<ecs::SubworldTag, ecs::PlayerTag>(e)) continue;
        const auto& hp = view.template get<const ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        const auto& p  = view.template get<const ecs::Position>(e);
        auto& b = h.buckets[std::size_t(h.bucket_index(p.x, p.y))];
        h.entries[b.end++] = {e, p.x, p.y};
    }
}

// Visit every entity within `radius` of (x, y). Visitor receives the
// entt::entity id and exact distance² (caller may use it).
template <typename Visit>
inline void for_each_in_radius(const SpatialHash& h, float x, float y,
                               float radius, Visit&& visit) {
    const float r = std::max(0.0f, radius);
    const int minCx = std::clamp(int(std::floor((x - r) / h.cell)), 0, h.cols - 1);
    const int maxCx = std::clamp(int(std::floor((x + r) / h.cell)), 0, h.cols - 1);
    const int minCy = std::clamp(int(std::floor((y - r) / h.cell)), 0, h.rows - 1);
    const int maxCy = std::clamp(int(std::floor((y + r) / h.cell)), 0, h.rows - 1);
    const float r2  = r * r;
    for (int cy = minCy; cy <= maxCy; ++cy) {
        const int rowBase = cy * h.cols;
        for (int cx = minCx; cx <= maxCx; ++cx) {
            const auto& b = h.buckets[std::size_t(rowBase + cx)];
            for (std::uint32_t i = b.begin; i < b.end; ++i) {
                const auto& e = h.entries[i];
                const float dx = e.x - x, dy = e.y - y;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= r2) visit(e.ent, d2);
            }
        }
    }
}

} // namespace sm
