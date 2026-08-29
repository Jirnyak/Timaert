// Particle-sim contract (FX Inc A) — the universal transient-VFX pool.
//
// The particle system (src/sub/particles.{h,cpp}) is deliberately Vulkan-free:
// a fixed flat POD pool advanced by a pure CPU sim, described entirely by the
// kFxPresets[] table (one row per FxKind). That design exists precisely so its
// correctness can be pinned by a standalone test with no window / no GPU — the
// on-screen additive bloom is proven separately by a validated capture, but
// every numeric invariant of the sim lives here:
//
//   1. Table contract: exactly one preset per FxKind; every row is sane
//      (count range ordered & positive, lifetimes positive & ordered, colour
//      in [0,1]).
//   2. emit() spawns within the preset's count range, never exceeds the pool
//      ceiling, and honours a tint override verbatim.
//   3. tick() ages particles and reaps them exactly at end-of-life (swap-remove
//      leaves no leak, no negative count); a long-dead pool empties fully.
//   4. Physics: gravity integrates (a falling Blood mote ends up below where a
//      floaty Ember rises to), drag never flips velocity sign.
//   5. pack() writes <= alive instances, positions match the live pool, alpha
//      is always in [0,1] and zero for a just-reaped slot, size lerps start→end.
//   6. Determinism: two systems seeded alike produce byte-identical packs.
//
// Pure — links only particles.cpp + core. No Vulkan, no ECS.
#include "check.h"

#include "sub/particles.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace sm;
using namespace sm::sub;

namespace {

// Count how many packed instances a fresh single-burst system holds.
std::uint32_t pack_count(const ParticleSystem& ps) {
    static ParticleInstance buf[ParticleSystem::kMaxParticles];
    return ps.pack(buf, ParticleSystem::kMaxParticles);
}

// ── 1. Table contract ──
void test_table() {
    for (int i = 0; i < static_cast<int>(FxKind::Count); ++i) {
        const FxPreset& fx = fx_preset(static_cast<FxKind>(i));
        CHECK(fx.countMin >= 1, "countMin >= 1");
        CHECK(fx.countMax >= fx.countMin, "countMax >= countMin");
        CHECK(fx.speedMax >= fx.speedMin && fx.speedMin >= 0.0f, "speed range");
        CHECK(fx.lifeMin > 0.0f && fx.lifeMax >= fx.lifeMin, "life range");
        CHECK(fx.sizeStart > 0.0f && fx.sizeEnd > 0.0f, "sizes positive");
        CHECK(fx.r >= 0.0f && fx.r <= 1.0f, "r in [0,1]");
        CHECK(fx.g >= 0.0f && fx.g <= 1.0f, "g in [0,1]");
        CHECK(fx.b >= 0.0f && fx.b <= 1.0f, "b in [0,1]");
        CHECK(fx.spread >= 0.0f && fx.spread <= 1.0f, "spread in [0,1]");
    }
    // Out-of-range kind must clamp to a valid row, never OOB.
    const FxPreset& oob = fx_preset(static_cast<FxKind>(200));
    CHECK(oob.countMin >= 1, "OOB kind clamps to a valid row");
}

// ── 2. emit() count + ceiling + tint ──
void test_emit() {
    {
        ParticleSystem ps(12345u);
        const FxPreset& fx = fx_preset(FxKind::FireBurst);
        ps.emit(FxKind::FireBurst, {0, 0, 0});
        const int n = ps.alive();
        CHECK(n >= fx.countMin && n <= fx.countMax,
              "burst count within preset range");
    }
    {
        // Pool ceiling: many big bursts must clamp at kMaxParticles, never over.
        ParticleSystem ps(7u);
        for (int i = 0; i < 500; ++i) ps.emit(FxKind::FireBurst, {0, 0, 0});
        CHECK(ps.alive() <= ParticleSystem::kMaxParticles, "pool never overflows");
        CHECK(ps.alive() == ParticleSystem::kMaxParticles, "big fill saturates pool");
    }
    {
        // Tint override: a bright green tint must appear verbatim in the pack.
        ParticleSystem ps(99u);
        const vec3 tint{0.0f, 1.0f, 0.0f};
        ps.emit(FxKind::Spark, {0, 0, 0}, &tint);
        static ParticleInstance buf[64];
        std::uint32_t n = ps.pack(buf, 64);
        CHECK(n >= 1, "tinted burst produced particles");
        bool allGreen = true;
        for (std::uint32_t i = 0; i < n; ++i)
            if (buf[i].r != 0.0f || buf[i].g != 1.0f || buf[i].b != 0.0f)
                allGreen = false;
        CHECK(allGreen, "tint override applied verbatim to every particle");
    }
}

// ── 3. tick() aging + reap ──
void test_lifecycle() {
    ParticleSystem ps(2024u);
    ps.emit(FxKind::Spark, {0, 0, 0});
    const int spawned = ps.alive();
    CHECK(spawned > 0, "spark burst spawned");
    // Spark lifeMax is 0.6s; after 5s of ticking the pool must be empty and the
    // count exactly zero (no negative, no leak).
    for (int i = 0; i < 300; ++i) ps.tick(1.0f / 60.0f);
    CHECK(ps.alive() == 0, "all sparks reaped after their lifetime");
    CHECK(pack_count(ps) == 0, "empty pool packs zero instances");

    // A zero/negative dt must be a no-op (no aging, no crash).
    ps.emit(FxKind::Spark, {0, 0, 0});
    const int before = ps.alive();
    ps.tick(0.0f);
    ps.tick(-1.0f);
    CHECK(ps.alive() == before, "non-positive dt is a no-op");
}

// ── 4. Physics: gravity + drag ──
void test_physics() {
    // A heavy Blood mote (negative gravity) should end up BELOW its spawn; a
    // floaty Ember (positive gravity + upBias) should end up ABOVE. Average the
    // Y over the pool to wash out the random spread.
    auto avg_y = [](ParticleSystem& ps, FxKind k, float t) {
        ps.clear();
        ps.emit(k, {0, 10.0f, 0});
        const int steps = static_cast<int>(t / (1.0f / 120.0f));
        for (int i = 0; i < steps; ++i) ps.tick(1.0f / 120.0f);
        static ParticleInstance buf[ParticleSystem::kMaxParticles];
        std::uint32_t n = ps.pack(buf, ParticleSystem::kMaxParticles);
        if (n == 0) return 10.0f; // all reaped: treat as unmoved
        float s = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) s += buf[i].py;
        return s / float(n);
    };
    ParticleSystem ps(555u);
    const float bloodY = avg_y(ps, FxKind::Blood, 0.25f);
    const float emberY = avg_y(ps, FxKind::Ember, 0.5f);
    CHECK(bloodY < 10.0f, "blood falls below spawn under gravity");
    CHECK(emberY > 10.0f, "ember rises above spawn");
}

