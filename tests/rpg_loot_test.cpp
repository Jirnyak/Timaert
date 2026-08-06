// Standalone unit tests for the shipped RPG spine + unified loot table.
//
// These cover pure, deterministic logic that previously had NO direct test:
//   • character_sheet.h  — project_combat() and make_character_sheet()
//   • items.cpp          — roll_loot_profile(), npc_loot_id(), generate_loot_gold()
//
// Style mirrors tests/item_use_parity_test.cpp: no framework, a plain main()
// that returns 1 on the first failed expectation. project_combat assertions
// re-derive their expected values from the SAME public formulas in
// attributes.h (calculate_combat_stats / calculate_derived) so the test pins
// the projection *contract* (melee→physical, missile→spell, HP floor, attack
// identity preserved) rather than hand-copied magic numbers.

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

static int g_fails = 0;

static bool expect(bool ok, const char* msg) {
    if (!ok) {
        std::printf("FAIL: %s\n", msg);
        ++g_fails;
    }
    return ok;
}

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

static int count_of(const std::vector<ItemStack>& v, const char* id) {
    for (const auto& s : v) if (s.id == id) return s.count;
    return -1; // absent
}

// ── project_combat: pins the documented projection contract ─────────────────

static void test_project_combat_melee() {
    // A melee body: known sheet, distinctive authored base with missile params
    // that MUST be preserved verbatim even though attackKind is Melee.
    CharacterSheet cs;
    cs.attributes.str  = 12;
    cs.attributes.intl = 7;
    cs.attributes.vit  = 9;
    cs.skills.fighter    = 4;
    cs.skills.spellcraft = 3;
    cs.skills.bodybuilding = 2;

    CombatTemplate base{};
    base.hp = 50; base.damage = 6; base.speed = 33; base.attackRange = 3.5f;
    base.cooldown = 1.25f; base.label = "TST"; base.attackKind = CombatTemplate::Melee;
    base.missileSpeed = 12.5f; base.missileBlast = 4.0f; base.missileColorRGBA = 0xAABBCCDDu;

    const CombatTemplate out = project_combat(cs, base);

    const CombatStats expCs =
        calculate_combat_stats(cs.attributes, cs.skills, int(base.hp));
    const DerivedBonuses expD = calculate_derived(cs.attributes, cs.skills);

    expect(approx(out.hp, float(expCs.maxHp)),
           "melee: hp == calculate_combat_stats(sheet, base.hp).maxHp");
    expect(approx(out.damage, base.damage + expD.rawPhysDamage),
           "melee: damage == base.damage + rawPhysDamage");
    // Attack identity preserved verbatim.
    expect(approx(out.speed, base.speed), "melee: speed preserved");
    expect(approx(out.attackRange, base.attackRange), "melee: range preserved");
    expect(approx(out.cooldown, base.cooldown), "melee: cooldown preserved");
    expect(out.attackKind == CombatTemplate::Melee, "melee: kind preserved");
    expect(out.label == base.label, "melee: label pointer preserved");
    expect(approx(out.missileSpeed, base.missileSpeed), "melee: missileSpeed preserved");
    expect(approx(out.missileBlast, base.missileBlast), "melee: missileBlast preserved");
    expect(out.missileColorRGBA == base.missileColorRGBA, "melee: missile color preserved");
}

static void test_project_combat_missile() {
    // Identical sheet, missile base: damage bonus must come from SPELL stats.
    CharacterSheet cs;
    cs.attributes.str  = 12;
    cs.attributes.intl = 7;
    cs.skills.fighter    = 4;
    cs.skills.spellcraft = 3;

    CombatTemplate base{};
    base.hp = 60; base.damage = 8; base.attackKind = CombatTemplate::Missile;

    const CombatTemplate out = project_combat(cs, base);
    const DerivedBonuses expD = calculate_derived(cs.attributes, cs.skills);

    expect(approx(out.damage, base.damage + expD.rawSpellDamage),
           "missile: damage == base.damage + rawSpellDamage");
    expect(out.attackKind == CombatTemplate::Missile, "missile: kind preserved");
    // Sanity: spell bonus (intl 7, spellcraft 3) differs from phys (str 12,
    // fighter 4), so the branch actually matters.
    expect(!approx(expD.rawSpellDamage, expD.rawPhysDamage),
           "missile: phys vs spell bonus differ (branch is meaningful)");
}

