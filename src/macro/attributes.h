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

// ── THE SKILL LAW ──────────────────────────────────────────────
//
// Attributes are what a body IS; skills are what it has been TRAINED to do.
// So attributes add and skills multiply — mastery framing raw nature — and the
// multiplier is stated the same way for every skill in the game:
//
//     ONE RANK = ONE PERCENT, and a rank is capped at kMaxSkillRank (100).
//
// A rank therefore reads directly as the percentage it grants: "Travel 37" is
// -37% terrain stamina, "Athletics 37" is +37% speed, no formula in the reader's
// head and none in the balancer's. Linear and capped on purpose: an asymptotic
// curve (which two of these used to be) cannot be balanced by reading it, and a
// cap makes the ceiling a design decision instead of an accident.
//
// 100 rather than a power of two: nothing indexes an array by rank, so a
// po2 bound buys nothing here, while "rank == percent" buys legibility every
// time anyone reads a sheet. (It still fits a byte if ranks are ever packed.)
//
// The cap is reachable, and meant to be: the player earns ONE skill point per
// level across eight skills, so rank 100 is a hundred levels poured into a
// single mastery. What it grants at that point — up to doubling what the skill
// governs, or, for a cost skill, removing that cost entirely — is a capstone,
// not an exploit.
constexpr int kMaxSkillRank = 100;

// Multiplier a rank-based skill contributes: 1 + rank/100 for a bonus.
inline float skill_bonus_mult(int rank) {
    if (rank < 0) rank = 0;
    if (rank > kMaxSkillRank) rank = kMaxSkillRank;
    return 1.0f + float(rank) * 0.01f;
}

// ...and 1 - rank/100 for a skill that BUYS DOWN a cost (never below zero).
inline float skill_cost_mult(int rank) {
    if (rank < 0) rank = 0;
    if (rank > kMaxSkillRank) rank = kMaxSkillRank;
    return 1.0f - float(rank) * 0.01f;
}

struct Skills {
    int bodybuilding  = 0; // +5% max HP per rank
    int meditation    = 0; // +5% max MP per rank
    int athletics     = 0; // +1% move speed per rank — the multiplier on the
                           //   speed the `spd` attribute grants directly.
    int travel        = 0; // -1% terrain stamina cost per rank: how FAR you get
                           //   on one bar, never how fast (movement_cost.h).
    int fighter       = 0; // +5% physical damage per rank
    int marathon      = 0; // +1% SP recovery RATE per rank (owner ruling,
                           //   Session 21): the bar itself is the END
                           //   attribute's business alone — this skill is the
                           //   only thing in the game that shortens the rest.
                           //   Passive, like every skill; the name says a
                           //   marathoner recovers between efforts, not that
                           //   he sprints. (Was `endurance`, +5% max SP — that
                           //   multiplier double-counted the attribute.)
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
    // The first aura perk (macro/aura.h kPerkAuras): its effect is a DATA row
    // there, this entry only makes it takeable and tells the player the truth.
    {PerkID::Leader,     "Leader",     "Born to command",
                         "+1 vitality to every soldier in your squad",
                         "Uses 1 perk point"},
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
    Bodybuilding, Meditation, Athletics, Travel, Fighter,
    Marathon, Spellcraft, Weightlifting,
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
        case SkillId::Athletics:     return &s.athletics;
        case SkillId::Travel:        return &s.travel;
        case SkillId::Fighter:       return &s.fighter;
        case SkillId::Marathon:      return &s.marathon;
        case SkillId::Spellcraft:    return &s.spellcraft;
        case SkillId::Weightlifting: return &s.weightlifting;
    }
    return nullptr;
}

