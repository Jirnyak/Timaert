#include "sub/particles.h"

#include <algorithm>
#include <cmath>

namespace sm::sub {

namespace {

// ── The one particle-effect table (source of truth) ──
// One row per FxKind, in declaration order. World-space metres / seconds.
// Fantasy-pixel palette: saturated, slightly over-bright (the additive pass
// clamps at 1.0 in the 8-bit LDR target, giving the FF "glow" bloom on stacks).
constexpr FxPreset kFxPresets[] = {
    // Spark: bright neutral pinprick burst (debug / generic).
    /*Spark*/     {12, 18, 2.5f, 4.5f, 1.0f, -3.0f, 0.35f, 0.60f,
                   0.11f, 0.02f, 2.0f, 1.00f, 0.95f, 0.70f, 0.7f,
                   FxBlend::Energy, MarkType::None, 0.0f, 0},
    // SpellTrail: a couple of faint motes shed each tick, nearly still, short.
    /*SpellTrail*/{1, 2, 0.4f, 1.0f, 0.0f, 0.0f, 0.22f, 0.40f,
                   0.10f, 0.03f, 3.0f, 0.70f, 0.80f, 1.00f, 1.0f,
                   FxBlend::Energy, MarkType::None, 0.0f, 0},
    // FireBurst: orange/red bloom that rises and drags (hot gas).
    /*FireBurst*/ {20, 30, 3.0f, 6.0f, 2.0f, 2.5f, 0.35f, 0.70f,
                   0.14f, 0.04f, 2.2f, 1.00f, 0.55f, 0.15f, 0.8f,
                   FxBlend::Energy, MarkType::None, 0.0f, 0},
    // IceBurst: pale-blue shatter, sharp shards flying out, slight fall.
    /*IceBurst*/  {18, 26, 4.0f, 7.5f, 0.5f, -2.0f, 0.30f, 0.55f,
                   0.12f, 0.02f, 2.6f, 0.65f, 0.85f, 1.00f, 1.0f,
                   FxBlend::Energy, MarkType::None, 0.0f, 0},
    // MagicBurst: violet arcane pop, floaty.
    /*MagicBurst*/{18, 26, 3.0f, 6.0f, 1.0f, 0.5f, 0.35f, 0.65f,
                   0.13f, 0.03f, 2.4f, 0.75f, 0.45f, 1.00f, 0.9f,
                   FxBlend::Energy, MarkType::None, 0.0f, 0},
    // Blood: dark-red spray of droplets flung in ALL directions (spread=1),
    // barely any upward bias, heavy gravity so it arcs down fast within its
    // short life — not rising hot gas. MATTER: blood darkens, it never glows
    // (the shipped additive blood read as a red lamp — the trailer bug).
    /*Blood*/     {10, 16, 2.0f, 4.5f, 0.3f, -14.0f, 0.30f, 0.55f,
                   0.09f, 0.05f, 1.5f, 0.55f, 0.03f, 0.03f, 1.0f,
                   // landMark: each droplet leaves a small splat where it
                   // falls (the reference's blood landMark, radius ~8 cm).
                   FxBlend::Matter, MarkType::Splat, 0.08f, 120},
    // Dust: brown-grey puff, slow, gravity-bound, grows as it disperses.
    // MATTER: grey cards on additive saturated to a white-hot orb.
    /*Dust*/      {8, 14, 1.0f, 2.5f, 0.5f, -3.5f, 0.45f, 0.90f,
                   0.10f, 0.30f, 2.5f, 0.55f, 0.48f, 0.40f, 0.5f,
                   // No landMark: dust settles without a stain (as the ref).
                   FxBlend::Matter, MarkType::None, 0.0f, 0},
    // Ember: single slow warm rising mote (torch ambient), long, tiny.
    // Energy: an ember is a point of light, not a mote of soot.
    /*Ember*/     {1, 1, 0.15f, 0.4f, 0.6f, 0.8f, 0.7f, 1.4f,
                   0.06f, 0.02f, 1.5f, 1.00f, 0.60f, 0.22f, 0.4f,
                   FxBlend::Energy, MarkType::None, 0.0f, 0},
    // Smoke: grey wisp that rises slowly and GROWS as it thins — torch haze
    // (emit_stream) and explosion aftersmoke (emit, 2-4 puffs). MATTER: smoke
    // occludes; on additive it would bloom white like the old dust bug. Lands
    // nowhere (it rises; even a downdraft mote stains nothing, as the ref).
    /*Smoke*/     {2, 4, 0.10f, 0.30f, 0.5f, 0.4f, 1.8f, 3.2f,
                   0.16f, 0.50f, 0.6f, 0.30f, 0.29f, 0.27f, 0.3f,
                   FxBlend::Matter, MarkType::None, 0.0f, 0},
};
static_assert(sizeof(kFxPresets) / sizeof(kFxPresets[0])
                  == static_cast<std::size_t>(FxKind::Count),
              "kFxPresets must have exactly one row per FxKind");

// Uniform in [lo, hi].
inline float frand(Rng& rng, float lo, float hi) {
    return lo + (hi - lo) * rng.next_f01();
}

} // namespace

const FxPreset& fx_preset(FxKind kind) {
    std::uint8_t i = static_cast<std::uint8_t>(kind);
    if (i >= static_cast<std::uint8_t>(FxKind::Count)) i = 0;
    return kFxPresets[i];
}

void ParticleSystem::spawn_one(FxKind kind, const FxPreset& fx, vec3 p,
                               vec3 col, float scale) {
    if (count_ >= kMaxParticles) return; // pool full: drop (never evict)

    // Random direction. spread=0 → tight hemisphere pointing up; spread=1 →
    // full sphere. Built from two angles so the distribution is smooth.
    const float az = frand(rng_, 0.0f, 6.28318531f);
    // Polar angle: cosTheta in [1-2*spread, 1] maps spread 0→straight up,
    // 1→whole sphere. Clamp so spread>0.5 still behaves.
    const float cosLo = 1.0f - 2.0f * fx.spread;
    const float cosT = frand(rng_, cosLo, 1.0f);
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    const vec3 dir{sinT * std::cos(az), cosT, sinT * std::sin(az)};

    const float speed = frand(rng_, fx.speedMin, fx.speedMax);
    Particle& q = pool_[count_++];
    q.pos = p;
    q.vel = dir * speed;
    q.vel.y += fx.upBias;
    q.age = 0.0f;
    q.life = frand(rng_, fx.lifeMin, fx.lifeMax);
    q.gravity = fx.gravity;
    q.drag = fx.drag;
    q.sizeStart = fx.sizeStart * scale;
    q.sizeEnd = fx.sizeEnd * scale;
    q.r = col.x;
    q.g = col.y;
    q.b = col.z;
    q.blend = fx.blend;
    q.kind = kind;
}

void ParticleSystem::emit(FxKind kind, vec3 p, const vec3* tint, float scale) {
    const FxPreset& fx = fx_preset(kind);
    const vec3 col = tint ? *tint : vec3{fx.r, fx.g, fx.b};
    const int lo = fx.countMin;
    const int hi = fx.countMax;
    int n = (hi > lo) ? rng_.next_int(lo, hi + 1) : lo;
    // Scale count with the burst scale (a bigger blast throws more), but keep
    // at least one and never exceed the pool.
    n = std::max(1, static_cast<int>(std::lround(n * scale)));
    for (int i = 0; i < n; ++i) spawn_one(kind, fx, p, col, scale);
}

float ParticleSystem::emit_stream(FxKind kind, vec3 p, float ratePerSec,
                                  float dt, float accum, const vec3* tint,
                                  float scale) {
    accum += ratePerSec * dt;
    // Spawn whole particles; carry the fraction to the next call so low rates
    // (or tiny dt) still emit at the right average cadence.
    const FxPreset& fx = fx_preset(kind);
    const vec3 col = tint ? *tint : vec3{fx.r, fx.g, fx.b};
    while (accum >= 1.0f) {
        spawn_one(kind, fx, p, col, scale);
        accum -= 1.0f;
    }
    return accum;
}

void ParticleSystem::emit_streak(FxKind kind, vec3 a, vec3 b, float spacingM,
                                 const vec3* tint, float scale) {
    const FxPreset& fx = fx_preset(kind);
    const vec3 col = tint ? *tint : vec3{fx.r, fx.g, fx.b};
    const vec3 d = b - a;
    const float dist = length(d);
    // Degenerate segment (bolt barely moved) or no spacing: one mote at the head.
    if (spacingM <= 0.0f || dist <= spacingM) {
        spawn_one(kind, fx, b, col, scale);
        return;
    }
    // One mote per `spacingM` of travel, seeded at evenly interpolated points
    // from the tail toward the head so a fast bolt lays a continuous streak
    // rather than a single clump at `b`. Cap the count defensively so an absurd
    // segment (e.g. a teleport) can never flood the pool in one call.
    int n = static_cast<int>(dist / spacingM);
    if (n > 64) n = 64;
    const float inv = 1.0f / static_cast<float>(n);
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) * inv; // (0,1], head-inclusive
        spawn_one(kind, fx, a + d * t, col, scale);
    }
}

