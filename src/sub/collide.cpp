#include "sub/collide.h"

#include <algorithm>
#include <cmath>

namespace sm::sub {

namespace {

// The solid set: what the 3D pass draws as an occluding volume — now a
// column of the ONE per-kind prop row (map_data.h kStructureKindRows).
inline bool is_solid_kind(Structure::Kind k) {
    return structure_is_solid(k);
}

constexpr float kNoSupport = -1.0e30f;

// A segment shorter than this along one axis is PARALLEL to that axis for the
// slab clip: dividing by it would make the clip parameters meaningless (and
// this TU is built with -ffast-math, so Inf is not a safe intermediate). The
// value is a tile fraction far below anything a body can be: the subworld tile
// is ~1 m and the smallest solid half-extent in the world is 0.2 of one, so
// 10⁻⁶ tiles can never be a step somebody meant to take.
constexpr float kDegenerateSegment = 1.0e-6f;
constexpr float kDegenerateSegment2 = kDegenerateSegment * kDegenerateSegment;

} // namespace

void StructureIndex::clear() {
    entries_.clear();
    binStart_.clear();
    binItems_.clear();
}

void StructureIndex::rebuild(const std::vector<Structure>& structs,
                             HeightFn heightFn, void* heightUser) {
    clear();
    entries_.reserve(structs.size() / 4 + 8);
    for (const auto& s : structs) {
        if (!is_solid_kind(s.kind)) continue;
        Entry e{};
        e.x = s.x;
        e.y = s.y;
        e.cs = std::cos(s.yaw);
        e.sn = std::sin(s.yaw);
        const float minR = structure_min_half_xy(s.kind);
        e.hx = std::max(minR, structure_half_x(s));
        e.hy = std::max(minR, structure_half_y(s));
        e.shape = std::uint8_t(s.shape);
        e.kind = std::uint8_t(s.kind);
        const float seatM = heightFn ? heightFn(heightUser, s.x, s.y) : 0.0f;
        structure_solid_span(s, seatM, e.z0, e.z1);
        e.boundR = (s.shape == Structure::Cylinder)
                       ? e.hx
                       : std::sqrt(e.hx * e.hx + e.hy * e.hy);
        entries_.push_back(e);
    }

    // CSR binning: count → prefix → fill. Each entry lands in every bin its
    // bounding square overlaps, so a point query reads exactly one bin ring.
    binStart_.assign(std::size_t(kBinDim) * kBinDim + 1, 0);
    auto bin_range = [](float lo, float hi, int& b0, int& b1) {
        b0 = std::clamp(int(lo) >> kBinShift, 0, kBinDim - 1);
        b1 = std::clamp(int(hi) >> kBinShift, 0, kBinDim - 1);
    };
    for (const auto& e : entries_) {
        int bx0, bx1, by0, by1;
        bin_range(e.x - e.boundR, e.x + e.boundR, bx0, bx1);
        bin_range(e.y - e.boundR, e.y + e.boundR, by0, by1);
        for (int by = by0; by <= by1; ++by)
            for (int bx = bx0; bx <= bx1; ++bx)
                ++binStart_[std::size_t(by) * kBinDim + bx + 1];
    }
    for (std::size_t i = 1; i < binStart_.size(); ++i)
        binStart_[i] += binStart_[i - 1];
    binItems_.resize(std::size_t(binStart_.back()));
    std::vector<std::int32_t> cursor(binStart_.begin(), binStart_.end() - 1);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        int bx0, bx1, by0, by1;
        bin_range(e.x - e.boundR, e.x + e.boundR, bx0, bx1);
        bin_range(e.y - e.boundR, e.y + e.boundR, by0, by1);
        for (int by = by0; by <= by1; ++by)
            for (int bx = bx0; bx <= bx1; ++bx)
                binItems_[std::size_t(
                    cursor[std::size_t(by) * kBinDim + bx]++)] =
                    std::int32_t(i);
    }
}

