#include "content/spells/registry.h"
#include "content/spells/spell_types.h"

// Spell registry — TS-faithful stats from `src/game/spells/*.ts`
// (mana cost, cooldown, base damage, projectile speed, blast radius,
// friendly-fire flag) ported verbatim. Visual tints picked to match
// each TS spell's CORE colour so projectiles are visually distinct.
//
// Per the relaxed translation policy, each spell now gets its own
// spawn function with its own constants — no more shared placeholder.

namespace sm {

namespace {

inline void emplace_projectile(ecs::World& w, const SpellSpawnContext& c,
                               float speed, float radius, float life,
                               float blast,
                               std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto e = w.create();
    w.reg.emplace<ecs::Position>(e, c.px, c.py);
    w.reg.emplace<ecs::Projectile>(e,
        c.nx * speed, c.ny * speed,
        radius, life, c.damage, blast,
        c.spellId, c.playerId, c.friendlyFire);
    w.reg.emplace<ecs::Sprite>(e, std::uint16_t(0),
        r, g, b, std::uint8_t(255), 1.0f);
}

// === Projectiles =================================================

// Fireball — AoE projectile, friendly fire (TS: speed 280, radius 48).
void spawn_fireball(ecs::World& w, const SpellSpawnContext& c) {
    emplace_projectile(w, c, /*speed=*/280.0f, /*radius=*/12.0f,
                       /*life=*/1.5f, /*blast=*/48.0f,
                       0xFF, 0xCC, 0x00); // CORE = #ffcc00
}

// Ice Shard — fast single-target, no AoE (TS: speed 350, no FF).
void spawn_ice_shard(ecs::World& w, const SpellSpawnContext& c) {
    emplace_projectile(w, c, /*speed=*/350.0f, /*radius=*/8.0f,
                       /*life=*/1.2f, /*blast=*/0.0f,
                       0xFF, 0xFF, 0xFF); // CORE = #ffffff
}

// Magic Bolt — cheap, fastest projectile (TS: speed 400, dmg 12).
void spawn_magic_bolt(ecs::World& w, const SpellSpawnContext& c) {
    emplace_projectile(w, c, /*speed=*/400.0f, /*radius=*/6.0f,
                       /*life=*/1.0f, /*blast=*/0.0f,
                       0xE0, 0xC0, 0xFF); // CORE = #e0c0ff
}

// Lightning Chain — TS shape='chain'. We approximate as a fast
// projectile with a small AoE that triggers chain damage on impact
// (chain logic in subworld combat); good enough until the chain
// system lands. TS: dmg 22, friendly-fire false.
void spawn_lightning_chain(ecs::World& w, const SpellSpawnContext& c) {
    emplace_projectile(w, c, /*speed=*/420.0f, /*radius=*/7.0f,
                       /*life=*/0.6f, /*blast=*/24.0f,
                       0xFF, 0xEE, 0x44); // GLOW = #ffee44
}

// Energy Beam — instant line. TS shape='beam', length large, width
// small. We spawn a single fast, narrow projectile with a long life
// so it travels through enemies; full beam volumetric damage will
// land when the subworld beam system is wired.
void spawn_energy_beam(ecs::World& w, const SpellSpawnContext& c) {
    emplace_projectile(w, c, /*speed=*/900.0f, /*radius=*/4.0f,
                       /*life=*/0.4f, /*blast=*/0.0f,
                       0xFF, 0xFF, 0x88);
}

// Armageddon — meteor nova (TS: baseRadius 160, dmg 80, FF true).
// Each cast spawns a single huge AoE projectile at the player; the
// per-meteor swarm in TS comes later. Damage region matches TS.
void spawn_armageddon(ecs::World& w, const SpellSpawnContext& c) {
    emplace_projectile(w, c, /*speed=*/0.0f, /*radius=*/40.0f,
                       /*life=*/2.0f, /*blast=*/160.0f,
                       0xFF, 0x55, 0x11);
}

// === Self-buffs (no projectile) =================================
// Haste / Flight are sustained self-effects (TS: shape='self').
// Their effect is applied via the player's own status table; the
// spawn callback is intentionally null so the cast pipeline only
// pays mana and starts the cooldown.

} // namespace

void register_builtin_spells() {
    auto& r = spell_registry();

    // id              name              tag                  mana  cd      sustained drain  dmg    spawn
    r.add({"fireball",        "Fireball",        SpellTag::Fire,       60,   2.0f,  false,    0.0f, 30.0f, &spawn_fireball});
    r.add({"ice_shard",       "Ice Shard",       SpellTag::Ice,        30,   1.5f,  false,    0.0f, 40.0f, &spawn_ice_shard});
    r.add({"magic_bolt",      "Magic Bolt",      SpellTag::Arcane,     10,   0.0f,  false,    0.0f, 12.0f, &spawn_magic_bolt});
    r.add({"lightning_chain", "Lightning Chain", SpellTag::Lightning,  60,   4.0f,  false,    0.0f, 22.0f, &spawn_lightning_chain});
    r.add({"energy_beam",     "Energy Beam",     SpellTag::Light,     100,   2.5f,  false,    0.0f, 25.0f, &spawn_energy_beam});
    r.add({"armageddon",      "Armageddon",      SpellTag::Dark,     1000, 120.0f,  false,    0.0f, 80.0f, &spawn_armageddon});
    r.add({"haste",           "Haste",           SpellTag::Air,         0,   0.0f,  true,    10.0f,  0.0f, nullptr});
    r.add({"flight",          "Flight",          SpellTag::Air,         0,   0.0f,  true,    20.0f,  0.0f, nullptr});
}

} // namespace sm