void ParticleSystem::tick(float dt, GroundFn ground, void* groundUser,
                          LandFn onLand, void* landUser) {
    if (dt <= 0.0f) return;
    for (int i = 0; i < count_;) {
        Particle& q = pool_[i];
        q.age += dt;
        if (q.age >= q.life) {
            // Swap-remove: O(1), order-independent (additive draw doesn't care).
            pool_[i] = pool_[--count_];
            continue;
        }
        // Semi-implicit Euler with linear drag.
        q.vel.y += q.gravity * dt;
        if (q.drag > 0.0f) {
            const float d = std::max(0.0f, 1.0f - q.drag * dt);
            q.vel = q.vel * d;
        }
        q.pos = q.pos + q.vel * dt;
        // The floor is lethal (gigahrush law): a particle at or below the
        // carrying surface dies THERE — clamped to the contact point so its
        // remains (a stain stamp, if its preset row has a landMark) land where
        // the eye saw it hit, not a half-step under the ground. Only falling
        // particles land: a mote drifting up through a slope's skin (spawn
        // jitter) is not a landing.
        if (ground != nullptr && q.vel.y <= 0.0f) {
            const float floorY = ground(groundUser, q.pos.x, q.pos.z);
            if (q.pos.y <= floorY) {
                q.pos.y = floorY;
                if (onLand != nullptr) onLand(landUser, q);
                pool_[i] = pool_[--count_];
                continue;
            }
        }
        ++i;
    }
}

