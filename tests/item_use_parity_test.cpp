#include "check.h"
#include "macro/items.h"

#include <cstdio>
#include <string>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/item_use_parity_test.cpp", 0);
    return 1;
}

bool expect(bool ok, const char* msg) {
    if (!ok) {
        fail(msg);
        return false;
    }
    return true;
}

bool test_hp_potion_clamps_and_removes_one() {
    sm::Inventory inv;
    inv.add("potion_hp", 2);
    sm::PlayerCombatSlice pc{95, 100, 12, 50, 7, 30};

    const std::string msg = sm::use_item(inv, "potion_hp", pc);
    return expect(msg == "Used Health Potion: +5 HP",
                  "hp potion message does not match TS clamp")
        && expect(pc.currentHp == 100 && pc.currentMp == 12 && pc.currentSp == 7,
                  "hp potion mutated wrong combat field")
        && expect(inv.count("potion_hp") == 1,
                  "hp potion did not remove exactly one item");
}

bool test_full_resource_still_uses_consumable_with_zero_message() {
    sm::Inventory inv;
    inv.add("potion_mp", 1);
    sm::PlayerCombatSlice pc{50, 100, 40, 40, 9, 30};

    const std::string msg = sm::use_item(inv, "potion_mp", pc);
    return expect(msg == "Used Mana Potion: +0 MP",
                  "full MP potion should report +0 MP like TS")
        && expect(pc.currentMp == 40,
                  "full MP potion changed capped MP")
        && expect(inv.count("potion_mp") == 0,
                  "full MP potion was not consumed");
}

bool test_food_and_non_consumables() {
    sm::Inventory inv;
    inv.add("bread", 1);
    inv.add("wood", 3);
    sm::PlayerCombatSlice pc{80, 100, 20, 40, 10, 30};

    const std::string food = sm::use_item(inv, "bread", pc);
    const std::string material = sm::use_item(inv, "wood", pc);
    const std::string missing = sm::use_item(inv, "missing_id", pc);
    return expect(food == "Used Bread: +10 HP",
                  "food message/effect does not match TS")
        && expect(pc.currentHp == 90,
                  "food did not restore HP")
        && expect(inv.count("bread") == 0,
                  "food was not consumed")
        && expect(material.empty() && inv.count("wood") == 3,
                  "non-consumable item was used or removed")
        && expect(missing.empty(),
                  "missing item returned a message");
}

} // namespace

int main() {
    if (!test_hp_potion_clamps_and_removes_one()) return 1;
    if (!test_full_resource_still_uses_consumable_with_zero_message()) return 1;
    if (!test_food_and_non_consumables()) return 1;

    std::printf("OK item_use_parity_test hp=ok full=ok food=ok non_consumable=ok\n");
    CHECK(true, "every gate above held");
    return sm::test::report("item_use_parity_test");
}
