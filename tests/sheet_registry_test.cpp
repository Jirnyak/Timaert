// THE SKILL LAW lives in ONE place, and a skill is a ROW.
//
// It used to take five files to add a skill: a named field in `Skills`, a
// value in the `SkillId` enum, a case in each of the two `skill_value`
// switches, a row in the UI table (ui/overlays.cpp) and a weight column in the
// per-role table (macro/character_sheet.h). rpg.md called that "four too many
// for a game that will grow many more skills". It is one row and one weight
// now — and the weight is a compile error until every role answers, because a
// role that silently rates a new skill at zero is a role that never trains it.
//
// The percent is a COLUMN. rpg.md and the canon audit (A7) both record the
// debt that settles: the law promised "one rank is one percent, ceiling ×2",
// while four of the most expensive numbers in the game — maxHp, maxMp and both
// raw damages — were computed inline at 0.05 per rank with NO clamp. So
// bodybuilding 100 gave ×6 HP against a documented ×2, and the law was a
// sentence the code did not obey. The owner's ruling was to legitimise the
// per-skill multiplier as a column rather than flatten every skill to 1 %:
// the ceiling is DERIVED per row now, and there is exactly one function that
// turns a rank into a multiplier.
#include "check.h"

#include "macro/attributes.h"
#include "macro/character_sheet.h"
#include "macro/movement_cost.h"

#include <cstdio>

