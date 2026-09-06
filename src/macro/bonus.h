// THE bonus registry — one table every modifier in the game is a row of.
//
// Owner's ruling, 2026-08-27: «ОДНА УНИВЕРСАЛЬНАЯ ТАБЛИЦА БОНУСОВ, из которой
// комбинируются аффиксы предметов, ПЕРКИ, аура лидера и эффекты зелий — цель
// бонуса = адрес в существующих системах, применение — ОДНОЙ функцией».
//
// Before it there were FIVE half-doors saying overlapping things in five
// vocabularies: `ItemEffect` (six named ints, three of which no code ever
// read), `AuraMods` (a target/index pair, the only one that was already
// honest), the perk system (24 ids, 8 descriptions, ONE effect — and that one
// expressed as a row in the aura table while a second perk was a hardcoded
// branch in a UI callback), the string-verb applicator in events/ ("heal_hp",
// "restore_mp"…), and the sustained-spell multipliers (a literal ×1.5 keyed by
// the string "haste"). Five answers to one question about the world, which
// CANON S26 forbids by name.
//
// ── WHY A REGISTRY AND NOT A STRUCT ──────────────────────────────────────
// The shape is not a preference, it is forced by the save. An item's affixes
// ride `ItemRef` as four `{row, value}` pairs where the row is ONE BYTE
// (macro/items.h) — so "where does this land" cannot be a pair of fields in
// the instance, it has to be an ordinal into a table. That is the same trade
// factions, biomes, spells and creatures already made: the instance carries an
// ordinal, the table carries the meaning, and retuning the meaning does not
// rewrite a single saved item.
//
// ── STANDING vs INSTANT IS A COLUMN ──────────────────────────────────────
// The old doors encoded this in WHICH FUNCTION applied them, which is why the
// aura could never be taken off: `apply_aura` wrote its deltas straight into
// the member's stored sheet at birth, with no source tracking and no way back,
// so a squad that outlived its leader's perk kept the buff forever. Here the
// difference is a column, and the two verbs are different by construction:
//
//   · STANDING bonuses accumulate into `BonusTotals` and are read through
//     `effective_sheet(base, totals)`, which returns a MODIFIED COPY. The
//     stored sheet is never touched, so a bonus can be removed by simply not
//     accumulating it next time.
//   · INSTANT bonuses (a potion, a meal, a scripted blessing) act once on the
//     pools through `apply_instant`, and leave nothing behind to remove.
#pragma once

#include "core/table_guard.h"
#include "macro/attributes.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace sm {

// ── The instance ─────────────────────────────────────────────────────────
// What an item, a perk or a spell actually carries: a row of the table below
// and how much of it. Two fields, three bytes, and it is ALREADY the layout
// `ItemAffix` rides in the save format — this type is that one, given its real
// name and a home beside the table it indexes.
struct Bonus {
    std::uint8_t row = 0;     // BonusId; 0 = None, so a zeroed slot is empty
    std::int16_t value = 0;   // signed: a curse is the same row as a blessing
};

// ── The rows ─────────────────────────────────────────────────────────────

// Where a bonus lands. Each is an address space the sheet already has, so a
// bonus never invents a number — it names one that exists.
enum class BonusTarget : std::uint8_t {
    Attribute,   // index = AttributeId; standing
    SkillRank,   // index = SkillId;     standing
    Pool,        // index = PoolId;      instant
};

// The three pools a body actually carries. They are NOT part of the sheet
// (rpg.md: combat is derived from the sheet, never stored inside it), which is
// exactly why they need their own small id space here.
enum class PoolId : std::uint8_t { Hp, Mp, Sp, Count };

