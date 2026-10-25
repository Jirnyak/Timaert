// СВИДЕТЕЛЬ МОДУЛЯ Collector — ЗАВЕДЁН ПОТОМУ, ЧТО ЕГО НЕ БЫЛО ВОВСЕ.
//
// ПОЧЕМУ ОН СУЩЕСТВУЕТ, ЧИСЛОМ. Доделки ломтя 4 (M-72) прогнали записанную
// мутацию «`collector_think` отключён целиком» — и дерево ответило
// **132 из 133 ЗЕЛЁНЫХ**. Весь модуль сбора дани можно было вырезать, и ни
// один прибор проекта этого не замечал: у рода `Collector` свидетелей было
// РОВНО НОЛЬ. Это не дыра в покрытии, а дыра в ЗАКОНЕ — целая макро-машина
// мира существовала без единого утверждения о том, что она делает.
//
// ЧТО ЗДЕСЬ УТВЕРЖДАЕТСЯ — ДВА ЗАКОНА, И ВТОРОЙ КУПЛЕН ЗАМЕРОМ:
//   1) ДАНЬ ДОЕЗЖАЕТ, А ДОЛГ ГАСНЕТ ОДНОЙ ТОЧКОЙ. `collect_tithe` есть одна
//      транзакция мира («сколько увёз, столько и списал»), и шва между
//      «взято» и «зачтено» быть не должно: долг ребра падает, дом
//      прибавляет.
//   2) ГРУЗ БЕРЁТСЯ СНАЧАЛА ПО НУЖДЕ ДОМА, А НЕ ПО ПЛОТНОСТИ СТОИМОСТИ.
//      Вердикт владельца 2026-09-21: «грузит ПО НУЖДЕ ДОМА, остаток — по
//      value_dense_order. Иначе сборщик везёт домой серебро, а меряем мы
//      еду». Цена ошибки замерена 2026-09-22, когда порядок был только по
//      плотности: город получал казну и ПРОДОЛЖАЛ ГОЛОДАТЬ (food_city
//      −99.4 %).
//
// ПОЧЕМУ ВТОРОЙ ЗАКОН ПРОВЕРЯЕМ ИМЕННО ЯКОРЕМ, А НЕ СВЕРКОЙ (§8 п.6, шестая
// форма слепоты): сверить груз с пересчётом той же функции значит проверить
// умение копировать. Здесь утверждается НЕЗАВИСИМО ИЗВЕСТНЫЙ ФАКТ МИРА:
// серебро плотнее еды по стоимости РОВНО В 64 РАЗА (строки `kItemDefs`:
// silver value 1280 при весе 4000 г = 0.32/г; food value 5 при 1000 г =
// 0.005/г). Значит порядок «по плотности» физически НЕ МОЖЕТ привезти ни
// одной еды, пока на складе вассала лежит хоть одно серебро, — и приехавшая
// еда есть доказательство, что грузили по нужде. Якорь считает отношение
// таблиц, а не повторяет формулу продакшена.
//
// ЧТО ЭТОТ ПРИБОР НАШЁЛ В ПЕРВЫЙ ЖЕ ПРОГОН — ЖИВОЙ ДЕФЕКТ, НЕ ПОКРЫТИЕ.
// Дань списывалась с долга, уезжала со склада вассала и НЕ ДОЕЗЖАЛА НИКОГДА:
// `collect_tithe` перенацеливал цель на дом, обнуляя `what`, а гейт должника
// читал должника ИЗ ЦЕЛИ — и гружёный сборщик на первой же думке обратного
// плеча получал «должника нет» и уходил в `home_wander`. Ветка `Returning` с
// `dump_bag_home` была НЕДОСТИЖИМА. Замер: долг 200 → 0, у вассала 40 → 0,
// дома 0, В СПИНЕ 40, сквад жив и бродит на (14,2). Причина — ломоть 4 M-72
// (должник переехал из `rt.errandObject` в колонку цели); чинится в
// `collector_think`, ступень `Returning` поднята ВЫШЕ гейта.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (обе полярности; записана по §8 п.6, потому что
// незаписанный контроль доказывает лишь, что детектор сработал однажды в
// голове автора). Контроль — чистое дерево: OK, 12 проверок.
//   · `collector_think` отключён целиком (тело → `return;`)
//        ДО этого прибора мутация давала 132/133 ЗЕЛЁНЫХ — весь модуль
//        вырезался незамеченным        →  ТЕПЕРЬ: КРАСНО, 7 из 12
//   · `collect_tithe` не списывает долг (`edge->owedValue -= took` → `-= 0`):
//     взял, но не зачёл                →  КРАСНО, 2 из 12
//   · порядок груза сведён к ПЛОТНОСТИ (ветка нужды дома обойдена)
//                                      →  КРАСНО, 2 из 12 (ровно якорь еды)
//   · ОТКАТ ПОЧИНКИ: ступень `Returning` возвращена ПОД гейт должника
//                                      →  КРАСНО, 3 из 12
//   · балансное число: срок ожидания `10 + rand(15)` → `12 + rand(15)`
//                                      →  МОЛЧИТ, OK 12 (детектор,
//        краснеющий на всё, — не детектор)
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/labour.h"        // settle_souls — двери душ
#include "macro/world_row.h"
#include "macro/chronicle.h"
#include "macro/econ_day.h"
#include "macro/squad.h"
#include "macro/store.h"
#include "tables/faction.h"
#include "tables/npc.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 32;

