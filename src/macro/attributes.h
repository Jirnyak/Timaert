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
#include "core/table_guard.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

// ── The id spaces ──────────────────────────────────────────────
// Declared FIRST because the blocks below are addressed BY them: a sheet's
// ranks are a flat array and its meanings are rows, and both index by these.
enum class AttributeId : std::uint8_t {
    Str, Vit, End, Wil, Intl, Wis, Lck, Cha, Spd,
    Count
};

enum class SkillId : std::uint8_t {
    Bodybuilding, Meditation, Athletics, Travel, Fighter,
    Marathon, Spellcraft, Weightlifting,
    Count
};

// ── Attributes ─────────────────────────────────────────────────

// The same shape the ranks have: a fixed envelope of scores, and a TABLE of
// what they mean. 16 slots for the 9 the game names today — work_vector §5
// asks for exactly this, and the reason is the save: a new attribute is a row
// under a fixed cap, so naming one does not move the format.
//
// A byte per score. The cap that makes a byte honest is stated, not assumed
// (kMaxAttributeScore below) and enforced at the one door into a score, the
// same way the rank cap is.
inline constexpr int kMaxAttributes = 16;

// Every slot starts at 1 — INCLUDING the reserved tail, so a score that gets
// named later begins where every other score began, rather than at a zero that
// would divide differently in the asymptotic formulas.
constexpr std::array<std::uint8_t, kMaxAttributes> attribute_bases() {
    std::array<std::uint8_t, kMaxAttributes> a{};
    for (std::uint8_t& v : a) v = 1;
    return a;
}

struct Attributes {
    std::array<std::uint8_t, kMaxAttributes> score = attribute_bases();

    std::uint8_t& operator[](AttributeId id) {
        return score[std::size_t(id)];
    }
    std::uint8_t operator[](AttributeId id) const {
        return score[std::size_t(id)];
    }
    int of(AttributeId id) const { return int(score[std::size_t(id)]); }
};

// What an attribute IS, in the sheet's own words. The character panel walks
// these rows instead of keeping a sixth copy of the same nine facts.
struct AttributeDef {
    // MUST equal the row's index in kAttributeDefs (guard below the table).
    AttributeId id;
    const char* key;      // authoring id; runtime addresses by ordinal
    const char* label;    // the three letters a player reads
    const char* effect;   // what one point buys
};

inline constexpr AttributeDef kAttributeDefs[] = {
    {AttributeId::Str,  "str",  "STR", "+1 physical damage per point"},
    {AttributeId::Vit,  "vit",  "VIT", "+10 max HP per point"},
    {AttributeId::End,  "end",  "END", "+10 max SP per point"},
    {AttributeId::Wil,  "wil",  "WIL", "+10 max MP per point"},
    {AttributeId::Intl, "intl", "INT", "+1 spell damage per point"},
    {AttributeId::Wis,  "wis",  "WIS", "+1% EXP bonus per point"},
    {AttributeId::Lck,  "lck",  "LCK", "Crit scaling and loot luck"},
    {AttributeId::Cha,  "cha",  "CHA", "Trade discount and relation bonus"},
    {AttributeId::Spd,  "spd",  "SPD", "Asymptotic movement speed"},
};
static_assert(sizeof(kAttributeDefs) / sizeof(kAttributeDefs[0])
                  == std::size_t(AttributeId::Count),
              "kAttributeDefs must carry one row per AttributeId");
static_assert(rows_in_enum_order(kAttributeDefs, &AttributeDef::id),
              "kAttributeDefs rows must stand in AttributeId order");
static_assert(int(AttributeId::Count) <= kMaxAttributes,
              "the attribute envelope must hold every score the game names");

inline constexpr const AttributeDef& attribute_def(AttributeId id) {
    return kAttributeDefs[std::size_t(id)];
}

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

// The generic 1 %/rank forms, kept for the two macro callers that hold a
// CACHED rank and not a sheet (ecs::MacroNpcRuntime travelRank/marathonRank —
// both 1 %/rank rows, so the answer is the same one skill_mult would give).
// New code names its skill and asks skill_mult; these do not know which skill
// they are speaking for, which is exactly why they are not THE law.
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

// ── Skills: a fixed envelope of ranks, and a TABLE of what they mean ──
//
// The ranks are a flat array under a po2 cap and the MEANINGS are rows beneath
// it — the same shape factions, biomes and creatures already have, and the
// shape work_vector §5 asks for by name. Adding a skill used to touch five
// places (a named field here, the SkillId enum, two `skill_value` switches,
// the UI row table in ui/overlays.cpp and the per-role weight table in
// character_sheet.h); it is one row and one weight per role now, and the save
// format does not move at all because the envelope is fixed.
//
// 32 slots for 8 skills: the envelope is the thing the save promises, so it is
// sized once, generously, in a power of two. A byte per rank because the rank
// cap is 100 and rank READS as a percent (kMaxSkillRank below).
inline constexpr int kMaxSkills = 32;