enum class BonusId : std::uint8_t {
    None = 0,
    // One row per attribute — an address, not a formula. (Vit died with the
    // canon eight, 2026-09-03; save bumped, ordinals re-keyed.)
    Str, End, Wil, Intl, Wis, Lck, Cha, Spd,
    // ...and one per skill rank. (Fighter became Armsmaster with the canon
    // skill list — same ordinal, the canon name.)
    Bodybuilding, Meditation, Athletics, Travel, Armsmaster,
    Marathon, Spellcraft, Weightlifting,
    // ...and the pools, which are the instant half.
    HealHp, HealMp, HealSp,
    // ── skills-64 tail (2026-09-03, kSaveVersion 78) — APPENDED, because a
    // bonus ordinal is a byte in every saved affix. One row per new skill.
    Sword, Axe, Spear, Mace, Dagger, Bow, Staff,
    HeavyArmor, LightArmor, Unarmored, Shield,
    FireMagic, WaterMagic, AirMagic, EarthMagic, ArcaneMagic, VoidMagic,
    Acrobatics, Scouting, Prospecting,
    Trade, Quartermaster, Foraging, Learning,
    // Appended v79 with SkillId::Unarmed — ordinals are forever.
    Unarmed,
    Count
};

struct BonusDef {
    // MUST equal the row's index in kBonusDefs (guard below the table).
    BonusId      id;
    const char*  key;      // authoring id; runtime addresses by ordinal
    const char*  label;    // what a human reads on an item or a perk
    BonusTarget  target;
    std::uint8_t index;    // into the target's own id space
};