// ДОЛГ ДАНИ СТАВИТСЯ ТОЙ ЖЕ ДВЕРЬЮ, КАКОЙ ЕГО СТАВИТ МИР: ребро Suzerain на
// конце ВАССАЛА, долг — колонка этого ребра (M-245). Фикстура не трогает
// память мимо двери — иначе она сторожила бы раскладку, а не закон.
void owe_tithe(MacroStore& st, int vassalId, int suzerainId,
               long long owedValue) {
    set_relation(st, vassalId, suzerainId, Stance::Suzerain);
    const MacroHandle vh =
        macro_handle_by_spawn_id(st, std::uint32_t(vassalId));
    Interest* edge = tithe_of(st, std::uint16_t(vh.slot));
    CHECK(edge != nullptr,
          "фикстура: ребро дани поставлено дверью мира, а не записью в память");
    if (edge) edge->owedValue = owedValue;
}

// Сборщик — сквад рода Collector: дом ребром Parent, ДОЛЖНИК — КОЛОНКОЙ
// ЦЕЛИ в той же форме {клетка, Place(ординал), 0, Working}, какой её
// поставит сам модуль, решив следующее плечо (ломоть 4 M-72).
MacroHandle make_collector(MacroStore& st, int x, int y,
                           int homeId, MacroHandle vassal, int vassalId,
                           std::uint32_t ordinal) {
    MacroHandle h = store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, kMap)};
    st.sub[h.slot] = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(SquadType::Collector);
    rt.state = std::uint8_t(NPCState::Idle);
    rt.stateTimer = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt,
        make_character_sheet(NPCType::Peasant, 3, leader_sheet_seed(ordinal)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    st.runtime[h.slot] = rt;
    ecs::goal_set(st.goal[h.slot], st.cell[vassal.slot].idx,
                  ecs::goal_what(ecs::GoalKind::Place,
                                 std::uint32_t(vassalId)),
                  0, std::uint8_t(NPCState::Working));
    store_rename_ordinal(st, h, ordinal);
    h = handle_at(st, h.slot);
    set_relation(st, int(h.id), homeId, Stance::Parent);
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, leader_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;
    return h;
}

// Мир фикстуры: столица-дом, деревня-должник, сборщик между ними.
struct Rig {
    GameState gs{};
    std::unique_ptr<MacroStore> store = make_macro_store();
    MacroHandle home{};
    MacroHandle vassal{};
    MacroHandle man{};
    int homeId = 0;
    int vassalId = 0;
};

