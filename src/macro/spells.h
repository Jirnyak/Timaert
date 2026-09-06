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
#include "core/table_guard.h"
#include "macro/attributes.h"
#include "macro/bonus.h"
#include "macro/damage_types.h"
#include <algorithm>
#include <cstdint>
#include <string_view>

namespace sm {

enum class SpellTag : std::uint8_t {
    Fire, Ice, Lightning, Dark, Light, Earth, Air, Arcane, Body, Mind,
    Count
};

enum class SpellRarity : std::uint8_t {
    Common, Uncommon, Rare, Epic, Mythic,
    Count
};

enum class DeliveryShape : std::uint8_t {
    Projectile, Beam, Nova, Self, Aura, Chain, Summon, Targeted,
    Count
};

// The words a player reads for each of the three classification enums — rows
// under the enum, not switches (a row added to a table cannot forget its
// label; the guards refuse a drifted one). Same shape as kSpellRuleDefs.
struct SpellTagDef {
    // MUST equal the row's index in kSpellTagDefs (guard below the table).
    SpellTag    id;
    const char* label;
    // Which of the nine armour columns this tag's damage argues with — the
    // canon remap (S15, owner verdict 2026-09-05: Ice→Water, Lightning→Air,
    // Dark→Void, Light→Arcane) applied as DATA, so the phase-5 tag→school
    // collapse changes rows here and nothing anywhere else. Body/Mind are
    // pre-war tags with no school: their damage reads as Blunt/Arcane until
    // that collapse retires them.
    DamageType  dmgType;
    // THE tag→school collapse itself (phase 5, CANON S15: «школа = скилл
    // листа, спелл читает ранг СВОЕЙ школы»), as a column: the six schools
    // are six skills the sheet already has, and the same canon remap names
    // which one this tag's spells train under. Body/Mind SLEEP («до апдейта
    // паладинов и клириков») — SkillId::Count says "no school yet", and a
    // sleeping tag's spells read Spellcraft alone, exactly as before.
    SkillId     school;
};

inline constexpr SpellTagDef kSpellTagDefs[] = {
    {SpellTag::Fire,      "Fire",      DamageType::Fire,   SkillId::FireMagic},
    {SpellTag::Ice,       "Ice",       DamageType::Water,  SkillId::WaterMagic},
    {SpellTag::Lightning, "Lightning", DamageType::Air,    SkillId::AirMagic},
    {SpellTag::Dark,      "Dark",      DamageType::Void,   SkillId::VoidMagic},
    {SpellTag::Light,     "Light",     DamageType::Arcane, SkillId::ArcaneMagic},
    {SpellTag::Earth,     "Earth",     DamageType::Earth,  SkillId::EarthMagic},
    {SpellTag::Air,       "Air",       DamageType::Air,    SkillId::AirMagic},
    {SpellTag::Arcane,    "Arcane",    DamageType::Arcane, SkillId::ArcaneMagic},
    {SpellTag::Body,      "Body",      DamageType::Blunt,  SkillId::Count},
    {SpellTag::Mind,      "Mind",      DamageType::Arcane, SkillId::Count},
};
static_assert(sizeof(kSpellTagDefs) / sizeof(kSpellTagDefs[0])
                  == std::size_t(SpellTag::Count),
              "kSpellTagDefs must carry one row per SpellTag");
static_assert(rows_in_enum_order(kSpellTagDefs, &SpellTagDef::id),
              "kSpellTagDefs rows must stand in SpellTag order");

struct SpellRarityDef {
    // MUST equal the row's index in kSpellRarityDefs (guard below the table).
    SpellRarity id;
    const char* label;
};

inline constexpr SpellRarityDef kSpellRarityDefs[] = {
    {SpellRarity::Common,   "Common"},
    {SpellRarity::Uncommon, "Uncommon"},
    {SpellRarity::Rare,     "Rare"},
    {SpellRarity::Epic,     "Epic"},
    {SpellRarity::Mythic,   "Mythic"},
};
static_assert(sizeof(kSpellRarityDefs) / sizeof(kSpellRarityDefs[0])
                  == std::size_t(SpellRarity::Count),
              "kSpellRarityDefs must carry one row per SpellRarity");
static_assert(rows_in_enum_order(kSpellRarityDefs, &SpellRarityDef::id),
              "kSpellRarityDefs rows must stand in SpellRarity order");

struct DeliveryShapeDef {
    // MUST equal the row's index in kDeliveryShapeDefs (guard below the table).
    DeliveryShape id;
    const char*   label;
};

inline constexpr DeliveryShapeDef kDeliveryShapeDefs[] = {
    {DeliveryShape::Projectile, "Projectile"},
    {DeliveryShape::Beam,       "Beam"},
    {DeliveryShape::Nova,       "Nova"},
    {DeliveryShape::Self,       "Self"},
    {DeliveryShape::Aura,       "Aura"},
    {DeliveryShape::Chain,      "Chain"},
    {DeliveryShape::Summon,     "Summon"},
    {DeliveryShape::Targeted,   "Targeted"},
};
static_assert(sizeof(kDeliveryShapeDefs) / sizeof(kDeliveryShapeDefs[0])
                  == std::size_t(DeliveryShape::Count),
              "kDeliveryShapeDefs must carry one row per DeliveryShape");
static_assert(rows_in_enum_order(kDeliveryShapeDefs, &DeliveryShapeDef::id),
              "kDeliveryShapeDefs rows must stand in DeliveryShape order");

// THE HYBRID (owner's ruling, 2026-08-27). A spell effect is one of two
// things, and which one is not a matter of taste:
//
//   · anything that moves a NUMBER a body already has — strength, speed,
//     health — is a row of the ONE bonus registry (macro/bonus.h). A haste
//     spell and a swift ring say the same thing in the same words, and the
//     magnitude scales with the caster the way every other number does.
//   · anything that changes a RULE OF THE WORLD — walking on air, seeing a
//     map you have not walked, ignoring what the ground costs — has no number
//     to move, so it is a switch. These are the rows below.
//
// `MacroEffectType` was the ancestor of both halves and consumed by NOBODY:
// a fully-authored vocabulary (TravelSpeed, BuffArmy, HealParty…) with power
// and duration columns that no code ever read, while the same facts were
// re-spelled as literals at the call sites. Its stat half is the registry
// now; its rule half is this.
enum class SpellRuleId : std::uint8_t {
    None = 0,
    Flight,         // the body leaves the ground: sub/height.h flight window
    IgnoreTerrain,  // the macro march stops paying what the ground asks
    RevealMap,      // the knowledge layer is written, not walked
    Count
};

struct SpellRuleDef {
    // MUST equal the row's index in kSpellRuleDefs (guard below the table).
    SpellRuleId id;
    const char* key;
    const char* label;
};

inline constexpr SpellRuleDef kSpellRuleDefs[] = {
    {SpellRuleId::None,          "none",           "—"},
    {SpellRuleId::Flight,        "flight",         "Flight"},
    {SpellRuleId::IgnoreTerrain, "ignore_terrain", "Unhindered"},
    {SpellRuleId::RevealMap,     "reveal_map",     "Farsight"},
};
static_assert(sizeof(kSpellRuleDefs) / sizeof(kSpellRuleDefs[0])
                  == std::size_t(SpellRuleId::Count),
              "kSpellRuleDefs must carry one row per SpellRuleId");
static_assert(rows_in_enum_order(kSpellRuleDefs, &SpellRuleDef::id),
              "kSpellRuleDefs rows must stand in SpellRuleId order");

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
    // The blow as DICE (CANON S13: урон = NdM строкой спелла). Scalar-era
    // rows converted mechanically to Nd1; authored spreads are content-stage.
    Dice  dice;
    float baseHeal;
    float baseRadius;                // blast / nova spread
    int   chainCount;
    std::uint8_t chainDecayPct;      // damage kept per jump, whole percent
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
    // ── what it DOES, in the one vocabulary ──
    // Stat effects as rows of the bonus registry, with their BASE magnitude:
    // what a novice's casting is worth. The caster scales it — `spell_bonus`
    // below applies the school law, so a master's haste is a master's haste
    // without a second number anywhere.
    //
    // A standing effect (`sustained`) contributes these while it burns and
    // stops contributing the moment it does not; an instant one applies them
    // once. That difference is the registry's own column, not a second field
    // here.
    static constexpr int kMaxSpellBonuses = 3;
    Bonus effects[kMaxSpellBonuses] = {};

