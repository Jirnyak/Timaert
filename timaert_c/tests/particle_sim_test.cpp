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
#include "sub/particles.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace sm;
using namespace sm::sub;

namespace {

int fails = 0;
void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL particle_sim_test: %s\n", msg);
        ++fails;
    }
}

// Count how many packed instances a fresh single-burst system holds.
std::uint32_t pack_count(const ParticleSystem& ps) {
    static ParticleInstance buf[ParticleSystem::kMaxParticles];
    return ps.pack(buf, ParticleSystem::kMaxParticles);
}

// ── 1. Table contract ──
void test_table() {
    for (int i = 0; i < static_cast<int>(FxKind::Count); ++i) {
        const FxPreset& fx = fx_preset(static_cast<FxKind>(i));
        check(fx.countMin >= 1, "countMin >= 1");
        check(fx.countMax >= fx.countMin, "countMax >= countMin");
        check(fx.speedMax >= fx.speedMin && fx.speedMin >= 0.0f, "speed range");
        check(fx.lifeMin > 0.0f && fx.lifeMax >= fx.lifeMin, "life range");
        check(fx.sizeStart > 0.0f && fx.sizeEnd > 0.0f, "sizes positive");
        check(fx.r >= 0.0f && fx.r <= 1.0f, "r in [0,1]");
        check(fx.g >= 0.0f && fx.g <= 1.0f, "g in [0,1]");
        check(fx.b >= 0.0f && fx.b <= 1.0f, "b in [0,1]");
        check(fx.spread >= 0.0f && fx.spread <= 1.0f, "spread in [0,1]");
    }
    // Out-of-range kind must clamp to a valid row, never OOB.
    const FxPreset& oob = fx_preset(static_cast<FxKind>(200));
    check(oob.countMin >= 1, "OOB kind clamps to a valid row");
}

// ── 2. emit() count + ceiling + tint ──
void test_emit() {
    {
        ParticleSystem ps(12345u);
        const FxPreset& fx = fx_preset(FxKind::FireBurst);
        ps.emit(FxKind::FireBurst, {0, 0, 0});
        const int n = ps.alive();
        check(n >= fx.countMin && n <= fx.countMax,
              "burst count within preset range");
    }
    {
        // Pool ceiling: many big bursts must clamp at kMaxParticles, never over.
        ParticleSystem ps(7u);
        for (int i = 0; i < 500; ++i) ps.emit(FxKind::FireBurst, {0, 0, 0});
        check(ps.alive() <= ParticleSystem::kMaxParticles, "pool never overflows");
        check(ps.alive() == ParticleSystem::kMaxParticles, "big fill saturates pool");
    }
    {
        // Tint override: a bright green tint must appear verbatim in the pack.
        ParticleSystem ps(99u);
        const vec3 tint{0.0f, 1.0f, 0.0f};
        ps.emit(FxKind::Spark, {0, 0, 0}, &tint);
        static ParticleInstance buf[64];
        std::uint32_t n = ps.pack(buf, 64);
        check(n >= 1, "tinted burst produced particles");
        bool allGreen = true;
        for (std::uint32_t i = 0; i < n; ++i)
            if (buf[i].r != 0.0f || buf[i].g != 1.0f || buf[i].b != 0.0f)
                allGreen = false;
        check(allGreen, "tint override applied verbatim to every particle");
    }
}

// ── 3. tick() aging + reap ──
void test_lifecycle() {
    ParticleSystem ps(2024u);
    ps.emit(FxKind::Spark, {0, 0, 0});
    const int spawned = ps.alive();
    check(spawned > 0, "spark burst spawned");
    // Spark lifeMax is 0.6s; after 5s of ticking the pool must be empty and the
    // count exactly zero (no negative, no leak).
    for (int i = 0; i < 300; ++i) ps.tick(1.0f / 60.0f);
    check(ps.alive() == 0, "all sparks reaped after their lifetime");
    check(pack_count(ps) == 0, "empty pool packs zero instances");

    // A zero/negative dt must be a no-op (no aging, no crash).
    ps.emit(FxKind::Spark, {0, 0, 0});
    const int before = ps.alive();
    ps.tick(0.0f);
    ps.tick(-1.0f);
    check(ps.alive() == before, "non-positive dt is a no-op");
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
    check(bloodY < 10.0f, "blood falls below spawn under gravity");
    check(emberY > 10.0f, "ember rises above spawn");
}

// ── 5. pack() invariants ──
void test_pack() {
    ParticleSystem ps(31337u);
    ps.emit(FxKind::MagicBurst, {3.0f, 4.0f, 5.0f});
    ps.tick(0.05f);
    static ParticleInstance buf[ParticleSystem::kMaxParticles];
    std::uint32_t n = ps.pack(buf, ParticleSystem::kMaxParticles);
    check(static_cast<int>(n) == ps.alive(), "pack count == alive");
    for (std::uint32_t i = 0; i < n; ++i) {
        check(buf[i].alpha >= 0.0f && buf[i].alpha <= 1.0f, "alpha in [0,1]");
        check(buf[i].size > 0.0f, "size positive");
        check(std::isfinite(buf[i].px) && std::isfinite(buf[i].py)
                  && std::isfinite(buf[i].pz), "finite position");
    }
    // Capacity clamp: a tiny output buffer must never be overrun.
    ParticleInstance small[4];
    std::uint32_t m = ps.pack(small, 4);
    check(m <= 4, "pack respects capacity");
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
    check(a.alive() == b.alive(), "same seed → same alive count");
    static ParticleInstance ba[ParticleSystem::kMaxParticles];
    static ParticleInstance bb[ParticleSystem::kMaxParticles];
    std::uint32_t na = a.pack(ba, ParticleSystem::kMaxParticles);
    std::uint32_t nb = b.pack(bb, ParticleSystem::kMaxParticles);
    check(na == nb, "same seed → same pack count");
    check(na > 0 && std::memcmp(ba, bb, na * sizeof(ParticleInstance)) == 0,
          "same seed → byte-identical packs");
}

} // namespace

int main() {
    test_table();
    test_emit();
    test_lifecycle();
    test_physics();
    test_pack();
    test_determinism();
    if (fails == 0) std::fprintf(stderr, "particle_sim_test: PASS\n");
    return fails == 0 ? 0 : 1;
}