inline constexpr BonusDef kBonusDefs[] = {
    {BonusId::None, "none", "—", BonusTarget::Attribute, 0},

    {BonusId::Str,  "str",  "Strength",     BonusTarget::Attribute, std::uint8_t(AttributeId::Str)},
    {BonusId::End,  "end",  "Endurance",    BonusTarget::Attribute, std::uint8_t(AttributeId::End)},
    {BonusId::Wil,  "wil",  "Will",         BonusTarget::Attribute, std::uint8_t(AttributeId::Wil)},
    {BonusId::Intl, "intl", "Intellect",    BonusTarget::Attribute, std::uint8_t(AttributeId::Intl)},
    {BonusId::Wis,  "wis",  "Wisdom",       BonusTarget::Attribute, std::uint8_t(AttributeId::Wis)},
    {BonusId::Lck,  "lck",  "Luck",         BonusTarget::Attribute, std::uint8_t(AttributeId::Lck)},
    {BonusId::Cha,  "cha",  "Charisma",     BonusTarget::Attribute, std::uint8_t(AttributeId::Cha)},
    {BonusId::Spd,  "spd",  "Speed",        BonusTarget::Attribute, std::uint8_t(AttributeId::Spd)},

    {BonusId::Bodybuilding,  "bodybuilding",  "Bodybuilding",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Bodybuilding)},
    {BonusId::Meditation,    "meditation",    "Meditation",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Meditation)},
    {BonusId::Athletics,     "athletics",     "Athletics",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Athletics)},
    {BonusId::Travel,        "travel",        "Travel",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Travel)},
    {BonusId::Armsmaster,    "armsmaster",    "Armsmaster",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Armsmaster)},
    {BonusId::Marathon,      "marathon",      "Marathon",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Marathon)},
    {BonusId::Spellcraft,    "spellcraft",    "Spellcraft",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Spellcraft)},
    {BonusId::Weightlifting, "weightlifting", "Weightlifting",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Weightlifting)},

    // The instant half. Signed, so one row says both "restores 30" and
    // "spends 30" — a draught that costs vigour is a potion with the other
    // sign, not another row.
    //
    // WITH ONE EXCEPTION, and it is a ruling rather than a quirk (owner,
    // 2026-08-27): HP does not go DOWN through this door. A wound is a blow,
    // blows have exactly one door (sub/damage.cpp apply_damage) and that door
    // owns mitigation, the already-dead guard and the death protocol. A
    // negative HP here would be a second damage system — and one that only the
    // player could ever be hurt by, which is the ИГРОК = НПЦ violation twice
    // over. `apply_instant` refuses it below. Mana and vigour have no such
    // door and no armour argues with them, so their rows stay two-way.
    // Labelled the way every BAR in the game is labelled, not with the pool's
    // full name: "Used Health Potion: +5 HP" is what a player reads. The
    // attribute rows above keep their full words for the opposite reason — an
    // item that says "+2 Strength" reads better than one that says "+2 STR".
    {BonusId::HealHp, "heal_hp", "HP", BonusTarget::Pool, std::uint8_t(PoolId::Hp)},
    {BonusId::HealMp, "heal_mp", "MP", BonusTarget::Pool, std::uint8_t(PoolId::Mp)},
    {BonusId::HealSp, "heal_sp", "SP", BonusTarget::Pool, std::uint8_t(PoolId::Sp)},

    // ── skills-64 tail (v78): every canon skill is addressable by an item
    // affix, a spell effect or a future perk row through the ONE registry.
    {BonusId::Sword,       "sword",        "Sword",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Sword)},
    {BonusId::Axe,         "axe",          "Axe",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Axe)},
    {BonusId::Spear,       "spear",        "Spear",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Spear)},
    {BonusId::Mace,        "mace",         "Mace",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Mace)},
    {BonusId::Dagger,      "dagger",       "Dagger",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Dagger)},
    {BonusId::Bow,         "bow",          "Bow",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Bow)},
    {BonusId::Staff,       "staff",        "Staff",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Staff)},
    {BonusId::HeavyArmor,  "heavy_armor",  "Heavy Armor",
     BonusTarget::SkillRank, std::uint8_t(SkillId::HeavyArmor)},
    {BonusId::LightArmor,  "light_armor",  "Light Armor",
     BonusTarget::SkillRank, std::uint8_t(SkillId::LightArmor)},
    {BonusId::Unarmored,   "unarmored",    "Unarmored",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Unarmored)},
    {BonusId::Shield,      "shield",       "Shield",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Shield)},
    {BonusId::FireMagic,   "fire_magic",   "Fire Magic",
     BonusTarget::SkillRank, std::uint8_t(SkillId::FireMagic)},
    {BonusId::WaterMagic,  "water_magic",  "Water Magic",
     BonusTarget::SkillRank, std::uint8_t(SkillId::WaterMagic)},
    {BonusId::AirMagic,    "air_magic",    "Air Magic",
     BonusTarget::SkillRank, std::uint8_t(SkillId::AirMagic)},
    {BonusId::EarthMagic,  "earth_magic",  "Earth Magic",
     BonusTarget::SkillRank, std::uint8_t(SkillId::EarthMagic)},
    {BonusId::ArcaneMagic, "arcane_magic", "Arcane Magic",
     BonusTarget::SkillRank, std::uint8_t(SkillId::ArcaneMagic)},
    {BonusId::VoidMagic,   "void_magic",   "Void Magic",
     BonusTarget::SkillRank, std::uint8_t(SkillId::VoidMagic)},
    {BonusId::Acrobatics,  "acrobatics",   "Acrobatics",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Acrobatics)},
    {BonusId::Scouting,    "scouting",     "Scouting",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Scouting)},
    {BonusId::Prospecting, "prospecting",  "Prospecting",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Prospecting)},
    {BonusId::Trade,       "trade",        "Trade",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Trade)},
    {BonusId::Quartermaster, "quartermaster", "Quartermaster",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Quartermaster)},
    {BonusId::Foraging,    "foraging",     "Foraging",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Foraging)},
    {BonusId::Learning,    "learning",     "Learning",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Learning)},
    {BonusId::Unarmed,     "unarmed",      "Unarmed",
     BonusTarget::SkillRank, std::uint8_t(SkillId::Unarmed)},
};
static_assert(sizeof(kBonusDefs) / sizeof(kBonusDefs[0])
                  == std::size_t(BonusId::Count),
              "kBonusDefs must carry one row per BonusId");
static_assert(rows_in_enum_order(kBonusDefs, &BonusDef::id),
              "kBonusDefs rows must stand in BonusId order");
// The instance's row ordinal is ONE BYTE in the save format (macro/items.h
// ItemRef::affix), so the table has a hard ceiling and it is stated here
// rather than discovered by a wrap on the 256th row.
static_assert(std::size_t(BonusId::Count) <= 256,
              "a bonus row ordinal must fit the byte the save format gives it");