    // ...and the RULE it switches on, if any. Two halves, because a rule has
    // no magnitude to scale and a stat has no need of a switch.
    SpellRuleId rule = SpellRuleId::None;
    // ── flavor ──
    const char* description = "";
    const char* pros[kMaxSpellFlavorItems] = {};
    const char* cons[kMaxSpellFlavorItems] = {};
};

// The armour column this spell's blow argues with: its primary tag's row.
// One reader, so the phase-5 tag→school collapse moves nothing but the table.
inline constexpr DamageType spell_damage_type(const SpellDef& s) {
    return kSpellTagDefs[std::size_t(s.tag)].dmgType;
}

// The school this spell trains under: its primary tag's row. SkillId::Count =
// a sleeping tag (Body/Mind) — no school, Spellcraft alone carries it.
inline constexpr SkillId spell_school(const SpellDef& s) {
    return kSpellTagDefs[std::size_t(s.tag)].school;
}

// What ONE effect cell of a spell is worth in the caster's hands.
//
// THE LAW is the project's own, said about magic: attributes ADD, skills
// MULTIPLY. The row states what a novice's casting does; the caster's
// training multiplies it through the one door that turns a rank into a
// multiplier (`skill_mult`, macro/attributes.h) — so magic's mastery curve is
// not a private formula and cannot drift from the rest of the sheet.
//
// Phase 5 (CANON S15): the training that scales an EFFECT is the spell's own
// SCHOOL — the line this shape was chosen for. A sleeping tag (school ==
// SkillId::Count) falls back to Spellcraft, which is exactly what every
// spell read before schools woke. The school is REQUIRED, not defaulted:
// callers hold the SpellDef and must say `spell_school(def)` — a default
// would be silently wrong for exactly the six tags that just woke.
inline Bonus spell_bonus(const Bonus& base, const Skills& caster,
                         SkillId school) {
    if (base.row == 0) return {};
    const SkillId trained =
        school != SkillId::Count ? school : SkillId::Spellcraft;
    const float scaled = float(base.value) * skill_mult(caster, trained);
    const int rounded = int(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
    Bonus out = base;
    out.value = std::int16_t(std::clamp(rounded, -32768, 32767));
    return out;
}

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
        .dice = {30, 1}, .baseHeal = 0.0f, .baseRadius = 48.0f,
        .chainCount = 0, .chainDecayPct = 0,
        .speed = 280.0f, .duration = 0.0f, .friendlyFire = true,
        .statusEffect = "burning", .statusDuration = 3.0f,
        .scalingPower = 1.2f, .scalingDuration = 0.0f, .scalingRadius = 0.5f,
        .projectileRadius = 2.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,
        // Damage is a BLOW and blows have one door; `baseDamage` above is
        // what this spell strikes for. No stat row and no rule switch.
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
        .dice = {40, 1}, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecayPct = 0,
        .speed = 350.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "chilled", .statusDuration = 4.0f,
        .scalingPower = 1.4f, .scalingDuration = 0.3f, .scalingRadius = 0.0f,
        .projectileRadius = 1.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,
        // A chill that slows what it touches — a STAT, so a row of the one
        // registry, with the sign the curse deserves.
        .effects = {{std::uint8_t(BonusId::Spd), -5}},
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
        .dice = {12, 1}, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecayPct = 0,
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
        .dice = {22, 1}, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 4, .chainDecayPct = 70,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "shocked", .statusDuration = 2.0f,
        .scalingPower = 1.0f, .scalingDuration = 0.0f, .scalingRadius = 0.4f,
        .projectileRadius = 1.5f, .projectileLife = kDefaultProjectileLifeS,
        .beamLength = 0.0f,

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
        .dice = {25, 1}, .baseHeal = 0.0f, .baseRadius = 8.0f,
        .chainCount = 0, .chainDecayPct = 0,
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
        .dice = {80, 1}, .baseHeal = 0.0f, .baseRadius = 160.0f,
        .chainCount = 0, .chainDecayPct = 0,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = true,
        .statusEffect = "burning", .statusDuration = 8.0f,
        .scalingPower = 2.0f, .scalingDuration = 0.5f, .scalingRadius = 1.0f,
        .projectileRadius = 2.5f, .projectileLife = 2.0f,
        .beamLength = 0.0f,

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
        .dice = {0, 1}, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecayPct = 0,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "hasted", .statusDuration = 0.0f,
        .scalingPower = 0.5f, .scalingDuration = 1.2f, .scalingRadius = 0.0f,
        .projectileRadius = 0.0f, .projectileLife = 0.0f,
        .beamLength = 0.0f,
        // Speed is a STAT the body already has, so haste is a row: +20 SPD
        // at a novice's hand, scaled by the caster (spell_bonus). The ×1.5
        // literal that used to live in main.cpp was this row's own number
        // told a second time, in a second place, in different units.
        .effects = {{std::uint8_t(BonusId::Spd), 20}},
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
        .dice = {0, 1}, .baseHeal = 0.0f, .baseRadius = 0.0f,
        .chainCount = 0, .chainDecayPct = 0,
        .speed = 0.0f, .duration = 0.0f, .friendlyFire = false,
        .statusEffect = "flying", .statusDuration = 0.0f,
        .scalingPower = 0.0f, .scalingDuration = 1.0f, .scalingRadius = 0.0f,
        .projectileRadius = 0.0f, .projectileLife = 0.0f,
        .beamLength = 0.0f,
        // Leaving the ground is not a bigger number, it is a different rule.
        .rule = SpellRuleId::Flight,
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
    return std::size_t(tag) < std::size_t(SpellTag::Count)
               ? kSpellTagDefs[std::size_t(tag)].label : "";
}

constexpr const char* spell_rarity_label(SpellRarity rarity) {
    return std::size_t(rarity) < std::size_t(SpellRarity::Count)
               ? kSpellRarityDefs[std::size_t(rarity)].label : "";
}

constexpr const char* spell_shape_label(DeliveryShape shape) {
    return std::size_t(shape) < std::size_t(DeliveryShape::Count)
               ? kDeliveryShapeDefs[std::size_t(shape)].label : "";
}

// (No `spell_macro_label` switch. A rule LABELS ITSELF in its row
// (kSpellRuleDefs) and a stat effect labels itself in the bonus registry, so
// there is nothing left to switch on — and a label added to a table cannot
// forget to appear here.)
constexpr const char* spell_rule_label(SpellRuleId rule) {
    return kSpellRuleDefs[std::size_t(rule) < std::size_t(SpellRuleId::Count)
                              ? std::size_t(rule) : 0].label;
}

} // namespace sm