// `silverLots` = 0 выключает соблазн плотности: тот же прогон без серебра
// отвечает на вопрос «а доехало бы вообще хоть что-то» отдельно от вопроса
// «что выбрано первым».
void build_rig(Rig& r, int foodLots, int silverLots, long long owed) {
    r.gs.mapW = kMap;
    r.gs.mapH = kMap;
    chronicle_init(r.gs.chronicle, kMap, kMap);
    MacroStore& st = *r.store;

    r.home = birth_place(r.gs, st, SquadType::City, 10, 10);
    r.homeId = int(st.spawnId[r.home.slot].index);
    settle_souls(r.gs, st, r.home.slot, 50);
    // ДОМ ГОЛОДЕН СЧЁТОМ (CANON S10): «нет хлеба» есть непогашенный сезонный
    // счёт, и ровно из него модуль читает, чего дому не хватает. Полка еды
    // пуста НАМЕРЕННО — иначе прибавку не отличить от того, что уже лежало.
    st.upkeep[r.home.slot].needDebt[commodity_index("food")] =
        std::int32_t(50 * kDaysPerSeason);

    r.vassal = birth_place(r.gs, st, SquadType::Village, 16, 10);
    r.vassalId = int(st.spawnId[r.vassal.slot].index);
    settle_souls(r.gs, st, r.vassal.slot, 30);
    {
        Inventory& row = st.inventory[r.vassal.slot].inv;
        if (foodLots > 0) row.add("food", foodLots);
        if (silverLots > 0) row.add("silver", silverLots);
    }
    owe_tithe(st, r.vassalId, r.homeId, owed);

    r.man = make_collector(st, 10, 10, r.homeId, r.vassal, r.vassalId, 17u);
}

void run_days(Rig& r, int ticks) {
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < ticks; ++i) {
        MacroWorld mw{.gs = &r.gs, .store = r.store.get()};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
}

long long owed_now(const Rig& r) {
    const Interest* e =
        tithe_of(*r.store, std::uint16_t(r.vassal.slot));
    return e ? e->owedValue : 0;
}

// ГОЛОД ДОМА — СЧЁТ, И ПРИЕХАВШЕЕ ВИДНО ПО НЕМУ, А НЕ ПО ПОЛКЕ (CANON S10).
// Это не мелочь свидетеля, а закон мира: привезённое в голодный дом гасит
// его сезонный счёт и СЪЕДАЕТСЯ в дверях прихода — полка держит только
// излишек. Первая редакция этого прибора смотрела на полку и краснела на
// РАБОТАЮЩЕМ мире: дань доехала, полка 0, счёт 1600 → 1560.
constexpr int kHomeSouls = 50;
constexpr int kHomeFoodBill = kHomeSouls * kDaysPerSeason;

int home_food_bill_paid(const Rig& r) {
    return kHomeFoodBill
        - int(r.store->upkeep[r.home.slot].needDebt[commodity_index("food")]);
}

// СКОЛЬКО ЕДЫ КЛАДЁТ ФИКСТУРА, И ПОЧЕМУ ИМЕННО СТОЛЬКО. Еда весит
// килограмм за единицу при базовой стоимости 5, поэтому долг в 1200
// стоимости есть 240 КГ груза — ровно потолок спины крестьянина. Первый
// прогон этого прибора так и встал: сборщик взял долг под завязку и УМЕР по
// дороге домой (замер: `alive=0 hp=0` на клетке x=11, в шаге от дома, с 240
// кг в спине). Это НЕ дефект мира, а его обратная связь — предел наступает
// SP-долгом, а не стеной (ЗАКОН КЛАМПА), — но для свидетеля это шум: он
// проверяет перенос дани, а не выносливость. Поэтому груз честно лёгкий.
//
// НАЙДЕНО ТЕМ ЖЕ ПРОГОНОМ И НЕ ЧИНИТСЯ ЗДЕСЬ: две половины загрузки
// спрашивают спину ПО-РАЗНОМУ — ветка нужды дома клампит остатком ёмкости
// (`carry_capacity_g - inventory_mass_g`), а остаток по плотности зовёт
// `transfer_value_dense` вовсе без лимита массы. Наряд, а не правка на ходу.
constexpr int kTitheFood = 40;   // 40 кг — треть спины, дорога переживаема
// ЛОТЫ СЕРЕБРА НАЗВАНЫ ЧИСЛОМ С ИМЕНЕМ, ПОТОМУ ЧТО ИХ ЧИТАЮТ ДВОЕ: фикстура,
// которая их кладёт, и проверка убыли у ИСТОЧНИКА, без которой вторая ступень
// загрузки не отличалась бы от «серебро не берут вовсе». Литерал в двух местах
// разъехался бы молча, и проверка снова стала бы тавтологией (§8 п.2, та же
// форма, что `kCityStartFood` у woodcutter_gather_test).
constexpr int kTitheSilverLots = 20;