inline constexpr const BonusDef& bonus_def(BonusId id) {
    return kBonusDefs[std::size_t(id)];
}

// Whether a row lands on the SHEET (standing) or on a POOL (instant). Derived
// from the target rather than stored beside it: two columns that must agree
// are one column and a bug waiting.
inline constexpr bool bonus_is_instant(const BonusDef& d) {
    return d.target == BonusTarget::Pool;
}

// The authoring key resolved to its ordinal. Content names a bonus by string
// (a table, a plot file); the runtime never does. Returns None for an unknown
// key — REFUSAL, not a silent zero-th row, because those are the same value
// and only one of them is a mistake.
inline BonusId bonus_id(const char* key) {
    if (!key) return BonusId::None;
    for (const BonusDef& d : kBonusDefs) {
        const char* a = d.key;
        const char* b = key;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return d.id;
    }
    return BonusId::None;
}

// ── Standing: accumulate, then read through a modified COPY ───────────────

// Everything standing, summed by address. Flat arrays over the sheet's own
// envelopes, so a body's whole modifier set is one small POD on the stack and
// summing it allocates nothing.
struct BonusTotals {
    std::array<std::int16_t, kMaxAttributes> attr{};
    std::array<std::int16_t, kMaxSkills>     skill{};
    // "Did what stands on him CHANGE?" is a question the per-step bar
    // refresh asks (app loop) — equality is the whole answer.
    bool operator==(const BonusTotals&) const = default;
};

inline void accumulate(BonusTotals& t, Bonus b) {
    if (b.row == 0 || b.row >= std::uint8_t(BonusId::Count)) return;
    const BonusDef& d = bonus_def(BonusId(b.row));
    switch (d.target) {
        case BonusTarget::Attribute:
            if (d.index < kMaxAttributes) t.attr[d.index] += b.value;
            break;
        case BonusTarget::SkillRank:
            if (d.index < kMaxSkills) t.skill[d.index] += b.value;
            break;
        case BonusTarget::Pool:
            break;   // instant rows do not stand; apply_instant takes them
    }
}

inline void accumulate(BonusTotals& t, const Bonus* first, int count) {
    for (int i = 0; i < count; ++i) accumulate(t, first[i]);
}

// (`effective_sheet(base, totals)` — the standing half's one reader — lives in
// macro/character_sheet.h, beside the type it copies. This file stays a leaf
// above attributes.h so the item catalog can include it without dragging the
// creature registry in behind it.)

// ── Instant: act once on the pools ───────────────────────────────────────

// The three pools, named so this layer can speak about them without knowing
// which container a particular body keeps them in (the player's CombatStats,
// a body's ecs::Health). Values are ints because pools are ints everywhere.
struct PoolSlice {
    int* current[int(PoolId::Count)] = {nullptr, nullptr, nullptr};
    int  maximum[int(PoolId::Count)] = {0, 0, 0};
};

// Apply one instant row. Returns how much actually moved — which is NOT the
// value asked for when the pool was already full or nearly empty, and callers
// that report to the player ("+30 HP") want the truth rather than the wish.
inline int apply_instant(const PoolSlice& pools, Bonus b) {
    if (b.row == 0 || b.row >= std::uint8_t(BonusId::Count)) return 0;
    const BonusDef& d = bonus_def(BonusId(b.row));
    if (d.target != BonusTarget::Pool) return 0;
    if (d.index >= std::uint8_t(PoolId::Count)) return 0;
    int* cur = pools.current[d.index];
    if (!cur) return 0;
    // A wound is a blow, and blows have one door (see the rows above).
    if (d.index == std::uint8_t(PoolId::Hp) && b.value < 0) return 0;
    const int max = pools.maximum[d.index];
    const int before = *cur;
    *cur = std::clamp(before + int(b.value), 0, std::max(0, max));
    return *cur - before;
}

} // namespace sm
