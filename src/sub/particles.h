// Universal subworld particle / FX system — the data-oriented home of every
// transient visual: spell-bolt trails, impact bursts, blood, dust, torch
// embers, explosions. Deliberately NOT one ECS entity per particle: a fixed
// flat POD pool advanced by a pure CPU sim, so the whole thing is
// Vulkan-free and unit-testable (see tests/particle_sim_test.cpp) — the ECS
// stays for persistent actors, VFX live here.
//
// One universal path, no per-effect hardcoding at the call sites: every effect
// is described by a row in kFxPresets[] indexed by FxKind (mirrors the monster
// / loot / NpcTypeDef "one table is the source of truth" rule). A caller emits
// an effect with a kind + a world position (+ optional tint override); the
// preset row supplies count, speed, lifetime, size, gravity, colour and
// spread. Adding an effect = adding a row, never touching the sim or renderer.
//
// Rendering: ParticleSystem::pack() flattens the live pool into ParticleInstance
// records (world position + size + colour + alpha) that the renderer uploads as
// ONE buffer feeding TWO instanced billboard passes, split by the preset's
// blend class (proposals/particles-unified-matter.md):
//   ENERGY (additive, shaders/particle.vert|frag) — light that adds: magic,
//     fire, sparks. Additive is commutative ⇒ order-independent ⇒ no sort.
//   MATTER (alpha-over, lit, shaders/particle_matter.vert|frag) — stuff that
//     occludes and darkens: blood, dust, smoke. Alpha-over is NOT commutative,
//     so pack() sorts the matter segment back-to-front from the camera.
// pack() lays energy at the head of the buffer and matter right after it; the
// renderer draws each segment with its own pipeline via firstInstance. The
// renderer still knows nothing about FxKind; it only draws two ranges.
#pragma once

#include <cstdint>

#include "core/math.h"
#include "core/rng.h"

namespace sm::sub {

// Effect archetypes. One row per kind in kFxPresets[]; keep this enum and that
// table in lockstep (a static_assert in particles.cpp pins the count).
enum class FxKind : std::uint8_t {
    Spark = 0,     // generic bright pinprick burst (default / debug)
    SpellTrail,    // faint tinted motes shed by a travelling bolt (per-tick)
    FireBurst,     // orange/red impact bloom (fireball, explosion)
    IceBurst,      // pale-blue impact shatter (ice shard)
    MagicBurst,    // violet arcane impact (magic bolt / generic spell)
    Blood,         // dark-red spray on a melee/projectile hit
    Dust,          // brown-grey puff (footfall, corpse fall) — gravity-bound
    Ember,         // slow warm rising mote from a flame (torch/lantern ambient)
    Smoke,         // grey rising wisp (torch haze, explosion aftersmoke) — matter
    Count
};

// The world's two classes of visible stuff. ENERGY emits light (additive,
// emissive, unlit); MATTER occludes it (alpha-over, lit by sun/ambient/points
// like every other surface). One column in the preset table decides which
// pipeline a burst rides — adding smoke/debris later is a row, not code.
enum class FxBlend : std::uint8_t {
    Energy = 0, // additive: magic, fire, sparks — can only ADD light
    Matter,     // alpha-over + lit: blood, dust, smoke — darkens what's behind
};

// What a particle leaves on the ground when it lands (the gigahrush landMark
// law): the stain-canvas stamp shape. Numeric values are the contract with
// shaders/stamp.frag's shape switch — append only.
enum class MarkType : std::uint8_t {
    None = 0, // lands silently (dust, smoke — the reference stamps neither)
    Splat,    // organic fluid splatter with tendrils (blood droplets)
    Pool,     // large irregular death pool
    Drip,     // elongated gravity-pulled drip (wounded trail)
    Scorch,   // charred radial burn (fire / explosion)
};

// One preset row: everything a burst needs, in world-space metres / seconds.
// POD, constexpr-constructible.
struct FxPreset {
    std::uint16_t countMin, countMax; // particles per burst (inclusive range)
    float speedMin, speedMax;         // initial radial speed (m/s)
    float upBias;                     // extra initial +Y velocity (m/s)
    float gravity;                    // +Y accel (m/s²); negative = falls
    float lifeMin, lifeMax;           // lifetime (s)
    float sizeStart, sizeEnd;         // billboard half-size (m), lerped by age
    float drag;                       // per-second velocity damping (0 = none)
    float r, g, b;                    // base linear colour (tint may override)
    float spread;                     // 0=hemisphere up, 1=full sphere
    FxBlend blend;                    // Energy (additive) or Matter (alpha-over)
    // landMark: what a landing particle stamps on the stain canvas (None =
    // nothing; the particle still dies at the ground). Mark colour = the
    // particle's own colour; radius in metres; intensity = max alpha 0-255.
    MarkType markType;
    float markRadiusM;
    std::uint8_t markIntensity;
};

// GPU-facing per-particle instance. 32 bytes — matches the instanced attribute
// layout declared in the particle pipeline (vec3 pos, float size, vec4 colour
// with alpha). Kept at 32 B so the whole 2048-slot pool is 64 KiB, exactly the
// vkCmdUpdateBuffer per-call ceiling.
struct ParticleInstance {
    float px, py, pz;     // world position (m), same space as vWorld
    float size;           // current billboard half-size (m)
    float r, g, b, alpha; // colour (already faded); additive pass uses rgb*alpha
};
static_assert(sizeof(ParticleInstance) == 32, "ParticleInstance must stay 32B");

// Live particle (CPU sim only, never uploaded as-is).
struct Particle {
    vec3 pos;
    vec3 vel;
    float age;      // seconds since spawn
    float life;     // total lifetime (s); dead when age >= life
    float gravity;  // +Y accel (m/s²)
    float drag;     // velocity damping (per s)
    float sizeStart, sizeEnd;
    float r, g, b;
    FxBlend blend;  // copied from the preset at spawn (pack partitions on it)
    FxKind kind;    // preset row — the landing hook reads its landMark columns
};

class ParticleSystem {
public:
    // Hard ceiling: 2048 × 32B instance = 64 KiB = the vkCmdUpdateBuffer cap.
    static constexpr int kMaxParticles = 2048;