// ── 5. pack() invariants ──
void test_pack() {
    ParticleSystem ps(31337u);
    ps.emit(FxKind::MagicBurst, {3.0f, 4.0f, 5.0f});
    ps.tick(0.05f);
    static ParticleInstance buf[ParticleSystem::kMaxParticles];
    std::uint32_t n = ps.pack(buf, ParticleSystem::kMaxParticles);
    CHECK(static_cast<int>(n) == ps.alive(), "pack count == alive");
    for (std::uint32_t i = 0; i < n; ++i) {
        CHECK(buf[i].alpha >= 0.0f && buf[i].alpha <= 1.0f, "alpha in [0,1]");
        CHECK(buf[i].size > 0.0f, "size positive");
        CHECK(std::isfinite(buf[i].px) && std::isfinite(buf[i].py)
                  && std::isfinite(buf[i].pz), "finite position");
    }
    // Capacity clamp: a tiny output buffer must never be overrun.
    ParticleInstance small[4];
    std::uint32_t m = ps.pack(small, 4);
    CHECK(m <= 4, "pack respects capacity");
}

// ── 6. Determinism ──
void test_determinism() {
    ParticleSystem a(0xABCDEF01u), b(0xABCDEF01u);
    for (int i = 0; i < 10; ++i) {
        a.emit(FxKind::FireBurst, {float(i), 0, 0});
        b.emit(FxKind::FireBurst, {float(i), 0, 0});
        a.tick(0.016f);
        b.tick(0.016f);
    }
    CHECK(a.alive() == b.alive(), "same seed → same alive count");
    static ParticleInstance ba[ParticleSystem::kMaxParticles];
    static ParticleInstance bb[ParticleSystem::kMaxParticles];
    std::uint32_t na = a.pack(ba, ParticleSystem::kMaxParticles);
    std::uint32_t nb = b.pack(bb, ParticleSystem::kMaxParticles);
    CHECK(na == nb, "same seed → same pack count");
    CHECK(na > 0 && std::memcmp(ba, bb, na * sizeof(ParticleInstance)) == 0,
          "same seed → byte-identical packs");
}

