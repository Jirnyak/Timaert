// ONE table every modifier in the game is a row of.
//
// There were FIVE half-doors saying overlapping things in five vocabularies:
// `ItemEffect` (six named ints, three of which no code ever read, one of them
// naming an attribute — `agi` — that does not exist), `AuraMods` (a
// target/index pair, the only one already honest), the perk system (24 ids, 8
// descriptions, ONE effect — expressed as a row in the AURA table, while a
// second perk was a hardcoded branch in a UI callback), the string-verb
// applicator in events/ ("heal_hp", "restore_mp", …), and the sustained-spell
// multipliers (a literal x1.5 keyed by the string "haste"). CANON S26 calls
// that a defect by name.
//
// What this file pins is not "the table exists" but the two properties that
// made the merge worth doing:
//   1. a bonus NAMES AN ADDRESS the sheet already has — it never invents a
//      number, so a new modifier cannot grow a private formula;
//   2. a standing bonus is REMOVABLE, because it is read through a modified
//      COPY instead of being written into the sheet. The old `apply_aura`
//      wrote its deltas into the member's stored sheet at birth, with no
//      source and no way back, so a squad that outlived its leader's perk kept
//      the buff forever.
#include "check.h"

#include "macro/bonus.h"
#include "macro/character_sheet.h"
#include "macro/aura.h"

#include <cstdio>

