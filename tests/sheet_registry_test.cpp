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
#include "macro/econ_day.h"   // kRecipes — чей ранг открывает рецепт
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
    CHECK(int(SkillId::Count) == 38,
          "the canon thirty-two, Unarmed (v79) and the five crafts "
          "(2026-09-18) stand in the registry today");
    // A SKILL MUST HAVE POWER — and this used to say «power is a percent per
    // rank», which is true of every skill that multiplies a blow or shaves a
    // price and false of a CRAFT: a craft's rank does not scale a number, it
    // OPENS A RECIPE. Weakening the rule to «or zero is fine too» would have
    // let the next dead column in, so it is stated the strong way instead:
    // every row either multiplies something per rank, or is named by at
    // least one recipe of the production table. An alchemy skill nobody can
    // brew with reddens here, which is exactly what it should do.
    // THE THIRD legal answer, added 2026-09-19 with the owner's verdict on
    // Unarmored: a row may SLEEP — openly, by name, in this closed list —
    // while the mechanic it would read does not exist yet (S14 «закон рамки
    // скилла»: a skill that needs a system built under it is a design defect,
    // so the row waits at pctPerRank 0 instead of lying in a tooltip). The
    // list is spelled HERE, in the witness, so adding a sleeper costs a
    // deliberate line in a test — not a silent zero in a table.
    const SkillId kSleepers[] = {SkillId::Unarmored};
    int craftRows = 0;
    int sleepers = 0;
    for (int i = 0; i < int(SkillId::Count); ++i) {
        const SkillDef& d = skill_def(SkillId(i));
        CHECK(int(d.id) == i, "every row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "every row names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
        bool namedByRecipe = false;
        for (const RecipeDef& r : kRecipes)
            if (r.craft == SkillId(i)) { namedByRecipe = true; break; }
        if (namedByRecipe) ++craftRows;
        bool sleeps = false;
        for (SkillId s : kSleepers) if (s == SkillId(i)) { sleeps = true; break; }
        if (sleeps) {
            ++sleepers;
            CHECK(d.pctPerRank == 0,
                  "a SLEEPING row promises nothing per rank — a sleeper with "
                  "a live percent is the tooltip lie this list exists against");
        }
        CHECK(d.pctPerRank > 0 || namedByRecipe || sleeps,
              "a skill must either multiply something per rank, unlock a "
              "recipe, or be a NAMED sleeper — a row that does none of the "
              "three is a dead column");
    }
    CHECK(sleepers == 1,
          "exactly one row sleeps today (Unarmored, owner 2026-09-19) — the "
          "sweep above actually judged it");
    // The negative control of the sweep itself: it must have SEEN crafts, or
    // the clause above proved nothing about them (testing law #3).
    CHECK(craftRows == 5,
          "five crafts are named by the recipe table — the sweep above "
          "actually judged them");
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
    const BarCeilings bare = bar_ceilings(a, none, 100, 100, 100);
    const DerivedBonuses bareD = calculate_derived(a, none);
    const float bareCarry = get_carry_capacity(a, none);

    Skills trained{};
    trained[SkillId::Bodybuilding]  = 20;   // 5 %/rank -> x2
    trained[SkillId::Meditation]    = 20;   // 5 %/rank -> x2
    trained[SkillId::Armsmaster]       = 20;   // 5 %/rank -> x2
    trained[SkillId::Spellcraft]    = 20;   // 5 %/rank -> x2
    trained[SkillId::Weightlifting] = 10;   // 10 %/rank -> x2
    const BarCeilings tr = bar_ceilings(a, trained, 100, 100, 100);
    const DerivedBonuses trD = calculate_derived(a, trained);

    CHECK(tr.maxHp == bare.maxHp * 2, "maxHp follows bodybuilding's row");
    CHECK(tr.maxMp == bare.maxMp * 2, "maxMp follows meditation's row");
    // «Один рычаг на ручку» (CANON S14, 2026-09-07): the generic pair moved
    // to TEMPO — the raw adds are the ATTRIBUTE's alone now, and training
    // Armsmaster/Spellcraft must move the recovery instead of the damage.
    CHECK(trD.rawPhysDamage == bareD.rawPhysDamage
              && trD.rawPhysDamage == a.of(AttributeId::Str),
          "phys add is raw Str — Armsmaster no longer multiplies damage");
    CHECK(trD.rawSpellDamage == bareD.rawSpellDamage
              && trD.rawSpellDamage == a.of(AttributeId::Intl),
          "spell add is raw Intl — Spellcraft no longer multiplies damage");
    CHECK(recovery_steps(1.0f, a, trained, SkillId::Armsmaster)
              < recovery_steps(1.0f, a, none, SkillId::Armsmaster),
          "Armsmaster's row now governs the arm's tempo");
    CHECK(recovery_steps(1.0f, a, trained, SkillId::Spellcraft)
              < recovery_steps(1.0f, a, none, SkillId::Spellcraft),
          "Spellcraft's row now governs the cast's tempo");
    CHECK(get_carry_capacity(a, trained) == bareCarry * 2.0f,
          "carry capacity follows weightlifting's row");

    // The SP bar is END's alone — no skill multiplies it, and that is a
    // deliberate absence, so it is pinned as one.
    Skills marathoner{};
    marathoner[SkillId::Marathon] = 50;
    CHECK(bar_ceilings(a, marathoner, 100, 100, 100).maxSp == bare.maxSp,
          "no skill grows the stamina BAR: marathon shortens the rest instead");
    CHECK(skill_mult_of(SkillId::Marathon, 50) > 1.0f,
          "negative control: the rank does move the rest RATE (rest_pools "
          "multiplies by this same skill law), so the check above is an "
          "absence and not a dead sheet");

    // And the cost skill, through the movement law's own door.
    Skills pathfinder{};
    pathfinder[SkillId::Travel] = 25;
    CHECK(travel_skill_efficiency(pathfinder) < 1.0f,
          "training travel makes ground cheaper");
    CHECK(calculate_derived(a, pathfinder).moveSpeedPct
              == bareD.moveSpeedPct,
          "and it does NOT make him faster: one skill, one meaning");
}