// ── make_character_sheet: the level point-budget identity ────────────────────

static int attr_sum(const Attributes& a) {
    return a.str + a.vit + a.end + a.wil + a.intl + a.wis + a.lck + a.cha + a.spd;
}
static int skill_sum(const Skills& s) {
    return s.bodybuilding + s.meditation + s.athletics + s.travel + s.fighter
         + s.marathon + s.spellcraft + s.weightlifting;
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
            expect(attr_sum(cs.attributes) == 9 + expAttr, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: skill budget fully spent", lvl, int(role));
            expect(skill_sum(cs.skills) == 0 + expSkill, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: pools drained to zero", lvl, int(role));
            expect(cs.levelData.attributePoints == 0
                   && cs.levelData.skillPoints == 0
                   && cs.levelData.perkPoints == 0, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: level + xp curve set", lvl, int(role));
            expect(cs.levelData.level == lvl
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

    expect(attr_sum(a.attributes) == attr_sum(b.attributes)
           && skill_sum(a.skills) == skill_sum(b.skills),
           "determinism: same seed -> same totals");
    expect(a.attributes.str == b.attributes.str
           && a.attributes.intl == b.attributes.intl
           && a.attributes.wil == b.attributes.wil
           && a.skills.spellcraft == b.skills.spellcraft,
           "determinism: same seed -> identical field allocation");
    expect(attr_sum(a.attributes) == attr_sum(c.attributes)
           && skill_sum(a.skills) == skill_sum(c.skills),
           "determinism: different seed -> same budget totals");
}

// ── Unified loot table ───────────────────────────────────────────────────────

static void test_npc_loot_id() {
    expect(std::string(npc_loot_id(0)) == "peasant", "npc_loot_id(0)=peasant");
    expect(std::string(npc_loot_id(7)) == "sorceress", "npc_loot_id(7)=sorceress");
    expect(std::string(npc_loot_id(8)) == "", "npc_loot_id(8)= (out of range)");
    expect(std::string(npc_loot_id(-1)) == "", "npc_loot_id(-1)= (negative)");
    // Every humanoid NPCType must resolve to a registered, rollable profile.
    for (int t = 0; t < int(NPCType::Count); ++t) {
        const char* id = npc_loot_id(t);
        char msg[96];
        std::snprintf(msg, sizeof msg, "npc_loot_id(%d) resolves to a profile", t);
        // rng_high on a real profile yields a vector (possibly empty); an
        // UNKNOWN id would also yield empty, so instead assert rng_zero (which
        // fires every entry) returns at least one stack for a valid role.
        expect(!roll_loot_profile(id, 1, rng_zero).empty(), msg);
    }
}

static void test_roll_loot_profile() {
    // rng_zero: 0.0 < chance for every entry, qty = min + int(0*range) = min.
    // Peasant table: bread(min1), wood(min1), herb(min1) — all unrestricted.
    auto peasant = roll_loot_profile("peasant", 1, rng_zero);
    expect(peasant.size() == 3, "peasant/rng_zero: all three entries drop");
    expect(count_of(peasant, "bread") == 1
           && count_of(peasant, "wood") == 1
           && count_of(peasant, "mat_herb") == 1,
           "peasant/rng_zero: quantities pinned at min");

    // minLevel gate: bandit's wpn_dagger requires level>=3.
    auto bandit1 = roll_loot_profile("bandit", 1, rng_zero);
    expect(count_of(bandit1, "wpn_dagger") == -1,
           "bandit L1: level-gated dagger excluded");
    expect(count_of(bandit1, "potion_hp") == 1 && count_of(bandit1, "misc_gem") == 1,
           "bandit L1: ungated entries still drop");
    auto bandit3 = roll_loot_profile("bandit", 3, rng_zero);
    expect(count_of(bandit3, "wpn_dagger") == 1,
           "bandit L3: level-gated dagger now included");

    // rng_high: only chance==1.0 entries fire; qty = min + int(0.9999*range).
    // Woodcutter: wood chance 1.0, min2 max7 -> 2 + int(0.9999*6) = 7.
    auto wood = roll_loot_profile("woodcutter", 1, rng_high);
    expect(wood.size() == 1 && count_of(wood, "wood") == 7,
           "woodcutter/rng_high: only the certain drop, at max qty");

    // Unknown / empty id -> no items.
    expect(roll_loot_profile("does_not_exist", 5, rng_zero).empty(),
           "unknown lootId -> empty");
    expect(roll_loot_profile("", 5, rng_zero).empty(), "empty lootId -> empty");
    expect(roll_loot_profile(nullptr, 5, rng_zero).empty(), "null lootId -> empty");

    // "bandits" faction id reuses the bandit table (the old zero-loot fix).
    auto bandits = roll_loot_profile("bandits", 5, rng_zero);
    expect(!bandits.empty(), "bandits faction id resolves (not zero-loot)");

    // Exact per-entry qty via a scripted sequence: chance-roll then qty-roll.
    // Peasant bread: chance 0.6, min1 max3. seq {0.1 (fire), 0.5 (qty)} ->
    // 1 + int(0.5*3) = 1 + 1 = 2. Then wood: {0.9 (skip)}, herb: {0.0,0.0->1}.
    seq_set({0.1f, 0.5f, 0.9f, 0.0f, 0.0f});
    auto scripted = roll_loot_profile("peasant", 1, rng_seq);
    expect(count_of(scripted, "bread") == 2, "scripted: bread qty = min + int(0.5*range)");
    expect(count_of(scripted, "wood") == -1, "scripted: wood skipped (roll>=chance)");
    expect(count_of(scripted, "mat_herb") == 1, "scripted: herb fires at min");
}

static void test_generate_loot_gold() {
    // base = (3 + level*2) * factionMult; v = floor(base + rng()*base).
    // rng_zero -> v = floor(base).
    expect(generate_loot_gold(10, "empire", rng_zero) == 23,
           "gold L10 empire rng0 = floor(23)");
    expect(generate_loot_gold(10, "wildlife", rng_zero) == 2,
           "gold L10 wildlife(0.1) rng0 = floor(2.3)");
    expect(generate_loot_gold(10, "demons", rng_zero) == 13,
           "gold L10 demons(0.6) rng0 = floor(13.8)");
    expect(generate_loot_gold(10, "bandits", rng_zero) == 18,
           "gold L10 bandits(0.8) rng0 = floor(18.4)");
    // rng_high -> jitter ~= base, v ~= floor(2*base).
    expect(generate_loot_gold(10, "empire", rng_high) == 45,
           "gold L10 empire rng_high = floor(23 + 22.9977)");
    // Never negative, even at level 0.
    expect(generate_loot_gold(0, "wildlife", rng_zero) >= 0, "gold never negative");
}

int main() {
    test_project_combat_melee();
    test_project_combat_missile();
    test_sheet_budget_identity();
    test_sheet_determinism();
    test_npc_loot_id();
    test_roll_loot_profile();
    test_generate_loot_gold();

    if (g_fails == 0) {
        std::printf("OK rpg_loot_test: all assertions passed\n");
        return 0;
    }
    std::printf("rpg_loot_test: %d assertion(s) FAILED\n", g_fails);
    return 1;
}
