// ОДИН ГЛАГОЛ ОБМЕНА — `deal` (ломоть 2 M-72). Что охраняется — ЗАКОНЫ:
//   · лот — НАМЕРЕНИЕ «до N»: дверь клампит до исполнимого (исток, слоты
//     и спина приёмника) и возвращает ФАКТ в строках;
//   · exact-строка («ровно N»), не покрытая миром, откатывает ВСЮ сделку —
//     обе половины, мир не тронут;
//   · СОХРАНЕНИЕ: сделка ничего не минтит и не испаряет — сумма единиц
//     каждой строки по обоим контейнерам до == после, всегда;
//   · перенос = сделка с пустым встречным лотом; души и предметы едут
//     ОДНОЙ дверью (перенос агностичен — вердикт 2026-10-23);
//   · подмена строки лотом (asDef): душа прибывает другой строкой БЕЗ
//     рукописного перерождения, счёт сохраняется;
//   · протухшая слот-строка (def разошёлся со стаком) не везёт чужой груз;
//   · долг места гасится приходом СРАЗУ (CANON S10) — у обеих сторон;
//   · стейджинг встречного лота по закону плотности (stage_value_lines)
//     покрывает запрошенную стоимость строками exact.
//
// ПАСТВЫ ЗДЕСЬ НЕТ ПО ПОСТРОЕНИЮ (вердикт владельца 2026-10-23: «паства
// это только ответ АИ модуля») — у двери НЕТ GameState в подписи, трогать
// поля мира ей НЕЧЕМ; стена стоит подписью, не проверкой.
//
// Негативные контроли — ТАБЛИЦЕЙ МУТАЦИЙ (§8 п.6) в теле коммита ломтя.
#include "check.h"

#include <memory>

#include "macro/anketa.h"
#include "macro/deal.h"
#include "macro/store.h"
#include "macro/world_row.h"
#include "tables/commodity.h"
#include "tables/items.h"
#include "tables/npc.h"

