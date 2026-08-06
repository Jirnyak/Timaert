#include "content/spells/registry.h"
#include "content/spells/spell_book.h"
#include "ecs/world.h"
#include "sub/spell_effects.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int projectile_count(sm::ecs::World& w) {
    int count = 0;
    for (auto e : w.reg.view<sm::ecs::Projectile>()) {
        (void)e;
        ++count;
    }
    return count;
}

bool nearf(float a, float b) {
    return std::fabs(a - b) <= 0.001f;
}

bool find_projectile_by_spell(sm::ecs::World& w, const char* spellId,
                              sm::ecs::Projectile& out) {
    const std::uint32_t stableId = sm::stable_spell_id(spellId);
    auto view = w.reg.view<sm::ecs::Projectile>();
    for (auto e : view) {
        const auto& p = view.get<sm::ecs::Projectile>(e);
        if (p.spellId == stableId) {
            out = p;
            return true;
        }
    }
    return false;
}

bool find_projectile_pose_by_spell(sm::ecs::World& w, const char* spellId,
                                   sm::ecs::Projectile& outProjectile,
                                   sm::ecs::Position& outPosition) {
    const std::uint32_t stableId = sm::stable_spell_id(spellId);
    auto view = w.reg.view<sm::ecs::Position, sm::ecs::Projectile>();
    for (auto e : view) {
        const auto& p = view.get<sm::ecs::Projectile>(e);
        if (p.spellId == stableId) {
            outProjectile = p;
            outPosition = view.get<sm::ecs::Position>(e);
            return true;
        }
    }
    return false;
}

entt::entity add_target(sm::ecs::World& w, float x, float y,
                        float hp, bool playerSide) {
    auto e = w.create();
    w.reg.emplace<sm::ecs::Position>(e, x, y, 0.0f);
    w.reg.emplace<sm::ecs::Health>(e, hp, hp);
    w.reg.emplace<sm::ecs::SubworldTag>(e);
    w.reg.emplace<sm::ecs::Sprite>(e, std::uint16_t(0),
        std::uint8_t(255), std::uint8_t(255), std::uint8_t(255),
        std::uint8_t(255), 6.0f);
    if (playerSide) {
        w.reg.emplace<sm::ecs::PlayerSoldierTag>(e);
    } else {
        w.reg.emplace<sm::ecs::NPCKind>(e, sm::ecs::NPCKind{2, 2});
    }
    return e;
}

// A real player-tagged entity to own player-cast projectiles. Inc 4d retired
// the ownerId==0 sentinel — ownership/faction now key off the owner entity's
// tags, exactly as production does via SubworldEngine::player_entity_id().
// Placed far from targets so it is never itself in a blast/line by accident.
std::uint32_t add_player(sm::ecs::World& w, float x, float y) {
    auto e = w.create();
    w.reg.emplace<sm::ecs::Position>(e, x, y, 0.0f);
    w.reg.emplace<sm::ecs::Health>(e, 1000.0f, 1000.0f);
    w.reg.emplace<sm::ecs::SubworldTag>(e);
    w.reg.emplace<sm::ecs::PlayerTag>(e);
    return std::uint32_t(entt::to_integral(e));
}

float hp_of(sm::ecs::World& w, entt::entity e) {
    const auto* hp = w.reg.try_get<sm::ecs::Health>(e);
    return hp ? hp->hp : -1.0f;
}

bool player_last_hit(sm::ecs::World& w, entt::entity e) {
    const auto* hit = w.reg.try_get<sm::ecs::LastHit>(e);
    return hit && hit->playerOwned;
}

int armageddon_meteor_count(int radius) {
    int count = int(std::ceil(float(radius) * 0.2f));
    if (count < 16) count = 16;
    return count;
}

struct SeqRng {
    const float* values = nullptr;
    int count = 0;
    int index = 0;
};

float seq_rng01(void* user) {
    auto* rng = static_cast<SeqRng*>(user);
    if (!rng || !rng->values || rng->count <= 0) return 0.0f;
    if (rng->index >= rng->count) return 0.0f;
    return rng->values[rng->index++];
}

} // namespace