struct Skills {
    std::array<std::uint8_t, kMaxSkills> rank{};

    std::uint8_t& operator[](SkillId id) {
        return rank[std::size_t(id)];
    }
    std::uint8_t operator[](SkillId id) const {
        return rank[std::size_t(id)];
    }
    int of(SkillId id) const { return int(rank[std::size_t(id)]); }
};

// What ONE RANK of a skill is worth, and which way it pushes.
//
// `pctPerRank` is the column that made the law honest. rpg.md and the canon
// audit (A7) both record the debt it settles: the law said "one rank is one
// percent, ceiling ×2", and four of the most expensive numbers in the game —
// maxHp, maxMp, and both raw damages — were computed inline at 0.05 per rank
// with no clamp, so bodybuilding 100 gave ×6 HP while the doc promised ×2.
// The owner's ruling (2026-08-27) was to LEGITIMISE the per-skill multiplier
// as a column rather than flatten every skill to 1 %. So the ceiling is now a
// DERIVED number and differs per row — bodybuilding tops out at ×6 because its
// row says 5 — and there is exactly one place that turns a rank into a
// multiplier, which is what the law was always about.
struct SkillDef {
    // MUST equal the row's index in kSkillDefs (guard below the table).
    SkillId      id;
    const char*  key;          // authoring id; runtime addresses by ordinal
    const char*  label;        // what a human reads on the sheet
    const char*  effect;       // what it does, in the sheet's own words
    std::uint8_t pctPerRank;
    // A COST skill buys a price DOWN (1 - rank·pct/100, never past free); every
    // other skill multiplies a bonus UP (1 + rank·pct/100). One flag rather
    // than two helpers, because "which direction" is a property of the skill
    // and belongs in its row.
    bool         buysCostDown = false;
};

inline constexpr SkillDef kSkillDefs[] = {
    {SkillId::Bodybuilding,  "bodybuilding",  "Bodybuilding",
     "max HP per rank",                  5},
    {SkillId::Meditation,    "meditation",    "Meditation",
     "max MP per rank",                  5},
    {SkillId::Athletics,     "athletics",     "Athletics",
     "move speed per rank",              1},
    // How FAR you get on one bar, never how fast (movement_cost.h): the only
    // cost skill, and the reason the flag exists.
    {SkillId::Travel,        "travel",        "Travel",
     "terrain stamina cost per rank",    1, /*buysCostDown*/true},
    {SkillId::Fighter,       "fighter",       "Fighter",
     "physical damage per rank",         5},
    // Owner ruling, Session 21: the BAR is the END attribute's business alone,
    // so this skill shortens the REST instead. (Was `endurance`, +5 % max SP —
    // a multiplier that double-counted the attribute.)
    {SkillId::Marathon,      "marathon",      "Marathon",
     "SP recovery rate per rank",        1},
    {SkillId::Spellcraft,    "spellcraft",    "Spellcraft",
     "spell damage per rank",            5},
    {SkillId::Weightlifting, "weightlifting", "Weightlifting",
     "carry capacity per rank",         10},
};
static_assert(sizeof(kSkillDefs) / sizeof(kSkillDefs[0])
                  == std::size_t(SkillId::Count),
              "kSkillDefs must carry one row per SkillId");
// The table CARRIES its enum as a column, so a drifted row refuses to compile
// — the same guard biomes, moons and creature roles already stand behind.
static_assert(rows_in_enum_order(kSkillDefs, &SkillDef::id),
              "kSkillDefs rows must stand in SkillId order");
static_assert(int(SkillId::Count) <= kMaxSkills,
              "the skill envelope must hold every skill the game names");

inline constexpr const SkillDef& skill_def(SkillId id) {
    return kSkillDefs[std::size_t(id)];
}

// THE skill law, and the ONE place a rank becomes a multiplier. Everything
// that a skill governs asks this and nothing else — no formula keeps a private
// curve, and no formula spells a percent inline. The direction and the percent
// are the row's; the CAP is the law's.
inline float skill_mult_of(SkillId id, int rank) {
    if (rank < 0) rank = 0;
    if (rank > kMaxSkillRank) rank = kMaxSkillRank;
    const SkillDef& d = skill_def(id);
    const float step = float(rank) * float(d.pctPerRank) * 0.01f;
    if (!d.buysCostDown) return 1.0f + step;
    return step >= 1.0f ? 0.0f : 1.0f - step;   // a cost never goes past free
}

