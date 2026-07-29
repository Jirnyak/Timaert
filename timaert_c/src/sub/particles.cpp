#include "sub/particles.h"

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
                   0.11f, 0.02f, 2.0f, 1.00f, 0.95f, 0.70f, 0.7f},
    // SpellTrail: a couple of faint motes shed each tick, nearly still, short.
    /*SpellTrail*/{1, 2, 0.4f, 1.0f, 0.0f, 0.0f, 0.22f, 0.40f,
                   0.10f, 0.03f, 3.0f, 0.70f, 0.80f, 1.00f, 1.0f},
    // FireBurst: orange/red bloom that rises and drags (hot gas).
    /*FireBurst*/ {20, 30, 3.0f, 6.0f, 2.0f, 2.5f, 0.35f, 0.70f,
                   0.14f, 0.04f, 2.2f, 1.00f, 0.55f, 0.15f, 0.8f},
    // IceBurst: pale-blue shatter, sharp shards flying out, slight fall.
    /*IceBurst*/  {18, 26, 4.0f, 7.5f, 0.5f, -2.0f, 0.30f, 0.55f,
                   0.12f, 0.02f, 2.6f, 0.65f, 0.85f, 1.00f, 1.0f},
    // MagicBurst: violet arcane pop, floaty.
    /*MagicBurst*/{18, 26, 3.0f, 6.0f, 1.0f, 0.5f, 0.35f, 0.65f,
                   0.13f, 0.03f, 2.4f, 0.75f, 0.45f, 1.00f, 0.9f},
    // Blood: dark-red spray of droplets flung in ALL directions (spread=1),
    // barely any upward bias, heavy gravity so it arcs down fast within its
    // short life — not rising hot gas.
    /*Blood*/     {10, 16, 2.0f, 4.5f, 0.3f, -14.0f, 0.30f, 0.55f,
                   0.09f, 0.05f, 1.5f, 0.55f, 0.03f, 0.03f, 1.0f},
    // Dust: brown-grey puff, slow, gravity-bound, grows as it disperses.
    /*Dust*/      {8, 14, 1.0f, 2.5f, 0.5f, -3.5f, 0.45f, 0.90f,
                   0.10f, 0.30f, 2.5f, 0.55f, 0.48f, 0.40f, 0.5f},
    // Ember: single slow warm rising mote (torch ambient), long, tiny.
    /*Ember*/     {1, 1, 0.15f, 0.4f, 0.6f, 0.8f, 0.7f, 1.4f,
                   0.06f, 0.02f, 1.5f, 1.00f, 0.60f, 0.22f, 0.4f},
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

void ParticleSystem::spawn_one(const FxPreset& fx, vec3 p, vec3 col,
                               float scale) {
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
    for (int i = 0; i < n; ++i) spawn_one(fx, p, col, scale);
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
        spawn_one(fx, p, col, scale);
        accum -= 1.0f;
    }
    return accum;
}

void ParticleSystem::tick(float dt) {
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
        ++i;
    }
}

std::uint32_t ParticleSystem::pack(ParticleInstance* out,
                                   std::uint32_t capacity) const {
    std::uint32_t n = 0;
    for (int i = 0; i < count_ && n < capacity; ++i) {
        const Particle& q = pool_[i];
        const float t = (q.life > 0.0f) ? clamp01(q.age / q.life) : 1.0f;
        // Alpha envelope: quick ramp-in over the first 15%, smooth fade to 0
        // over the remaining life. Keeps sparks from popping on/off.
        float a;
        if (t < 0.15f) {
            a = t / 0.15f;
        } else {
            a = 1.0f - (t - 0.15f) / 0.85f;
        }
        a = clamp01(a);
        ParticleInstance& o = out[n++];
        o.px = q.pos.x;
        o.py = q.pos.y;
        o.pz = q.pos.z;
        o.size = lerp(q.sizeStart, q.sizeEnd, t);
        o.r = q.r;
        o.g = q.g;
        o.b = q.b;
        o.alpha = a;
    }
    return n;
}

} // namespace sm::sub