inline const int* skill_value(const Skills& s, SkillId id) {
    switch (id) {
        case SkillId::Bodybuilding:  return &s.bodybuilding;
        case SkillId::Meditation:    return &s.meditation;
        case SkillId::Athletics:     return &s.athletics;
        case SkillId::Travel:        return &s.travel;
        case SkillId::Fighter:       return &s.fighter;
        case SkillId::Marathon:      return &s.marathon;
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

// The cap is enforced HERE, at the only door into a skill rank, so no caller
// can push one past mastery and no formula has to defend itself against a rank
// nobody could legitimately have. A refused spend keeps the point.
inline bool spend_skill_point(LevelData& ld, Skills& s, SkillId id) {
    int* value = skill_value(s, id);
    if (!value || ld.skillPoints <= 0) return false;
    if (*value >= kMaxSkillRank) return false;
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
inline Attributes default_attributes() { return {}; }
inline Skills     default_skills()     { return {}; }
inline Perks      default_perks()      { return {}; }

inline LevelData default_level_data() {
    LevelData ld{};
    ld.expToNext = exp_to_next_level(1);
    return ld;
}

// Fraction of maxSp recovered per GAME HOUR at rest (Session 21, owner
// ruling): SP regeneration is a PERCENT of the bar, not a flat number, so a
// full rest takes the same 8 hours for every body in the world — the veteran's
// bigger bar refills proportionally faster in absolute SP, and nobody "rests
// longer because he is tougher" (the perversity a flat rate had). The ONLY
// thing that shortens the rest is the `marathon` skill, multiplying this rate
// by THE skill law (+1%/rank; capstone rank 100 halves the rest to 4 h).
// Attributes deliberately do not touch the rate: END's whole business is the
// bar (maxSp below). 1/8 (po2): full bar in 8 game hours — a night refills
// any traveller with room to spare.
constexpr float kSpRegenPctPerHour = 0.125f;

// FinalStat = (base + attrRaw) × (1 + skillRank × skillMult)
inline CombatStats calculate_combat_stats(const Attributes& a, const Skills& s,
                                          int baseHp = 100,
                                          int baseMp = 100,
                                          int baseSp = 100) {
    const float rawHp = float(baseHp + a.vit * 10);
    const float rawMp = float(baseMp + a.wil * 10);
    // The SP bar is the END attribute's alone — no skill multiplier. The old
    // `endurance` skill (+5% max SP) double-counted the attribute; its points
    // now live in `marathon`, which multiplies the RECOVERY RATE instead
    // (kSpRegenPctPerHour above), so bar and rest are two separate levers.
    const float rawSp = float(baseSp + a.end * 10);
    CombatStats c;
    c.maxHp = int(rawHp * (1.0f + float(s.bodybuilding) * 0.05f));
    c.maxMp = int(rawMp * (1.0f + float(s.meditation)   * 0.05f));
    c.maxSp = int(rawSp);
    c.currentHp = c.maxHp;
    c.currentMp = c.maxMp;
    c.currentSp = c.maxSp;
    c.hpRegen = 10.0f * (1.0f + float(a.vit) * 0.01f);
    c.mpRegen = 10.0f * (1.0f + float(a.wil) * 0.01f);
    // SP per game hour at rest, THE one regen law for the player and every
    // macro leader (npc_ai reads the same formula through the leader's sheet).
    c.spRegen = float(c.maxSp) * kSpRegenPctPerHour
                * skill_bonus_mult(s.marathon);
    return c;
}

// Recompute the MAXIMA from attributes/skills while PRESERVING the current
// pools (clamped into the new maxima). Spending a point must never be a free
// full heal (owner ruling 2026-08-05): the full restore in
// calculate_combat_stats belongs to the moments that SAY they heal —
// character creation and the LEVEL-UP itself (M&M tradition), plus explicit
// healing (inn, potions).
inline void recompute_combat_maxima(CombatStats& c, const Attributes& a,
                                    const Skills& s,
                                    int baseHp = 100, int baseMp = 100,
                                    int baseSp = 100) {
    const int curHp = c.currentHp;
    const int curMp = c.currentMp;
    const int curSp = c.currentSp;
    c = calculate_combat_stats(a, s, baseHp, baseMp, baseSp);
    c.currentHp = std::min(curHp, c.maxHp);
    c.currentMp = std::min(curMp, c.maxMp);
    c.currentSp = std::min(curSp, c.maxSp);
}

inline DerivedBonuses calculate_derived(const Attributes& a, const Skills& s) {
    DerivedBonuses d;
    const float rawPhys  = float(a.str);
    const float rawSpell = float(a.intl);
    d.rawPhysDamage  = rawPhys  * (1.0f + float(s.fighter)    * 0.05f);
    d.rawSpellDamage = rawSpell * (1.0f + float(s.spellcraft) * 0.05f);
    d.expMult        = 1.0f + float(a.wis) * 0.01f;
    // Attributes add, skills multiply. `spd` is the body's own quickness
    // (asymptotic, so a monstrous score cannot run away with the game);
    // `athletics` is training on top of it. `travel` has no business here — it
    // buys DISTANCE per bar of stamina, not speed (macro/movement_cost.h).
    d.moveSpeedMult  = (1.0f + float(a.spd) / float(a.spd + 50))
                       * skill_bonus_mult(s.athletics);
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

// THE way experience is granted, and the only one. Awarding XP and consuming it
// into levels is a SINGLE act: every source that split them — a quest reward, a
// scripted grant_xp effect — silently handed the player experience he could
// never spend, because the one place that drained the pool was the subworld kill
// path. A hero could finish ten contracts, sit on four levels' worth of exp, and
// stay level 1 until he stabbed a wolf.
//
// A single grant can cross several thresholds (a chapter reward at low level),
// hence the loop. Returns how many levels it produced, which is what a caller
// needs to say so, and 0 for a grant that only fills the bar.
inline int award_exp(LevelData& ld, int amount) {
    if (amount > 0) ld.exp += amount;
    int gained = 0;
    while (try_level_up(ld)) ++gained;
    return gained;
}

// The wis dividend (owner ruling 2026-08-05: the first formerly-dead
// attribute wired live): every award multiplies by the recipient's expMult
// (calculate_derived — +1% per wis point). Round half up, so small grants
// still feel the attribute.
inline int award_exp(LevelData& ld, int amount, float expMult) {
    const int scaled = amount > 0
        ? int(float(amount) * expMult + 0.5f)
        : amount;
    return award_exp(ld, scaled);
}

} // namespace sm