inline float skill_mult(const Skills& s, SkillId id) {
    return skill_mult_of(id, s.of(id));
}

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
    // The first perk whose effect is a DATA ROW (character_sheet.h
    // kSquadPerkBonuses); this entry only makes it takeable and tells the
    // player the truth about what it does.
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

// (No `attribute_value` switches either. A score is `attributes[AttributeId::X]`
// — an index into a flat array — so there is nothing to switch on, and naming
// a new attribute cannot forget a case.)

// (No `skill_value` switches. A rank is `skills[SkillId::X]` — an index into
// a flat array — so there is nothing left to switch on, and adding a skill
// cannot forget to update a case.)

// A score is a BYTE, so the ceiling is the byte's — stated here rather than
// discovered by a wrap. It is enormous by design (a hundred levels of nothing
// but one stat lands nowhere near it); what matters is that the one door into
// a score refuses rather than rolls over.
constexpr int kMaxAttributeScore = 255;

inline bool spend_attribute_point(LevelData& ld, Attributes& a, AttributeId id) {
    if (id >= AttributeId::Count || ld.attributePoints <= 0) return false;
    std::uint8_t& score = a[id];
    if (int(score) >= kMaxAttributeScore) return false;
    ++score;
    --ld.attributePoints;
    return true;
}

// The cap is enforced HERE, at the only door into a skill rank, so no caller
// can push one past mastery and no formula has to defend itself against a rank
// nobody could legitimately have. A refused spend keeps the point.
inline bool spend_skill_point(LevelData& ld, Skills& s, SkillId id) {
    if (id >= SkillId::Count || ld.skillPoints <= 0) return false;
    std::uint8_t& rank = s[id];
    if (int(rank) >= kMaxSkillRank) return false;
    ++rank;
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
    const float rawHp = float(baseHp + a.of(AttributeId::Vit) * 10);
    const float rawMp = float(baseMp + a.of(AttributeId::Wil) * 10);
    // The SP bar is the END attribute's alone — no skill multiplier. The old
    // `endurance` skill (+5% max SP) double-counted the attribute; its points
    // now live in `marathon`, which multiplies the RECOVERY RATE instead
    // (kSpRegenPctPerHour above), so bar and rest are two separate levers.
    const float rawSp = float(baseSp + a.of(AttributeId::End) * 10);
    CombatStats c;
    c.maxHp = int(rawHp * skill_mult(s, SkillId::Bodybuilding));
    c.maxMp = int(rawMp * skill_mult(s, SkillId::Meditation));
    c.maxSp = int(rawSp);
    c.currentHp = c.maxHp;
    c.currentMp = c.maxMp;
    c.currentSp = c.maxSp;
    c.hpRegen = 10.0f * (1.0f + float(a.of(AttributeId::Vit)) * 0.01f);
    c.mpRegen = 10.0f * (1.0f + float(a.of(AttributeId::Wil)) * 0.01f);
    // SP per game hour at rest, THE one regen law for the player and every
    // macro leader (npc_ai reads the same formula through the leader's sheet).
    c.spRegen = float(c.maxSp) * kSpRegenPctPerHour
                * skill_mult(s, SkillId::Marathon);
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
    const float rawPhys  = float(a.of(AttributeId::Str));
    const float rawSpell = float(a.of(AttributeId::Intl));
    d.rawPhysDamage  = rawPhys  * skill_mult(s, SkillId::Fighter);
    d.rawSpellDamage = rawSpell * skill_mult(s, SkillId::Spellcraft);
    d.expMult        = 1.0f + float(a.of(AttributeId::Wis)) * 0.01f;
    // Attributes add, skills multiply. `spd` is the body's own quickness
    // (asymptotic, so a monstrous score cannot run away with the game);
    // `athletics` is training on top of it. `travel` has no business here — it
    // buys DISTANCE per bar of stamina, not speed (macro/movement_cost.h).
    d.moveSpeedMult  = (1.0f + float(a.of(AttributeId::Spd)) / float(a.of(AttributeId::Spd) + 50))
                       * skill_mult(s, SkillId::Athletics);
    d.tradeDiscount  = float(a.of(AttributeId::Cha)) * 0.01f;
    d.relationBonus  = float(a.of(AttributeId::Cha));
    d.critBase       = float(a.of(AttributeId::Lck)) / float(a.of(AttributeId::Lck) + 50);
    return d;
}

// ── Carry weight ───────────────────────────────────────────────

constexpr float kBaseCarryKg = 100.0f;

inline float get_carry_capacity(const Attributes& a, const Skills& s) {
    return (kBaseCarryKg + float(a.of(AttributeId::Str)) * 10.0f)
           * skill_mult(s, SkillId::Weightlifting);
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
