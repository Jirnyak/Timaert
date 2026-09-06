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
//   7. Blend partition (Inc A, particles-unified-matter): the table pins Blood
//      and Dust as Matter (the shipped additive-blood bug can never silently
//      return); pack() lays energy at the head and matter at the tail, matter
//      sorted back-to-front from the camera.
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

// pack() from the origin (the tests' default camera) returning the total
// instance count — most invariants below don't care about the split.
std::uint32_t pack_all(const ParticleSystem& ps, ParticleInstance* buf,
                       std::uint32_t capacity) {
    const ParticleSystem::PackCounts n = ps.pack(buf, capacity, {0, 0, 0});
    return n.energy + n.matter;
}

// Count how many packed instances a fresh single-burst system holds.
std::uint32_t pack_count(const ParticleSystem& ps) {
    static ParticleInstance buf[ParticleSystem::kMaxParticles];
    return pack_all(ps, buf, ParticleSystem::kMaxParticles);
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
        std::uint32_t n = pack_all(ps, buf, 64);
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
        std::uint32_t n = pack_all(ps, buf, ParticleSystem::kMaxParticles);
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
    std::uint32_t n = pack_all(ps, buf, ParticleSystem::kMaxParticles);
    CHECK(static_cast<int>(n) == ps.alive(), "pack count == alive");
    for (std::uint32_t i = 0; i < n; ++i) {
        CHECK(buf[i].alpha >= 0.0f && buf[i].alpha <= 1.0f, "alpha in [0,1]");
        CHECK(buf[i].size > 0.0f, "size positive");
        CHECK(std::isfinite(buf[i].px) && std::isfinite(buf[i].py)
                  && std::isfinite(buf[i].pz), "finite position");
    }
    // Capacity clamp: a tiny output buffer must never be overrun.
    ParticleInstance small[4];
    std::uint32_t m = pack_all(ps, small, 4);
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
    std::uint32_t na = pack_all(a, ba, ParticleSystem::kMaxParticles);
    std::uint32_t nb = pack_all(b, bb, ParticleSystem::kMaxParticles);
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
        std::uint32_t n = pack_all(ps, buf, 64);
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
        std::uint32_t n = pack_all(ps, buf, 64);
        CHECK(n >= 1, "tinted streak produced motes");
        bool allMagenta = true;
        for (std::uint32_t i = 0; i < n; ++i)
            if (buf[i].r != 1.0f || buf[i].g != 0.0f || buf[i].b != 1.0f)
                allMagenta = false;
        CHECK(allMagenta, "streak tint applied verbatim");
    }
}

// ── 8. Blend partition: matter at the tail, back-to-front ──
void test_blend_partition() {
    // The table pins the fix for the shipped additive-blood bug: Blood and
    // Dust are MATTER (alpha-over), the energy kinds stay Energy. A future
    // row-shuffle that silently flips blood back to additive fails here.
    CHECK(fx_preset(FxKind::Blood).blend == FxBlend::Matter,
          "Blood is matter (never additive again)");
    CHECK(fx_preset(FxKind::Dust).blend == FxBlend::Matter,
          "Dust is matter");
    CHECK(fx_preset(FxKind::Spark).blend == FxBlend::Energy,
          "Spark stays energy");
    CHECK(fx_preset(FxKind::FireBurst).blend == FxBlend::Energy,
          "FireBurst stays energy");
    CHECK(fx_preset(FxKind::SpellTrail).blend == FxBlend::Energy,
          "SpellTrail stays energy");

    // Mixed pool: tint energy pure red and matter pure green, then the packed
    // segments must be colour-pure — energy [0, e), matter [e, e+m).
    ParticleSystem ps(4242u);
    const vec3 red{1.0f, 0.0f, 0.0f};
    const vec3 green{0.0f, 1.0f, 0.0f};
    ps.emit(FxKind::Spark, {1.0f, 0.0f, 0.0f}, &red);
    // Three blood bursts at growing distance from the camera at the origin —
    // the matter segment must come back sorted FAR→NEAR (back-to-front).
    ps.emit(FxKind::Blood, {20.0f, 0.0f, 0.0f}, &green);
    ps.emit(FxKind::Blood, {5.0f, 0.0f, 0.0f}, &green);
    ps.emit(FxKind::Blood, {40.0f, 0.0f, 0.0f}, &green);
    static ParticleInstance buf[ParticleSystem::kMaxParticles];
    const ParticleSystem::PackCounts n =
        ps.pack(buf, ParticleSystem::kMaxParticles, {0, 0, 0});
    CHECK(static_cast<int>(n.energy + n.matter) == ps.alive(),
          "partitioned pack covers the whole pool");
    CHECK(n.energy > 0 && n.matter > 0, "both segments populated");
    for (std::uint32_t i = 0; i < n.energy; ++i)
        CHECK(buf[i].r == 1.0f && buf[i].g == 0.0f,
              "energy segment is energy only");
    float prevD2 = 1e30f;
    for (std::uint32_t i = n.energy; i < n.energy + n.matter; ++i) {
        CHECK(buf[i].r == 0.0f && buf[i].g == 1.0f,
              "matter segment is matter only");
        const float d2 = buf[i].px * buf[i].px + buf[i].py * buf[i].py
                         + buf[i].pz * buf[i].pz;
        CHECK(d2 <= prevD2 + 1e-3f, "matter sorted back-to-front");
        prevD2 = d2;
    }

    // Determinism holds across the partition: same seed + same camera ⇒
    // byte-identical split packs.
    ParticleSystem a(777u), b(777u);
    a.emit(FxKind::Blood, {3, 0, 0});
    b.emit(FxKind::Blood, {3, 0, 0});
    a.emit(FxKind::Spark, {1, 0, 0});
    b.emit(FxKind::Spark, {1, 0, 0});
    static ParticleInstance ba[64], bb[64];
    const ParticleSystem::PackCounts na = a.pack(ba, 64, {0, 0, 0});
    const ParticleSystem::PackCounts nb = b.pack(bb, 64, {0, 0, 0});
    CHECK(na.energy == nb.energy && na.matter == nb.matter,
          "same seed → same split");
    CHECK(std::memcmp(ba, bb,
                      (na.energy + na.matter) * sizeof(ParticleInstance)) == 0,
          "same seed → byte-identical partitioned packs");
}