    explicit ParticleSystem(std::uint32_t seed = 0x9E3779B9u) : rng_(seed) {}

    // Emit one burst of `kind` at world position `p`. If `tint` is non-null it
    // overrides the preset RGB (e.g. a spell bolt colouring its own trail);
    // `scale` multiplies count and size (e.g. blast radius). New particles past
    // kMaxParticles are dropped (oldest live on — a burst never evicts).
    void emit(FxKind kind, vec3 p, const vec3* tint = nullptr, float scale = 1.0f);

    // Convenience: emit a continuous stream. `ratePerSec` particles/second are
    // accumulated across calls in `accum` (caller-owned, one per emitter) so a
    // sub-frame rate still spawns correctly. Returns the updated accumulator.
    // (Used for torch embers, where the emitter is a persistent entity that can
    // own the accumulator.)
    float emit_stream(FxKind kind, vec3 p, float ratePerSec, float dt,
                      float accum, const vec3* tint = nullptr,
                      float scale = 1.0f);

    // Shed a trail along the segment a→b, one mote every `spacingM` metres of
    // travel. STATELESS (no caller-owned accumulator) so a transient emitter
    // that lives only for the tick — a spell bolt, which has nowhere to store an
    // accumulator across frames — still lays an even, framerate-independent
    // trail: density is per-metre-travelled, not per-frame, so a 400 m/s bolt
    // and a 100 m/s bolt leave the same visual density. Motes are seeded at
    // interpolated points along the segment (not all at `b`) so a fast bolt does
    // not stripe. `spacingM<=0` is treated as a single emit at `b`.
    void emit_streak(FxKind kind, vec3 a, vec3 b, float spacingM,
                     const vec3* tint = nullptr, float scale = 1.0f);

    // Ground hooks for tick() — the sim stays Vulkan-free and pure, so the
    // world's floor comes in as a function (the movement.h hook pattern):
    //   GroundFn — height of the carrying surface (m) under world (x, z);
    //   LandFn   — called when a particle touches that surface (its pos is
    //              clamped to the contact point). The engine turns a landing
    //              into a stain-canvas stamp command when the particle's
    //              preset row has a landMark (the gigahrush law: gravity
    //              landing kills the particle; the mark is its remains).
    using GroundFn = float (*)(void* user, float wx, float wz);
    using LandFn = void (*)(void* user, const Particle& p);

    // Advance every live particle by dt (integrate, fade, reap dead via
    // swap-remove). Pure — no allocation, no Vulkan. With a GroundFn the
    // floor becomes lethal: a particle at or below ground height dies there
    // (and reports to onLand); without one, behaviour is exactly the old law
    // (age-only reaping) — every existing caller and test is untouched.
    void tick(float dt, GroundFn ground = nullptr, void* groundUser = nullptr,
              LandFn onLand = nullptr, void* landUser = nullptr);

    // How pack() split the buffer: energy instances occupy [0, energy), matter
    // occupies [energy, energy + matter). The renderer draws each range with
    // its own pipeline via firstInstance; total upload = energy + matter.
    struct PackCounts {
        std::uint32_t energy = 0;
        std::uint32_t matter = 0;
    };

    // Flatten live particles into `out` (caller supplies a buffer of at least
    // capacity slots), partitioned by blend class: energy first (pool order —
    // additive needs no sort), then matter sorted BACK-TO-FRONT from `camPos`
    // (alpha-over is order-dependent; ties break on pool index so equal-seed
    // systems stay byte-identical). Colour is pre-faded (alpha ramps in over
    // the first 15% of life, out over the tail) so both shaders stay trivial.
    PackCounts pack(ParticleInstance* out, std::uint32_t capacity,
                    vec3 camPos) const;

    int alive() const { return count_; }
    void clear() { count_ = 0; }

private:
    void spawn_one(FxKind kind, const FxPreset& fx, vec3 p, vec3 col,
                   float scale);

    Particle pool_[kMaxParticles];
    int count_ = 0;
    Rng rng_;
};

// The one source of truth. Indexed by FxKind. Defined in particles.cpp.
const FxPreset& fx_preset(FxKind kind);

} // namespace sm::sub
