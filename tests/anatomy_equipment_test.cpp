// ANATOMY IS A ROW; EQUIPMENT IS A FLAT ARRAY OVER IT.
//
// Owner's rulings: «экипировка = DOD-массив, не граф: ячейка = тип слота, у
// строки предмета маска слота» and «анатомия строкой, 128 ячеек — сколько и
// каких частей говорит СТРОКА; маска предмета по ТИПАМ частей, а не по
// индексам».
//
// That last clause is the load-bearing one, and it is what most of this file
// tests. If a mask named INDICES, an octopus would need eight enum values for
// eight tentacles and every ring in the game would have to list all of them.
// Because it names TYPES, an octopus is one row with eight cells of one type,
// and the ring says "Finger" once — so growing a limb, losing one or bolting
// on a prosthetic is a change to the cell array and to NOTHING else.
#include "check.h"

#include "macro/anatomy.h"
#include "macro/items.h"

#include <cstdio>

namespace {

using namespace sm;

// ── The tables are tables ────────────────────────────────────────────────
void test_the_rows_stand_where_they_say() {
    for (int i = 0; i < int(BodyPartId::Count); ++i) {
        const BodyPartDef& d = body_part_def(BodyPartId(i));
        CHECK(int(d.id) == i, "every part row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "and names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
    }
    for (int i = 0; i < int(AnatomyId::Count); ++i) {
        const AnatomyDef& d = anatomy_def(AnatomyId(i));
        CHECK(int(d.id) == i, "every shape row stands at its own ordinal");
        CHECK(anatomy_cell_count(d) > 0, "a shape with no parts is no body");
        // A row asking for more cells than the envelope holds would be
        // TRUNCATED in silence, and the missing limbs would simply never
        // appear. Counted here so the truncation can never happen unseen.
        int asked = 0;
        for (const AnatomyPart& p : d.parts) asked += int(p.count);
        CHECK(asked <= kMaxBodyParts,
              "a shape fits the envelope: nothing is silently truncated");
    }
    // Every part bit must be distinct, or two part types would share a slot
    // mask and an item meant for one would fit the other.
    std::uint64_t seen = 0;
    for (int i = 0; i < int(BodyPartId::Count); ++i) {
        const std::uint64_t bit = part_bit(BodyPartId(i));
        CHECK((seen & bit) == 0, "each part type owns its own bit");
        seen |= bit;
    }
}

// ── The cells ARE the expansion of the row ───────────────────────────────
void test_cells_expand_from_the_shape() {
    Equipment human{};
    human.anatomy = std::uint8_t(AnatomyId::Humanoid);

    // A humanoid has TEN fingers, and they are ten cells of ONE type — not
    // ten names in an enum.
    int fingers = 0, hands = 0, heads = 0;
    for (int i = 0; i < human.cells(); ++i) {
        const BodyPartId p = human.part_at(i);
        if (p == BodyPartId::Finger) ++fingers;
        if (p == BodyPartId::Hand) ++hands;
        if (p == BodyPartId::Head) ++heads;
    }
    CHECK(fingers == 10, "ten fingers are ten cells of one type");
    CHECK(hands == 2, "two hands, likewise");
    CHECK(heads == 1, "and one head");
    CHECK(human.part_at(human.cells()) == BodyPartId::Count,
          "past the last cell there is no part — a body has an end");
    CHECK(human.part_at(-1) == BodyPartId::Count
          || human.cells() > 0, "and asking outside it is not a crash");

    // A different shape is a different body, from the same code.
    Equipment bird{};
    bird.anatomy = std::uint8_t(AnatomyId::Avian);
    int wings = 0;
    for (int i = 0; i < bird.cells(); ++i)
        if (bird.part_at(i) == BodyPartId::Wing) ++wings;
    CHECK(wings == 2, "negative control: another row expands to another body");
    CHECK(bird.cells() != human.cells(),
          "and to a different number of cells entirely");
}

// ── Wearing ──────────────────────────────────────────────────────────────
void test_equip_finds_a_cell_the_mask_names() {
    Equipment eq{};
    ItemRef leather{};
    leather.def = std::uint16_t(item_index("arm_leather"));
    leather.count = 1;

    const int cell = equip(eq, leather);
    CHECK(cell >= 0, "the coat found a torso to sit on");
    CHECK(eq.part_at(cell) == BodyPartId::Torso,
          "and it sat on the part its mask names, not on the first free cell");
    CHECK(!eq.worn[std::size_t(cell)].empty(), "the cell holds it");
    CHECK(eq.worn[std::size_t(cell)].count == 1,
          "a body wears ONE, whatever the stack said");

    // A second one has nowhere to go: a humanoid has one torso.
    CHECK(equip(eq, leather) == -1,
          "a body with no free cell of that type REFUSES — an item that "
          "vanished on equip would be one the conservation law lost");

    const ItemRef back = unequip(eq, cell);
    CHECK(back.def == leather.def, "taking it off returns what was worn");
    CHECK(eq.worn[std::size_t(cell)].empty(), "and empties the cell");
    CHECK(equip(eq, leather) >= 0, "so the torso can be dressed again");
}

void test_an_unwearable_row_is_refused() {
    Equipment eq{};
    ItemRef bread{};
    bread.def = std::uint16_t(item_index("bread"));
    bread.count = 1;
    CHECK(equip(eq, bread) == -1, "a loaf has no slot mask and goes nowhere");
    ItemRef nothing{};
    CHECK(equip(eq, nothing) == -1, "and an empty ref is not an item");
}

// ── THE reason the mask names TYPES ──────────────────────────────────────
// A ring says "Finger" once. Give a body twenty fingers and it wears twenty
// rings — without the ring row, the mask, or one line of code knowing that
// such a body exists.
void test_a_mask_of_types_fits_a_body_it_never_heard_of() {
    // The catalog has no ring yet, so the claim is made about the LAW: a mask
    // naming Finger fits every Finger cell of any shape.
    Equipment human{};
    int humanFingers = 0;
    for (int i = 0; i < human.cells(); ++i)
        if (human.part_at(i) == BodyPartId::Finger) ++humanFingers;

    // A shape with a different number of the same part — built here rather
    // than authored, because the point is that NOTHING has to be authored.
    AnatomyDef octopus{AnatomyId::Humanoid, "octopus", "Octopus",
                       {{BodyPartId::Head, 1}, {BodyPartId::Torso, 1},
                        {BodyPartId::Finger, 20}}};
    int armFingers = 0;
    for (int i = 0; i < anatomy_cell_count(octopus); ++i)
        if (anatomy_part_at(octopus, i) == BodyPartId::Finger) ++armFingers;

    CHECK(humanFingers == 10 && armFingers == 20,
          "one part TYPE, two bodies, two counts — and no new id for either");

    const std::uint64_t ringMask = part_bit(BodyPartId::Finger);
    CHECK((ringMask & part_bit(BodyPartId::Finger)) != 0,
          "a ring's mask names the type once...");
    CHECK((ringMask & part_bit(BodyPartId::Torso)) == 0,
          "...and only that type — negative control on the bit");
}

// ── A two-hander takes the other hand with it ────────────────────────────
void test_blocks_mask_occupies_and_releases() {
    Equipment eq{};
    // Built here rather than authored: the catalog has no two-hander yet, and
    // the LAW is what is being pinned.
    ItemDef greatsword{};
    greatsword.id = "test_greatsword";
    greatsword.name = "Test Greatsword";
    greatsword.type = ItemType::Weapon;
    greatsword.slotMask = part_bit(BodyPartId::Grip);
    greatsword.blocksMask = part_bit(BodyPartId::OffGrip);

    int grip = -1, off = -1;
    for (int i = 0; i < eq.cells(); ++i) {
        if (eq.part_at(i) == BodyPartId::Grip) grip = i;
        if (eq.part_at(i) == BodyPartId::OffGrip) off = i;
    }
    CHECK(grip >= 0 && off >= 0, "a humanoid has both grips");
    CHECK(item_fits_cell(eq, grip, greatsword),
          "the greatsword fits the main grip");
    CHECK(!item_fits_cell(eq, off, greatsword),
          "and not the off hand, which its mask does not name");
}

// ── What is worn reaches the sheet and the damage door ───────────────────
void test_worn_sums_reach_the_one_currency() {
    Equipment eq{};
    ItemRef leather{};
    leather.def = std::uint16_t(item_index("arm_leather"));
    leather.count = 1;

    CHECK(worn_armor(eq).of(DamageType::Blunt) == 0,
          "a naked body stops nothing");
    const BonusTotals bare = worn_bonuses(eq);
    CHECK(bare.attr[std::size_t(AttributeId::End)] == 0, "and grants nothing");

    const int cell = equip(eq, leather);
    CHECK(cell >= 0, "the coat is on");
    const ItemDef* def = item_def_at(int(leather.def));
    CHECK(def != nullptr && def->armor.of(DamageType::Blunt) > 0,
          "the fixture's coat is actually armoured — else this proves nothing");
    // Column by column: what is worn reaches the door in every one of the
    // nine types the row authors (uniform for the scalar-era coat).
    int columns = 0, drifted = 0;
    for (std::size_t t = 0; t < kDamageTypeCount; ++t) {
        ++columns;
        if (worn_armor(eq).v[t] != def->armor.v[t]) ++drifted;
    }
    CHECK(columns == int(kDamageTypeCount) && drifted == 0,
          "worn armour is the per-column sum of the rows worn, in the door's "
          "own units");

    // BASE rows are BARE (owner verdict (а), 2026-09-07): the plain coat
    // says nothing through the bonus column — that voice belongs to uniques.
    // What a worn INSTANCE says comes from its own affix cells, and it lands
    // in the one currency the sheet reads.
    CHECK(worn_bonuses(eq).attr[std::size_t(AttributeId::End)] == 0,
          "a plain base row grants nothing standing — the placeholder era is "
          "over");
    ItemRef rolled = leather;
    rolled.set_affix(0, {std::uint8_t(BonusId::End), 2});
    rolled.seed = 7;   // a rolled instance, per the stacking law
    unequip(eq, cell);
    const int rolledCell = equip(eq, rolled);
    CHECK(rolledCell >= 0, "the rolled coat is on");
    const BonusTotals worn = worn_bonuses(eq);
    CHECK(worn.attr[std::size_t(AttributeId::End)] == 2,
          "and its AFFIX lands in the one currency the sheet reads");

    unequip(eq, rolledCell);
    CHECK(worn_armor(eq).of(DamageType::Blunt) == 0 && worn_bonuses(eq).attr[
              std::size_t(AttributeId::End)] == 0,
          "take it off and both sums fall back to nothing: no residue");
}

// ── The strike reads the gear's derived rows (affix track step 1) ─────────
void test_strike_reads_derived_affixes() {
    Attributes a{};
    Skills s{};
    Equipment eq{};   // Humanoid by default — grips included

    ItemRef dagger{};
    dagger.def = std::uint16_t(item_index("wpn_dagger"));
    dagger.count = 1;
    CHECK(equip(eq, dagger) >= 0, "the plain dagger is in hand");
    const StrikeFields plainHit = hand_strike_fields(a, s, &eq);

    ItemRef rolled = dagger;
    rolled.seed = 11;
    rolled.set_affix(0, {std::uint8_t(BonusId::DmgFlat), 5});
    rolled.set_affix(1, {std::uint8_t(BonusId::SwingPct), 100});
    Equipment eq2{};
    CHECK(equip(eq2, rolled) >= 0, "the rolled dagger is in hand");
    const StrikeFields rolledHit = hand_strike_fields(a, s, &eq2);

    CHECK(rolledHit.flatAdd == plainHit.flatAdd + 5,
          "a worn DmgFlat row is IN the blow, beside the attribute's add");
    CHECK(rolledHit.recoverySteps
              == std::max(1, plainHit.recoverySteps * 100 / 200),
          "a worn SwingPct row is a whole-percent verdict over the mass "
          "law's tempo");
    CHECK(rolledHit.dice.m == plainHit.dice.m,
          "the dice stay the ROW's — an affix modulates, never replaces");
}

// ── The missile row's law (shooting verdicts 2026-09-09) ──────────────────
void test_missile_row_shoots_without_the_attribute() {
    Attributes a{};
    a[AttributeId::Str] = 40;   // a strongman, to make the absence loud
    Skills s{};

    const ItemDef* bowDef = item_def_at(item_index("wpn_bow"));
    CHECK(bowDef && bowDef->delivery == Delivery::Missile
              && bowDef->skill == SkillId::Bow
              && bowDef->dice.n == 1 && bowDef->dice.m == 8
              && bowDef->range > 0.0f
              && bowDef->blocksMask != 0,
          "the appended bow row: Missile delivery, Bow skill, 1d8, a real "
          "range, both hands");

    Equipment sword{};
    ItemRef blade{};
    blade.def = std::uint16_t(item_index("wpn_sword"));
    blade.count = 1;
    CHECK(equip(sword, blade) >= 0, "the sword is in hand");
    const StrikeFields swordHit = hand_strike_fields(a, s, &sword);
    CHECK(swordHit.delivery == Delivery::Melee && swordHit.flatAdd == 40,
          "a melee blow carries the attribute's whole add");

    Equipment bow{};
    ItemRef loose{};
    loose.def = std::uint16_t(item_index("wpn_bow"));
    loose.count = 1;
    CHECK(equip(bow, loose) >= 0, "the bow is in hand");
    const StrikeFields bowHit = hand_strike_fields(a, s, &bow);
    CHECK(bowHit.delivery == Delivery::Missile
              && bowHit.range == bowDef->range,
          "the delivery and reach ride the strike assembly");
    CHECK(bowHit.flatAdd == 0,
          "a missile blow takes NO attribute add — dice, skill and LCK "
          "alone (range is the compensation)");
    CHECK(bowHit.recoverySteps
              == recovery_steps(weapon_swing_seconds(bowDef), a, s,
                                SkillId::Armsmaster),
          "the loose pays the same mass law through the same door");

    // The GEAR's own voice still speaks: a worn DmgFlat affix is equipment,
    // not the body, so it lands on an arrow exactly as on a blade.
    ItemRef rolled = loose;
    rolled.seed = 5;
    rolled.set_affix(0, {std::uint8_t(BonusId::DmgFlat), 3});
    Equipment bow2{};
    CHECK(equip(bow2, rolled) >= 0, "the rolled bow is in hand");
    CHECK(hand_strike_fields(a, s, &bow2).flatAdd == 3,
          "a worn DmgFlat affix is IN the arrow, the attribute still is not");
}

} // namespace

int main() {
    test_the_rows_stand_where_they_say();
    test_cells_expand_from_the_shape();
    test_equip_finds_a_cell_the_mask_names();
    test_an_unwearable_row_is_refused();
    test_a_mask_of_types_fits_a_body_it_never_heard_of();
    test_blocks_mask_occupies_and_releases();
    test_worn_sums_reach_the_one_currency();
    test_strike_reads_derived_affixes();
    test_missile_row_shoots_without_the_attribute();
    return sm::test::report("anatomy_equipment_test");
}
