// GEAR (M-183): СЛОТ = тип×16+n ГЛОБАЛЬНО, «ЕСТЬ ЛИ СЛОТ» = БИТ МАСКИ,
// НАДЕТОЕ = ИНДЕКС В ИНВЕНТАРЬ.
//
// Вердикты владельца 2026-09-28: «наверное анатоми и не нужна надо просто
// вшить её в анкеты сквадов … на каждый тип слота по сколько то слотов тогда
// можно вообще плоским набором 0 и 1»; «надо ещё знать что одето но это я
// думаю можно просто указатель на место в инвентаре»; «отлично индекс
// идеально». Истина вещи ОДНА — инвентарь; ячейка тела лишь указывает.
// Маска предмета по-прежнему называет ТИПЫ частей (кольцо говорит «Finger»
// один раз — и подходит и человеку, и спруту), а «сколько таких частей у
// тела» — биты маски: отрастить конечность = поставить бит, в рантайме.
#include "check.h"

#include "macro/anketa.h"
#include "tables/body_parts.h"
#include "tables/items.h"

#include <cstdio>

namespace {

using namespace sm;

// ── Словарь типов и раскладка ────────────────────────────────────────────
void test_the_rows_stand_where_they_say() {
    for (int i = 0; i < int(BodyPartId::Count); ++i) {
        const BodyPartDef& d = body_part_def(BodyPartId(i));
        CHECK(int(d.id) == i, "every part row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "and names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
    }
    // Адрес слота — сдвиг, и он обратим: тип восстанавливается из адреса.
    int checked = 0;
    for (int t = 0; t < int(BodyPartId::Count); ++t) {
        for (int n = 0; n < kSlotsPerPartType; ++n) {
            const int cell = equip_cell(BodyPartId(t), n);
            if (equip_cell_part(cell) == BodyPartId(t)) ++checked;
        }
    }
    CHECK(checked == kEquipCells,
          "тип×16+n и обратно — раскладка обратима на всех ячейках");
}

// ── Маска тела: бит есть — слот есть ─────────────────────────────────────
void test_the_mask_is_the_body() {
    Gear g{};
    Inventory bag{};
    gear_init(g, kHumanoidSlots);
    CHECK(g.has.count() == 42, "the humanoid mask carries its 42 slots");

    ItemRef coat{};
    coat.def = std::uint16_t(item_index("arm_leather"));
    coat.count = 1;
    CHECK(bag.add_ref(coat), "the bag takes the coat");
    const int coatSlot = 0;
    CHECK(!bag.slots[0].empty(), "…в первый предметный слот");

    const int cell = equip(g, bag, coatSlot);
    CHECK(cell >= 0, "the coat goes on a torso-typed cell");
    CHECK(equip_cell_part(cell) == BodyPartId::Torso,
          "…и именно на Torso — тип назван маской строки");
    CHECK(g.worn[std::size_t(cell)] == std::uint16_t(coatSlot),
          "ячейка хранит ИНДЕКС слота инвентаря, не копию вещи");
    CHECK(!bag.slots[0].empty(),
          "истина одна: вещь ОСТАЛАСЬ в инвентаре после надевания");
    CHECK(slot_is_worn(g, coatSlot), "и слот числится надетым");

    // Второй такой же плащ СТАКУЕТСЯ в надетый слот (инвентарь не знает о
    // теле); второго торса у человека нет — надеть второй из стака нельзя,
    // и вещь остаётся на месте.
    ItemRef coat2 = coat;
    CHECK(bag.add_ref(coat2), "второй плащ ложится в сумку (стаком)");
    CHECK(bag.slots[std::size_t(coatSlot)].count == 2,
          "…в ТОТ ЖЕ слот: у стака одна строка");
    CHECK(equip(g, bag, coatSlot) == -1,
          "второго торса нет — тело отказывает, стак не тронут");
    CHECK(bag.slots[std::size_t(coatSlot)].count == 2,
          "…и ни один плащ не испарился");

    // Снятие: вещь никуда не движется, указатель гаснет.
    CHECK(unequip(g, bag, cell), "the coat comes off");
    CHECK(g.worn[std::size_t(cell)] == kWornNothing, "ячейка пуста");
    CHECK(!bag.slots[0].empty(), "вещь как лежала в сумке, так и лежит");
    CHECK(!slot_is_worn(g, coatSlot), "и слот больше не числится надетым");
}

// ── Тело без слота отказывает; отращенный бит — надевается ──────────────
void test_growing_a_limb_is_a_bit() {
    Gear g{};
    Inventory bag{};
    gear_init(g, kSerpentSlots);   // у змея нет хвата (Grip)
    ItemRef dagger{};
    dagger.def = std::uint16_t(item_index("wpn_dagger"));
    dagger.count = 1;
    CHECK(bag.add_ref(dagger), "the serpent's bag takes the dagger");
    CHECK(equip(g, bag, 0) == -1,
          "no grip slot — the serpent refuses the dagger");
    // Бесплатный редактор тел: отрастить хват = поставить бит. В рантайме,
    // без новой строки контента (вердикт владельца).
    g.has.set(equip_cell(BodyPartId::Grip, 0));
    CHECK(equip(g, bag, 0) >= 0, "…и с отращенной рукой кинжал надевается");
}

// ── Двуручник: блок производный, снятие освобождает ──────────────────────
void test_two_hander_blocks() {
    Gear g{};
    Inventory bag{};
    gear_init(g, kHumanoidSlots);
    ItemRef spear{};
    spear.def = std::uint16_t(item_index("wpn_spear"));
    spear.count = 1;
    ItemRef dagger{};
    dagger.def = std::uint16_t(item_index("wpn_dagger"));
    dagger.count = 1;
    CHECK(bag.add_ref(spear) && bag.add_ref(dagger), "оба в сумке");
    int spearSlot = -1, daggerSlot = -1;
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        const ItemRef& s = bag.slots[std::size_t(i)];
        if (s.empty()) continue;
        if (int(s.def) == item_index("wpn_spear")) spearSlot = i;
        if (int(s.def) == item_index("wpn_dagger")) daggerSlot = i;
    }
    CHECK(spearSlot >= 0 && daggerSlot >= 0, "оба слота найдены");

    const int cell = equip(g, bag, spearSlot);
    CHECK(cell >= 0, "the spear goes into the grip");
    const int off = equip_cell(BodyPartId::OffGrip, 0);
    CHECK(g.worn[std::size_t(off)] == kWornBlocked,
          "…и занимает вторую руку производным маркером");
    CHECK(equip(g, bag, daggerSlot) == -1,
          "кинжалу некуда: обе руки заняты копьём");
    CHECK(unequip(g, bag, cell), "копьё снимается");
    CHECK(g.worn[std::size_t(off)] == kWornNothing,
          "…и блок отпускается вместе с ним");
    CHECK(equip(g, bag, daggerSlot) >= 0, "теперь кинжал надевается");
}

// ── Сумма стоящего и брони читается ИЗ инвентаря ─────────────────────────
void test_worn_sums_read_the_bag() {
    Gear g{};
    Inventory bag{};
    gear_init(g, kHumanoidSlots);
    ItemRef coat{};
    coat.def = std::uint16_t(item_index("arm_leather"));
    coat.count = 1;
    coat.set_affix(0, Bonus{std::uint8_t(BonusId::End), 4});
    CHECK(bag.add_ref(coat), "плащ с аффиксом в сумке");
    const int cell = equip(g, bag, 0);
    CHECK(cell >= 0, "и на теле");

    const BonusTotals t = worn_bonuses(g, bag);
    CHECK(int(t.attr[std::size_t(AttributeId::End)]) == 4,
          "аффикс надетого читается из ИНВЕНТАРЯ по индексу");
    const ItemDef* def = item_def_at(item_index("arm_leather"));
    const DefenseSum worn =
        def != nullptr ? worn_defense(g, bag, Skills{}, DamageType::Blunt)
                       : DefenseSum{};
    CHECK(def != nullptr
              && worn.armor == def->defense.armor_of(DamageType::Blunt)
              && worn.block == def->defense.block_of(DamageType::Blunt),
          "ОБЕ колонки надетого — колонки строки, прочитанные той же дорогой");
    CHECK(weapon_in_hand(g, bag) == nullptr, "плащ — не оружие в руке");

    // Негативный контроль протухшего индекса: вещь исчезла из инвентаря —
    // перерасчёт гасит ячейку, бонус исчезает.
    bag.slots[std::size_t(g.worn[std::size_t(cell)])] = ItemRef{};
    remark_gear_blocks(g, bag);
    CHECK(g.worn[std::size_t(cell)] == kWornNothing,
          "протухший индекс зануляется перерасчётом");
    CHECK(int(worn_bonuses(g, bag).attr[std::size_t(AttributeId::End)]) == 0,
          "…и сумма стоящего больше его не видит");
}

// ── Стак расщепляется: ячейка держит ровно один экземпляр ────────────────
void test_stack_splits_on_equip() {
    Gear g{};
    Inventory bag{};
    gear_init(g, kHumanoidSlots);
    ItemRef daggers{};
    daggers.def = std::uint16_t(item_index("wpn_dagger"));
    daggers.count = 3;
    CHECK(bag.add_ref(daggers), "стак из трёх кинжалов в сумке");
    const int cell = equip(g, bag, 0);
    CHECK(cell >= 0, "один из них надевается");
    const std::uint16_t wornSlot = g.worn[std::size_t(cell)];
    CHECK(wornSlot != 0, "…расщеплённый В СВОЙ слот, не весь стак");
    CHECK(bag.slots[0].count == 2 && bag.slots[std::size_t(wornSlot)].count == 1,
          "стак похудел на единицу, ячейка держит ровно один экземпляр");
    CHECK(bag.count_of(item_index("wpn_dagger")) == 3,
          "…и ни один кинжал не испарился (3 == 3)");
}

} // namespace

int main() {
    test_the_rows_stand_where_they_say();
    test_the_mask_is_the_body();
    test_growing_a_limb_is_a_bit();
    test_two_hander_blocks();
    test_worn_sums_read_the_bag();
    test_stack_splits_on_equip();
    return sm::test::report("gear_equipment_test");
}