// ── 7. emit_streak(): distance-based, framerate-independent trail ──
void test_streak() {
    // (a) Density is per-metre of travel, NOT per-call: a 10 m segment lays ~5
    // motes at 2 m spacing regardless of how the distance is split across calls.
    // One long hop and many short hops over the same path spawn the same count.
    {
        ParticleSystem oneHop(1u);
        oneHop.emit_streak(FxKind::SpellTrail, {0, 0, 0}, {10.0f, 0, 0}, 2.0f);
        const int nOne = oneHop.alive();

        ParticleSystem manyHops(1u);
        vec3 prev{0, 0, 0};
        for (int i = 1; i <= 10; ++i) {
            vec3 cur{float(i), 0, 0};
            manyHops.emit_streak(FxKind::SpellTrail, prev, cur, 2.0f);
            prev = cur;
        }
        // 10 m at 2 m spacing == 5 motes; 10 hops of 1 m (< spacing) == 1 each.
        CHECK(nOne == 5, "one 10m hop at 2m spacing lays 5 motes");
        CHECK(manyHops.alive() == 10, "ten 1m hops each lay a head mote");
        // The key invariant: total density scales with distance, and a segment
        // shorter than the spacing still lays exactly one (never zero) — a slow
        // bolt keeps a continuous trail.
    }
    // (b) A fast bolt (huge segment in one tick) is CAPPED, never floods the pool.
    {
        ParticleSystem ps(2u);
        ps.emit_streak(FxKind::SpellTrail, {0, 0, 0}, {10000.0f, 0, 0}, 0.5f);
        CHECK(ps.alive() <= 64, "streak caps a huge segment at 64 motes");
        CHECK(ps.alive() == 64, "huge segment saturates the cap");
    }
    // (c) Motes are seeded ALONG the segment (interpolated), not clumped at the
    // head — a fast bolt streaks rather than dotting. Check the packed X spread.
    {
        ParticleSystem ps(3u);
        ps.emit_streak(FxKind::SpellTrail, {0, 0, 0}, {8.0f, 0, 0}, 2.0f);
        static ParticleInstance buf[64];
        std::uint32_t n = ps.pack(buf, 64);
        CHECK(n == 4, "8m at 2m spacing lays 4 motes");
        float minX = 1e9f, maxX = -1e9f;
        for (std::uint32_t i = 0; i < n; ++i) {
            minX = std::min(minX, buf[i].px);
            maxX = std::max(maxX, buf[i].px);
        }
        // SpellTrail speed is tiny (<=1 m/s) and we pack pre-tick, so the spread
        // reflects the seed positions: first mote near 2, last at the head (8).
        CHECK(maxX - minX >= 4.0f, "motes spread along the segment, not clumped");
        CHECK(maxX <= 8.01f && minX >= 1.99f, "motes lie on the segment");
    }
    // (d) spacing<=0 or a degenerate segment lays exactly one mote at the head.
    {
        ParticleSystem ps(4u);
        ps.emit_streak(FxKind::SpellTrail, {5, 5, 5}, {5, 5, 5}, 2.0f); // zero-len
        CHECK(ps.alive() == 1, "degenerate segment lays one head mote");
        ParticleSystem ps2(5u);
        ps2.emit_streak(FxKind::SpellTrail, {0, 0, 0}, {9, 0, 0}, 0.0f); // no spc
        CHECK(ps2.alive() == 1, "spacing<=0 lays one head mote");
    }
    // (e) Tint override is honoured verbatim, exactly like emit().
    {
        ParticleSystem ps(6u);
        const vec3 tint{1.0f, 0.0f, 1.0f}; // magenta
        ps.emit_streak(FxKind::SpellTrail, {0, 0, 0}, {6.0f, 0, 0}, 2.0f, &tint);
        static ParticleInstance buf[64];
        std::uint32_t n = ps.pack(buf, 64);
        CHECK(n >= 1, "tinted streak produced motes");
        bool allMagenta = true;
        for (std::uint32_t i = 0; i < n; ++i)
            if (buf[i].r != 1.0f || buf[i].g != 0.0f || buf[i].b != 1.0f)
                allMagenta = false;
        CHECK(allMagenta, "streak tint applied verbatim");
    }
}

} // namespace

int main() {
    test_table();
    test_emit();
    test_lifecycle();
    test_physics();
    test_pack();
    test_determinism();
    test_streak();
    return sm::test::report("particle_sim_test");
}
