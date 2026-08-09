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
    if (!blocked_at(toX, toY, r, zFeet, bodyH, stepUp)) return;
    // Escape rule: a body already inside a solid may always move (out).
    if (blocked_at(fromX, fromY, r, zFeet, bodyH, stepUp)) return;
    // Axis slide: keep whichever component stays free.
    if (!blocked_at(toX, fromY, r, zFeet, bodyH, stepUp)) {
        toY = fromY;
        return;
    }
    if (!blocked_at(fromX, toY, r, zFeet, bodyH, stepUp)) {
        toX = fromX;
        return;
    }
    toX = fromX;
    toY = fromY;
}

} // namespace sm::sub