namespace {

using namespace sm;

// Свидетель рожает предусловия САМ (§8 п.11): стор и две стороны.
struct World {
    std::unique_ptr<MacroStore> store;
    std::uint16_t a = 1;   // слот 0 служебен у ординалов — не трогаем
    std::uint16_t b = 2;
};

World make_world() {
    World w{make_macro_store(), 1, 2};
    return w;
}

// Сумма единиц строки по обоим контейнерам — якорь сохранения.
int total_of(const World& w, int defIdx) {
    return w.store->inventory[w.a].inv.count_of(defIdx)
         + w.store->inventory[w.b].inv.count_of(defIdx);
}

void test_transfer_clamps_to_fact() {
    World w = make_world();
    const int food = item_index("food");
    CHECK(food >= 0, "catalog knows food");
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(A.add_of(food, 10), "fixture seeds 10 food");
    const int before = total_of(w, food);

    DealLine give[] = {{/*want=*/25, 0, std::uint16_t(food)}};
    const DealFact f = deal(*w.store, w.a, w.b, give, {});
    CHECK(f.settled, "clamped transfer settles");
    CHECK(f.unitsAB == 10, "intent 25 clamps to the 10 the source holds");
    CHECK(give[0].done == 10, "the line carries the FACT");
    CHECK(w.store->inventory[w.b].inv.count_of(food) == 10, "cargo arrived");
    CHECK(A.count_of(food) == 0, "source paid exactly the fact");
    CHECK(total_of(w, food) == before, "conservation: nothing minted");
}

void test_exact_refusal_rolls_back_both_halves() {
    World w = make_world();
    const int food = item_index("food");
    const int wood = item_index("wood");
    Inventory& A = w.store->inventory[w.a].inv;
    Inventory& B = w.store->inventory[w.b].inv;
    CHECK(A.add_of(food, 10), "A seeds 10 food");
    CHECK(B.add_of(wood, 7), "B seeds 7 wood");

    // Встречная exact-строка просит больше, чем у B есть: вся сделка —
    // отказ, ПЕРВАЯ половина (еда A→B) тоже не едет.
    DealLine give[] = {{/*want=*/10, 0, std::uint16_t(food), kDealAnyStack,
                        0, /*exact=*/1}};
    DealLine take[] = {{/*want=*/8, 0, std::uint16_t(wood), kDealAnyStack,
                        0, /*exact=*/1}};
    const DealFact f = deal(*w.store, w.a, w.b, give, take);
    CHECK(!f.settled, "uncovered exact line refuses the whole deal");
    CHECK(f.unitsAB == 0 && f.unitsBA == 0, "no units moved");
    CHECK(give[0].done == 0 && take[0].done == 0, "facts zeroed on refusal");
    CHECK(A.count_of(food) == 10 && B.count_of(wood) == 7,
          "world untouched after refusal");
}

void test_two_sided_exact_deal_settles_whole() {
    World w = make_world();
    const int food = item_index("food");
    const int wood = item_index("wood");
    Inventory& A = w.store->inventory[w.a].inv;
    Inventory& B = w.store->inventory[w.b].inv;
    CHECK(A.add_of(food, 10), "A seeds food");
    CHECK(B.add_of(wood, 7), "B seeds wood");

    DealLine give[] = {{5, 0, std::uint16_t(food), kDealAnyStack, 0, 1}};
    DealLine take[] = {{3, 0, std::uint16_t(wood), kDealAnyStack, 0, 1}};
    const DealFact f = deal(*w.store, w.a, w.b, give, take);
    CHECK(f.settled && f.unitsAB == 5 && f.unitsBA == 3,
          "both exact halves settle atomically");
    CHECK(A.count_of(food) == 5 && A.count_of(wood) == 3,
          "A paid food, received wood");
    CHECK(B.count_of(food) == 5 && B.count_of(wood) == 4,
          "B paid wood, received food");
}

void test_mass_cap_clamps_arrival() {
    World w = make_world();
    const int wood = item_index("wood");   // 2000 г за единицу (каталог)
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(A.add_of(wood, 10), "A seeds 10 wood");
    const std::int64_t unitG = unit_mass_g(item_def_at(wood));
    CHECK(unitG > 0, "catalog mass is positive by the item_mass_g guard");

    DealLine give[] = {{10, 0, std::uint16_t(wood)}};
    // Спина приёмника вмещает ровно 3 единицы — намерение 10 клампится.
    const DealFact f =
        deal(*w.store, w.a, w.b, give, {}, /*massCapToB=*/unitG * 3);
    CHECK(f.settled && f.unitsAB == 3, "mass cap clamps 10 -> 3");
    CHECK(give[0].done == 3, "fact is the clamped count");
    CHECK(A.count_of(wood) == 7, "source keeps what the back refused");
}

void test_souls_travel_by_the_same_door() {
    World w = make_world();
    const std::uint16_t peasant = world_row_of_creature(NPCType::Peasant);
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(creatures_push_stack(A, NPCType::Peasant,
                               npc_def(NPCType::Peasant).baseLevel, 6),
          "A seeds 6 peasants");
    const int before = total_of(w, int(peasant));

    DealLine give[] = {{4, 0, peasant}};
    const DealFact f = deal(*w.store, w.a, w.b, give, {});
    CHECK(f.settled && f.unitsAB == 4, "souls move as objects, same verb");
    CHECK(w.store->inventory[w.b].inv.count_of(int(peasant)) == 4,
          "4 heads stand at B");
    CHECK(A.count_of(int(peasant)) == 2, "2 heads stayed at A");
    CHECK(total_of(w, int(peasant)) == before, "no soul minted or lost");
}

void test_asdef_rebirth_conserves_heads() {
    World w = make_world();
    const std::uint16_t peasant = world_row_of_creature(NPCType::Peasant);
    const std::uint16_t guard = world_row_of_creature(NPCType::Guard);
    CHECK(peasant != guard, "two distinct creature rows");
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(creatures_push_stack(A, NPCType::Peasant,
                               npc_def(NPCType::Peasant).baseLevel, 5),
          "A seeds 5 peasants");

    DealLine give[] = {{3, 0, peasant, kDealAnyStack, /*asDef=*/guard}};
    const DealFact f = deal(*w.store, w.a, w.b, give, {});
    CHECK(f.settled && f.unitsAB == 3, "rebirth lot settles");
    CHECK(w.store->inventory[w.b].inv.count_of(int(guard)) == 3,
          "souls arrive as the lot's row - no handwritten rebirth");
    CHECK(A.count_of(int(peasant)) == 2, "source row paid the fact");
    CHECK(w.store->inventory[w.b].inv.count_of(int(peasant)) == 0,
          "nothing arrives under the source row");
}

void test_stale_slot_line_ships_nothing() {
    World w = make_world();
    const int food = item_index("food");
    const int wood = item_index("wood");
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(A.add_of(wood, 5), "slot 0 holds wood");
    // Строка целится в слот 0, но заявляет food — протухла (полку
    // перезаполнили под открытой панелью): чужой груз не едет.
    DealLine give[] = {{5, 0, std::uint16_t(food), /*slot=*/0}};
    const DealFact f = deal(*w.store, w.a, w.b, give, {});
    CHECK(!f.settled && give[0].done == 0, "stale line ships nothing");
    CHECK(A.count_of(wood) == 5, "the stranger cargo stayed home");
}

void test_place_debt_paid_on_arrival() {
    World w = make_world();
    const int food = item_index("food");
    const int foodOrd = commodity_index("food");
    CHECK(foodOrd >= 0, "food is a commodity row");
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(A.add_of(food, 10), "A seeds 10 food");
    w.store->upkeep[w.b].needDebt[foodOrd] = 4;

    DealLine give[] = {{10, 0, std::uint16_t(food)}};
    const DealFact f = deal(*w.store, w.a, w.b, give, {});
    CHECK(f.settled && f.unitsAB == 10, "delivery settles");
    CHECK(w.store->upkeep[w.b].needDebt[foodOrd] == 0,
          "arrival pays the place debt AT ONCE (CANON S10)");
    CHECK(w.store->inventory[w.b].inv.count_of(food) == 6,
          "the shelf keeps only the surplus: 10 - 4 eaten");
}

void test_self_deal_refused() {
    World w = make_world();
    const int food = item_index("food");
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(A.add_of(food, 5), "A seeds food");
    DealLine give[] = {{5, 0, std::uint16_t(food)}};
    const DealFact f = deal(*w.store, w.a, w.a, give, {});
    CHECK(!f.settled && give[0].done == 0, "a side cannot deal with itself");
    CHECK(A.count_of(food) == 5, "nothing moved");
}

void test_stage_value_lines_cover_cost() {
    World w = make_world();
    const int silver = item_index("coin_empire_silver");   // 10 ценности, 10 г
    const int wood = item_index("wood");                   // 5 ценности, 2000 г
    CHECK(silver >= 0, "catalog knows the silver crown");
    Inventory& A = w.store->inventory[w.a].inv;
    CHECK(A.add_of(silver, 3), "A seeds 3 silver (value 30)");
    CHECK(A.add_of(wood, 20), "A seeds 20 wood (value 100)");

    DealLine pay[8];
    const StagedValue sv = stage_value_lines(A, 42, pay);
    CHECK(sv.value >= 42, "staged lines cover the asked value");
    CHECK(sv.lines >= 2,
          "coin alone (30) cannot cover 42 - the density law reaches wood");
    // Монета плотнее дерева (1 ценность/г против 0.0025) — идёт первой.
    CHECK(pay[0].def == std::uint16_t(silver) && pay[0].want == 3,
          "densest stack staged first, whole");

    // Встречный лот едет сделкой целиком: exact-строки staged той же сумки.
    const DealFact f = deal(*w.store, w.a, w.b, {pay, std::size_t(sv.lines)},
                            {});
    CHECK(f.settled, "staged payment settles");
    CHECK(w.store->inventory[w.b].inv.count_of(silver) == 3,
          "all coin travelled");
    const int woodMoved = w.store->inventory[w.b].inv.count_of(wood);
    CHECK(woodMoved * 5 + 30 >= 42, "what travelled covers the cost");
    CHECK(woodMoved < 20, "the last line overpays, it does not drain the bag");
}

void test_receiver_without_slots_refuses() {
    World w = make_world();
    const int food = item_index("food");
    Inventory& A = w.store->inventory[w.a].inv;
    Inventory& B = w.store->inventory[w.b].inv;
    CHECK(A.add_of(food, 5), "A seeds food");
    // Приёмник забит ЧУЖИМИ идентичностями под завязку: каждый слот
    // предметной области — отдельный сеяный стак, стыковать некуда.
    int filled = 0;
    for (std::uint32_t s = 1; B.creature_first() > filled; ++s) {
        ItemRef r{};
        r.def = std::uint16_t(item_index("wood"));
        r.count = 1;
        r.seed = s;   // сеяный ≠ plain: не стакуется (same_kind_as)
        if (!B.add_ref(r)) break;
        ++filled;
    }
    CHECK(filled > 0, "fixture filled the item area");
    DealLine give[] = {{5, 0, std::uint16_t(food)}};
    const DealFact f = deal(*w.store, w.a, w.b, give, {});
    CHECK(!f.settled && give[0].done == 0,
          "a full receiver clamps the lot to zero - nothing settles");
    CHECK(A.count_of(food) == 5, "cargo stayed with the source");
}

} // namespace

int main() {
    test_transfer_clamps_to_fact();
    test_exact_refusal_rolls_back_both_halves();
    test_two_sided_exact_deal_settles_whole();
    test_mass_cap_clamps_arrival();
    test_souls_travel_by_the_same_door();
    test_asdef_rebirth_conserves_heads();
    test_stale_slot_line_ships_nothing();
    test_place_debt_paid_on_arrival();
    test_self_deal_refused();
    test_stage_value_lines_cover_cost();
    test_receiver_without_slots_refuses();
    return sm::test::report("deal_doors_test");
}
