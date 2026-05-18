#include "content/spells/registry.h"
#include "content/spells/spell_types.h"

#include <cmath>
#include <cstdint>

namespace sm {

namespace {

constexpr float kArmageddonTau = 6.28318530717958647692f;
constexpr float kArmageddonPerMeteorBlast = 25.0f;
constexpr int kArmageddonMinMeteors = 16;
constexpr float kDefaultProjectileLife = 3.0f;
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

float caster_spawn_offset(const SpellSpawnContext& c) {
    return c.playerRadius + 2.0f;
}

float spawn_random01(const SpellSpawnContext& c, std::uint32_t fallbackSeed) {
    if (c.rng01) return c.rng01(c.rngUser);
    return armageddon_hash01(fallbackSeed);
}

void emplace_projectile(ecs::World& w, const SpellSpawnContext& c,
                        float speed, float radius, float life,
                        float blast,
                        std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const float spawnOffset = caster_spawn_offset(c);
    auto e = w.create();
    w.reg.emplace<ecs::Position>(e,
        c.px + c.nx * spawnOffset,
        c.py + c.ny * spawnOffset);
    w.reg.emplace<ecs::Projectile>(e,
        c.nx * speed, c.ny * speed,
        radius, life, life, c.damage, blast,
        c.px, c.py, 0.0f,
        0.0f, 0.0f,
        c.spellId, c.playerId, std::int16_t(0), ecs::Projectile::Bolt,
        c.friendlyFire, false, false);
    w.reg.emplace<ecs::Sprite>(e, std::uint16_t(0),
        r, g, b, std::uint8_t(255), 1.0f);
    w.reg.emplace<ecs::SubworldTag>(e);
}

void spawn_fireball(ecs::World& w, const SpellSpawnContext& c) {
    const float speed = c.speed > 0.0f ? c.speed : 280.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 2.5f;
    emplace_projectile(w, c, speed, radius, kDefaultProjectileLife, c.effectRadius,
                       0xFF, 0xCC, 0x00);
}

void spawn_ice_shard(ecs::World& w, const SpellSpawnContext& c) {
    const float speed = c.speed > 0.0f ? c.speed : 350.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    emplace_projectile(w, c, speed, radius, kDefaultProjectileLife, 0.0f,
                       0xFF, 0xFF, 0xFF);
}

void spawn_magic_bolt(ecs::World& w, const SpellSpawnContext& c) {
    const float speed = c.speed > 0.0f ? c.speed : 400.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    emplace_projectile(w, c, speed, radius, kDefaultProjectileLife, 0.0f,
                       0xE0, 0xC0, 0xFF);
}

void spawn_lightning_chain(ecs::World& w, const SpellSpawnContext& c) {
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    emplace_projectile(w, c, 300.0f, radius, kDefaultProjectileLife, 0.0f,
                       0xFF, 0xEE, 0x44);
}

void spawn_energy_beam(ecs::World& w, const SpellSpawnContext& c) {
    constexpr float kBeamLen = 300.0f;
    const float radius = c.projectileRadius > 0.0f ? c.projectileRadius : 1.5f;
    const float spawnOffset = caster_spawn_offset(c);
    auto e = w.create();
    w.reg.emplace<ecs::Position>(e,
        c.px + c.nx * (kBeamLen * 0.5f),
        c.py + c.ny * (kBeamLen * 0.5f));
    w.reg.emplace<ecs::Projectile>(e,
        c.nx, c.ny,
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
            c.py + std::sin(angle) * dist);
        w.reg.emplace<ecs::Projectile>(e,
            0.0f, 0.0f,
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

} // namespace

void register_builtin_spells() {
    auto& r = spell_registry();

    r.add({"fireball", "Fireball", "*", "\xF0\x9F\x94\xA5",
        SpellTag::Fire, SpellTag::Fire,
        std::uint8_t{1},
        SpellRarity::Common,
        DeliveryShape::Projectile, 2, 60, 2.0f, 0.3f, false, 0.0f,
        true, true, 30.0f, 0.0f, 48.0f, 0, 0.0f, 280.0f, 0.0f, true,
        "burning", 3.0f,
        1.2f, 0.0f, 0.5f, 2.5f, kDefaultProjectileLife, 0.0f, &spawn_fireball,
        "Hurls a ball of fire that explodes on impact, burning everything in the blast radius - allies included. The classic.",
        MacroEffectType::DamageRegion, 10.0f, 0.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "Strong AoE damage", "Burning DOT", "Good at chokepoints"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "Friendly fire", "Cast time", "Higher mana cost"},
        std::uint8_t{3}});

    r.add({"ice_shard", "Ice Shard", "I", "\xE2\x9D\x84",
        SpellTag::Ice, SpellTag::Ice,
        std::uint8_t{1},
        SpellRarity::Uncommon,
        DeliveryShape::Projectile, 2, 30, 1.5f, 0.2f, false, 0.0f,
        true, true, 40.0f, 0.0f, 0.0f, 0, 0.0f, 350.0f, 0.0f, false,
        "chilled", 4.0f,
        1.4f, 0.3f, 0.0f, 1.5f, kDefaultProjectileLife, 0.0f, &spawn_ice_shard,
        "A razor-sharp shard of magical ice that pierces flesh and numbs the soul. Excellent against bosses and elites - useless against a horde.",
        MacroEffectType::BuffArmy, -5.0f, 1.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "High single-target burst", "Chill slows enemy", "No friendly fire"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "Single target only", "Short cooldown still matters",
            "Weak vs crowds"},
        std::uint8_t{3}});