// ── ЗАКОН 1: дань доезжает, долг гаснет ─────────────────────────────────
void test_the_tithe_is_collected_and_comes_home() {
    Rig r;
    build_rig(r, kTitheFood, /*silverLots*/0, /*owed*/200);
    const long long owedBefore = owed_now(r);
    CHECK(owedBefore > 0, "фикстура: вассал ДОЛЖЕН (условие проверки живо)");

    run_days(r, 600);

    MacroStore& st = *r.store;
    const long long owedAfter = owed_now(r);
    const int homeFood = st.inventory[r.home.slot].inv.count("food");
    const int vassalFood = st.inventory[r.vassal.slot].inv.count("food");

    const int billPaid = home_food_bill_paid(r);

    CHECK(owedAfter < owedBefore,
          "долг ребра ПОГАШЕН взысканием — collect_tithe отработал");
    CHECK(billPaid > 0,
          "дань ДОЕХАЛА ДОМОЙ: привезённое НАКОРМИЛО голод сюзерена — его "
          "сезонный счёт еды упал (полка держит только излишек, CANON S10)");
    CHECK(vassalFood < kTitheFood,
          "у вассала УБЫЛО ровно потому, что увезли, а не напечатали дома");
    // ЯКОРЬ СОХРАНЕНИЯ: взыскание не творит материю и не жжёт её. Спина
    // сборщика считается наравне со складами — груз в пути тоже существует.
    // Сборщик обязан ДОЖИТЬ до конца прогона: мёртвый уронил бы счёт спины
    // молча, поэтому его жизнь утверждается, а не предполагается.
    CHECK(st.alive[r.man.slot] != 0 && st.dead[r.man.slot] == 0,
          "сборщик дожил до конца прогона — иначе счёт спины ниже лжёт");
    const auto* bag = body_state<ecs::NpcInventory>(st, r.man);
    const int inBag = bag ? bag->inv.count("food") : 0;
    // ЧЕТЫРЕ МЕСТА, ГДЕ МОЖЕТ БЫТЬ ЕДА, И БОЛЬШЕ НИГДЕ: склад вассала,
    // полка дома, спина в пути и СЪЕДЕННОЕ ПРОТИВ СЧЁТА. Последнее —
    // полноправный путь материи, а не утечка: дом её проел.
    CHECK(vassalFood + homeFood + inBag + billPaid == kTitheFood,
          "СОХРАНЕНИЕ: у вассала убыло ровно столько, сколько легло дома, "
          "съедено против счёта и едет в спине — дань переносится, а не "
          "чеканится и не исчезает");
}

