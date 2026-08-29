// Standalone unit tests for the shipped RPG spine + unified loot table.
//
// These cover pure, deterministic logic that previously had NO direct test:
//   • character_sheet.h  — project_combat() and make_character_sheet()
//   • items.cpp          — roll_loot_profile(), npc_loot_id(), generate_loot_gold()
//
// Assertions go through tests/check.h (CHECK + sm::test::report).
// project_combat assertions re-derive their expected values from the SAME
// public formulas in attributes.h (calculate_combat_stats / calculate_derived)
// so the test pins the projection *contract* (melee→physical, missile→spell,
// HP floor, attack identity preserved) rather than hand-copied magic numbers.

#include "check.h"

#include "macro/attributes.h"
#include "macro/army.h"
#include "macro/character_sheet.h"
#include "macro/items.h"
#include "macro/npc.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace sm;

static bool approx(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

// ── Deterministic RNG stubs (RngFn is float(*)() — needs file-scope state) ──

static float rng_zero() { return 0.0f; }      // fires every chance>0, qty = min
static float rng_high() { return 0.9999f; }   // fires only chance==1.0, qty ~ max

static float       g_seq[64];
static std::size_t g_seqLen = 0;
static std::size_t g_seqPos = 0;
static float rng_seq() {
    const float v = (g_seqPos < g_seqLen) ? g_seq[g_seqPos] : 0.0f;
    ++g_seqPos;
    return v;
}
static void seq_set(std::initializer_list<float> vals) {
    g_seqLen = 0;
    for (float v : vals) {
        if (g_seqLen < 64) g_seq[g_seqLen++] = v;
    }
    g_seqPos = 0;
}

static int count_of(const std::vector<ItemRef>& v, const char* id) {
    const int idx = item_index(id);
    for (const ItemRef& s : v) {
        if (idx >= 0 && s.def == std::uint16_t(idx)) return s.count;
    }
    return -1; // absent
}

// ── project_combat: pins the documented projection contract ─────────────────

static void test_project_combat_melee() {
    // A melee body: known sheet, distinctive authored base with missile params
    // that MUST be preserved verbatim even though attackKind is Melee.
    CharacterSheet cs;
    cs.attributes[sm::AttributeId::Str] = 12;
    cs.attributes[sm::AttributeId::Intl] = 7;
    cs.attributes[sm::AttributeId::Vit] = 9;
    cs.skills[sm::SkillId::Fighter] = 4;
    cs.skills[sm::SkillId::Spellcraft] = 3;
    cs.skills[sm::SkillId::Bodybuilding] = 2;

    CombatTemplate base{};
    base.hp = 50; base.damage = 6; base.speed = 33; base.attackRange = 3.5f;
    base.cooldown = 1.25f; base.label = "TST"; base.attackKind = CombatTemplate::Melee;
    base.missileSpeed = 12.5f; base.missileBlast = 4.0f; base.missileColorRGBA = 0xAABBCCDDu;

    const CombatTemplate out = project_combat(cs, base);

    const CombatStats expCs =
        calculate_combat_stats(cs.attributes, cs.skills, int(base.hp));
    const DerivedBonuses expD = calculate_derived(cs.attributes, cs.skills);

    CHECK(approx(out.hp, float(expCs.maxHp)),
          "melee: hp == calculate_combat_stats(sheet, base.hp).maxHp");
    CHECK(approx(out.damage, base.damage + expD.rawPhysDamage),
          "melee: damage == base.damage + rawPhysDamage");
    // Attack identity preserved verbatim.
    CHECK(approx(out.speed, base.speed), "melee: speed preserved");
    CHECK(approx(out.attackRange, base.attackRange), "melee: range preserved");
    CHECK(approx(out.cooldown, base.cooldown), "melee: cooldown preserved");
    CHECK(out.attackKind == CombatTemplate::Melee, "melee: kind preserved");
    CHECK(out.label == base.label, "melee: label pointer preserved");
    CHECK(approx(out.missileSpeed, base.missileSpeed), "melee: missileSpeed preserved");
    CHECK(approx(out.missileBlast, base.missileBlast), "melee: missileBlast preserved");
    CHECK(out.missileColorRGBA == base.missileColorRGBA, "melee: missile color preserved");
}

static void test_project_combat_missile() {
    // Identical sheet, missile base: damage bonus must come from SPELL stats.
    CharacterSheet cs;
    cs.attributes[sm::AttributeId::Str] = 12;
    cs.attributes[sm::AttributeId::Intl] = 7;
    cs.skills[sm::SkillId::Fighter] = 4;
    cs.skills[sm::SkillId::Spellcraft] = 3;

    CombatTemplate base{};
    base.hp = 60; base.damage = 8; base.attackKind = CombatTemplate::Missile;

    const CombatTemplate out = project_combat(cs, base);
    const DerivedBonuses expD = calculate_derived(cs.attributes, cs.skills);

    CHECK(approx(out.damage, base.damage + expD.rawSpellDamage),
          "missile: damage == base.damage + rawSpellDamage");
    CHECK(out.attackKind == CombatTemplate::Missile, "missile: kind preserved");
    // Sanity: spell bonus (intl 7, spellcraft 3) differs from phys (str 12,
    // fighter 4), so the branch actually matters.
    CHECK(!approx(expD.rawSpellDamage, expD.rawPhysDamage),
          "missile: phys vs spell bonus differ (branch is meaningful)");
}

// ── make_character_sheet: the level point-budget identity ────────────────────

static int attr_sum(const Attributes& a) {
    // Walk the NAMED scores: an attribute the game names later joins this sum
    // by existing, and the reserved tail is not a score anybody spent.
    int sum = 0;
    for (int i = 0; i < int(AttributeId::Count); ++i) sum += a.of(AttributeId(i));
    return sum;
}
static int skill_sum(const Skills& s) {
    // Walk the envelope, not a hand-listed eight: a skill added to the
    // registry joins this sum by existing, which is the whole point of the
    // ranks being a flat array.
    int sum = 0;
    for (std::uint8_t r : s.rank) sum += int(r);
    return sum;
}

static void test_sheet_budget_identity() {
    // Every attribute starts at 1 (9 total baseline); every skill starts at 0.
    // A level-N sheet must spend EXACTLY the player economy: 8+3(N-1) attribute
    // points and 3+(N-1) skill points, leaving the pools at zero.
    const int levels[] = {1, 5, 20, 32};
    for (int lvl : levels) {
        for (NPCType role : {NPCType::Peasant, NPCType::Bandit, NPCType::Sorceress}) {
            const CharacterSheet cs = make_character_sheet(role, lvl, 0xC0FFEEu);
            const int expAttr = 8 + 3 * (lvl - 1);
            const int expSkill = 3 + (lvl - 1);

            char msg[128];
            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: attribute budget fully spent", lvl, int(role));
            CHECK(attr_sum(cs.attributes) == 9 + expAttr, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: skill budget fully spent", lvl, int(role));
            CHECK(skill_sum(cs.skills) == 0 + expSkill, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: pools drained to zero", lvl, int(role));
            CHECK(cs.levelData.attributePoints == 0
                  && cs.levelData.skillPoints == 0
                  && cs.levelData.perkPoints == 0, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: level + xp curve set", lvl, int(role));
            CHECK(cs.levelData.level == lvl
                  && cs.levelData.expToNext == exp_to_next_level(lvl), msg);
        }
    }
}

static void test_sheet_determinism() {
    // Same (role, level, seed) is bit-identical; a different seed keeps the
    // SAME budget totals (identity is seed-independent) while (almost surely)
    // redistributing the points.
    const CharacterSheet a = make_character_sheet(NPCType::Sorceress, 20, 42u);
    const CharacterSheet b = make_character_sheet(NPCType::Sorceress, 20, 42u);
    const CharacterSheet c = make_character_sheet(NPCType::Sorceress, 20, 43u);

    CHECK(attr_sum(a.attributes) == attr_sum(b.attributes)
          && skill_sum(a.skills) == skill_sum(b.skills),
          "determinism: same seed -> same totals");
    CHECK(a.attributes.of(sm::AttributeId::Str) == b.attributes.of(sm::AttributeId::Str)
          && a.attributes.of(sm::AttributeId::Intl) == b.attributes.of(sm::AttributeId::Intl)
          && a.attributes.of(sm::AttributeId::Wil) == b.attributes.of(sm::AttributeId::Wil)
          && a.skills.of(sm::SkillId::Spellcraft) == b.skills.of(sm::SkillId::Spellcraft),
          "determinism: same seed -> identical field allocation");
    CHECK(attr_sum(a.attributes) == attr_sum(c.attributes)
          && skill_sum(a.skills) == skill_sum(c.skills),
          "determinism: different seed -> same budget totals");
}

// ── Unified loot table ───────────────────────────────────────────────────────

static void test_npc_loot_id() {
    CHECK(std::string(npc_loot_id(0)) == "peasant", "npc_loot_id(0)=peasant");
    // Derived from the enum, never a pinned literal (Testing law #4): the
    // last row is whatever type the registry ends on, and one PAST the end
    // is out of range — both stay true however many professions are added.
    CHECK(std::string(npc_loot_id(int(sm::NPCType::ClayDigger))) == "clay_digger",
          "the last ROLE row names its loot profile");
    CHECK(std::string(npc_loot_id(int(sm::NPCType::Count))) == "",
          "one past the registry end is out of range");
    CHECK(std::string(npc_loot_id(-1)) == "", "npc_loot_id(-1)= (negative)");
    // Every ROLE row must resolve to a registered, rollable profile. A creature
    // row answers with "" on purpose — it names its drop in its own `lootId`
    // column and otherwise takes its faction's default (macro/npc.h), so the
    // per-role list stops where the roles stop. That boundary is asserted right
    // below rather than assumed.
    for (int t = 0; t < int(NPCType::Count); ++t) {
        const char* id = npc_loot_id(t);
        char msg[96];
        if (sm::is_creature_row(NPCType(t))) {
            std::snprintf(msg, sizeof msg,
                          "creature row %d defers its loot to its own column", t);
            CHECK(std::string(id).empty(), msg);
            continue;
        }
        std::snprintf(msg, sizeof msg, "npc_loot_id(%d) resolves to a profile", t);
        // rng_high on a real profile yields a vector (possibly empty); an
        // UNKNOWN id would also yield empty, so instead assert rng_zero (which
        // fires every entry) returns at least one stack for a valid role.
        CHECK(!roll_loot_profile(id, 1, rng_zero).empty(), msg);
    }
}

static void test_roll_loot_profile() {
    // rng_zero: 0.0 < chance for every entry, qty = min + int(0*range) = min.
    // Peasant table: bread(min1), wood(min1), herb(min1) — the PURSE is not
    // loot: it is the faction's coin, added by make_npc (macro/currency.h).
    auto peasant = roll_loot_profile("peasant", 1, rng_zero);
    CHECK(peasant.size() == 3, "peasant/rng_zero: all three entries drop");
    CHECK(count_of(peasant, "bread") == 1
          && count_of(peasant, "wood") == 1
          && count_of(peasant, "mat_herb") == 1,
          "peasant/rng_zero: quantities pinned at min");

    // minLevel gate: bandit's wpn_dagger requires level>=3.
    auto bandit1 = roll_loot_profile("bandit", 1, rng_zero);
    CHECK(count_of(bandit1, "wpn_dagger") == -1,
          "bandit L1: level-gated dagger excluded");
    CHECK(count_of(bandit1, "potion_hp") == 1 && count_of(bandit1, "misc_gem") == 1,
          "bandit L1: ungated entries still drop");
    auto bandit3 = roll_loot_profile("bandit", 3, rng_zero);
    CHECK(count_of(bandit3, "wpn_dagger") == 1,
          "bandit L3: level-gated dagger now included");

    // rng_high: only chance==1.0 entries fire; qty = min + int(0.9999*range).
    // Woodcutter: wood chance 1.0, min2 max7 -> 2 + int(0.9999*6) = 7.
    auto wood = roll_loot_profile("woodcutter", 1, rng_high);
    CHECK(wood.size() == 1 && count_of(wood, "wood") == 7,
          "woodcutter/rng_high: only the certain drop, at max qty");

    // World props pay through the same registry: the crop profile (Field Inc
    // F2) rolls grain — the harvest door then scales by metric height.
    auto crop = roll_loot_profile("crop", 1, rng_zero);
    CHECK(crop.size() == 1 && count_of(crop, "grain") == 1,
          "crop/rng_zero: grain at min qty");
    auto cropHigh = roll_loot_profile("crop", 1, rng_high);
    CHECK(count_of(cropHigh, "grain") == 2, "crop/rng_high: grain at max qty");

    // Unknown / empty id -> no items.
    CHECK(roll_loot_profile("does_not_exist", 5, rng_zero).empty(),
          "unknown lootId -> empty");
    CHECK(roll_loot_profile("", 5, rng_zero).empty(), "empty lootId -> empty");
    CHECK(roll_loot_profile(nullptr, 5, rng_zero).empty(), "null lootId -> empty");

    // "bandits" faction id reuses the bandit table (the old zero-loot fix).
    auto bandits = roll_loot_profile("bandits", 5, rng_zero);
    CHECK(!bandits.empty(), "bandits faction id resolves (not zero-loot)");

    // Exact per-entry qty via a scripted sequence: chance-roll then qty-roll.
    // Peasant bread: chance 0.6, min1 max3. seq {0.1 (fire), 0.5 (qty)} ->
    // 1 + int(0.5*3) = 1 + 1 = 2. Then wood: {0.9 (skip)}, herb: {0.0,0.0->1}.
    seq_set({0.1f, 0.5f, 0.9f, 0.0f, 0.0f});
    auto scripted = roll_loot_profile("peasant", 1, rng_seq);
    CHECK(count_of(scripted, "bread") == 2, "scripted: bread qty = min + int(0.5*range)");
    CHECK(count_of(scripted, "wood") == -1, "scripted: wood skipped (roll>=chance)");
    CHECK(count_of(scripted, "mat_herb") == 1, "scripted: herb fires at min");
}

static void test_generate_loot_gold() {
    // THE purse law (damage-door Inc 5): the ROW says what this creature is
    // worth to rob, the PLACE modulates it. Every expectation below is
    // derived from the row, never a copied literal — retuning kNpcPurse
    // touches no test.
    const auto bandit = [](RngFn r, float wealth, std::uint8_t danger = 0) {
        return generate_loot_gold(int(NPCType::Bandit), 1,
                                  CorpseLootContext{danger, wealth}, r);
    };
    const NpcPurseRow& row = npc_purse(NPCType::Bandit);
    CHECK(bandit(rng_zero, 1.0f) == row.min,
          "a level-1 body on open land carries its row's floor");
    CHECK(bandit(rng_high, 1.0f) == row.max - 1,
          "the roll spans the row's own range");
    // The place is the modulation — a city (1.5×) pays more for the same
    // creature, a ruin (0.5×) less. The direction is the law; the exact
    // numbers are the landmark rows'.
    CHECK(bandit(rng_zero, 1.5f) > bandit(rng_zero, 1.0f),
          "a rich place multiplies the same row's purse");
    CHECK(bandit(rng_zero, 0.5f) < bandit(rng_zero, 1.0f),
          "a poor place divides it");
    // Level grows the purse: a veteran has robbed more than a fresh recruit.
    CHECK(generate_loot_gold(int(NPCType::Bandit), 10,
                             CorpseLootContext{}, rng_zero)
              > bandit(rng_zero, 1.0f),
          "a higher-level body of the same row carries more");
    // The danger continuum is the third contribution (owner's design): the
    // deepest ground doubles the purse, safe ground says nothing at all.
    CHECK(bandit(rng_zero, 1.0f, 255) > bandit(rng_zero, 1.0f, 0),
          "dangerous country pays better for the same body");
    CHECK(bandit(rng_zero, 1.0f, 0) == bandit(rng_zero, 1.0f),
          "danger 0 is a SILENT contribution, not a discount");
    // A beast has no pockets — whatever banner it fights under and however
    // rich the ground it dies on. This is the check that the faction-keyed
    // multiplier could not make: under it, a ruin's wolf was six times richer
    // than a meadow's.
    CHECK(generate_loot_gold(int(NPCType::Wolf), 10,
                             CorpseLootContext{255, 1.5f}, rng_high) == 0,
          "a beast carries no coin, in a ruin or in a capital");
    CHECK(generate_loot_gold(int(NPCType::Goblin), 5,
                             CorpseLootContext{}, rng_high) > 0,
          "the goblin, who robs what he kills, does carry coin");
    // Fail-closed on a kind the table does not know.
    CHECK(generate_loot_gold(-1, 10, CorpseLootContext{}, rng_high) == 0,
          "an unknown kind carries nothing rather than a plausible number");
    CHECK(generate_loot_gold(int(NPCType::Peasant), 0,
                             CorpseLootContext{}, rng_zero) >= 0,
          "gold never negative");
}

int main() {
    test_project_combat_melee();
    test_project_combat_missile();
    test_sheet_budget_identity();
    test_sheet_determinism();
    test_npc_loot_id();
    test_roll_loot_profile();
    test_generate_loot_gold();
    return sm::test::report("rpg_loot_test");
}
