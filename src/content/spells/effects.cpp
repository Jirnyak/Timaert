// Spell effects — the behaviour half of the spell content class. The data
// half is macro/spells.h (kSpellDefs); this TU binds one spawn function to
// each row BY ORDINAL through kSpellEffects, and the static_asserts below
// refuse to compile a table that drifted out of step (the kInteractRows
// lesson: a parallel table without a guard ran verbs out of order for
// months). Adding a spell = one data row there + one effect row here.
#include "content/spells/casting.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ecs/components.h"
#include "ecs/world.h"

namespace sm {

namespace {

constexpr float kArmageddonTau = 6.28318530717958647692f;
constexpr float kArmageddonPerMeteorBlast = 25.0f;
constexpr int kArmageddonMinMeteors = 16;
constexpr std::uint32_t kArmageddonMixA = std::uint32_t{747796405};
constexpr std::uint32_t kArmageddonMixB =
    std::uint32_t{2147483647} + std::uint32_t{743852806};
constexpr std::uint32_t kArmageddonSaltA = std::uint32_t{0x68bc21eb};
constexpr std::uint32_t kArmageddonSaltB = std::uint32_t{0x02e5be93};

float armageddon_hash01(std::uint32_t seed) {
    seed = seed * std::uint32_t{1664525} + std::uint32_t{1013904223};
    seed ^= seed >> 16;
    seed = seed * std::uint32_t{1664525} + std::uint32_t{1013904223};
    return float((seed >> 8) & std::uint32_t{0x00ffffff})
        * (1.0f / 16777216.0f);
}

std::uint32_t armageddon_seed(const SpellSpawnContext& c) {
    const float ax = c.px >= 0.0f ? c.px : -c.px;
    const float ay = c.py >= 0.0f ? c.py : -c.py;
    const auto qx = static_cast<std::uint32_t>(ax * 17.0f);
    const auto qy = static_cast<std::uint32_t>(ay * 31.0f);
    return c.spellId ^ (qx * kArmageddonMixA) ^ (qy * kArmageddonMixB);
}

// Push the projectile far enough ahead that it spawns fully clear of the
// caster's own hit shell (playerRadius + projectileRadius) and then flies away,
// so it can never detonate on the caster at the muzzle. The universal "any
// projectile can hit anyone" rule is unchanged — the caster simply never
// overlaps their own outgoing bolt, and the projectile's own radius is added
// so the clearance also holds for fat bolts (e.g. the fireball, radius 2.5,
// which a bare +2 margin did not clear).
float caster_spawn_offset(const SpellSpawnContext& c, float projectileRadius) {
    return c.playerRadius + projectileRadius + 2.0f;
}

// ── Spell-bolt point light (graphics) ──────────────────────────────────────
// A flying bolt carries a travelling ecs::LightEmitter so it lights the ground
// and nearby actors in its element's colour — the universal light path the
// player lantern already uses (view<Position, LightEmitter, SubworldTag>), no
// renderer change. Colour is DERIVED from the sprite tint the spell already
// passes (fireball orange, ice white, arcane lavender, lightning yellow), so a
// new spell lights in its own colour for free. Radius/intensity scale with the
// projectile radius: a fat fireball throws a wider, brighter pool than a thin
// bolt. These are the "тюнер" knobs for the whole class — one formula, no
// per-spell hardcode. NOTE: in the 3D subworld the bolt sprite itself is not
// drawn (archetype 0xFF is skipped by the creature pass), so this glow is also
// the bolt's only visual presence there — a moving mote of elemental light.
constexpr float kBoltLightBaseRadiusM = 7.0f;  // reach of a unit-radius bolt
constexpr float kBoltLightRadiusPerR  = 2.2f;  // extra reach per projectile r
constexpr float kBoltLightIntensity   = 1.6f;  // linear gain (additive over sun)
constexpr float kBoltLightHeightM     = 1.0f;  // seat the glow ~a metre up

// Normalise the sprite tint to a vivid unit-ish light colour: scale so the
// brightest channel is 1.0, keeping the hue but guaranteeing a saturated,
// non-dim light even from a pale tint (e.g. near-white ice still reads bright).
ecs::LightEmitter bolt_light(float radius,
                             std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const float rf = float(r), gf = float(g), bf = float(b);
    const float peak = std::max(rf, std::max(gf, bf));
    const float inv = peak > 1.0f ? 1.0f / peak : 1.0f;
    ecs::LightEmitter le{};
    le.offX = 0.0f; le.offY = kBoltLightHeightM; le.offZ = 0.0f;
    le.r = rf * inv; le.g = gf * inv; le.b = bf * inv;
    le.radius = kBoltLightBaseRadiusM + kBoltLightRadiusPerR * radius;
    le.intensity = kBoltLightIntensity;
    return le;
}

float spawn_random01(const SpellSpawnContext& c, std::uint32_t fallbackSeed) {
    if (c.rng01) return c.rng01(c.rngUser);
    return armageddon_hash01(fallbackSeed);
}

void emplace_projectile(ecs::World& w, const SpellSpawnContext& c,
                        float speed, float radius, float life,
                        float blast,
                        std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const float spawnOffset = caster_spawn_offset(c, radius);
    auto e = w.create();
    w.reg.emplace<ecs::Position>(e,
        c.px + c.nx * spawnOffset,
        c.py + c.ny * spawnOffset,
        c.pz + c.nz * spawnOffset);
    w.reg.emplace<ecs::Projectile>(e,
        c.nx * speed, c.ny * speed, c.nz * speed,
        radius, life, life, c.damage, blast,
        c.px, c.py, 0.0f,
        0.0f, 0.0f,
        c.spellId, c.playerId, std::int16_t(0), ecs::Projectile::Bolt,
        c.friendlyFire, false, false);
    w.reg.emplace<ecs::Sprite>(e, std::uint16_t(0),
        r, g, b, std::uint8_t(255), 1.0f);
    w.reg.emplace<ecs::SubworldTag>(e);
    // Travelling elemental glow (same universal LightEmitter path as the player
    // lantern), coloured from this bolt's own tint and sized to its radius.
    w.reg.emplace<ecs::LightEmitter>(e, bolt_light(radius, r, g, b));
}

void spawn_fireball(ecs::World& w, const SpellSpawnContext& c) {
    const float speed = c.speed > 0.0f ? c.speed : 280.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 2.5f;
    emplace_projectile(w, c, speed, radius, kDefaultProjectileLifeS,
                       c.effectRadius, 0xFF, 0xCC, 0x00);
}

void spawn_ice_shard(ecs::World& w, const SpellSpawnContext& c) {
    const float speed = c.speed > 0.0f ? c.speed : 350.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    emplace_projectile(w, c, speed, radius, kDefaultProjectileLifeS, 0.0f,
                       0xFF, 0xFF, 0xFF);
}

void spawn_magic_bolt(ecs::World& w, const SpellSpawnContext& c) {
    const float speed = c.speed > 0.0f ? c.speed : 400.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    emplace_projectile(w, c, speed, radius, kDefaultProjectileLifeS, 0.0f,
                       0xE0, 0xC0, 0xFF);
}

void spawn_lightning_chain(ecs::World& w, const SpellSpawnContext& c) {
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    emplace_projectile(w, c, 300.0f, radius, kDefaultProjectileLifeS, 0.0f,
                       0xFF, 0xEE, 0x44);
}

void spawn_energy_beam(ecs::World& w, const SpellSpawnContext& c) {
    constexpr float kBeamLen = 300.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    const float spawnOffset = caster_spawn_offset(c, radius);
    auto e = w.create();
    w.reg.emplace<ecs::Position>(e,
        c.px + c.nx * (kBeamLen * 0.5f),
        c.py + c.ny * (kBeamLen * 0.5f),
        c.pz + c.nz * (kBeamLen * 0.5f));
    w.reg.emplace<ecs::Projectile>(e,
        c.nx, c.ny, c.nz,
        radius,
        0.35f, 0.35f, c.damage, 0.0f,
        c.px + c.nx * spawnOffset, c.py + c.ny * spawnOffset, kBeamLen,
        0.0f, 0.0f,
        c.spellId, c.playerId, std::int16_t(0), ecs::Projectile::Beam,
        c.friendlyFire, true, true);
    w.reg.emplace<ecs::Sprite>(e, std::uint16_t(0),
        std::uint8_t(0xAA), std::uint8_t(0xDD), std::uint8_t(0xFF),
        std::uint8_t(220), 1.0f);
    w.reg.emplace<ecs::SubworldTag>(e);
}

void spawn_armageddon(ecs::World& w, const SpellSpawnContext& c) {
    const float spread = c.effectRadius > 0.0f ? c.effectRadius : 160.0f;
    int count = int(std::ceil(spread * 0.2f));
    if (count < kArmageddonMinMeteors) count = kArmageddonMinMeteors;

    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 40.0f;
    const std::uint32_t baseSeed = armageddon_seed(c);
    for (int i = 0; i < count; ++i) {
        const std::uint32_t seed =
            baseSeed + std::uint32_t(i) * kArmageddonMixA;
        const float angle = spawn_random01(c, seed) * kArmageddonTau;
        const float dist = spawn_random01(c, seed ^ kArmageddonSaltA) * spread;
        const float delay = spawn_random01(c, seed ^ kArmageddonSaltB) * 0.5f;
        const float life = 0.3f + delay;

        auto e = w.create();
        w.reg.emplace<ecs::Position>(e,
            c.px + std::cos(angle) * dist,
            c.py + std::sin(angle) * dist,
            c.pz);
        w.reg.emplace<ecs::Projectile>(e,
            0.0f, 0.0f, 0.0f,
            radius, life, life, c.damage, kArmageddonPerMeteorBlast,
            c.px, c.py, 0.0f,
            0.0f, 0.0f,
            c.spellId, c.playerId, std::int16_t(0), ecs::Projectile::Bolt,
            c.friendlyFire, true, true);
        w.reg.emplace<ecs::Sprite>(e, std::uint16_t(0),
            std::uint8_t(0xFF), std::uint8_t(0x55), std::uint8_t(0x11),
            std::uint8_t(255), 1.0f);
        w.reg.emplace<ecs::SubworldTag>(e);
    }
}

// ── The binding table ──────────────────────────────────────────────────────
// Row i binds kSpellDefs[i]. nullptr = the spell has no subworld spawn (self
// buffs — the spellbook applies them without an effect entity).
struct SpellEffectRow {
    std::string_view id;   // MUST equal kSpellDefs[row].id — asserted below
    SpellSpawnFn     spawn;
};

constexpr SpellEffectRow kSpellEffects[] = {
    {"fireball",        &spawn_fireball},
    {"ice_shard",       &spawn_ice_shard},
    {"magic_bolt",      &spawn_magic_bolt},
    {"lightning_chain", &spawn_lightning_chain},
    {"energy_beam",     &spawn_energy_beam},
    {"armageddon",      &spawn_armageddon},
    {"haste",           nullptr},
    {"flight",          nullptr},
};

static_assert(sizeof(kSpellEffects) / sizeof(kSpellEffects[0])
                  == std::size_t(kSpellCount),
              "every kSpellDefs row needs exactly one kSpellEffects row");

constexpr bool spell_effects_rows_match() {
    for (int i = 0; i < kSpellCount; ++i)
        if (kSpellEffects[i].id != kSpellDefs[i].id) return false;
    return true;
}
static_assert(spell_effects_rows_match(),
              "kSpellEffects row order must mirror kSpellDefs — the ordinal "
              "IS the binding");

} // namespace

bool cast_spell(ecs::World& w, const SpellDef& spell,
                const SpellSpawnContext& ctx) {
    const int ord = spell_ordinal(spell.id);
    if (ord < 0) return false;
    const SpellSpawnFn fn = kSpellEffects[ord].spawn;
    if (!fn) return false;
    fn(w, ctx);
    return true;
}

bool cast_spell(ecs::World& w, std::string_view id,
                std::uint32_t playerId, float px, float py, float nx, float ny) {
    const SpellDef* s = spell_find(id);
    if (!s) return false;
    SpellSpawnContext ctx{px, py,
                          0.0f,
                          kSpellCasterRadius,
                          nx, ny, 0.0f,
                          s->baseDamage,
                          s->speed > 0.0f ? s->speed : 300.0f,
                          s->projectileRadius,
                          s->friendlyFire ? s->baseRadius : 0.0f,
                          s->friendlyFire,
                          playerId,
                          stable_spell_id(s->id)};
    return cast_spell(w, *s, ctx);
}

} // namespace sm
