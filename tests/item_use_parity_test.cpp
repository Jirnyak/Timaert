// Locks what USING an item does: the message tells the truth about what
// actually happened, the right pool moves, and the item is spent.
//
// The shape here was the worst of the `fail()` family. Every fact was an
// `expect(...)` chained with `&&`, so the chain SHORT-CIRCUITED: the first
// broken fact hid every one behind it, and a run learned one thing per fix.
// `main` then spelled its verdict `return 1`, walking straight past
// `report()` — a verdict carried by a return value, which is the exact shape
// §8 п.1 exists to forbid. The file's only COUNTED check was the un-failable
// `CHECK(true, "every gate above held")` at the bottom.
#include "check.h"
#include "macro/items.h"

#include <string>

namespace {

void test_hp_potion_reports_what_it_restored() {
    sm::Inventory inv;
    inv.add("potion_hp", 2);
    sm::PlayerCombatSlice pc{95, 100, 12, 50, 7, 30};

    const std::string msg = sm::use_item(inv, "potion_hp", pc);
    CHECK(msg == "Used Health Potion: +5 HP",
          "the message reports what was ACTUALLY restored, not the potion's "
          "nominal value — at 95/100 a big potion still says +5");
    CHECK(pc.currentHp == 100, "the HP potion fills HP to its cap");
    CHECK(pc.currentMp == 12 && pc.currentSp == 7,
          "...and touches no other pool — one potion, one column");
    CHECK(inv.count("potion_hp") == 1,
          "exactly ONE potion left the bag, not the whole stack");
}

void test_a_useless_potion_is_still_spent() {
    sm::Inventory inv;
    inv.add("potion_mp", 1);
    sm::PlayerCombatSlice pc{50, 100, 40, 40, 9, 30};

    const std::string msg = sm::use_item(inv, "potion_mp", pc);
    CHECK(msg == "Used Mana Potion: +0 MP",
          "at full MP the potion honestly reports +0 rather than its nominal");
    CHECK(pc.currentMp == 40, "and cannot push a pool past its cap");
    CHECK(inv.count("potion_mp") == 0,
          "and is SPENT anyway — use costs the item whether or not it helped");
}

void test_food_heals_and_materials_do_nothing() {
    sm::Inventory inv;
    inv.add("food", 1);
    inv.add("wood", 3);
    sm::PlayerCombatSlice pc{80, 100, 20, 40, 10, 30};

    const std::string food = sm::use_item(inv, "food", pc);
    const std::string material = sm::use_item(inv, "wood", pc);
    const std::string missing = sm::use_item(inv, "missing_id", pc);

    CHECK(food == "Used Provisions: +5 HP",
          "provisions heal by the value their row names");
    // 80 → 85: строка ПИЩИ лечит на свою стоимость (5). Прототип на TS
    // кормил игрока ХЛЕБОМ (+10) — хлеб вырезан из мира 2026-09-20, и
    // закон держится на строке каталога, а не на исчезнувшей реализации.
    CHECK(pc.currentHp == 85, "and the pool moved by exactly that much");
    CHECK(inv.count("food") == 0, "and the ration was eaten");
    // NEGATIVE CONTROL, asserted: a material and a name that is not in the
    // catalogue at all must both be inert. Without these the three checks
    // above could be passing because `use_item` consumes ANYTHING it is
    // handed and reports on it.
    CHECK(material.empty(), "a material is not a consumable and says nothing");
    CHECK(inv.count("wood") == 3, "...and stays in the bag, all three of it");
    CHECK(missing.empty(), "an id the catalogue does not know does nothing");
}

} // namespace

int main() {
    test_hp_potion_reports_what_it_restored();
    test_a_useless_potion_is_still_spent();
    test_food_heals_and_materials_do_nothing();
    return sm::test::report("item_use_parity_test");
}