namespace {

// Shared instance write: position, age-lerped size, pre-faded colour. The
// alpha envelope (quick ramp-in over the first 15%, smooth fade over the tail)
// keeps sparks from popping on/off; both blend classes use the same envelope.
inline void write_instance(ParticleInstance& o, const Particle& q) {
    const float t = (q.life > 0.0f) ? clamp01(q.age / q.life) : 1.0f;
    float a;
    if (t < 0.15f) {
        a = t / 0.15f;
    } else {
        a = 1.0f - (t - 0.15f) / 0.85f;
    }
    o.px = q.pos.x;
    o.py = q.pos.y;
    o.pz = q.pos.z;
    o.size = lerp(q.sizeStart, q.sizeEnd, t);
    o.r = q.r;
    o.g = q.g;
    o.b = q.b;
    o.alpha = clamp01(a);
}

} // namespace

ParticleSystem::PackCounts ParticleSystem::pack(ParticleInstance* out,
                                                std::uint32_t capacity,
                                                vec3 camPos) const {
    PackCounts n{};
    // Energy first, in pool order (additive is commutative — no sort). Matter
    // indices are collected for the sorted second segment.
    struct MatterRef {
        float dist2; // squared distance to the camera (sort key)
        int index;   // pool index (tie-break: keeps packs deterministic)
    };
    MatterRef matter[kMaxParticles];
    int matterCount = 0;
    for (int i = 0; i < count_; ++i) {
        const Particle& q = pool_[i];
        if (q.blend == FxBlend::Matter) {
            const vec3 d = q.pos - camPos;
            matter[matterCount].dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
            matter[matterCount].index = i;
            ++matterCount;
            continue;
        }
        if (n.energy < capacity) write_instance(out[n.energy++], q);
    }
    // Matter BACK-TO-FRONT (alpha-over composites far-to-near). Ties break on
    // pool index so two equal-seed systems pack byte-identically.
    std::sort(matter, matter + matterCount,
              [](const MatterRef& a, const MatterRef& b) {
                  if (a.dist2 != b.dist2) return a.dist2 > b.dist2;
                  return a.index < b.index;
              });
    for (int i = 0; i < matterCount; ++i) {
        if (n.energy + n.matter >= capacity) break;
        write_instance(out[n.energy + n.matter], pool_[matter[i].index]);
        ++n.matter;
    }
    return n;
}

} // namespace sm::sub