    r.add({"magic_bolt", "Magic Bolt", "+", "\xE2\x9C\xA6",
        SpellTag::Arcane,
        SpellTag::Arcane, std::uint8_t{1},
        SpellRarity::Common,
        DeliveryShape::Projectile, 1, 10, 0.0f, 0.0f, false, 0.0f,
        true, false, 12.0f, 0.0f, 0.0f, 0, 0.0f, 400.0f, 0.0f, false,
        "", 0.0f,
        1.0f, 0.0f, 0.0f, 1.5f, kDefaultProjectileLife, 0.0f, &spawn_magic_bolt,
        "A bolt of raw arcane energy. Cheap, fast, reliable - the bread and butter of every spell-caster. Won't win wars, but keeps you alive.",
        MacroEffectType::None, 0.0f, 0.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "No cooldown", "Low mana cost", "Fast projectile"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "Weak scaling at high tiers", "No AoE", "No utility"},
        std::uint8_t{3}});

    r.add({"lightning_chain", "Lightning Chain", "Z", "\xE2\x9B\xA7",
        SpellTag::Lightning,
        SpellTag::Lightning, std::uint8_t{1}, SpellRarity::Rare,
        DeliveryShape::Chain, 3, 60, 4.0f, 0.1f, false, 0.0f,
        true, true, 22.0f, 0.0f, 0.0f, 4, 0.70f, 0.0f, 0.0f, false,
        "shocked", 2.0f,
        1.0f, 0.0f, 0.4f, 1.5f, kDefaultProjectileLife, 0.0f, &spawn_lightning_chain,
        "Lightning arcs from the first target to nearby enemies, losing force with each jump. Brilliant against scattered groups - unreliable when you need precision.",
        MacroEffectType::DamageRegion, 5.0f, 0.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "Hits up to 5 targets", "Shock interrupts", "Fast cast"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "Unpredictable jumps", "Damage decays per jump", "High mana"},
        std::uint8_t{3}});

    r.add({"energy_beam", "Energy Beam", "=", "\xE2\x9A\xA1",
        SpellTag::Arcane,
        SpellTag::Light, std::uint8_t{2},
        SpellRarity::Uncommon,
        DeliveryShape::Beam, 2, 100, 2.5f, 0.4f, false, 0.0f,
        true, false, 25.0f, 0.0f, 8.0f, 0, 0.0f, 0.0f, 0.0f, true,
        "", 0.0f,
        1.1f, 0.0f, 0.3f, 1.5f, 0.35f, 300.0f, &spawn_energy_beam,
        "A searing beam of pure energy cuts through everything in its path. Devastating against enemies foolish enough to line up.",
        MacroEffectType::None, 0.0f, 0.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "Pierces all enemies in line", "Instant hit", "Great vs formations"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "Requires aim", "Friendly fire", "Medium-high mana"},
        std::uint8_t{3}});

    r.add({"armageddon", "Armageddon", "X", "\xE2\x98\xA0",
        SpellTag::Fire,
        SpellTag::Dark, std::uint8_t{2},
        SpellRarity::Mythic,
        DeliveryShape::Nova, 5, 1000, 120.0f, 2.0f, false, 0.0f,
        true, true, 80.0f, 0.0f, 160.0f, 0, 0.0f, 0.0f, 0.0f, true,
        "burning", 8.0f,
        2.0f, 0.5f, 1.0f, 2.5f, 2.0f, 0.0f, &spawn_armageddon,
        "Rain fire and ruin upon the world. Everything burns - enemies, allies, buildings, reputation. The ultimate expression of magical supremacy and moral bankruptcy.",
        MacroEffectType::DamageRegion, 50.0f, 0.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "Massive AoE", "Battle-ending power", "Burns everything"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "Friendly fire", "2s cast time", "Enormous mana cost",
            "Faction reputation hit", "2 min cooldown"},
        std::uint8_t{5}});

    r.add({"haste", "Haste", ">", "\xF0\x9F\x92\xA8",
        SpellTag::Body, SpellTag::Air,
        std::uint8_t{2},
        SpellRarity::Uncommon,
        DeliveryShape::Self, 2, 0, 0.0f, 0.0f, true, 10.0f,
        true, true, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, false,
        "hasted", 0.0f,
        0.5f, 1.2f, 0.0f, 0.0f, 0.0f, 0.0f, nullptr,
        "Accelerates body and mind. In combat, you move and strike faster. On the world map, your party covers ground at supernatural speed.",
        MacroEffectType::TravelSpeed, 1.5f, 8.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "Move + attack speed up", "Great for kiting", "Works on world map"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "No direct damage", "Continuous mana drain", "Buff upkeep tax"},
        std::uint8_t{3}});

    r.add({"flight", "Flight", "^", "\xF0\x9F\x95\x8A",
        SpellTag::Air, SpellTag::Arcane,
        std::uint8_t{2},
        SpellRarity::Rare,
        DeliveryShape::Self, 3, 0, 0.0f, 0.0f, true, 20.0f,
        true, true, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f, false,
        "flying", 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, nullptr,
        "Rise above the ground and soar. Walls, rivers, mountains - none of it matters while you fly. But the magic fades fast, and the fall is unforgiving.",
        MacroEffectType::IgnoreTerrain, 1.0f, 12.0f,
        std::array<const char*, kMaxSpellFlavorItems>{
            "Ignore all terrain penalties", "Fly over obstacles",
            "Strategic repositioning"},
        std::uint8_t{3},
        std::array<const char*, kMaxSpellFlavorItems>{
            "High mana drain", "No combat benefit", "Blocked indoors"},
        std::uint8_t{3}});
}

} // namespace sm
