// Spell registry — the DATA rows of the spell content class (ARCHITECTURE.md
// Rule 13, owner ruling 2026-08-14). A spell's numbers and strings live HERE,
// in the world layers, beside kFactionDefs / kNpcTypeDefs / the creature
// catalog, so macro worldgen (spire placement asks tier), the subworld and the
// event pump may all ASK the registry instead of carrying its numbers around
// as cargo. Behaviour stays above: the spawn functions bind to these rows by
// ordinal in content/spells/ (kSpellEffects), the way a creature row binds to
// its archetype silhouette in the shader.
//
// ORDINALS ARE APPEND-ONLY (the creature-catalog law): the row index rides in
// saves (spire → spellOrdinal) and in events; reordering or inserting
// mid-table silently re-keys every spire in every save. Add new spells at the
// END. spell_registry_test pins the existing order.
//
// Adding a spell = one row here + one effect row in content/spells (the
// static_assert there refuses a mismatch).
#pragma once
#include <cstdint>
#include <string_view>

namespace sm {

enum class SpellTag : std::uint8_t {
    Fire, Ice, Lightning, Dark, Light, Earth, Air, Arcane, Body, Mind,
};

enum class SpellRarity : std::uint8_t {
    Common, Uncommon, Rare, Epic, Mythic,
};

enum class DeliveryShape : std::uint8_t {
    Projectile, Beam, Nova, Self, Aura, Chain, Summon, Targeted,
};

enum class MacroEffectType : std::uint8_t {
    None,
    TravelSpeed,
    IgnoreTerrain,
    HealParty,
    DamageRegion,
    RevealMap,
    BuffArmy,
};

inline constexpr int kMaxSpellFlavorItems = 5;

// Shared bolt lifetime. The 3×3 window is a closed box on all six sides, so a
// bolt that hits nothing dies on a wall — this timer is not the range law, it
// is the projectile pool's straggler-reaper (and the range for headless tests
// that run with the box open).
inline constexpr float kDefaultProjectileLifeS = 3.0f;

// Pure data — no behaviour, no ownership, everything points at string
// literals. tag == secondaryTag means the spell has ONE tag. pros/cons are
// null-terminated within their fixed arrays (spell_flavor_count).
struct SpellDef {
    // ── identity ──
    const char* id;
    const char* name;
    const char* icon;        // ASCII fallback for ImGui's default font
    const char* sourceIcon;  // full glyph (emoji), kept for icon-capable fonts
    // ── classification ──
    SpellTag      tag;
    SpellTag      secondaryTag;      // == tag when single-tagged
    SpellRarity   rarity;
    DeliveryShape shape;
    int           tier;              // 1..5; drives spire zone gating
    // ── cast economy ──
    int   manaCost;
    float cooldown;                  // seconds
    float castTime;                  // seconds
    bool  sustained;                 // toggled aura paid by drain, not cost
    float manaDrain;                 // mana per second while sustained
    bool  hasMicro;                  // castable in the subworld
    bool  hasMacro;                  // castable on the world map
    // ── effect numbers ──
    float baseDamage;
    float baseHeal;
    float baseRadius;                // blast / nova spread
    int   chainCount;
    float chainDecay;                // damage multiplier per jump
    float speed;                     // projectile speed, tiles/s
    float duration;
    bool  friendlyFire;
    const char* statusEffect;        // "" = none
    float statusDuration;
    // ── per-rank scaling ──
    float scalingPower;
    float scalingDuration;
    float scalingRadius;
    // ── projectile geometry ──
    float projectileRadius;
    float projectileLife;
    float beamLength;
    // ── macro-map effect ──
    MacroEffectType macroType = MacroEffectType::None;
    float macroPower    = 0.0f;
    float macroDuration = 0.0f;
    // ── flavor ──
    const char* description = "";
    const char* pros[kMaxSpellFlavorItems] = {};
    const char* cons[kMaxSpellFlavorItems] = {};
};

inline constexpr SpellDef kSpellDefs[] = {
    {
        .id = "fireball", .name = "Fireball", .icon = "*",
        .sourceIcon = "\xF0\x9F\x94\xA5",
        .tag = SpellTag::Fire, .secondaryTag = SpellTag::Fire,
        .rarity = SpellRarity::Common, .shape = DeliveryShape::Projectile,
        .tier = 2,
        .manaCost = 60, .cooldown = 2.0f, .castTime = 0.3f,
        .sustained = false, .manaDrain = 0.0f,
        .hasMicro = true, .hasMacro = true,
        .baseDamage = 30.0f, .baseHeal = 0.0f, .baseRadius = 48.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 280.0f, .duration = 0.0f, .friendlyFire = true,
        .statusEffect = "burning", .statusDuration = 3.0f,
        .scalingPower = 1.2f, .scalingDuration = 0.0f, .scalingRadius = 0.5f,
        .projectileRadius = 2.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,
        .macroType = MacroEffectType::DamageRegion,
        .macroPower = 10.0f, .macroDuration = 0.0f,
        .description = "Hurls a ball of fire that explodes on impact, burning "
                       "everything in the blast radius - allies included. The "
                       "classic.",
        .pros = {"Strong AoE damage", "Burning DOT", "Good at chokepoints"},
        .cons = {"Friendly fire", "Cast time", "Higher mana cost"},
    },
    {
        .id = "ice_shard", .name = "Ice Shard", .icon = "I",
        .sourceIcon = "\xE2\x9D\x84",
        .tag = SpellTag::Ice, .secondaryTag = SpellTag::Ice,
        .rarity = SpellRarity::Uncommon, .shape = DeliveryShape::Projectile,
        .tier = 2,
        .manaCost = 30, .cooldown = 1.5f, .castTime = 0.2f,
        .sustained = false, .manaDrain = 0.0f,
        .hasMicro = true, .hasMacro = true,
        .baseDamage = 40.0f, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 350.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "chilled", .statusDuration = 4.0f,
        .scalingPower = 1.4f, .scalingDuration = 0.3f, .scalingRadius = 0.0f,
        .projectileRadius = 1.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,
        .macroType = MacroEffectType::BuffArmy,
        .macroPower = -5.0f, .macroDuration = 1.0f,
        .description = "A razor-sharp shard of magical ice that pierces flesh "
                       "and numbs the soul. Excellent against bosses and "
                       "elites - useless against a horde.",
        .pros = {"High single-target burst", "Chill slows enemy",
                 "No friendly fire"},
        .cons = {"Single target only", "Short cooldown still matters",
                 "Weak vs crowds"},
    },
    {
        .id = "magic_bolt", .name = "Magic Bolt", .icon = "+",
        .sourceIcon = "\xE2\x9C\xA6",
        .tag = SpellTag::Arcane, .secondaryTag = SpellTag::Arcane,
        .rarity = SpellRarity::Common, .shape = DeliveryShape::Projectile,
        .tier = 1,
        .manaCost = 10, .cooldown = 0.0f, .castTime = 0.0f,
        .sustained = false, .manaDrain = 0.0f,
        .hasMicro = true, .hasMacro = false,
        .baseDamage = 12.0f, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 400.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "", .statusDuration = 0.0f,
        .scalingPower = 1.0f, .scalingDuration = 0.0f, .scalingRadius = 0.0f,
        .projectileRadius = 1.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,
        .description = "A bolt of raw arcane energy. Cheap, fast, reliable - "
                       "the bread and butter of every spell-caster. Won't win "
                       "wars, but keeps you alive.",
        .pros = {"No cooldown", "Low mana cost", "Fast projectile"},
        .cons = {"Weak scaling at high tiers", "No AoE", "No utility"},
    },
    {
        .id = "lightning_chain", .name = "Lightning Chain", .icon = "Z",
        .sourceIcon = "\xE2\x9B\xA7",
        .tag = SpellTag::Lightning, .secondaryTag = SpellTag::Lightning,
        .rarity = SpellRarity::Rare, .shape = DeliveryShape::Chain,
        .tier = 3,
        .manaCost = 60, .cooldown = 4.0f, .castTime = 0.1f,
        .sustained = false, .manaDrain = 0.0f,
        .hasMicro = true, .hasMacro = true,
        .baseDamage = 22.0f, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 4, .chainDecay = 0.70f,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "shocked", .statusDuration = 2.0f,
        .scalingPower = 1.0f, .scalingDuration = 0.0f, .scalingRadius = 0.4f,
        .projectileRadius = 1.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,
        .macroType = MacroEffectType::DamageRegion,
        .macroPower = 5.0f, .macroDuration = 0.0f,
        .description = "Lightning arcs from the first target to nearby "
                       "enemies, losing force with each jump. Brilliant "
                       "against scattered groups - unreliable when you need "
                       "precision.",
        .pros = {"Hits up to 5 targets", "Shock interrupts", "Fast cast"},
        .cons = {"Unpredictable jumps", "Damage decays per jump", "High mana"},
    },
    {
        .id = "energy_beam", .name = "Energy Beam", .icon = "=",
        .sourceIcon = "\xE2\x9A\xA1",
        .tag = SpellTag::Arcane, .secondaryTag = SpellTag::Light,
        .rarity = SpellRarity::Uncommon, .shape = DeliveryShape::Beam,
        .tier = 2,
        .manaCost = 100, .cooldown = 2.5f, .castTime = 0.4f,
        .sustained = false, .manaDrain = 0.0f,
        .hasMicro = true, .hasMacro = false,
        .baseDamage = 25.0f, .baseHeal = 0.0f, .baseRadius = 8.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = true,
        .statusEffect = "", .statusDuration = 0.0f,
        .scalingPower = 1.1f, .scalingDuration = 0.0f, .scalingRadius = 0.3f,
        .projectileRadius = 1.5f, .projectileLife = 0.35f,
        .beamLength = 300.0f,
        .description = "A searing beam of pure energy cuts through everything "
                       "in its path. Devastating against enemies foolish "
                       "enough to line up.",
        .pros = {"Pierces all enemies in line", "Instant hit",
                 "Great vs formations"},
        .cons = {"Requires aim", "Friendly fire", "Medium-high mana"},
    },
    {
        .id = "armageddon", .name = "Armageddon", .icon = "X",
        .sourceIcon = "\xE2\x98\xA0",
        .tag = SpellTag::Fire, .secondaryTag = SpellTag::Dark,
        .rarity = SpellRarity::Mythic, .shape = DeliveryShape::Nova,
        .tier = 5,
        .manaCost = 1000, .cooldown = 120.0f, .castTime = 2.0f,
        .sustained = false, .manaDrain = 0.0f,
        .hasMicro = true, .hasMacro = true,
        .baseDamage = 80.0f, .baseHeal = 0.0f, .baseRadius = 160.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = true,
        .statusEffect = "burning", .statusDuration = 8.0f,
        .scalingPower = 2.0f, .scalingDuration = 0.5f, .scalingRadius = 1.0f,
        .projectileRadius = 2.5f, .projectileLife = 2.0f,
        .beamLength = 0.0f,
        .macroType = MacroEffectType::DamageRegion,
        .macroPower = 50.0f, .macroDuration = 0.0f,
        .description = "Rain fire and ruin upon the world. Everything burns - "
                       "enemies, allies, buildings, reputation. The ultimate "
                       "expression of magical supremacy and moral bankruptcy.",
        .pros = {"Massive AoE", "Battle-ending power", "Burns everything"},
        .cons = {"Friendly fire", "2s cast time", "Enormous mana cost",
                 "Faction reputation hit", "2 min cooldown"},
    },
    {
        .id = "haste", .name = "Haste", .icon = ">",
        .sourceIcon = "\xF0\x9F\x92\xA8",
        .tag = SpellTag::Body, .secondaryTag = SpellTag::Air,
        .rarity = SpellRarity::Uncommon, .shape = DeliveryShape::Self,
        .tier = 2,
        .manaCost = 0, .cooldown = 0.0f, .castTime = 0.0f,
        .sustained = true, .manaDrain = 10.0f,
        .hasMicro = true, .hasMacro = true,
        .baseDamage = 0.0f, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "hasted", .statusDuration = 0.0f,
        .scalingPower = 0.5f, .scalingDuration = 1.2f, .scalingRadius = 0.0f,
        .projectileRadius = 0.0f, .projectileLife = 0.0f,
        .beamLength = 0.0f,
        .macroType = MacroEffectType::TravelSpeed,
        .macroPower = 1.5f, .macroDuration = 8.0f,
        .description = "Accelerates body and mind. In combat, you move and "
                       "strike faster. On the world map, your party covers "
                       "ground at supernatural speed.",
        .pros = {"Move + attack speed up", "Great for kiting",
                 "Works on world map"},
        .cons = {"No direct damage", "Continuous mana drain",
                 "Buff upkeep tax"},
    },
    {
        .id = "flight", .name = "Flight", .icon = "^",
        .sourceIcon = "\xF0\x9F\x95\x8A",
        .tag = SpellTag::Air, .secondaryTag = SpellTag::Arcane,
        .rarity = SpellRarity::Rare, .shape = DeliveryShape::Self,
        .tier = 3,
        .manaCost = 0, .cooldown = 0.0f, .castTime = 0.0f,
        .sustained = true, .manaDrain = 20.0f,
        .hasMicro = true, .hasMacro = true,
        .baseDamage = 0.0f, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecay = 0.0f,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "flying", .statusDuration = 0.0f,
        .scalingPower = 0.0f, .scalingDuration = 1.0f, .scalingRadius = 0.0f,
        .projectileRadius = 0.0f, .projectileLife = 0.0f,
        .beamLength = 0.0f,
        .macroType = MacroEffectType::IgnoreTerrain,
        .macroPower = 1.0f, .macroDuration = 12.0f,
        .description = "Rise above the ground and soar. Walls, rivers, "
                       "mountains - none of it matters while you fly. But the "
                       "magic fades fast, and the fall is unforgiving.",
        .pros = {"Ignore all terrain penalties", "Fly over obstacles",
                 "Strategic repositioning"},
        .cons = {"High mana drain", "No combat benefit", "Blocked indoors"},
    },
};

inline constexpr int kSpellCount =
    int(sizeof(kSpellDefs) / sizeof(kSpellDefs[0]));

constexpr const SpellDef* spell_find(std::string_view id) {
    for (const SpellDef& s : kSpellDefs)
        if (id == s.id) return &s;
    return nullptr;
}

// Row index of `id` in kSpellDefs — THE stable spell ordinal (append-only law
// above); -1 if unknown.
constexpr int spell_ordinal(std::string_view id) {
    for (int i = 0; i < kSpellCount; ++i)
        if (id == kSpellDefs[i].id) return i;
    return -1;
}

// FNV-1a 32-bit over the id string — the transient wire id events and live
// projectiles carry (never saved; the SAVED key is the ordinal above).
constexpr std::uint32_t stable_spell_id(std::string_view id) {
    std::uint32_t h = std::uint32_t{2166136261};
    for (char c : id) {
        h ^= static_cast<std::uint8_t>(c);
        h *= std::uint32_t{16777619};
    }
    return h;
}

// Entries used, counting the non-null prefix of a flavor array.
constexpr int spell_flavor_count(
        const char* const (&items)[kMaxSpellFlavorItems]) {
    int n = 0;
    while (n < kMaxSpellFlavorItems && items[n]) ++n;
    return n;
}

constexpr bool spell_has_tag(const SpellDef& spell, SpellTag tag) {
    return spell.tag == tag || spell.secondaryTag == tag;
}

constexpr const char* spell_tag_label(SpellTag tag) {
    switch (tag) {
        case SpellTag::Fire: return "Fire";
        case SpellTag::Ice: return "Ice";
        case SpellTag::Lightning: return "Lightning";
        case SpellTag::Dark: return "Dark";
        case SpellTag::Light: return "Light";
        case SpellTag::Earth: return "Earth";
        case SpellTag::Air: return "Air";
        case SpellTag::Arcane: return "Arcane";
        case SpellTag::Body: return "Body";
        case SpellTag::Mind: return "Mind";
    }
    return "";
}

constexpr const char* spell_rarity_label(SpellRarity rarity) {
    switch (rarity) {
        case SpellRarity::Common: return "Common";
        case SpellRarity::Uncommon: return "Uncommon";
        case SpellRarity::Rare: return "Rare";
        case SpellRarity::Epic: return "Epic";
        case SpellRarity::Mythic: return "Mythic";
    }
    return "";
}

constexpr const char* spell_shape_label(DeliveryShape shape) {
    switch (shape) {
        case DeliveryShape::Projectile: return "Projectile";
        case DeliveryShape::Beam: return "Beam";
        case DeliveryShape::Nova: return "Nova";
        case DeliveryShape::Self: return "Self";
        case DeliveryShape::Aura: return "Aura";
        case DeliveryShape::Chain: return "Chain";
        case DeliveryShape::Summon: return "Summon";
        case DeliveryShape::Targeted: return "Targeted";
    }
    return "";
}

constexpr const char* spell_macro_label(MacroEffectType macro) {
    switch (macro) {
        case MacroEffectType::None: return "None";
        case MacroEffectType::TravelSpeed: return "Travel Speed";
        case MacroEffectType::IgnoreTerrain: return "Ignore Terrain";
        case MacroEffectType::HealParty: return "Heal Party";
        case MacroEffectType::DamageRegion: return "Damage Region";
        case MacroEffectType::RevealMap: return "Reveal Map";
        case MacroEffectType::BuffArmy: return "Buff Army";
    }
    return "";
}

} // namespace sm