int main() {
    sm::register_builtin_spells();

    sm::ecs::World world;
    sm::SpellBook book;
    sm::CombatStats combat{};
    combat.currentMp = 2000;
    combat.maxMp = 2000;
    sm::Attributes attributes{};
    sm::Skills skills{};

    const sm::SpellDef* fireDef = sm::spell_registry().find("fireball");
    const sm::SpellDef* iceDef = sm::spell_registry().find("ice_shard");
    const sm::SpellDef* magicDef = sm::spell_registry().find("magic_bolt");
    const sm::SpellDef* chainDef = sm::spell_registry().find("lightning_chain");
    const sm::SpellDef* beamDef = sm::spell_registry().find("energy_beam");
    const sm::SpellDef* armDef = sm::spell_registry().find("armageddon");
    const sm::SpellDef* hasteDef = sm::spell_registry().find("haste");
    const sm::SpellDef* flightDef = sm::spell_registry().find("flight");
    if (!fireDef || fireDef->tagCount != 1
        || !sm::spell_has_tag(*fireDef, sm::SpellTag::Fire)
        || std::strcmp(fireDef->statusEffect, "burning") != 0
        || !nearf(fireDef->statusDuration, 3.0f)) {
        return fail("fireball metadata wrong");
    }
    if (!iceDef || iceDef->tagCount != 1
        || !sm::spell_has_tag(*iceDef, sm::SpellTag::Ice)
        || std::strcmp(iceDef->statusEffect, "chilled") != 0
        || !nearf(iceDef->statusDuration, 4.0f)) {
        return fail("ice_shard metadata wrong");
    }
    if (!chainDef || chainDef->tagCount != 1
        || !sm::spell_has_tag(*chainDef, sm::SpellTag::Lightning)
        || std::strcmp(chainDef->statusEffect, "shocked") != 0
        || !nearf(chainDef->statusDuration, 2.0f)) {
        return fail("lightning_chain metadata wrong");
    }
    if (!beamDef || beamDef->tagCount != 2
        || !sm::spell_has_tag(*beamDef, sm::SpellTag::Arcane)
        || !sm::spell_has_tag(*beamDef, sm::SpellTag::Light)
        || beamDef->statusEffect[0] != '\0') {
        return fail("energy_beam metadata wrong");
    }
    if (!hasteDef || hasteDef->tagCount != 2
        || !sm::spell_has_tag(*hasteDef, sm::SpellTag::Body)
        || !sm::spell_has_tag(*hasteDef, sm::SpellTag::Air)
        || std::strcmp(hasteDef->statusEffect, "hasted") != 0) {
        return fail("haste metadata wrong");
    }
    if (!flightDef || flightDef->tagCount != 2
        || !sm::spell_has_tag(*flightDef, sm::SpellTag::Air)
        || !sm::spell_has_tag(*flightDef, sm::SpellTag::Arcane)
        || std::strcmp(flightDef->statusEffect, "flying") != 0) {
        return fail("flight metadata wrong");
    }
    if (!fireDef->icon || std::strcmp(fireDef->icon, "*") != 0
        || !fireDef->sourceIcon || std::strcmp(fireDef->sourceIcon, "\xF0\x9F\x94\xA5") != 0
        || fireDef->rarity != sm::SpellRarity::Common
        || !nearf(fireDef->castTime, 0.3f)) {
        return fail("fireball identity metadata wrong");
    }
    if (!iceDef->icon || std::strcmp(iceDef->icon, "I") != 0
        || !iceDef->sourceIcon || std::strcmp(iceDef->sourceIcon, "\xE2\x9D\x84") != 0
        || iceDef->rarity != sm::SpellRarity::Uncommon
        || !nearf(iceDef->castTime, 0.2f)) {
        return fail("ice_shard identity metadata wrong");
    }
    if (!magicDef || !magicDef->icon || std::strcmp(magicDef->icon, "+") != 0
        || !magicDef->sourceIcon || std::strcmp(magicDef->sourceIcon, "\xE2\x9C\xA6") != 0
        || magicDef->rarity != sm::SpellRarity::Common
        || !nearf(magicDef->castTime, 0.0f)) {
        return fail("magic_bolt identity metadata wrong");
    }
    if (!chainDef->icon || std::strcmp(chainDef->icon, "Z") != 0
        || !chainDef->sourceIcon || std::strcmp(chainDef->sourceIcon, "\xE2\x9B\xA7") != 0
        || chainDef->rarity != sm::SpellRarity::Rare
        || !nearf(chainDef->castTime, 0.1f)) {
        return fail("lightning_chain identity metadata wrong");
    }
    if (!beamDef->icon || std::strcmp(beamDef->icon, "=") != 0
        || !beamDef->sourceIcon || std::strcmp(beamDef->sourceIcon, "\xE2\x9A\xA1") != 0
        || beamDef->rarity != sm::SpellRarity::Uncommon
        || !nearf(beamDef->castTime, 0.4f)) {
        return fail("energy_beam identity metadata wrong");
    }
    if (!armDef || !armDef->icon || std::strcmp(armDef->icon, "X") != 0
        || !armDef->sourceIcon || std::strcmp(armDef->sourceIcon, "\xE2\x98\xA0") != 0
        || armDef->rarity != sm::SpellRarity::Mythic
        || !nearf(armDef->castTime, 2.0f)) {
        return fail("armageddon identity metadata wrong");
    }
    if (!hasteDef->icon || std::strcmp(hasteDef->icon, ">") != 0
        || !hasteDef->sourceIcon || std::strcmp(hasteDef->sourceIcon, "\xF0\x9F\x92\xA8") != 0
        || hasteDef->rarity != sm::SpellRarity::Uncommon
        || !nearf(hasteDef->castTime, 0.0f)) {
        return fail("haste identity metadata wrong");
    }
    if (!flightDef->icon || std::strcmp(flightDef->icon, "^") != 0
        || !flightDef->sourceIcon || std::strcmp(flightDef->sourceIcon, "\xF0\x9F\x95\x8A") != 0
        || flightDef->rarity != sm::SpellRarity::Rare
        || !nearf(flightDef->castTime, 0.0f)) {
        return fail("flight identity metadata wrong");
    }
    if (!std::strstr(fireDef->description, "allies included")
        || !std::strstr(iceDef->description, "bosses and elites")
        || !std::strstr(magicDef->description, "bread and butter")
        || !std::strstr(chainDef->description, "losing force")
        || !std::strstr(beamDef->description, "line up")
        || !std::strstr(armDef->description, "moral bankruptcy")
        || !std::strstr(hasteDef->description, "supernatural speed")
        || !std::strstr(flightDef->description, "fall is unforgiving")) {
        return fail("spell description metadata wrong");
    }
    if (!magicDef || magicDef->macroType != sm::MacroEffectType::None
        || magicDef->prosCount != 3
        || std::strcmp(magicDef->pros[0], "No cooldown") != 0
        || std::strcmp(magicDef->cons[2], "No utility") != 0) {
        return fail("magic_bolt flavor metadata wrong");
    }
    if (fireDef->macroType != sm::MacroEffectType::DamageRegion
        || !nearf(fireDef->macroPower, 10.0f)
        || fireDef->prosCount != 3
        || std::strcmp(fireDef->pros[1], "Burning DOT") != 0) {
        return fail("fireball macro/flavor metadata wrong");
    }
    if (!armDef || armDef->macroType != sm::MacroEffectType::DamageRegion
        || !nearf(armDef->macroPower, 50.0f)
        || armDef->consCount != 5
        || std::strcmp(armDef->cons[4], "2 min cooldown") != 0) {
        return fail("armageddon macro/flavor metadata wrong");
    }
    if (hasteDef->macroType != sm::MacroEffectType::TravelSpeed
        || !nearf(hasteDef->macroPower, 1.5f)
        || !nearf(hasteDef->macroDuration, 8.0f)
        || std::strcmp(hasteDef->pros[2], "Works on world map") != 0) {
        return fail("haste macro/flavor metadata wrong");
    }
    if (flightDef->macroType != sm::MacroEffectType::IgnoreTerrain
        || !nearf(flightDef->macroPower, 1.0f)
        || !nearf(flightDef->macroDuration, 12.0f)
        || std::strcmp(flightDef->cons[2], "Blocked indoors") != 0) {
        return fail("flight macro/flavor metadata wrong");
    }

    sm::spellbook_learn(book, "magic_bolt");
    if (!sm::spellbook_cast(world, book, combat, attributes, skills,
                            "magic_bolt", std::uint32_t{0}, 100.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("magic_bolt cast rejected");
    }
    if (combat.currentMp != 1990) return fail("magic_bolt mana cost");
    if (projectile_count(world) != 1) return fail("magic_bolt projectile spawn");
    sm::ecs::Projectile magicBolt{};
    sm::ecs::Position magicBoltPos{};
    if (!find_projectile_pose_by_spell(world, "magic_bolt",
                                       magicBolt, magicBoltPos)) {
        return fail("magic_bolt descriptor missing");
    }
    if (magicBolt.kind != sm::ecs::Projectile::Bolt
        || magicBolt.blastRadius != 0.0f
        || !nearf(magicBolt.radius, 1.5f)
        // Muzzle spawn = caster.x(100) + caster_spawn_offset =
        // playerRadius(1.5) + projectileRadius(1.5) + 2 = 5.0 (Inc 4b geometry:
        // projectileRadius was added so the clearance also holds for fat bolts).
        || !nearf(magicBoltPos.x, 105.0f)
        || !nearf(magicBoltPos.y, 100.0f)
        || !nearf(magicBolt.maxLifeTimer, 3.0f)) {
        return fail("magic_bolt descriptor wrong");
    }
    const sm::CastCheck macroBolt =
        sm::spellbook_can_cast_ex(book, combat, "magic_bolt", false);
    if (macroBolt.ok || macroBolt.reason != "Cannot use on world map") {
        return fail("magic_bolt macro gate failed");
    }

    sm::spellbook_learn(book, "fireball");
    if (!sm::spellbook_cast(world, book, combat, attributes, skills,
                            "fireball", std::uint32_t{0}, 100.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("fireball cast rejected");
    }
    const auto fireCd = book.cooldowns.find("fireball");
    if (fireCd == book.cooldowns.end() || fireCd->second <= 0.0f) {
        return fail("fireball cooldown not started");
    }
    const sm::CastCheck fireBlocked =
        sm::spellbook_can_cast_ex(book, combat, "fireball", true);
    if (fireBlocked.ok || fireBlocked.cooldownRemaining <= 0.0f
        || fireBlocked.reason != "Cooldown 2.0s") {
        return fail("fireball cooldown gate failed");
    }

    sm::ecs::Projectile fireball{};
    if (!find_projectile_by_spell(world, "fireball", fireball)) {
        return fail("fireball descriptor missing");
    }
    if (fireball.kind != sm::ecs::Projectile::Bolt
        || !nearf(fireball.radius, 2.5f)
        || !nearf(fireball.blastRadius, 48.0f)
        || fireball.explodeOnExpiry
        || !nearf(fireball.maxLifeTimer, 3.0f)) {
        return fail("fireball descriptor wrong");
    }

    sm::ecs::World fireExpiryWorld;
    sm::SpellBook fireExpiryBook;
    sm::CombatStats fireExpiryCombat{};
    fireExpiryCombat.currentMp = 1000;
    fireExpiryCombat.maxMp = 1000;
    sm::spellbook_learn(fireExpiryBook, "fireball");
    const auto fireExpiryVictim =
        add_target(fireExpiryWorld, 120.0f, 100.0f, 100.0f, false);
    if (!sm::spellbook_cast(fireExpiryWorld, fireExpiryBook,
                            fireExpiryCombat, attributes, skills,
                            "fireball", std::uint32_t{0}, 100.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("fireball expiry setup cast rejected");
    }
    sm::sub::tick_spell_projectiles(fireExpiryWorld, nullptr, 3.1f);
    if (!nearf(hp_of(fireExpiryWorld, fireExpiryVictim), 100.0f)
        || projectile_count(fireExpiryWorld) != 0) {
        return fail("fireball exploded on timeout unlike TS");
    }

    sm::ecs::World blastWorld;
    auto blastProjectile = blastWorld.create();
    blastWorld.reg.emplace<sm::ecs::Position>(blastProjectile, 0.0f, 0.0f, 0.0f);
    blastWorld.reg.emplace<sm::ecs::Projectile>(blastProjectile,
        0.0f, 0.0f, 0.0f, 2.5f, 0.0f, 0.0f, 10.0f, 48.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        sm::stable_spell_id("fireball"), std::uint32_t{0},
        std::int16_t{0}, sm::ecs::Projectile::Bolt,
        true, true, true);
    const auto blastEdge =
        add_target(blastWorld, 48.0f, 0.0f, 100.0f, false);
    const auto blastOutside =
        add_target(blastWorld, 53.0f, 0.0f, 100.0f, false);
    sm::sub::tick_spell_projectiles(blastWorld, nullptr, 0.0f);
    if (!(hp_of(blastWorld, blastEdge) < 100.0f)
        || !nearf(hp_of(blastWorld, blastOutside), 100.0f)
        || projectile_count(blastWorld) != 0) {
        return fail("fireball blast edge drifted from TS center-radius check");
    }

    sm::ecs::World iceWorld;
    sm::SpellBook iceBook;
    sm::CombatStats iceCombat{};
    iceCombat.currentMp = 1000;
    iceCombat.maxMp = 1000;
    sm::spellbook_learn(iceBook, "ice_shard");
    if (!sm::spellbook_cast(iceWorld, iceBook, iceCombat, attributes, skills,
                            "ice_shard", std::uint32_t{0}, 10.0f, 10.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("ice_shard cast rejected");
    }
    sm::ecs::Projectile ice{};
    if (!find_projectile_by_spell(iceWorld, "ice_shard", ice)
        || ice.kind != sm::ecs::Projectile::Bolt
        || !nearf(ice.radius, 1.5f)
        || ice.blastRadius != 0.0f
        || !nearf(ice.maxLifeTimer, 3.0f)) {
        return fail("ice_shard descriptor wrong");
    }

    sm::SpellBook lowBook;
    sm::spellbook_learn(lowBook, "fireball");
    sm::CombatStats lowCombat{};
    lowCombat.currentMp = 0;
    lowCombat.maxMp = 10;
    const sm::CastCheck lowMana =
        sm::spellbook_can_cast_ex(lowBook, lowCombat, "fireball", true);
    if (lowMana.ok || lowMana.reason != "Not enough mana") {
        return fail("low mana gate failed");
    }

    sm::SpellBook macroBook;
    const char* macroSpellIds[] = {
        "fireball", "ice_shard", "lightning_chain",
        "armageddon", "haste", "flight",
    };
    for (const char* id : macroSpellIds) {
        sm::spellbook_learn(macroBook, id);
    }
    sm::CombatStats macroCombat{};
    macroCombat.currentMp = 2000;
    macroCombat.maxMp = 2000;
    for (const char* id : macroSpellIds) {
        const sm::CastCheck macroCheck =
            sm::spellbook_can_cast_ex(macroBook, macroCombat, id, false);
        if (!macroCheck.ok) {
            return fail("world-map macro canCast drifted from TS");
        }
    }
    const int beforeMacroProjectiles = projectile_count(world);
    if (sm::spellbook_cast(world, macroBook, macroCombat, attributes, skills,
                           "fireball", std::uint32_t{0}, 100.0f, 100.0f, 0.0f,
                           1.0f, 0.0f, 0.0f, false)) {
        return fail("world-map fireball spawned micro projectile");
    }
    if (projectile_count(world) != beforeMacroProjectiles
        || macroCombat.currentMp != 2000) {
        return fail("world-map fireball mutated state");
    }

    sm::spellbook_tick(book, combat, 10.0f);
    if (book.cooldowns.find("fireball") != book.cooldowns.end()) {
        return fail("fireball cooldown did not expire");
    }

    sm::spellbook_learn(book, "energy_beam");
    if (!sm::spellbook_cast(world, book, combat, attributes, skills,
                            "energy_beam", std::uint32_t{0}, 100.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("energy_beam cast rejected");
    }
    bool beamFound = false;
    auto view = world.reg.view<sm::ecs::Projectile>();
    for (auto e : view) {
        const auto& p = view.get<sm::ecs::Projectile>(e);
        if (p.kind == sm::ecs::Projectile::Beam
            && p.visualOnly
            && p.beamLength > 0.0f) {
            beamFound = true;
        }
    }
    if (!beamFound) return fail("energy_beam did not spawn beam visual");
    sm::ecs::Projectile beamDesc{};
    if (!find_projectile_by_spell(world, "energy_beam", beamDesc)
        || !nearf(beamDesc.radius, 1.5f)
        // Beam origin uses the same muzzle offset: 100 + 1.5+1.5+2 = 105.0.
        || !nearf(beamDesc.originX, 105.0f)
        || !nearf(beamDesc.originY, 100.0f)) {
        return fail("energy_beam radius drifted from TS spawn radius");
    }

    sm::spellbook_learn(book, "lightning_chain");
    if (!sm::spellbook_cast(world, book, combat, attributes, skills,
                            "lightning_chain", std::uint32_t{0}, 100.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("lightning_chain cast rejected");
    }
    sm::ecs::Projectile chain{};
    if (!find_projectile_by_spell(world, "lightning_chain", chain)) {
        return fail("lightning_chain descriptor missing");
    }
    // WHAT THIS ASSERTS, AND WHAT IT DELIBERATELY DOES NOT.
    //
    // Lightning Chain flies; it does not chain. `emplace_projectile`
    // (registry.cpp) hardcodes chainRemaining/chainDecay/chainRadius to zero
    // and never feeds them from the row, which declares chainCount 4 and
    // chainDecay 0.70 — so `apply_spell_chain` returns on its first line and
    // the spell is a plain bolt (audit.md III.15, problems.md §19.4).
    //
    // Until 2026-08-06 this block REQUIRED those three zeros. That made a
    // green ctest the proof that the feature is missing: wiring the chain up
    // would have turned this test red, and the next reader would have assumed
    // they broke something. A test may guard a behaviour; it must never guard
    // a defect. The chain fields are therefore asserted NEITHER way here —
    // when the feature is wired, its own test comes with it.
    //
    // What IS promised is asserted, and asserted against the registry row
    // rather than against restated numbers: the row is the single source of
    // truth, so retuning the spell retunes the expectation with it.
    if (chain.kind != sm::ecs::Projectile::Bolt) {
        return fail("lightning_chain must spawn a bolt");
    }
    if (!nearf(chain.radius, chainDef->projectileRadius)) {
        return fail("the bolt's radius must be the one the registry row states");
    }
    if (!(chain.maxLifeTimer > 0.0f) || !nearf(chain.lifeTimer, chain.maxLifeTimer)) {
        return fail("a freshly cast bolt starts with a full life timer");
    }
    // Cast direction was (1, 0, 0): the bolt travels along the aim vector, at
    // whatever speed the spawner gives it.
    if (!(chain.vx > 0.0f) || !nearf(chain.vy, 0.0f) || !nearf(chain.vz, 0.0f)) {
        return fail("the bolt must fly along the direction it was aimed");
    }

    sm::spellbook_learn(book, "haste");
    if (!sm::spellbook_cast(world, book, combat, attributes, skills,
                            "haste", std::uint32_t{0}, 0.0f, 0.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("haste toggle rejected");
    }
    if (!sm::spellbook_has_sustained(book, "haste")) {
        return fail("haste not sustained");
    }
    const int beforeDrain = combat.currentMp;
    sm::spellbook_tick(book, combat, 1.2f);
    if (beforeDrain - combat.currentMp != 12) {
        return fail("haste fractional drain wrong");
    }
    if (!sm::spellbook_has_sustained(book, "haste")) {
        return fail("haste dropped while mana remained");
    }
    combat.currentMp = 1;
    sm::spellbook_tick(book, combat, 1.0f);
    if (combat.currentMp != 0 || sm::spellbook_has_sustained(book, "haste")) {
        return fail("haste did not stop on mana depletion");
    }

    sm::SpellBook zeroTickBook;
    sm::spellbook_learn(zeroTickBook, "haste");
    sm::CombatStats zeroTickCombat{};
    zeroTickCombat.currentMp = 1;
    zeroTickCombat.maxMp = 1;
    if (!sm::spellbook_cast(world, zeroTickBook, zeroTickCombat,
                            attributes, skills, "haste", std::uint32_t{0},
                            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true)) {
        return fail("zero-mp sustained setup rejected");
    }
    sm::spellbook_tick(zeroTickBook, zeroTickCombat, 0.10f);
    if (zeroTickCombat.currentMp != 0
        || !sm::spellbook_has_sustained(zeroTickBook, "haste")) {
        return fail("zero-mp sustained setup did not reach TS boundary");
    }
    sm::spellbook_tick(zeroTickBook, zeroTickCombat, 0.05f);
    if (sm::spellbook_has_sustained(zeroTickBook, "haste")) {
        return fail("zero-mp sustained spell survived positive drain tick");
    }

    sm::SpellBook flightBook;
    sm::spellbook_learn(flightBook, "flight");
    sm::CombatStats flightCombat{};
    flightCombat.currentMp = 50;
    flightCombat.maxMp = 50;
    if (!sm::spellbook_cast(world, flightBook, flightCombat, attributes, skills,
                            "flight", std::uint32_t{0}, 0.0f, 0.0f, 0.0f,
                            0.0f, 1.0f, 0.0f, false)) {
        return fail("flight macro toggle rejected");
    }
    if (!sm::spellbook_has_sustained(flightBook, "flight")) {
        return fail("flight not sustained");
    }
    sm::spellbook_tick(flightBook, flightCombat, 0.5f);
    if (flightCombat.currentMp != 40) {
        return fail("flight drain wrong");
    }

    sm::SpellBook multiSustainBook;
    sm::CombatStats multiSustainCombat{};
    multiSustainCombat.currentMp = 25;
    multiSustainCombat.maxMp = 25;
    sm::spellbook_learn(multiSustainBook, "haste");
    sm::spellbook_learn(multiSustainBook, "flight");
    if (!sm::spellbook_cast(world, multiSustainBook, multiSustainCombat,
                            attributes, skills, "haste", std::uint32_t{0},
                            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true)
        || !sm::spellbook_cast(world, multiSustainBook, multiSustainCombat,
                               attributes, skills, "flight", std::uint32_t{0},
                               0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true)) {
        return fail("multi sustained setup cast rejected");
    }
    sm::spellbook_tick(multiSustainBook, multiSustainCombat, 1.0f);
    if (multiSustainCombat.currentMp != 0
        || sm::spellbook_has_sustained(multiSustainBook, "haste")
        || !sm::spellbook_has_sustained(multiSustainBook, "flight")
        || multiSustainBook.sustainedActive.size() != 1) {
        return fail("multi sustained partial depletion wrong");
    }

    sm::ecs::World hitWorld;
    sm::SpellBook hitBook;
    sm::CombatStats hitCombat{};
    hitCombat.currentMp = 1000;
    hitCombat.maxMp = 1000;
    sm::spellbook_learn(hitBook, "magic_bolt");
    // Agnostic projectiles (owner design decision 2026-07-30): there is NO
    // friendly filter — a player bolt flying through the player's own soldier
    // CONNECTS. Friendly fire is real; watch where you aim.
    const auto hitPlayer = add_player(hitWorld, -1000.0f, -1000.0f);
    const auto friendly = add_target(hitWorld, 104.0f, 100.0f, 100.0f, true);
    if (!sm::spellbook_cast(hitWorld, hitBook, hitCombat, attributes, skills,
                            "magic_bolt", hitPlayer, 0.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("magic_bolt agnostic-hit setup cast rejected");
    }
    sm::sub::tick_spell_projectiles(hitWorld, nullptr, 0.25f);
    if (nearf(hp_of(hitWorld, friendly), 100.0f)
        || projectile_count(hitWorld) != 0) {
        return fail("agnostic projectiles: bolt must strike a friendly in its path");
    }
    // The bolt was consumed by the friendly it struck; the hostile hit runs in
    // its own fresh world (fresh cooldown, clear lane) — the stages no longer
    // share one projectile now that nothing flies through a friendly.
    sm::ecs::World hostWorld;
    sm::SpellBook hostBook;
    sm::CombatStats hostCombat{};
    hostCombat.currentMp = 1000;
    hostCombat.maxMp = 1000;
    sm::spellbook_learn(hostBook, "magic_bolt");
    const auto hostPlayer = add_player(hostWorld, -1000.0f, -1000.0f);
    const auto hostile = add_target(hostWorld, 204.0f, 100.0f, 100.0f, false);
    if (!sm::spellbook_cast(hostWorld, hostBook, hostCombat, attributes, skills,
                            "magic_bolt", hostPlayer, 0.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("magic_bolt hostile-hit setup cast rejected");
    }
    sm::sub::tick_spell_projectiles(hostWorld, nullptr, 0.25f);
    sm::sub::tick_spell_projectiles(hostWorld, nullptr, 0.25f);
    if (!(hp_of(hostWorld, hostile) < 100.0f)
        || !player_last_hit(hostWorld, hostile)
        || projectile_count(hostWorld) != 0) {
        return fail("magic_bolt did not hit hostile target");
    }

    sm::ecs::World beamWorld;
    sm::SpellBook beamBook;
    sm::CombatStats beamCombat{};
    beamCombat.currentMp = 1000;
    beamCombat.maxMp = 1000;
    sm::spellbook_learn(beamBook, "energy_beam");
    const auto beamPlayer = add_player(beamWorld, -1000.0f, -1000.0f);
    const auto beamHit = add_target(beamWorld, 160.0f, 10.0f, 100.0f, false);
    const auto beamNearMiss = add_target(beamWorld, 160.0f, 21.0f, 100.0f, false);
    const auto beamMiss = add_target(beamWorld, 160.0f, 40.0f, 100.0f, false);
    if (!sm::spellbook_cast(beamWorld, beamBook, beamCombat, attributes, skills,
                            "energy_beam", beamPlayer, 0.0f, 10.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("energy_beam effect setup cast rejected");
    }
    sm::sub::tick_spell_projectiles(beamWorld, nullptr, 0.40f);
    if (!(hp_of(beamWorld, beamHit) < 100.0f)
        || !player_last_hit(beamWorld, beamHit)
        || !nearf(hp_of(beamWorld, beamNearMiss), 100.0f)
        || !nearf(hp_of(beamWorld, beamMiss), 100.0f)
        || projectile_count(beamWorld) != 0) {
        return fail("energy_beam line damage wrong");
    }

    sm::ecs::World chainWorld;
    sm::SpellBook chainBook;
    sm::CombatStats chainCombat{};
    chainCombat.currentMp = 1000;
    chainCombat.maxMp = 1000;
    sm::spellbook_learn(chainBook, "lightning_chain");
    const auto chainPlayer = add_player(chainWorld, -1000.0f, -1000.0f);
    // The FRIENDLY stands nearest the caster, the hostile behind him. That
    // ordering is the point: the bolt reaches the ally first, so striking him
    // proves faction plays no part. It used to be the other way round, and the
    // ally was hit only because the old point-sampled collision happened to
    // land nearer 113 than 109 — the test was resting on a sampling artefact,
    // which the swept collision (spell_effects.cpp find_projectile_hit) removed.
    const auto chainFriendly = add_target(chainWorld, 109.0f, 50.0f, 100.0f, true);
    const auto chainFirst = add_target(chainWorld, 113.0f, 50.0f, 100.0f, false);
    const auto chainSecond = add_target(chainWorld, 120.0f, 50.0f, 100.0f, false);
    if (!sm::spellbook_cast(chainWorld, chainBook, chainCombat, attributes, skills,
                            "lightning_chain", chainPlayer, 0.0f, 50.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true)) {
        return fail("lightning_chain effect setup cast rejected");
    }
    sm::sub::tick_spell_projectiles(chainWorld, nullptr, 0.35f);
    // Agnostic projectiles (owner design decision 2026-07-30): the bolt strikes
    // the body it REACHES FIRST — faction plays no part. Here that is the
    // friendly at x=109; with a faction shield he would be filtered out of the
    // candidates and the hostile behind him would take the hit instead. One
    // victim, bolt consumed, the bodies further along untouched.
    if (!(hp_of(chainWorld, chainFriendly) < 100.0f)
        || !nearf(hp_of(chainWorld, chainFirst), 100.0f)
        || !nearf(hp_of(chainWorld, chainSecond), 100.0f)
        || projectile_count(chainWorld) != 0) {
        return fail("lightning_chain agnostic collision wrong");
    }

    sm::ecs::World armWorld;
    sm::SpellBook armBook;
    sm::CombatStats armCombat{};
    armCombat.currentMp = 2000;
    armCombat.maxMp = 2000;
    sm::spellbook_learn(armBook, "armageddon");
    if (!armDef) return fail("armageddon definition missing");
    sm::Attributes armAttributes{};
    armAttributes.intl = 1000;
    if (sm::spell_radius(*armDef, armAttributes, skills)
        <= int(armDef->baseRadius)) {
        return fail("armageddon high-radius test does not exceed base radius");
    }
    const int expectedMeteors =
        armageddon_meteor_count(int(armDef->baseRadius));
    if (expectedMeteors != 32) {
        return fail("armageddon runtime meteor count drifted from TS base radius");
    }
    const float armRngValues[] = {0.0f, 0.5f, 0.25f};
    SeqRng armRng{armRngValues, 3, 0};
    const auto armPlayer = add_player(armWorld, -1000.0f, -1000.0f);
    if (!sm::spellbook_cast(armWorld, armBook, armCombat, armAttributes, skills,
                            "armageddon", armPlayer, 100.0f, 100.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, true,
                            &seq_rng01, &armRng)) {
        return fail("armageddon cast rejected");
    }
    if (armCombat.currentMp != 1000) return fail("armageddon mana cost");
    if (projectile_count(armWorld) != expectedMeteors) {
        return fail("armageddon meteor count wrong");
    }

    const std::uint32_t armId = sm::stable_spell_id("armageddon");
    int armSeen = 0;
    bool haveMeteorPosition = false;
    bool sawTsRngMeteor = false;
    float meteorX = 0.0f;
    float meteorY = 0.0f;
    auto armView = armWorld.reg.view<sm::ecs::Position, sm::ecs::Projectile>();
    for (auto e : armView) {
        const auto& pos = armView.get<sm::ecs::Position>(e);
        const auto& p = armView.get<sm::ecs::Projectile>(e);
        if (p.spellId != armId) return fail("armageddon spawned wrong spell id");
        if (p.kind != sm::ecs::Projectile::Bolt
            || !p.visualOnly
            || !p.explodeOnExpiry
            || !p.friendlyFire
            || !nearf(p.blastRadius, 25.0f)
            || !nearf(p.radius, 2.5f)
            || p.lifeTimer < 0.3f
            || p.lifeTimer > 0.8f
            || !nearf(p.lifeTimer, p.maxLifeTimer)) {
            return fail("armageddon meteor descriptor wrong");
        }
        if (!haveMeteorPosition) {
            meteorX = pos.x;
            meteorY = pos.y;
            haveMeteorPosition = true;
        }
        if (nearf(pos.x, 180.0f)
            && nearf(pos.y, 100.0f)
            && nearf(p.lifeTimer, 0.425f)) {
            sawTsRngMeteor = true;
        }
        ++armSeen;
    }
    if (armSeen != expectedMeteors || !haveMeteorPosition
        || !sawTsRngMeteor) {
        return fail("armageddon meteor inspection failed");
    }
    const auto armVictim =
        add_target(armWorld, meteorX, meteorY, 1000.0f, false);
    sm::sub::tick_spell_projectiles(armWorld, nullptr, 1.0f);
    if (!(hp_of(armWorld, armVictim) < 1000.0f)
        || !player_last_hit(armWorld, armVictim)
        || projectile_count(armWorld) != 0) {
        return fail("armageddon expiry blast wrong");
    }

    // ---- Inc 4d: the owner self-exclusion is gone. Whether a caster is
    // caught by its OWN projectile is decided purely by geometry (does the
    // caster stand in the hit region?) and faction (friendlyFire bypasses the
    // same-side shield). No player special-casing — the owner is a real entity
    // id, exactly like an NPC firer's. -----------------------------------
    {
        // (a) Q1 — "свой взрыв ловит и тебя": a player's own friendlyFire
        // blast catches the player when the player stands in it, and the hit
        // is still attributed player-owned (XP/log route to the player).
        sm::ecs::World selfWorld;
        auto selfPlayer = selfWorld.create();
        selfWorld.reg.emplace<sm::ecs::Position>(selfPlayer, 0.0f, 0.0f, 0.0f);
        selfWorld.reg.emplace<sm::ecs::Health>(selfPlayer, 100.0f, 100.0f);
        selfWorld.reg.emplace<sm::ecs::SubworldTag>(selfPlayer);
        selfWorld.reg.emplace<sm::ecs::PlayerTag>(selfPlayer);
        auto selfBlast = selfWorld.create();
        selfWorld.reg.emplace<sm::ecs::Position>(selfBlast, 0.0f, 0.0f, 0.0f);
        selfWorld.reg.emplace<sm::ecs::Projectile>(selfBlast,
            0.0f, 0.0f, 0.0f, 2.5f, 0.0f, 0.0f, 10.0f, 48.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            sm::stable_spell_id("fireball"),
            std::uint32_t(entt::to_integral(selfPlayer)),
            std::int16_t{0}, sm::ecs::Projectile::Bolt,
            true, true, true);   // friendlyFire, visualOnly, explodeOnExpiry
        sm::sub::tick_spell_projectiles(selfWorld, nullptr, 0.0f);
        if (!(hp_of(selfWorld, selfPlayer) < 100.0f)
            || !player_last_hit(selfWorld, selfPlayer)) {
            return fail("Inc 4d: player friendlyFire blast did not catch its "
                        "own caster (Q1)");
        }
    }
    {
        // (b) NO faction shield (owner design decision 2026-07-30): projectiles
        // are faction-agnostic and strike whoever stands in their path — the
        // caster's own side included. The muzzle offset (spawn geometry), not
        // any exclusion rule, is all that keeps a live cast off its own shell.
        // This bolt is placed ON its player caster, so it must connect.
        sm::ecs::World shieldWorld;
        auto shieldPlayer = shieldWorld.create();
        shieldWorld.reg.emplace<sm::ecs::Position>(shieldPlayer, 0.0f, 0.0f, 0.0f);
        shieldWorld.reg.emplace<sm::ecs::Health>(shieldPlayer, 100.0f, 100.0f);
        shieldWorld.reg.emplace<sm::ecs::SubworldTag>(shieldPlayer);
        shieldWorld.reg.emplace<sm::ecs::PlayerTag>(shieldPlayer);
        auto shieldBolt = shieldWorld.create();
        shieldWorld.reg.emplace<sm::ecs::Position>(shieldBolt, 0.0f, 0.0f, 0.0f);
        shieldWorld.reg.emplace<sm::ecs::Projectile>(shieldBolt,
            0.0f, 0.0f, 0.0f, 1.5f, 1.0f, 1.0f, 10.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            sm::stable_spell_id("magic_bolt"),
            std::uint32_t(entt::to_integral(shieldPlayer)),
            std::int16_t{0}, sm::ecs::Projectile::Bolt,
            false, false, false);   // NOT friendlyFire
        sm::sub::tick_spell_projectiles(shieldWorld, nullptr, 0.0f);
        if (!nearf(hp_of(shieldWorld, shieldPlayer), 90.0f)) {
            return fail("agnostic projectiles: bolt on its own player caster "
                        "must connect (friendly fire is real)");
        }
    }
    {
        // (c) Owner-agnostic — no player special-case: an NPC-owned
        // friendlyFire blast catches its OWN NPC caster too, and the hit is
        // NOT player-owned (no XP leaks to the player sheet).
        sm::ecs::World npcWorld;
        auto npcCaster = npcWorld.create();
        npcWorld.reg.emplace<sm::ecs::Position>(npcCaster, 0.0f, 0.0f, 0.0f);
        npcWorld.reg.emplace<sm::ecs::Health>(npcCaster, 100.0f, 100.0f);
        npcWorld.reg.emplace<sm::ecs::SubworldTag>(npcCaster);
        npcWorld.reg.emplace<sm::ecs::NPCKind>(npcCaster, sm::ecs::NPCKind{2, 2});
        auto npcBlast = npcWorld.create();
        npcWorld.reg.emplace<sm::ecs::Position>(npcBlast, 0.0f, 0.0f, 0.0f);
        npcWorld.reg.emplace<sm::ecs::Projectile>(npcBlast,
            0.0f, 0.0f, 0.0f, 2.5f, 0.0f, 0.0f, 10.0f, 48.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            sm::stable_spell_id("fireball"),
            std::uint32_t(entt::to_integral(npcCaster)),
            std::int16_t{0}, sm::ecs::Projectile::Bolt,
            true, true, true);   // friendlyFire
        sm::sub::tick_spell_projectiles(npcWorld, nullptr, 0.0f);
        if (!(hp_of(npcWorld, npcCaster) < 100.0f)
            || player_last_hit(npcWorld, npcCaster)) {
            return fail("Inc 4d: NPC friendlyFire blast self-catch/ownership "
                        "wrong (must hit, must not be player-owned)");
        }
    }

    {
        // A BOLT MUST CONNECT AT EVERY RANGE, point-blank included.
        //
        // The collision used to sample a single POINT after each step. A magic
        // bolt crosses 6.25 units per tick (400 u/s at 1/64 s) while its contact
        // radius against an ordinary body is ~2.7, so the probe landed at 11.25,
        // 17.5, 23.75 … and saw nothing in between: everything nearer than ~8.5
        // units was untouchable — the whole melee band, kPlayerMeleeRange being
        // 5 — and further out a ~0.85-unit gap recurred every 6.25. Reported
        // from play as "the closer the enemy, the more the bolt flies through
        // him", which is exactly the shape those numbers predict.
        //
        // The ladder below is deliberately irregular and straddles the muzzle
        // offset (5.0) and the old probe points, so a return to point sampling
        // fails here loudly instead of passing by luck. Radii are production's:
        // BodyRadius 1.2 on the target, not the fat 6.0 the other fixtures use,
        // because a fat target hides precisely this bug.
        const float kRanges[] = {1.5f, 3.0f, 4.9f, 6.0f, 8.0f, 9.4f,
                                 14.3f, 20.6f, 27.0f, 35.8f};
        for (const float range : kRanges) {
            sm::ecs::World sweepWorld;
            sm::SpellBook sweepBook;
            sm::CombatStats sweepCombat{};
            sweepCombat.currentMp = 1000;
            sweepCombat.maxMp = 1000;
            sm::spellbook_learn(sweepBook, "magic_bolt");
            // Caster at the origin, so the muzzle geometry is production's —
            // including his BODY RADIUS. add_player leaves it off, and without
            // it target_radius falls back to 6.0, wrapping the caster in a shell
            // far bigger than kPlayerBodyRadius (1.5): the swept segment would
            // then start inside its own caster and strike him instead of the
            // target. Production emplaces BodyRadius on the player
            // (sub/engine.cpp spawn_player_entity), and caster_spawn_offset's
            // +2.0 margin is exactly what keeps the muzzle clear of that shell.
            const auto sweepCaster = add_player(sweepWorld, 0.0f, 0.0f);
            sweepWorld.reg.emplace<sm::ecs::BodyRadius>(
                entt::entity(sweepCaster), sm::ecs::BodyRadius{1.5f});
            auto sweepTarget = sweepWorld.create();
            sweepWorld.reg.emplace<sm::ecs::Position>(
                sweepTarget, range, 0.0f, 0.0f);
            sweepWorld.reg.emplace<sm::ecs::Health>(sweepTarget, 100.0f, 100.0f);
            sweepWorld.reg.emplace<sm::ecs::SubworldTag>(sweepTarget);
            sweepWorld.reg.emplace<sm::ecs::BodyRadius>(
                sweepTarget, sm::ecs::BodyRadius{1.2f});
            if (!sm::spellbook_cast(sweepWorld, sweepBook, sweepCombat,
                                    attributes, skills, "magic_bolt",
                                    sweepCaster, 0.0f, 0.0f, 0.0f,
                                    1.0f, 0.0f, 0.0f, true)) {
                return fail("sweep: magic_bolt cast rejected");
            }
            // Production-sized ticks, until the bolt is spent.
            for (int i = 0; i < 64 && projectile_count(sweepWorld) > 0; ++i) {
                sm::sub::tick_spell_projectiles(sweepWorld, nullptr,
                                                1.0f / 64.0f);
            }
            if (!(hp_of(sweepWorld, sweepTarget) < 100.0f)) {
                std::fprintf(stderr, "  range=%.1f survived untouched\n",
                             double(range));
                return fail("sweep: bolt passed through a body in its path");
            }
            // The caster stands at the origin, behind the muzzle: the stretch
            // swept back to his hand must never wound him.
            if (!nearf(hp_of(sweepWorld, entt::entity(sweepCaster)), 1000.0f)) {
                return fail("sweep: muzzle stretch wounded its own caster");
            }
        }
    }

    std::fprintf(stderr,
                 "PASS: projectiles=%d mp=%d cooldowns=%zu sustained=%zu\n",
                 projectile_count(world), combat.currentMp,
                 book.cooldowns.size(), book.sustainedActive.size());
    return 0;
}