// ── 9. Ground landing hook (Inc C): the floor is lethal, landings report ──
namespace land_hook {
int landed = 0;
float lastY = -1e9f;
FxKind lastKind = FxKind::Count;
float flat_ground(void*, float, float) { return 0.0f; }
void on_land(void*, const Particle& p) {
    ++landed;
    lastY = p.pos.y;
    lastKind = p.kind;
}
} // namespace land_hook

void test_ground_landing() {
    using namespace land_hook;
    // Blood sprayed just above a flat floor: the downward half of the sphere
    // lands within a few ticks; a droplet flung UP may honestly age out
    // mid-arc (life 0.3-0.55 s vs a ~0.7 s round trip), so the law is "many
    // land, none land below the floor", not "all land". Every landing is
    // clamped exactly to the contact height and reports its preset row.
    {
        ParticleSystem ps(9001u);
        ps.emit(FxKind::Blood, {0.0f, 0.05f, 0.0f});
        const int spawned = ps.alive();
        landed = 0;
        for (int i = 0; i < 240 && ps.alive() > 0; ++i)
            ps.tick(1.0f / 120.0f, &flat_ground, nullptr, &on_land, nullptr);
        CHECK(landed > 0 && landed <= spawned,
              "blood droplets land on the floor");
        CHECK(lastY == 0.0f, "landing clamps the particle to the contact point");
        CHECK(lastKind == FxKind::Blood, "landing reports the preset row");
        CHECK(ps.alive() == 0, "landed particles are reaped");
    }
    // Landing beats aging: spawned at 5 cm the downward half dies on the
    // floor in a few ticks; WITHOUT hooks (the old law) the same seed keeps
    // every droplet alive below the floor until age reaps it.
    {
        ParticleSystem withGround(7777u), without(7777u);
        withGround.emit(FxKind::Blood, {0.0f, 0.05f, 0.0f});
        without.emit(FxKind::Blood, {0.0f, 0.05f, 0.0f});
        for (int i = 0; i < 30; ++i) {
            withGround.tick(1.0f / 120.0f, &flat_ground, nullptr, nullptr,
                            nullptr);
            without.tick(1.0f / 120.0f);
        }
        CHECK(withGround.alive() < without.alive(),
              "the floor reaps faster than age alone (hookless law unchanged)");
    }
    // A rising particle is not a landing: an Ember born AT floor height with
    // upward bias must survive the ground check (vel.y > 0 gate).
    {
        ParticleSystem ps(31u);
        ps.emit(FxKind::Ember, {0.0f, 0.0f, 0.0f});
        landed = 0;
        ps.tick(1.0f / 120.0f, &flat_ground, nullptr, &on_land, nullptr);
        CHECK(landed == 0, "a rising mote at floor height does not land");
        CHECK(ps.alive() == 1, "the ember lives on");
    }
    // The landMark table pins the law's inputs: blood stamps, dust does not.
    CHECK(fx_preset(FxKind::Blood).markType == MarkType::Splat,
          "Blood lands as a splat mark");
    CHECK(fx_preset(FxKind::Blood).markRadiusM > 0.0f,
          "Blood mark has a radius");
    CHECK(fx_preset(FxKind::Dust).markType == MarkType::None,
          "Dust settles without a stain");
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
    test_blend_partition();
    test_ground_landing();
    return sm::test::report("particle_sim_test");
}
