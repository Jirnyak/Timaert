// Ring GEOMETRY — the shape a wall is raised on, shared by every kind that
// raises one (owner's ruling 2026-09-13: the kit is geometry, the masonry and
// the stockade are their own modules).
#include "sub/gens/kit/outline.h"

#include "sub/city_layout.h"

#include <array>
#include <cmath>

namespace sm::sub::kit {

Outline wall_ring_noise(const Outline& base, float roughness, Rng& r) {
    Outline o = base;
    // Ring noise amplitudes live in sub/city_layout.h: the citizen populator
    // derives the ring's worst INWARD excursion from them (wall_inner_bound)
    // so nobody is placed in a dip of the wall — i.e. outside their own town.
    // Same model for every ring; one authority.
    const float phase1 = r.next_f01() * Outline::kTwoPi;
    const float phase2 = r.next_f01() * Outline::kTwoPi;
    for (int i = 0; i < Outline::kBearings; ++i) {
        const float angle = float(i) * Outline::kTwoPi / float(Outline::kBearings);
        const float harmonic =
            std::sin(angle * 3.0f + phase1) * kSettlementWallRing.harmonic3Amp
          + std::sin(angle * 5.0f + phase2) * kSettlementWallRing.harmonic5Amp;
        // The amplitudes scale with the LOCAL radius, so a town that runs long
        // down its tract wanders proportionally everywhere rather than
        // wobbling hugely at its narrow waist.
        const float base_r = base.r[std::size_t(i)];
        const float jitter = (r.next_f01() * 2.0f - 1.0f) * base_r * roughness
                           * kSettlementWallRing.jitterAmp;
        o.r[std::size_t(i)] = base_r + base_r * roughness * harmonic + jitter;
    }
    // Two 1-2-1 passes. `wall_inner_bound`'s conservative bound depends on
    // this smoothing only ever pulling the extremes IN, never pushing them
    // out — which a symmetric averaging kernel cannot do.
    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, Outline::kBearings> next = o.r;
        for (int i = 0; i < Outline::kBearings; ++i) {
            const std::size_t prev = std::size_t((i + Outline::kBearings - 1) % Outline::kBearings);
            const std::size_t cur  = std::size_t(i);
            const std::size_t nxt  = std::size_t((i + 1) % Outline::kBearings);
            next[cur] = (o.r[prev] + o.r[cur] * 2.0f + o.r[nxt]) * 0.25f;
        }
        o.r = next;
    }
    return o;
}

Outline wall_outline(float cx, float cy, float radius, float roughness, Rng& r) {
    return wall_ring_noise(Outline::disk(cx, cy, radius), roughness, r);
}

} // namespace sm::sub::kit
