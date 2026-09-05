// The character sheet's numeric core (CANON S14, finalized 2026-09-03).
//
// Schema: 8 attributes (str/end/int/wil/spd/lck/cha/wis — VIT and PER died
// in the canon session; their work is re-dealt: END owns HP AND half of SP,
// WILL owns MP and the other half), 8 skills, level/XP curve
// `1000 * level * (0.1 * level + 1)`, universal carry-weight rule.
// (Perks purged 2026-09-03 pending redesign — see the block below.)
//
// Naming: `int` is reserved in C++; the attribute is named `intl` (kept short
// since this struct is hot data). `wil` reads WILL on the sheet.
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
    Str, End, Intl, Wil, Spd, Lck, Cha, Wis,
    Count
};

// The canon skill list (S14, 2026-09-03), in registry groups. ORDINALS ARE
// FOREVER from kSaveVersion 78 on — append at the END, never insert.
// Leadership is deliberately absent: its work is undecided (NOT a squad cap —
// CANON S14), and a row without a law would be a liar; it appends later
// without moving the save. The old `Fighter` became `Armsmaster` (same idea,
// the canon name): the generic multiplier ON TOP of the weapon skills.
enum class SkillId : std::uint8_t {
    // weapons (7) — rank multiplies damage DONE WITH that weapon type
    Sword, Axe, Spear, Mace, Dagger, Bow, Staff,
    // armor (4) — rank multiplies protection OF that armor type
    HeavyArmor, LightArmor, Unarmored, Shield,
    // magic schools (6, S15) — rank multiplies power of the school's spells
    FireMagic, WaterMagic, AirMagic, EarthMagic, ArcaneMagic, VoidMagic,
    // the generic pair — a SMALLER percent on top of the typed skills
    Armsmaster, Spellcraft,
    // body (5)
    Bodybuilding, Meditation, Marathon, Athletics, Weightlifting,
    // road & world (4)
    Travel, Acrobatics, Scouting, Prospecting,
    // husbandry (4)
    Trade, Quartermaster, Foraging, Learning,
    // the EIGHTH weapon (owner verdict 2026-09-05, appended v79 — ordinals
    // are forever): the bare fist is a weapon type like any other, so an
    // unarmed monk is a build and not a gap in the law.
    Unarmed,
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
// these rows instead of keeping a sixth copy of the same eight facts.
struct AttributeDef {
    // MUST equal the row's index in kAttributeDefs (guard below the table).
    AttributeId id;
    const char* key;      // authoring id; runtime addresses by ordinal
    const char* label;    // the short name a player reads
    const char* effect;   // what one point buys
};

// The canon eight (S14, 2026-09-03), in the canon's own order. END and WILL
// each feed half of SP — the one bar with two owners, deliberately: the
// warrior and the mage come to stamina from opposite sides, the hybrid wins.
// LCK's reader is the dice door (S13; lands with the dice phase).
inline constexpr AttributeDef kAttributeDefs[] = {
    {AttributeId::Str,  "str",  "STR", "+1 physical damage, +10 kg carry per point"},
    {AttributeId::End,  "end",  "END", "+10 max HP, +5 max SP per point"},
    {AttributeId::Intl, "intl", "INT", "+1 spell damage per point"},
    {AttributeId::Wil,  "wil",  "WILL", "+10 max MP, +5 max SP per point"},
    {AttributeId::Spd,  "spd",  "SPD", "Asymptotic movement speed"},
    {AttributeId::Lck,  "lck",  "LCK", "Shifts the game's dice in your favor"},
    {AttributeId::Cha,  "cha",  "CHA", "1% off prices and payroll per point"},
    {AttributeId::Wis,  "wis",  "WIS", "+1% EXP bonus per point"},
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
// 64 slots for the canon ~35 (CANON S14 says the envelope by name): the
// envelope is the thing the save promises, so it is sized once, generously,
// in a power of two. A byte per rank because the rank cap is 100 and rank
// READS as a percent (kMaxSkillRank below).
inline constexpr int kMaxSkills = 64;

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

// Percent verdicts (owner, 2026-09-03 evening): TYPED skills (weapons, armor,
// schools) = 10 %/rank — capstone ×11 on your own type; the GENERIC pair =
// 5 %/rank ON TOP — Armsmaster multiplies the FINAL physical damage whatever
// the weapon, Spellcraft the final spell power whatever the school (the M&M
// shape; "меньший процент ПОВЕРХ типовых", CANON S14). World-skill rows whose
// reader is a later phase (weapons → the damage door, schools → S15 wiring,
// Acrobatics/Scouting/Prospecting/Trade/… → the world readers) still state
// their law here: the row IS the design, the reader arrives once.
inline constexpr SkillDef kSkillDefs[] = {
    {SkillId::Sword,       "sword",       "Sword",
     "sword damage per rank",                 10},
    {SkillId::Axe,         "axe",         "Axe",
     "axe damage per rank",                   10},
    {SkillId::Spear,       "spear",       "Spear",
     "spear damage per rank",                 10},
    {SkillId::Mace,        "mace",        "Mace",
     "mace damage per rank",                  10},
    {SkillId::Dagger,      "dagger",      "Dagger",
     "dagger damage per rank",                10},
    {SkillId::Bow,         "bow",         "Bow",
     "bow damage per rank",                   10},
    {SkillId::Staff,       "staff",       "Staff",
     "staff damage per rank",                 10},
    {SkillId::HeavyArmor,  "heavy_armor", "Heavy Armor",
     "heavy armor protection per rank",       10},
    {SkillId::LightArmor,  "light_armor", "Light Armor",
     "light armor protection per rank",       10},
    {SkillId::Unarmored,   "unarmored",   "Unarmored",
     "protection while unarmored per rank",   10},
    {SkillId::Shield,      "shield",      "Shield",
     "shield block per rank",                 10},
    {SkillId::FireMagic,   "fire_magic",  "Fire Magic",
     "fire spell power per rank",             10},
    {SkillId::WaterMagic,  "water_magic", "Water Magic",
     "water spell power per rank",            10},
    {SkillId::AirMagic,    "air_magic",   "Air Magic",
     "air spell power per rank",              10},
    {SkillId::EarthMagic,  "earth_magic", "Earth Magic",
     "earth spell power per rank",            10},
    {SkillId::ArcaneMagic, "arcane_magic", "Arcane Magic",
     "arcane spell power per rank",           10},
    {SkillId::VoidMagic,   "void_magic",  "Void Magic",
     "void spell power per rank",             10},
    // The generic pair multiplies the FINAL number on top of the typed skill
    // (owner, 2026-09-03: «процент поверх итогового — усиляет весь урон»).
    {SkillId::Armsmaster,  "armsmaster",  "Armsmaster",
     "ALL physical damage per rank",           5},
    {SkillId::Spellcraft,  "spellcraft",  "Spellcraft",
     "ALL spell power per rank",               5},
    {SkillId::Bodybuilding, "bodybuilding", "Bodybuilding",
     "max HP per rank",                        5},
    {SkillId::Meditation,  "meditation",  "Meditation",
     "max MP per rank",                        5},
    // Owner ruling, Session 21: the BAR belongs to attributes alone (END and
    // WILL by half each since the canon eight), so this skill shortens the
    // REST instead. (Was `endurance`, +5 % max SP — a multiplier that
    // double-counted the attribute.)
    {SkillId::Marathon,    "marathon",    "Marathon",
     "SP recovery rate per rank",              1},
    {SkillId::Athletics,   "athletics",   "Athletics",
     "move speed per rank",                    1},
    {SkillId::Weightlifting, "weightlifting", "Weightlifting",
     "carry capacity per rank",               10},
    // How FAR you get on one bar, never how fast (movement_cost.h): a cost
    // skill, and the reason the flag exists.
    {SkillId::Travel,      "travel",      "Travel",
     "terrain stamina cost per rank",          1, /*buysCostDown*/true},
    {SkillId::Acrobatics,  "acrobatics",  "Acrobatics",
     "jump height per rank",                   1},
    {SkillId::Scouting,    "scouting",    "Scouting",
     "track-field reading per rank",           1},
    {SkillId::Prospecting, "prospecting", "Prospecting",
     "deposit sense per rank",                 1},
    {SkillId::Trade,       "trade",       "Trade",
     "final price edge per rank",              1},
    {SkillId::Quartermaster, "quartermaster", "Quartermaster",
     "squad payroll per rank",                 1, /*buysCostDown*/true},
    {SkillId::Foraging,    "foraging",    "Foraging",
     "provision drain per rank",               1, /*buysCostDown*/true},
    {SkillId::Learning,    "learning",    "Learning",
     "experience gained per rank",             1},
    // Appended v79 with its enum row — a weapon skill like the seven above.
    {SkillId::Unarmed,     "unarmed",     "Unarmed",
     "unarmed damage per rank",               10},
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

// The SAME law in whole percent, for the integer combat house (the strike
// assembly multiplies by multPct/100 — macro/damage_types.h roll_strike).
// Only the growing direction exists here: no combat law buys a cost down.
inline int skill_mult_pct(const Skills& s, SkillId id) {
    int rank = s.of(id);
    if (rank > kMaxSkillRank) rank = kMaxSkillRank;
    return 100 + rank * int(skill_def(id).pctPerRank);
}

// ── Perks: PURGED 2026-09-03 ───────────────────────────────────
//
// The TS-era perk block (24 ids, 8 tooltip rows, effects on TWO) died whole:
// six of the eight rows promised mechanics that did not exist, and the system
// is to be REDESIGNED from scratch (CANON S14 — Underrail-grade, one perk at a
// time, once the RPG core stands). Until then the game carries no perk state:
// no enum, no bag, no points, no save bytes (kSaveVersion 76). The one thing
// that survives is the aura DOOR (character_sheet.h squad_bonuses) — the
// mechanism perks will feed rows into when they return.

// ── Combat stats ───────────────────────────────────────────────
//
// int for the HP/SP/MP pools, float for the per-game-hour rest rates.
// `current*` start equal to `max*`; defaults are the 100-bar under the one
// recovery law (kRestRegenPctPerHour below): 100 × 1/8 per rest hour.

struct CombatStats {
    int   currentHp = 100, maxHp = 100;
    int   currentMp = 100, maxMp = 100;
    int   currentSp = 100, maxSp = 100;
    float hpRegen   = 12.5f;
    float mpRegen   = 12.5f;
    float spRegen   = 12.5f;
};

// ── Derived bonuses (ephemeral) ────────────────────────────────

// (critBase and relationBonus died in the 2026-09-03 sweep: zero readers
// beyond one UI print — LCK's real home is the dice-roll door, CANON S13.)
struct DerivedBonuses {
    float rawPhysDamage = 0;
    float rawSpellDamage = 0;
    float expMult = 1;
    float moveSpeedMult = 1;
    float tradeDiscount = 0;
};

// ── Level data ─────────────────────────────────────────────────

struct LevelData {
    int level             = 1;
    int exp               = 0;
    int expToNext         = 0; // populated by `default_level_data`
    // The creation budget (CANON S14, owner 2026-09-03): 5 attribute points
    // and 5 LEARN PICKS — a pick teaches a skill (rank 0 → 1), it is not a
    // rank point. Skill points arrive with levels and spend only into what
    // is already known.
    int attributePoints   = 5;
    int skillPoints       = 0;
    int learnPicks        = 5;
    // (perkPoints return with the redesigned perk system — every 10th level
    // + one starter-pool pick at creation, CANON S14. No state until then.)
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

// ── THE LEARN LAW (CANON S14, owner 2026-09-03) ────────────────
//
// Rank 0 IS "you do not know this skill" — no bit beside the rank, zero and
// ignorance are one fact. Points spend only into what is KNOWN; knowing comes
// from the WORLD (teachers, events) or from creation's learn picks. To learn
// is to reach rank 1.

// The one door from ignorance to rank 1. Refuses what is already known — a
// teacher cannot teach you twice, and a world-source that lands on a known
// skill should say so rather than silently burn.
inline bool learn_skill(Skills& s, SkillId id) {
    if (id >= SkillId::Count) return false;
    std::uint8_t& rank = s[id];
    if (rank != 0) return false;
    rank = 1;
    return true;
}

// A creation pick is a learn with a budget: 5 at character creation
// (default_level_data), consumed through this door so the pool cannot
// over-spend and a refused learn keeps the pick.
inline bool spend_learn_pick(LevelData& ld, Skills& s, SkillId id) {
    if (ld.learnPicks <= 0) return false;
    if (!learn_skill(s, id)) return false;
    --ld.learnPicks;
    return true;
}

// The cap is enforced HERE, at the only door into a skill rank, so no caller
// can push one past mastery and no formula has to defend itself against a rank
// nobody could legitimately have. A refused spend keeps the point. Rank 0
// refuses too — THE learn law above: you cannot train what you do not know.
inline bool spend_skill_point(LevelData& ld, Skills& s, SkillId id) {
    if (id >= SkillId::Count || ld.skillPoints <= 0) return false;
    std::uint8_t& rank = s[id];
    if (rank == 0) return false;               // unknown: learn first
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
// (exp_from_fight — the flat 10·lvl kill payout — died 2026-08-29: every
// kill pays through the ONE npc_xp_reward law in macro/npc.h now.)
inline Attributes default_attributes() { return {}; }
inline Skills     default_skills()     { return {}; }

inline LevelData default_level_data() {
    LevelData ld{};
    ld.expToNext = exp_to_next_level(1);
    return ld;
}

// THE ONE RECOVERY LAW (CANON S14; owner rulings Session 21 and 2026-09-03):
// every bar recovers as a PERCENT of itself per GAME HOUR of REST, and rest —
// standing still, doing nothing — is the ONLY thing that recovers a bar
// (the march heals nothing; kMarchRecoveryPct is zero on all three now).
// A percent, not a flat number, so a full rest takes the same 8 hours for
// every body in the world — the veteran's bigger bar refills proportionally
// faster in absolute points, and nobody "rests longer because he is tougher"
// (the perversity the flat 10·(1+vit·0.01) legacy had). Since only waiting
// refills a bar, SP is literally time in a bar: END/WILL do not shorten the
// night, they fatten the day. The ONLY thing that shortens a rest is the
// `marathon` skill on SP, multiplying this rate by THE skill law (+1%/rank;
// capstone rank 100 halves the rest to 4 h). One fraction for all three bars
// (owner, 2026-09-03): 1/8 (po2) — a full bar in 8 game hours, a night
// refills any traveller with room to spare.
constexpr float kRestRegenPctPerHour = 0.125f;

// FinalStat = (base + attrRaw) × (1 + skillRank × skillMult)
inline CombatStats calculate_combat_stats(const Attributes& a, const Skills& s,
                                          int baseHp = 100,
                                          int baseMp = 100,
                                          int baseSp = 100) {
    const float rawHp = float(baseHp + a.of(AttributeId::End) * 10);
    const float rawMp = float(baseMp + a.of(AttributeId::Wil) * 10);
    CombatStats c;
    c.maxHp = int(rawHp * skill_mult(s, SkillId::Bodybuilding));
    c.maxMp = int(rawMp * skill_mult(s, SkillId::Meditation));
    // The SP bar has TWO owners by half each (CANON S14): the warrior's END
    // and the mage's WILL both buy the day. Integer floor, house style; no
    // skill multiplies the bar — `marathon` multiplies the RECOVERY RATE
    // instead (kRestRegenPctPerHour above), so bar and rest are two levers.
    c.maxSp = baseSp + ((a.of(AttributeId::End) + a.of(AttributeId::Wil)) >> 1)
                           * 10;
    c.currentHp = c.maxHp;
    c.currentMp = c.maxMp;
    c.currentSp = c.maxSp;
    // Per game hour AT REST, all three through the one law above — for the
    // player and every macro leader (npc_ai reads the same formula through
    // the leader's sheet).
    c.hpRegen = float(c.maxHp) * kRestRegenPctPerHour;
    c.mpRegen = float(c.maxMp) * kRestRegenPctPerHour;
    c.spRegen = float(c.maxSp) * kRestRegenPctPerHour
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

// THE one CHA→trade-discount formula (1 % per point). Both of its doors read
// it — the trade price (economy.cpp trade_buy/sell_price) and the payroll
// (calculate_squad_upkeep via the derived column below). The 2026-09-03 sweep
// found it spelled inline in TWO places; a drifted copy here would desync the
// shop from the sheet panel silently.
inline float cha_trade_discount(int cha) {
    return float(cha) * 0.01f;
}

inline DerivedBonuses calculate_derived(const Attributes& a, const Skills& s) {
    DerivedBonuses d;
    const float rawPhys  = float(a.of(AttributeId::Str));
    const float rawSpell = float(a.of(AttributeId::Intl));
    // The GENERIC half of the damage stack (S14): Armsmaster multiplies all
    // physical, Spellcraft all spell power. The TYPED half — the weapon skill
    // of what the hand holds, the school of the spell being cast — reads at
    // the damage door (dice phase) and the school wiring (S15), on top.
    d.rawPhysDamage  = rawPhys  * skill_mult(s, SkillId::Armsmaster);
    d.rawSpellDamage = rawSpell * skill_mult(s, SkillId::Spellcraft);
    d.expMult        = 1.0f + float(a.of(AttributeId::Wis)) * 0.01f;
    // Attributes add, skills multiply. `spd` is the body's own quickness
    // (asymptotic, so a monstrous score cannot run away with the game);
    // `athletics` is training on top of it. `travel` has no business here — it
    // buys DISTANCE per bar of stamina, not speed (macro/movement_cost.h).
    d.moveSpeedMult  = (1.0f + float(a.of(AttributeId::Spd)) / float(a.of(AttributeId::Spd) + 50))
                       * skill_mult(s, SkillId::Athletics);
    d.tradeDiscount  = cha_trade_discount(a.of(AttributeId::Cha));
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

// THE level grant (CANON S14, owner verdict 2026-09-03 evening): +1 attribute
// point AND +1 skill point EVERY level — 1:1, «для чистоты». Specialization
// is held by perk GATES (designed against this income, up to "requires 100"),
// not by point scarcity. The perk point (every 10th level) returns with the
// perk system itself — no state to accrue into until then.
inline bool try_level_up(LevelData& ld) {
    if (ld.exp < ld.expToNext) return false;
    ld.exp           -= ld.expToNext;
    ld.level         += 1;
    ld.expToNext      = exp_to_next_level(ld.level);
    ld.attributePoints += 1;
    ld.skillPoints     += 1;
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