bool StructureIndex::entry_overlaps(const Entry& e, float x, float y,
                                    float r) const {
    const float dx = x - e.x;
    const float dy = y - e.y;
    if (e.shape == std::uint8_t(Structure::Cylinder)) {
        const float rr = e.hx + r;
        return dx * dx + dy * dy <= rr * rr;
    }
    // Rotate the world delta into the box frame (inverse yaw), then a
    // Minkowski-expanded AABB test — the standard circle-vs-OBB gameplay
    // approximation (corners read slightly fat by ≤ r·(√2−1)).
    const float lx = dx * e.cs + dy * e.sn;
    const float ly = -dx * e.sn + dy * e.cs;
    return std::fabs(lx) <= e.hx + r && std::fabs(ly) <= e.hy + r;
}

bool StructureIndex::entry_swept(const Entry& e, float x0, float y0,
                                 float x1, float y1, float r) const {
    const float ax = x0 - e.x, ay = y0 - e.y;
    const float bx = x1 - e.x, by = y1 - e.y;
    if (e.shape == std::uint8_t(Structure::Cylinder)) {
        // Closest approach of the segment to the circle's centre: the
        // Minkowski-expanded cylinder is a disc, and a segment touches a disc
        // iff its nearest point does.
        const float ex = bx - ax, ey = by - ay;
        const float len2 = ex * ex + ey * ey;
        float t = 0.0f;
        if (len2 > kDegenerateSegment2) {
            t = std::clamp(-(ax * ex + ay * ey) / len2, 0.0f, 1.0f);
        }
        const float px = ax + ex * t, py = ay + ey * t;
        const float rr = e.hx + r;
        return px * px + py * py <= rr * rr;
    }
    // Rotate both ends into the box frame (inverse yaw) — the SAME transform
    // and the SAME expanded half-extents entry_overlaps uses — then clip the
    // segment against the two slabs. Exact, frame-rate independent and O(1):
    // no substep count to pick and nothing that gets coarser as a body gets
    // faster.
    const float lx0 = ax * e.cs + ay * e.sn;
    const float ly0 = -ax * e.sn + ay * e.cs;
    const float lx1 = bx * e.cs + by * e.sn;
    const float ly1 = -bx * e.sn + by * e.cs;
    const float hx = e.hx + r, hy = e.hy + r;
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = lx1 - lx0, dy = ly1 - ly0;
    if (std::fabs(dx) <= kDegenerateSegment) {
        if (std::fabs(lx0) > hx) return false;   // parallel, outside the slab
    } else {
        float ta = (-hx - lx0) / dx, tb = (hx - lx0) / dx;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    if (std::fabs(dy) <= kDegenerateSegment) {
        if (std::fabs(ly0) > hy) return false;
    } else {
        float ta = (-hy - ly0) / dy, tb = (hy - ly0) / dy;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

template <typename Fn>
void StructureIndex::for_candidates(float x, float y, float r, Fn&& fn) const {
    if (entries_.empty() || binStart_.empty()) return;
    const int bx0 = std::clamp(int(x - r) >> kBinShift, 0, kBinDim - 1);
    const int bx1 = std::clamp(int(x + r) >> kBinShift, 0, kBinDim - 1);
    const int by0 = std::clamp(int(y - r) >> kBinShift, 0, kBinDim - 1);
    const int by1 = std::clamp(int(y + r) >> kBinShift, 0, kBinDim - 1);
    for (int by = by0; by <= by1; ++by) {
        for (int bx = bx0; bx <= bx1; ++bx) {
            const std::size_t bin = std::size_t(by) * kBinDim + bx;
            const std::int32_t s = binStart_[bin];
            const std::int32_t e = binStart_[bin + 1];
            for (std::int32_t k = s; k < e; ++k) {
                const Entry& en = entries_[std::size_t(binItems_[std::size_t(k)])];
                // An entry spanning several bins is visited once per bin; the
                // per-entry tests are idempotent, so no dedup set is needed.
                if (!entry_overlaps(en, x, y, r)) continue;
                fn(en);
            }
        }
    }
}

template <typename Fn>
void StructureIndex::for_candidates_seg(float x0, float y0, float x1, float y1,
                                        float r, Fn&& fn) const {
    if (entries_.empty() || binStart_.empty()) return;
    // Bins of the step's own swept bounding box. For an ordinary step this is
    // the same single bin ring a point query reads (bins are 32 tiles and the
    // ceiling step is 2.24), so the broad phase cost is unchanged in the case
    // that happens every frame; a long step legitimately reads more bins,
    // which is exactly the ground it is about to cross.
    const int bx0 = std::clamp(int(std::min(x0, x1) - r) >> kBinShift,
                               0, kBinDim - 1);
    const int bx1 = std::clamp(int(std::max(x0, x1) + r) >> kBinShift,
                               0, kBinDim - 1);
    const int by0 = std::clamp(int(std::min(y0, y1) - r) >> kBinShift,
                               0, kBinDim - 1);
    const int by1 = std::clamp(int(std::max(y0, y1) + r) >> kBinShift,
                               0, kBinDim - 1);
    for (int by = by0; by <= by1; ++by) {
        for (int bx = bx0; bx <= bx1; ++bx) {
            const std::size_t bin = std::size_t(by) * kBinDim + bx;
            const std::int32_t s = binStart_[bin];
            const std::int32_t e = binStart_[bin + 1];
            for (std::int32_t k = s; k < e; ++k) {
                const Entry& en =
                    entries_[std::size_t(binItems_[std::size_t(k)])];
                // As in for_candidates: an entry spanning several bins is
                // visited once per bin and the per-entry tests are
                // idempotent, so no dedup set is needed.
                if (!entry_swept(en, x0, y0, x1, y1, r)) continue;
                fn(en);
            }
        }
    }
}

bool StructureIndex::swept_blocked(float x0, float y0, float x1, float y1,
                                   float r, float zFeet, float bodyH,
                                   float stepUp) const {
    bool hit = false;
    for_candidates_seg(x0, y0, x1, y1, r, [&](const Entry& e) {
        if (hit) return;
        // The SAME z law blocked_at applies: a top within stepUp of the feet
        // is a floor, anything protruding higher whose volume crosses
        // [feet, crown] is a wall. A horizontal step does not change z, so
        // this stays a per-entry filter.
        if (!(e.z1 > zFeet + stepUp && e.z0 < zFeet + bodyH)) return;
        // ПОБЕГ ПРИНАДЛЕЖИТ СОЛИДУ, А НЕ ТЕЛУ (M-406, вердикт владельца
        // 2026-11-04 — вариант «побег ПО СОЛИДУ»). Этот вопрос стоял ОДИН
        // уровнем выше — `resolve_step` спрашивала «внутри ли я ХОТЬ
        // ЧЕГО-НИБУДЬ» и на «да» отпускала шаг целиком, то есть тело,
        // законно пронизывающее ОДИН солид, проходило сквозь ВСЕ прочие.
        // Замер: дракон (макушка 6 м) под воротной перекладиной (span z
        // 2.0..3.0) шагал СКВОЗЬ глухую стену рядом и КОНЧАЛ шаг внутри
        // кладки, где ему снова был положен побег. Живой геймплей, а не
        // угол: всякая макушка выше низа перекладины стоит в проёме в
        // состоянии побега ПОСТОЯННО, значит ходит сквозь воротные башни.
        //
        // Теперь пропуск даёт только САМ солид, внутри которого тело уже
        // стояло, и «нельзя запереть» цело по построению: солид, обнявший
        // тело, его не отказывает НИКОГДА, поэтому выйти из него можно
        // всегда — а соседняя стена остаётся стеной.
        if (entry_overlaps(e, x0, y0, r)) return;
        hit = true;
    });
    return hit;
}

float StructureIndex::support_at(float x, float y, float r, float zFeet,
                                 float stepUp) const {
    float best = kNoSupport;
    for_candidates(x, y, r, [&](const Entry& e) {
        if (e.z1 <= zFeet + stepUp && e.z1 > best) best = e.z1;
    });
    return best;
}

bool StructureIndex::blocked_at(float x, float y, float r, float zFeet,
                                float bodyH, float stepUp) const {
    bool hit = false;
    for_candidates(x, y, r, [&](const Entry& e) {
        if (hit) return;
        // A top within stepUp of the feet is a floor; anything protruding
        // higher whose volume crosses [feet, crown] is a wall.
        if (e.z1 > zFeet + stepUp && e.z0 < zFeet + bodyH) hit = true;
    });
    return hit;
}

std::uint8_t StructureIndex::walk_tile_at(float x, float y, float r,
                                          float zFeet,
                                          std::uint8_t terrainTile,
                                          float stand) const {
    // The HIGHEST floor within a step of the feet decides — the same rule
    // support_at uses to seat a body, so what holds you up and what you walk
    // on can never be two different things. A solid that lays no ground of
    // its own (kWalkTileTransparent) is skipped, not treated as bare earth:
    // standing on a lantern's head does not turn the road below into grass.
    float best = kNoSupport;
    std::uint8_t tile = terrainTile;
    for_candidates(x, y, r, [&](const Entry& e) {
        if (e.z1 > zFeet + stand || e.z1 < zFeet - stand) return;
        if (e.z1 <= best) return;
        const std::uint8_t lays =
            structure_walk_tile(Structure::Kind(e.kind));
        if (lays == kWalkTileTransparent) return;
        best = e.z1;
        tile = lays;
    });
    return tile;
}

bool StructureIndex::solid_at(float x, float y, float z) const {
    bool hit = false;
    for_candidates(x, y, 0.0f, [&](const Entry& e) {
        if (hit) return;
        if (z >= e.z0 && z <= e.z1) hit = true;
    });
    return hit;
}

void StructureIndex::resolve_step(float fromX, float fromY,
                                  float& toX, float& toY,
                                  float r, float zFeet,
                                  float bodyH, float stepUp) const {
    if (entries_.empty()) return;
    // The PATH, not its end point. This line used to read
    // `blocked_at(toX, toY, …)`, and that is the whole of M-398: a body whose
    // step was longer than the refusing band (2·halfXy + 2·r) landed past the
    // solid, every test said "free", and it walked through masonry. Asking the
    // sweep instead costs one query in the case that happens every frame —
    // the same one the destination test cost — because a clear path and a
    // clear destination are the same answer whenever the step is short.
    if (!swept_blocked(fromX, fromY, toX, toY, r, zFeet, bodyH, stepUp)) return;
    // (ЗДЕСЬ СТОЯЛ ГЛОБАЛЬНЫЙ ПОБЕГ — `if (blocked_at(fromX, fromY, …)) return;`
    //  с надписью «UNCHANGED». Он отвечал на вопрос «внутри ли я ХОТЬ
    //  ЧЕГО-НИБУДЬ» и отпускал шаг ЦЕЛИКОМ, то есть выдавал пропуск сразу
    //  через ВСЕ солиды сцены — M-406. Правило не снято, а СПУЩЕНО К СОЛИДУ:
    //  его исполняет `swept_blocked` строкой `entry_overlaps(e, x0, y0, r)`,
    //  поэтому пропуск даёт только тот солид, внутри которого тело стояло.
    //  Держать обе формы было нельзя — это два ответа на один вопрос о мире,
    //  и широкий ответ всегда выигрывал, потому что стоял первым.)
    // Axis slide: keep whichever component reaches its destination by a CLEAR
    // PATH. Same two candidates as before, each asked as a sweep, so sliding
    // along a wall cannot tunnel through the next one.
    if (!swept_blocked(fromX, fromY, toX, fromY, r, zFeet, bodyH, stepUp)) {
        toY = fromY;
        return;
    }
    if (!swept_blocked(fromX, fromY, fromX, toY, r, zFeet, bodyH, stepUp)) {
        toX = fromX;
        return;
    }
    // Refused outright. The body stops where it stood rather than at the
    // surface it met: advancing it to the contact point would have to park it
    // a float's breadth outside a solid, and a body parked INSIDE one is
    // handed the escape rule — i.e. permission to walk through on the next
    // step. Stopping short is what the destination test already did at every
    // speed, so this costs no feel that was ever there.
    toX = fromX;
    toY = fromY;
}

} // namespace sm::sub