// ── ЗАКОН 2: ЯКОРЬ — сначала по нужде дома, и только потом по плотности ──
void test_the_tithe_loads_by_home_need_before_value_density() {
    Rig r;
    // РАСКЛАД ПОДОБРАН ТАК, ЧТО ДВА ПОРЯДКА ДАЮТ РАЗНЫЙ ОТВЕТ, И РАЗНИЦА
    // ВИДНА ОДНИМ ЧИСЛОМ. Долг 2000; еды у вассала всего 40 (стоимость 200
    // — ПЯТАЯ ЧАСТЬ долга), серебра 20 лотов (стоимость 25 600 — долг
    // закрывается им в двенадцать раз с запасом).
    //   · по нужде дома: сначала берётся ВСЯ еда (дом голоден счётом),
    //     остаток добирается серебром → homeFood == 40;
    //   · по плотности:  долг закрывается ОДНИМ-ДВУМЯ лотами серебра, еда
    //     не нужна вовсе → homeFood == 0.
    // Еда взята лёгкой (40 кг) намеренно: тяжёлый груз убил бы сборщика по
    // дороге и свидетель краснел бы от выносливости, а не от порядка.
    build_rig(r, kTitheFood, kTitheSilverLots, /*owed*/2000);

    run_days(r, 600);

    MacroStore& st = *r.store;
    const int foodArrived = home_food_bill_paid(r)
        + st.inventory[r.home.slot].inv.count("food");
    const int vassalFoodLeft = st.inventory[r.vassal.slot].inv.count("food");

    // ЯКОРЬ. Утверждение НЕ пересчитывает продакшен-формулу: оно опирается
    // на отношение двух СТРОК каталога (серебро плотнее еды в 64 раза) и на
    // то, что дом голоден счётом. При загрузке по плотности еды приехало бы
    // РОВНО НОЛЬ при любом раскладе — долг закрывается серебром с запасом в
    // двенадцать раз, и трогать килограммовую еду плотности незачем.
    CHECK(foodArrived > 0,
          "ГРУЗ ВЗЯТ ПО НУЖДЕ ДОМА: голодный дом получил ЕДУ, хотя серебро "
          "в 64 раза плотнее по стоимости и закрыло бы долг одно");
    CHECK(vassalFoodLeft == 0,
          "нужда дома выбрана ПЕРВОЙ и ЦЕЛИКОМ: вся еда вассала уехала "
          "раньше, чем долг стали добирать серебром");
    // ── ВТОРАЯ СТУПЕНЬ СПРАШИВАЕТСЯ У ИСТОЧНИКА, А НЕ У ПОЛКИ ДОМА ──────
    //
    // Здесь стояло `silverArrived > 0` по СКЛАДУ ДОМА, и ломоть 5а M-72 эту
    // мерку обесценил: дом есть ГОРОД, у города теперь модуль, а его анкета
    // знает чеканку (Blacksmith 30) — значит серебро, доехав, уходит в
    // МОНЕТУ в тот же день. Замер назвал это числом: у вассала серебра
    // 20 → 18 лотов, на полке дома серебра 0, а лежит `coin_empire_silver`
    // x256 стоимостью РОВНО 2560 = два лота по 1280. Вольные сводятся к
    // имперской монетной семье (`faction_or_freefolk@src/tables/faction.h`),
    // поэтому номинал имперский у места без своей фракции.
    //
    // ТО ЕСТЬ ЗАКОН ЦЕЛ, А МЕРКА БЫЛА НЕ У ТОГО КОНЦА: «серебро берут»
    // есть утверждение о ПОГРУЗКЕ, и спрашивать его надо там, где грузят, —
    // у вассала. Склад дома отвечает на другой вопрос («что у дома лежит»),
    // и ответ на него законно меняет всякий, кто из этого серебра делает
    // вещь. Чинится СВИДЕТЕЛЬ (ЗАКОН НУЛЕВОЙ п.3), и чинится в сторону
    // большей силы: мерка у источника не зависит от того, что дом умеет.
    const int vassalSilverLeft =
        st.inventory[r.vassal.slot].inv.count("silver");
    CHECK(vassalSilverLeft < kTitheSilverLots,
          "а ОСТАТОК долга добран по плотности — серебро УШЛО СО СКЛАДА "
          "ВАССАЛА, и закон двухступенчатый (иначе якорь выше не отличал бы "
          "его от «серебро вообще не берут»)");
    // И СТОИМОСТЬ ДОЕХАЛА — второй конец того же утверждения, уже у дома.
    // Он нарочно спрашивает СТОИМОСТЬ, а не строку: во что дом обратил
    // привезённое — его дело (монета, украшение, инструмент), а вот
    // ПРИБЫТЬ оно обязано.
    CHECK(inventory_value(st.inventory[r.home.slot].inv) > 0,
          "и доехало оно ДОМОЙ стоимостью — чем бы дом её ни сделал");
    CHECK(owed_now(r) < 2000,
          "и это был именно сбор дани, а не что-то ещё: долг упал");
}

} // namespace

int main() {
    test_the_tithe_is_collected_and_comes_home();
    test_the_tithe_loads_by_home_need_before_value_density();
    return sm::test::report("collector_tithe_test");
}
