// Faithful port of `src/game/attributes.ts`.
//
// Schema: 9 attributes (str/vit/end/wil/int/wis/lck/cha/spd), 7 skills,
// 24 perks (only level-relevant subset is enforced; cosmetic perks live
// in PERK_LIST), level/XP curve `1000 * level * (0.1 * level + 1)`,
// universal carry-weight rule.
//
// Naming: TS `int` is reserved in C+ +; field is renamed `intl` (kept short
// since this struct is hot data).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

// ── Attributes ─────────────────────────────────────────────────

struct Attributes {
    int str = 1; // +1 physical damage per point
    int vit = 1; // +10 max HP per point
    int end = 1; // +10 max SP per point
    int wil = 1; // +10 max MP per point
    int intl = 1; // +1 spell damage per point  (TS: int)
    int wis = 1; // +1% EXP bonus per point
    int lck = 1; // crit scaling, better loot
    int cha = 1; // trade discount, relation bonus
    int spd = 1; // movement speed (asymptotic)
};

// ── Skills ─────────────────────────────────────────────────────

struct Skills {
    int bodybuilding  = 0; // +5% max HP per rank
    int meditation    = 0; // +5% max MP per rank
    int travel        = 0; // +3% move speed, -2% terrain SP cost per rank
    int fighter       = 0; // +5% physical damage per rank
    int endurance     = 0; // +5% max SP per rank
    int spellcraft    = 0; // +5% spell damage per rank
    int weightlifting = 0; // +10% carry capacity per rank
};

// ── Perks ──────────────────────────────────────────────────────

enum class PerkID : std::uint8_t {
    Immortal, ShortLived, Mechanical, Talented, Gifted, GodsMark, Saint,
    Possess, DeathWord, Antimagus, MagicBody, BloodMagic, Autist, Leader,
    Specialization, Generalist, Educated, Natural, Apostle, Demiurg,
    Revenant, Stonks, Sacrilegist, KingPesant,
};

struct PerkInfo {
    PerkID id;
    const char* name;
    const char* description;
    const char* advantage;
    const char* disadvantage;
};

// Cosmetic registry — currently exposes the 7 perks the TS file enumerated
// (the rest of `PerkID` values exist for save-format parity but have no
// info row yet; mirror the TS PERK_LIST length exactly).
inline constexpr PerkInfo kPerkList[] = {
    {PerkID::Immortal,   "Immortal",   "Never die from old age",
                         "Immunity to aging",                "100% more EXP needed to level up"},
    {PerkID::ShortLived, "Short-Lived","Die of old age at 33",
                         "100% more EXP gained",             "Die at age 33"},
    {PerkID::Mechanical, "Mechanical", "Constructed being with no growth",
                         "Start with +100 attribute points & choose 10 skills",
                         "No level-up or EXP gain"},
    {PerkID::Talented,   "Talented",   "Natural prodigy",
                         "Instantly gain 1 level",           "Uses 1 perk point"},
    {PerkID::Gifted,     "Gifted",     "Extreme specialization",
                         "Choose two attributes; one is multiplied by 2",
                         "Other chosen attribute is divided by 2"},
    {PerkID::Natural,    "Natural",    "Pure physical excellence",
                         "+1 attribute point per level",     "No skill points gained"},
    {PerkID::Educated,   "Educated",   "Highly trained specialist",
                         "+1 skill point per level",         "No attribute points gained"},
};

// Bag of perk ids — small set, vector is fine (rarely > 5 entries).
struct Perks { std::vector<PerkID> ids; };

inline bool has_perk(const Perks& p, PerkID id) {
    for (auto i : p.ids) if (i == id) return true;
    return false;
}
inline void add_perk(Perks& p, PerkID id) {
    if (!has_perk(p, id)) p.ids.push_back(id);
}
inline void remove_perk(Perks& p, PerkID id) {
    for (auto it = p.ids.begin(); it != p.ids.end(); ++it)
        if (*it == id) { p.ids.erase(it); return; }
}