// ── The third currency: 5-5-5 at creation, 1-1-1 per level (CANON S14) ───
// The perk GRAPH is consciously absent (stub, owner 2026-09-19) — what must
// already be true is the ECONOMY: perk points accrue symmetrically with the
// other two currencies, for the player and for every procedural sheet, and
// the 256-bit mask answers has_perk without a graph to walk. The old
// «perk point every 10th level» died with this witness watching.
void test_the_third_currency_accrues() {
    LevelData ld = default_level_data();
    CHECK(ld.attributePoints == 5 && ld.learnPicks == 5 && ld.perkPoints == 5,
          "creation is the K-K-K budget: 5-5-5 (owner 2026-09-14)");
    ld.exp = ld.expToNext;
    CHECK(try_level_up(ld), "the threshold levels");
    CHECK(ld.attributePoints == 6 && ld.skillPoints == 1 && ld.perkPoints == 6,
          "a level grants 1-1-1: full isotropy, no special levels");
    // A level-N procedural sheet is budget-identical to a level-N player;
    // the perk pool accrues UNSPENT while the graph is a stub.
    const CharacterSheet cs =
        make_character_sheet(NPCType(0), 6, leader_sheet_seed(1u));
    CHECK(cs.levelData.perkPoints == 5 + 5,
          "an NPC sheet accrues the perk pool it cannot spend yet");
    bool bornEmpty = true;
    for (int i = 0; i < 256; ++i) bornEmpty &= !cs.perks.has_perk(i);
    CHECK(bornEmpty, "a sheet's mask is born all zeroes — no graph, no perks");
    // The mask itself: 1-instruction reads by ordinal across the envelope.
    PerkMask m{};
    m.set_perk(0); m.set_perk(63); m.set_perk(64); m.set_perk(255);
    CHECK(m.has_perk(0) && m.has_perk(63) && m.has_perk(64) && m.has_perk(255),
          "set/has round-trips across word boundaries");
    CHECK(!m.has_perk(1) && !m.has_perk(62) && !m.has_perk(254),
          "and neighbours stay untouched");
}