namespace {

using namespace sm;

// ── The table is a table ─────────────────────────────────────────────────
void test_every_row_names_an_address_that_exists() {
    CHECK(int(BonusId::Count) > 1, "the registry has rows");
    for (int i = 0; i < int(BonusId::Count); ++i) {
        const BonusDef& d = bonus_def(BonusId(i));
        CHECK(int(d.id) == i, "every row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "every row names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
        // THE property: the address is one the game already has. A row that
        // pointed past its target's id space would be a modifier landing on
        // a number nobody else can read.
        switch (d.target) {
            case BonusTarget::Attribute:
                CHECK(i == 0 || d.index < std::uint8_t(AttributeId::Count),
                      "an attribute row names a score the sheet has");
                break;
            case BonusTarget::SkillRank:
                CHECK(d.index < std::uint8_t(SkillId::Count),
                      "a skill row names a rank the sheet has");
                break;
            case BonusTarget::Pool:
                CHECK(d.index < std::uint8_t(PoolId::Count),
                      "a pool row names a pool a body has");
                break;
        }
    }
}

void test_the_authoring_key_resolves_and_refuses() {
    CHECK(bonus_id("vit") == BonusId::Vit, "content names a row by string");
    CHECK(bonus_id("heal_hp") == BonusId::HealHp, "and the instant rows too");
    // Refusal, not the zero-th row by accident: those are the same value and
    // only one of them is a mistake.
    CHECK(bonus_id("no_such_bonus") == BonusId::None,
          "an unknown key resolves to None");
    CHECK(bonus_id(nullptr) == BonusId::None, "and so does no key at all");
    CHECK(bonus_def(BonusId::None).target == BonusTarget::Attribute
              && bonus_def(BonusId::None).index == 0,
          "None is a real row, so an empty affix slot reads as empty rather "
          "than as +N to strength");
    Bonus empty{};
    BonusTotals t{};
    accumulate(t, empty);
    CHECK(t.attr[0] == 0,
          "negative control: an all-zero slot moves NOTHING — which is what a "
          "freshly-zeroed item's four affix cells are");
}

// ── Standing: the sum lands on the sheet's own addresses ─────────────────
void test_standing_bonuses_land_where_the_row_says() {
    CharacterSheet base{};
    base.attributes[AttributeId::Str] = 10;
    base.skills[SkillId::Fighter] = 20;

    BonusTotals t{};
    accumulate(t, Bonus{std::uint8_t(BonusId::Str), +5});
    accumulate(t, Bonus{std::uint8_t(BonusId::Fighter), +10});
    const CharacterSheet eff = effective_sheet(base, t);

    CHECK(eff.attributes.of(AttributeId::Str) == 15,
          "the attribute row adds to the score it names");
    CHECK(eff.skills.of(SkillId::Fighter) == 30,
          "the skill row adds to the rank it names");
    CHECK(eff.attributes.of(AttributeId::Vit)
              == base.attributes.of(AttributeId::Vit),
          "negative control: a score no row named did not move");

    // ...and the derived numbers follow, because they are derived from the
    // sheet and the sheet is what was modified. This is the whole reason a
    // bonus targets an ADDRESS and not an outcome.
    CHECK(calculate_derived(eff.attributes, eff.skills).rawPhysDamage
              > calculate_derived(base.attributes, base.skills).rawPhysDamage,
          "a +STR ring makes the blow land harder without naming damage");
}

// ── THE property: standing means REMOVABLE ───────────────────────────────
void test_a_standing_bonus_can_be_taken_off() {
    CharacterSheet base{};
    base.attributes[AttributeId::Vit] = 8;
    const int bareHp = calculate_combat_stats(base.attributes, base.skills).maxHp;

    BonusTotals worn{};
    accumulate(worn, Bonus{std::uint8_t(BonusId::Vit), +6});
    const CharacterSheet armoured = effective_sheet(base, worn);
    const int wornHp =
        calculate_combat_stats(armoured.attributes, armoured.skills).maxHp;
    CHECK(wornHp > bareHp, "wearing it raises the ceiling");

    // The STORED sheet never moved — which is exactly what the old aura could
    // not say about itself.
    CHECK(base.attributes.of(AttributeId::Vit) == 8,
          "the character the player built is untouched by what he is wearing");

    // Take it off: not by undoing anything, but by not adding it.
    const CharacterSheet bare = effective_sheet(base, BonusTotals{});
    CHECK(calculate_combat_stats(bare.attributes, bare.skills).maxHp == bareHp,
          "taking it off is simply not accumulating it — no undo, no residue");
}

void test_effective_sheet_owns_the_clamps() {
    CharacterSheet s{};
    BonusTotals curse{};
    accumulate(curse, Bonus{std::uint8_t(BonusId::Vit), -50});
    accumulate(curse, Bonus{std::uint8_t(BonusId::Bodybuilding),
                            std::int16_t(500)});
    const CharacterSheet out = effective_sheet(s, curse);
    CHECK(out.attributes.of(AttributeId::Vit) == 1,
          "no modifier curses a score below the base every score starts at");
    CHECK(out.skills.of(SkillId::Bodybuilding) == kMaxSkillRank,
          "and none pushes a rank past mastery");
}

// ── Instant: acts once, on a pool, and reports what MOVED ────────────────
void test_instant_bonuses_move_pools_and_tell_the_truth() {
    int hp = 40, mp = 5, sp = 100;
    PoolSlice pools{};
    pools.current[int(PoolId::Hp)] = &hp;  pools.maximum[int(PoolId::Hp)] = 100;
    pools.current[int(PoolId::Mp)] = &mp;  pools.maximum[int(PoolId::Mp)] = 50;
    pools.current[int(PoolId::Sp)] = &sp;  pools.maximum[int(PoolId::Sp)] = 100;

    CHECK(apply_instant(pools, {std::uint8_t(BonusId::HealHp), 30}) == 30,
          "a potion moves what it says while there is room");
    CHECK(hp == 70, "and the pool holds it");

    // The truth, not the wish: a caller that reports "+30 HP" to the player
    // when 5 landed is lying to him.
    CHECK(apply_instant(pools, {std::uint8_t(BonusId::HealHp), 100}) == 30,
          "a full pool takes only what fits, and says so");
    CHECK(hp == 100, "and stops at its ceiling");

    // One row, both signs: a poison is a potion with the other sign.
    CHECK(apply_instant(pools, {std::uint8_t(BonusId::HealSp), -40}) == -40,
          "the same row spends what it can heal");
    CHECK(sp == 60, "and the pool falls by exactly that");
    CHECK(apply_instant(pools, {std::uint8_t(BonusId::HealSp), -500}) == -60,
          "a pool empties at zero, it does not go negative through this door");

    // A standing row is not an instant one, and asking gets nothing rather
    // than something arbitrary.
    CHECK(apply_instant(pools, {std::uint8_t(BonusId::Str), 5}) == 0,
          "an attribute row does nothing to a pool");
    CHECK(mp == 5, "negative control: and it did not quietly hit another pool");
}

// ── The aura is a tenant, not a second vocabulary ────────────────────────
void test_the_aura_speaks_the_registry() {
    CharacterSheet leader{};
    add_perk(leader.perks, PerkID::Leader);
    const AuraMods aura = collect_leader_aura(leader);
    CHECK(aura.count == 1, "the Leader perk row collects one modifier");
    CHECK(aura.mods[0].row == std::uint8_t(BonusId::Vit),
          "and it is a ROW of the one registry, not a private encoding");

    // The same row, summed by the same accumulator, through the same copy.
    BonusTotals t{};
    accumulate(t, aura.mods.data(), int(aura.count));
    CHECK(t.attr[std::size_t(AttributeId::Vit)] == 1,
          "a leader's aura and a sword's affix reach the sheet by one road");
}

} // namespace

int main() {
    test_every_row_names_an_address_that_exists();
    test_the_authoring_key_resolves_and_refuses();
    test_standing_bonuses_land_where_the_row_says();
    test_a_standing_bonus_can_be_taken_off();
    test_effective_sheet_owns_the_clamps();
    test_instant_bonuses_move_pools_and_tell_the_truth();
    test_the_aura_speaks_the_registry();
    return sm::test::report("bonus_registry_test");
}