// ── Combat stats (player) ──────────────────────────────────────
//
// Mirrors `CombatStats` exactly; uses int for HP/SP/MP and float for
// regen rates. `current*` start equal to `max*`.

struct CombatStats {
    int   currentHp = 100, maxHp = 100;
    int   currentMp = 100, maxMp = 100;
    int   currentSp = 100, maxSp = 100;
    float hpRegen   = 10.0f;
    float mpRegen   = 10.0f;
    float spRegen   = 10.0f;
};

// ── Derived bonuses (ephemeral) ────────────────────────────────

struct DerivedBonuses {
    float rawPhysDamage = 0;
    float rawSpellDamage = 0;
    float expMult = 1;
    float moveSpeedMult = 1;
    float tradeDiscount = 0;
    float relationBonus = 0;
    float critBase = 0;
};

// ── Level data ─────────────────────────────────────────────────

struct LevelData {
    int level             = 1;
    int exp               = 0;
    int expToNext         = 0; // populated by `default_level_data`
    int attributePoints   = 8;
    int skillPoints       = 3;
    int perkPoints        = 1;
};

enum class AttributeId : std::uint8_t {
    Str, Vit, End, Wil, Intl, Wis, Lck, Cha, Spd,
};

enum class SkillId : std::uint8_t {
    Bodybuilding, Meditation, Travel, Fighter,
    Endurance, Spellcraft, Weightlifting,
};

inline int* attribute_value(Attributes& a, AttributeId id) {
    switch (id) {
        case AttributeId::Str:  return &a.str;
        case AttributeId::Vit:  return &a.vit;
        case AttributeId::End:  return &a.end;
        case AttributeId::Wil:  return &a.wil;
        case AttributeId::Intl: return &a.intl;
        case AttributeId::Wis:  return &a.wis;
        case AttributeId::Lck:  return &a.lck;
        case AttributeId::Cha:  return &a.cha;
        case AttributeId::Spd:  return &a.spd;
    }
    return nullptr;
}

inline const int* attribute_value(const Attributes& a, AttributeId id) {
    switch (id) {
        case AttributeId::Str:  return &a.str;
        case AttributeId::Vit:  return &a.vit;
        case AttributeId::End:  return &a.end;
        case AttributeId::Wil:  return &a.wil;
        case AttributeId::Intl: return &a.intl;
        case AttributeId::Wis:  return &a.wis;
        case AttributeId::Lck:  return &a.lck;
        case AttributeId::Cha:  return &a.cha;
        case AttributeId::Spd:  return &a.spd;
    }
    return nullptr;
}

inline int* skill_value(Skills& s, SkillId id) {
    switch (id) {
        case SkillId::Bodybuilding:  return &s.bodybuilding;
        case SkillId::Meditation:    return &s.meditation;
        case SkillId::Travel:        return &s.travel;
        case SkillId::Fighter:       return &s.fighter;
        case SkillId::Endurance:     return &s.endurance;
        case SkillId::Spellcraft:    return &s.spellcraft;
        case SkillId::Weightlifting: return &s.weightlifting;
    }
    return nullptr;
}

inline const int* skill_value(const Skills& s, SkillId id) {
    switch (id) {
        case SkillId::Bodybuilding:  return &s.bodybuilding;
        case SkillId::Meditation:    return &s.meditation;
        case SkillId::Travel:        return &s.travel;
        case SkillId::Fighter:       return &s.fighter;
        case SkillId::Endurance:     return &s.endurance;
        case SkillId::Spellcraft:    return &s.spellcraft;
        case SkillId::Weightlifting: return &s.weightlifting;
    }
    return nullptr;
}

inline bool spend_attribute_point(LevelData& ld, Attributes& a, AttributeId id) {
    int* value = attribute_value(a, id);
    if (!value || ld.attributePoints <= 0) return false;
    ++(*value);
    --ld.attributePoints;
    return true;
}

inline bool spend_skill_point(LevelData& ld, Skills& s, SkillId id) {
    int* value = skill_value(s, id);
    if (!value || ld.skillPoints <= 0) return false;
    ++(*value);
    --ld.skillPoints;
    return true;
}