namespace {

using namespace sm;

// ── Attributes are the SAME shape: an envelope and a table ───────────────
void test_attributes_are_an_envelope_and_a_table() {
    CHECK(int(AttributeId::Count) == 8,
          "the canon eight stand in the registry today");
    for (int i = 0; i < int(AttributeId::Count); ++i) {
        const AttributeDef& d = attribute_def(AttributeId(i));
        CHECK(int(d.id) == i, "every row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "every row names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
        CHECK(d.effect != nullptr && d.effect[0] != '\0',
              "and says what a point buys, so the panel need not");
    }

    Attributes a{};
    for (int i = 0; i < kMaxAttributes; ++i) {
        CHECK(a.score[std::size_t(i)] == 1,
              "every slot starts at 1 — the reserved tail included, so a score "
              "named later begins where the others began");
    }
    a[AttributeId::Str] = 7;
    CHECK(a.of(AttributeId::Str) == 7, "a score writes and reads by ordinal");
    CHECK(a.of(AttributeId::End) == 1,
          "negative control: writing one score moved no other");
    CHECK(int(a.score.size()) == kMaxAttributes,
          "the envelope is the size the save promises");

    // The one door into a score refuses rather than rolls the byte over.
    LevelData ld{};
    ld.attributePoints = kMaxAttributeScore + 10;
    Attributes cap{};
    int spent = 0;
    while (spend_attribute_point(ld, cap, AttributeId::Lck)) ++spent;
    CHECK(cap.of(AttributeId::Lck) == kMaxAttributeScore,
          "a score stops at the byte's ceiling");
    CHECK(spent == kMaxAttributeScore - 1, "having started at its base of 1");
    CHECK(!spend_attribute_point(ld, cap, AttributeId::Count),
          "a score that names no row is refused, not written past the end");
}

// ── The table is a table ─────────────────────────────────────────────────
void test_the_registry_is_addressable_by_ordinal() {
    CHECK(int(SkillId::Count) == 33,
          "the canon thirty-two plus Unarmed (v79 append) stand in the "
          "registry today");
    for (int i = 0; i < int(SkillId::Count); ++i) {
        const SkillDef& d = skill_def(SkillId(i));
        CHECK(int(d.id) == i, "every row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "every row names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
        CHECK(d.pctPerRank > 0,
              "a skill that does nothing per rank is not a skill");
    }
}

// ── The ranks are a flat envelope, addressed by index ────────────────────
void test_ranks_are_a_flat_envelope() {
    Skills s{};
    for (int i = 0; i < int(SkillId::Count); ++i) {
        CHECK(s.of(SkillId(i)) == 0, "a fresh sheet is trained in nothing");
    }
    s[SkillId::Armsmaster] = 7;
    CHECK(s.of(SkillId::Armsmaster) == 7, "a rank writes and reads by ordinal");
    CHECK(s.of(SkillId::Travel) == 0,
          "negative control: writing one rank moved no other");
    CHECK(int(s.rank.size()) == kMaxSkills,
          "the envelope is the size the save promises, not the count in use");
    CHECK(int(SkillId::Count) <= kMaxSkills, "and the count fits inside it");
}

// ── ONE door turns a rank into a multiplier ──────────────────────────────
void test_one_door_and_the_row_decides() {
    Skills s{};
    // Bodybuilding's row says 5 %/rank. Twenty ranks is +100 %.
    s[SkillId::Bodybuilding] = 20;
    CHECK(skill_mult(s, SkillId::Bodybuilding) == 2.0f,
          "the multiplier is rank x the ROW's percent");
    // Athletics' row says 1 %/rank. The SAME twenty ranks is +20 %.
    s[SkillId::Athletics] = 20;
    CHECK(skill_mult(s, SkillId::Athletics) > 1.19f
          && skill_mult(s, SkillId::Athletics) < 1.21f,
          "negative control: the same rank in another skill is worth what THAT "
          "row says — the percent is read, not assumed");

    // Travel is the one row that buys a cost DOWN, and its direction is a
    // column too, not a second helper.
    s[SkillId::Travel] = 40;
    CHECK(skill_mult(s, SkillId::Travel) > 0.59f
          && skill_mult(s, SkillId::Travel) < 0.61f,
          "a cost skill subtracts where a bonus skill adds");
    CHECK(skill_def(SkillId::Travel).buysCostDown,
          "and it is the row that says so");
}

// ── The CAP is the law's, and it is enforced once ────────────────────────
void test_the_cap_belongs_to_the_law() {
    CHECK(skill_mult_of(SkillId::Athletics, -5) == 1.0f,
          "a negative rank grants nothing");
    CHECK(skill_mult_of(SkillId::Athletics, kMaxSkillRank + 500)
              == skill_mult_of(SkillId::Athletics, kMaxSkillRank),
          "and nothing past mastery is worth anything more");
    // A cost skill can never pay you to travel, whatever its percent.
    CHECK(skill_mult_of(SkillId::Travel, kMaxSkillRank) == 0.0f,
          "at mastery the world stops resisting the traveller");
    CHECK(skill_mult_of(SkillId::Travel, kMaxSkillRank * 4) >= 0.0f,
          "and never starts paying him");

    // The rank cap is enforced at the ONE door into a rank — and so is THE
    // learn law: rank 0 is ignorance, ignorance refuses the point, and
    // mastery is therefore 1 (learned) + 99 spends.
    LevelData ld{};
    Skills s{};
    ld.skillPoints = kMaxSkillRank + 10;
    CHECK(!spend_skill_point(ld, s, SkillId::Travel),
          "an unknown skill refuses the point: learn first");
    CHECK(learn_skill(s, SkillId::Travel),
          "learning is the one door out of ignorance (rank 0 -> 1)");
    CHECK(!learn_skill(s, SkillId::Travel),
          "and a teacher cannot teach what is already known");
    int spent = 0;
    while (spend_skill_point(ld, s, SkillId::Travel)) ++spent;
    CHECK(spent == kMaxSkillRank - 1, "a rank stops at mastery");
    CHECK(ld.skillPoints == 11,
          "and a refused spend keeps the point for another skill");
    CHECK(!spend_skill_point(ld, s, SkillId::Count),
          "a rank that names no row is refused, not written past the end");
}

// ── Every formula that a skill governs asks that one door ────────────────
// The witness is arithmetic, not a grep: change a row's percent and every
// number the skill governs must move with it. These pin the four that used to
// spell 0.05 inline and bypass the law entirely.
void test_the_governed_numbers_follow_the_row() {
    Attributes a{};
    a[AttributeId::End] = 10;  a[AttributeId::Wil]  = 10;
    a[AttributeId::Str] = 10;  a[AttributeId::Intl] = 10;
    a[AttributeId::Spd] = 10;
    Skills none{};
    const CombatStats bare = calculate_combat_stats(a, none);
    const DerivedBonuses bareD = calculate_derived(a, none);
    const float bareCarry = get_carry_capacity(a, none);

    Skills trained{};
    trained[SkillId::Bodybuilding]  = 20;   // 5 %/rank -> x2
    trained[SkillId::Meditation]    = 20;   // 5 %/rank -> x2
    trained[SkillId::Armsmaster]       = 20;   // 5 %/rank -> x2
    trained[SkillId::Spellcraft]    = 20;   // 5 %/rank -> x2
    trained[SkillId::Weightlifting] = 10;   // 10 %/rank -> x2
    const CombatStats tr = calculate_combat_stats(a, trained);
    const DerivedBonuses trD = calculate_derived(a, trained);

    CHECK(tr.maxHp == bare.maxHp * 2, "maxHp follows bodybuilding's row");
    CHECK(tr.maxMp == bare.maxMp * 2, "maxMp follows meditation's row");
    CHECK(trD.rawPhysDamage == bareD.rawPhysDamage * 2.0f,
          "physical damage follows fighter's row");
    CHECK(trD.rawSpellDamage == bareD.rawSpellDamage * 2.0f,
          "spell damage follows spellcraft's row");
    CHECK(get_carry_capacity(a, trained) == bareCarry * 2.0f,
          "carry capacity follows weightlifting's row");

    // The SP bar is END's alone — no skill multiplies it, and that is a
    // deliberate absence, so it is pinned as one.
    Skills marathoner{};
    marathoner[SkillId::Marathon] = 50;
    CHECK(calculate_combat_stats(a, marathoner).maxSp == bare.maxSp,
          "no skill grows the stamina BAR: marathon shortens the rest instead");
    CHECK(calculate_combat_stats(a, marathoner).spRegen > bare.spRegen,
          "negative control: it does move the rest, so the check above is "
          "an absence and not a dead sheet");

    // And the cost skill, through the movement law's own door.
    Skills pathfinder{};
    pathfinder[SkillId::Travel] = 25;
    CHECK(travel_skill_efficiency(pathfinder) < 1.0f,
          "training travel makes ground cheaper");
    CHECK(calculate_derived(a, pathfinder).moveSpeedMult
              == bareD.moveSpeedMult,
          "and it does NOT make him faster: one skill, one meaning");
}

// ── A role's opinion of every skill is stated, not defaulted ─────────────
void test_every_role_rates_every_skill() {
    for (int r = 0; r < int(NPCType::Count); ++r) {
        const CharacterSheet sheet =
            make_character_sheet(NPCType(r), 6, leader_sheet_seed(std::uint32_t(r)));
        int spent = 0;
        for (std::uint8_t rank : sheet.skills.rank) spent += int(rank);
        CHECK(spent > 0, "every role trains SOMETHING by level six");
    }
}

} // namespace

int main() {
    test_attributes_are_an_envelope_and_a_table();
    test_the_registry_is_addressable_by_ordinal();
    test_ranks_are_a_flat_envelope();
    test_one_door_and_the_row_decides();
    test_the_cap_belongs_to_the_law();
    test_the_governed_numbers_follow_the_row();
    test_every_role_rates_every_skill();
    return sm::test::report("sheet_registry_test");
}