// ── An NPC hits with his SHEET (CANON S13/S14, session Е 2026-09-19) ─────
// The largest «игрок == НПЦ» asymmetry: both consumers (spawn's
// combat_from_sheet and auto_battle's fighter_power) hardcoded multPct 100,
// so the points the generator spent into weapon skills and schools
// multiplied nothing. One door now — project_combat.multPct, the
// best-trained skill of the attack's domain — and both ends read it.
void test_npc_strikes_with_his_sheet() {
    CharacterSheet bare{};
    CombatTemplate melee{};                      // Melee by default
    CombatTemplate cast{};
    cast.attackKind = CombatTemplate::Missile;   // today: always a caster row
    CHECK(project_combat(bare, melee).multPct == 100,
          "an untrained sheet swings at x1 — the player's bare fist");

    CharacterSheet swordsman{};
    swordsman.skills[SkillId::Sword] = 20;
    const int swordPct = skill_mult_pct(swordsman.skills, SkillId::Sword);
    CHECK(swordPct > 100, "negative control: the rank does move the law");
    CHECK(int(project_combat(swordsman, melee).multPct) == swordPct,
          "a melee row multiplies by the best-trained weapon skill");
    CHECK(project_combat(swordsman, cast).multPct == 100,
          "and a sword rank multiplies no cast — domains do not leak");

    // ── РАСКЛЕЙКА КАСТА И ВЫСТРЕЛА (owner 2026-09-17, built 2026-09-19) ──
    // A SHOT is a Missile row that names no spell: dice + typed weapon skill
    // + LCK, and NO attribute add (CANON S14). Before the split, `Missile`
    // MEANT "caster", so this row would have drawn an INT bonus to arrows.
    CharacterSheet archer{};
    archer.attributes[AttributeId::Intl] = 40;
    archer.attributes[AttributeId::Str]  = 40;
    archer.skills[SkillId::Bow] = 20;
    CombatTemplate shot{};
    shot.attackKind = CombatTemplate::Missile;      // castSpell stays null
    const CombatTemplate shotOut = project_combat(archer, shot);
    CHECK(shotOut.flatAdd == 0,
          "a SHOT takes no attribute add — not INT, and not STR either");
    CHECK(int(shotOut.multPct) == skill_mult_pct(archer.skills, SkillId::Bow),
          "its typed lever is the weapon skill, like any other blow");
    CHECK(project_combat(archer, melee).flatAdd
              == std::int16_t(archer.attributes.of(AttributeId::Str)),
          "negative control: a SWING still takes STR, so the zero above is "
          "the shooting law and not a dead door");

    // A CAST is a row that NAMES a spell: the blow is that spell's — its
    // dice, its damage type, the caster's INT, its school's rank.
    CombatTemplate casterRow{};
    casterRow.attackKind = CombatTemplate::Missile;
    casterRow.dice = Dice{1, 1};                    // the row's own dice…
    casterRow.castSpell = spell_ordinal("fireball");               // …are not read at all
    const SpellDef* fire = spell_find("fireball");
    CHECK(fire != nullptr, "the fixture's spell exists");
    CharacterSheet mage{};
    mage.attributes[AttributeId::Intl] = 7;
    mage.skills[SkillId::FireMagic] = 30;
    const CombatTemplate castOut = project_combat(mage, casterRow);
    CHECK(castOut.dice.n == fire->dice.n && castOut.dice.m == fire->dice.m,
          "a caster has no dice of his own: the SPELL's dice are the blow");
    CHECK(castOut.dmgType == spell_damage_type(*fire),
          "and the spell's damage type is the column it argues with");
    CHECK(castOut.flatAdd == std::int16_t(mage.attributes.of(AttributeId::Intl)),
          "the add is the caster's INT — the cast is magic, and says so");
    CHECK(int(castOut.multPct)
              == skill_mult_pct(mage.skills, SkillId::FireMagic),
          "and the lever is the rank of the spell's OWN school");

    CharacterSheet witch{};
    witch.skills[SkillId::FireMagic] = 30;
    CHECK(project_combat(witch, melee).multPct == 100,
          "a school rank multiplies no sword");
    // The fist is a weapon type like any other (S14, appended v79).
    CharacterSheet monk{};
    monk.skills[SkillId::Unarmed] = 40;
    CHECK(int(project_combat(monk, melee).multPct)
              == skill_mult_pct(monk.skills, SkillId::Unarmed),
          "the unarmed monk is a build, not a gap in the law");
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

// §41 root 2: the row's base bars are COLUMNS of CombatTemplate now, not
// default arguments of bar_ceilings. The witness is arithmetic: author a row
// with a different well and the body's bar must move by exactly the sheet
// law over that base — while the untouched bars stand still (the negative
// control against a door that ignores its row or crosses its columns).
void test_the_rows_base_bars_reach_the_body() {
    CharacterSheet sheet{};
    sheet.attributes[AttributeId::End] = 4;
    sheet.attributes[AttributeId::Wil] = 6;

    CombatTemplate canon{};      // the bare 100/100/100 body (member defaults)
    CombatTemplate frail = canon;
    frail.mp = 40;               // a small well…
    frail.sp = 60;               // …and short legs, authored BY THE ROW

    // The same sheet over two rows: the delta is the BASE delta through the
    // sheet law (no skills → multiplier 1), not a restated formula.
    CHECK(body_max_mp(sheet, canon) - body_max_mp(sheet, frail) == 100 - 40,
          "the row's mp column is the well the sheet law grows");
    CHECK(body_max_sp(sheet, canon) - body_max_sp(sheet, frail) == 100 - 60,
          "the row's sp column is the legs the sheet law grows");
    // Negative controls: the columns do not cross, and hp keeps its own.
    CHECK(body_max_hp(sheet, canon) == body_max_hp(sheet, frail),
          "mp/sp authoring leaves the hp bar untouched");
    CombatTemplate tank = canon;
    tank.hp = 250.0f;
    CHECK(body_max_mp(sheet, tank) == body_max_mp(sheet, canon)
              && body_max_sp(sheet, tank) == body_max_sp(sheet, canon),
          "hp authoring leaves the mp/sp bars untouched");
}

int main() {
    test_the_rows_base_bars_reach_the_body();
    test_attributes_are_an_envelope_and_a_table();
    test_the_registry_is_addressable_by_ordinal();
    test_ranks_are_a_flat_envelope();
    test_one_door_and_the_row_decides();
    test_the_cap_belongs_to_the_law();
    test_the_third_currency_accrues();
    test_npc_strikes_with_his_sheet();
    test_the_governed_numbers_follow_the_row();
    test_every_role_rates_every_skill();
    return sm::test::report("sheet_registry_test");
}