// ── Formulas (verbatim from attributes.ts) ─────────────────────

// EXP_next(lvl) = floor(1000 * lvl * (0.1 * lvl + 1))
inline int exp_to_next_level(int level) {
    return int(1000.0 * level * (0.1 * level + 1.0));
}
// EXP_fight(lvl_m, k) = floor(10 * lvl_m * k)
inline int exp_from_fight(int enemyLevel, float modifier = 1.0f) {
    return int(10.0f * float(enemyLevel) * modifier);
}
// EXP_quest(lvl_q, k) = floor(100 * lvl_q * k)
inline int exp_from_quest(int questLevel, float modifier = 1.0f) {
    return int(100.0f * float(questLevel) * modifier);
}

inline Attributes default_attributes() { return {}; }
inline Skills     default_skills()     { return {}; }
inline Perks      default_perks()      { return {}; }

inline LevelData default_level_data() {
    LevelData ld{};
    ld.expToNext = exp_to_next_level(1);
    return ld;
}

// FinalStat = (base + attrRaw) × (1 + skillRank × skillMult)
inline CombatStats calculate_combat_stats(const Attributes& a, const Skills& s,
                                          int baseHp = 100,
                                          int baseMp = 100,
                                          int baseSp = 100) {
    const float rawHp = float(baseHp + a.vit * 10);
    const float rawMp = float(baseMp + a.wil * 10);
    const float rawSp = float(baseSp + a.end * 10);
    CombatStats c;
    c.maxHp = int(rawHp * (1.0f + float(s.bodybuilding) * 0.05f));
    c.maxMp = int(rawMp * (1.0f + float(s.meditation)   * 0.05f));
    c.maxSp = int(rawSp * (1.0f + float(s.endurance)    * 0.05f));
    c.currentHp = c.maxHp;
    c.currentMp = c.maxMp;
    c.currentSp = c.maxSp;
    c.hpRegen = 10.0f * (1.0f + float(a.vit) * 0.01f);
    c.mpRegen = 10.0f * (1.0f + float(a.wil) * 0.01f);
    c.spRegen = 10.0f * (1.0f + float(a.end) * 0.01f);
    return c;
}

inline DerivedBonuses calculate_derived(const Attributes& a, const Skills& s) {
    DerivedBonuses d;
    const float rawPhys  = float(a.str);
    const float rawSpell = float(a.intl);
    d.rawPhysDamage  = rawPhys  * (1.0f + float(s.fighter)    * 0.05f);
    d.rawSpellDamage = rawSpell * (1.0f + float(s.spellcraft) * 0.05f);
    d.expMult        = 1.0f + float(a.wis) * 0.01f;
    d.moveSpeedMult  = (1.0f + float(a.spd) / float(a.spd + 50))
                       * (1.0f + float(s.travel) * 0.03f);
    d.tradeDiscount  = float(a.cha) * 0.01f;
    d.relationBonus  = float(a.cha);
    d.critBase       = float(a.lck) / float(a.lck + 50);
    return d;
}

// ── Carry weight ───────────────────────────────────────────────

constexpr float kBaseCarryKg = 100.0f;

inline float get_carry_capacity(const Attributes& a, const Skills& s) {
    return (kBaseCarryKg + float(a.str) * 10.0f)
           * (1.0f + float(s.weightlifting) * 0.1f);
}
inline float get_overload_penalty(float weightKg, float capacityKg) {
    return weightKg > capacityKg ? (weightKg - capacityKg) : 0.0f;
}

// ── Level-up ───────────────────────────────────────────────────

inline bool try_level_up(LevelData& ld) {
    if (ld.exp < ld.expToNext) return false;
    ld.exp           -= ld.expToNext;
    ld.level         += 1;
    ld.expToNext      = exp_to_next_level(ld.level);
    ld.attributePoints += 3;
    ld.skillPoints     += 1;
    if (ld.level % 10 == 0) ld.perkPoints += 1;
    return true;
}

} // namespace sm
