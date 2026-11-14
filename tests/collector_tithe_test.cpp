// СВИДЕТЕЛЬ МОДУЛЯ Collector — ЗАВЕДЁН ПОТОМУ, ЧТО ЕГО НЕ БЫЛО ВОВСЕ.
//
// ПОЧЕМУ ОН СУЩЕСТВУЕТ, ЧИСЛОМ. Доделки ломтя 4 (M-72) прогнали записанную
// мутацию «`collector_think` отключён целиком» — и дерево ответило
// **132 из 133 ЗЕЛЁНЫХ**. Весь модуль сбора дани можно было вырезать, и ни
// один прибор проекта этого не замечал: у рода `Collector` свидетелей было
// РОВНО НОЛЬ. Это не дыра в покрытии, а дыра в ЗАКОНЕ — целая макро-машина
// мира существовала без единого утверждения о том, что она делает.
//
// ЗАКОНЫ ЭТОГО ФАЙЛА:
//   1) ДАНЬ ДОЕЗЖАЕТ, А ДОЛГ ГАСНЕТ: долг ребра падает, дом прибавляет,
//      материя сохраняется.
//   2) ГРУЗ ВЫБИРАЕТ ОЦЕНКА ДОМА — голодному дому едет еда, хотя серебро
//      плотнее по стоимости (якорь отношением строк каталога).
//   3) СПИНА НЕ ПРЕВЫШАЕТ `carry_capacity_g` (M-308).
//   4) ОСТАТОК ЧУЖОЙ ПОГОНИ ДОЛЖНИКОМ НЕ ЧИТАЕТСЯ (M-459).
//   5) ДАНЬ — БАРТЕРОМ (ряд M-495; CANON S25 «ТРИ РЕШЕНИЯ О ВЗАИМОДЕЙСТВИИ»
//      п.2, владелец: «даннь также идеёт через бартер так задумано»):
//      (а) долг мерит оценка ВАССАЛА — единица из завала гасит меньше, чем
//          такая же единица без завала, и обе не больше стоимости;
//      (б) груз выбирает оценка ДОМА СЮЗЕРЕНА — первой едет строка, которую
//          дом ценит плотнее, с контролем перестановкой нужды дома;
//      (в) взято не больше долга в оценке вассала (спину держит закон 3);
//      (г) начисление — доля того, во сколько вассал ценит полку: пустая
//          полка не облагается, та же полка, ценимая ниже, облагается меньше;
//      (д) погашено ровно столько, во сколько вассал ценит отданное, —
//          второго счёта нет;
//      (е) факт дани — «вассал заплатил сюзерену»: субъект вассал, объект
//          сюзерен, с контролем перестановкой ординалов;
//      (ж) мера начисления — то, что лежит на полке: полка под
//          непогашенным счётом неотрицательна и не дешевле той же без счёта.
//
// ПОЧЕМУ ЗАКОН 2 ПРОВЕРЯЕМ ЯКОРЕМ, А НЕ СВЕРКОЙ (§8 п.6, шестая форма
// слепоты): сверить груз с пересчётом той же функции значит проверить умение
// копировать. Здесь утверждается НЕЗАВИСИМО ИЗВЕСТНЫЙ ФАКТ МИРА: серебро
// плотнее еды по стоимости РОВНО В 64 РАЗА (строки `kItemDefs`: silver value
// 1280 при весе 4000 г = 0.32/г; food value 5 при 1000 г = 0.005/г). Значит
// порядок «по плотности стоимости» физически НЕ МОЖЕТ привезти ни одной еды,
// пока у вассала лежит серебро, — и приехавшая еда есть доказательство, что
// грузили по оценке дома.
//
// ЧТО ЭТОТ ПРИБОР НАШЁЛ В ПЕРВЫЙ ЖЕ ПРОГОН — ЖИВОЙ ДЕФЕКТ, НЕ ПОКРЫТИЕ.
// Дань списывалась с долга, уезжала со склада вассала и НЕ ДОЕЗЖАЛА НИКОГДА:
// глагол взыскания перенацеливал цель на дом, обнуляя `what`, а гейт
// должника читал должника ИЗ ЦЕЛИ — и гружёный сборщик на первой же думке
// обратного плеча получал «должника нет» и уходил в `home_wander`. Замер:
// долг 200 → 0, у вассала 40 → 0, дома 0, В СПИНЕ 40. Чинится в
// `collector_think`, ступень `Returning` поднята ВЫШЕ гейта.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» ПЕРВОЙ РЕДАКЦИИ (до ряда M-495; глагол
// `collect_tithe` тогда держал всё взыскание). Контроль — OK, 12 проверок:
//   · `collector_think` отключён целиком   →  КРАСНО, 7 из 12 (до прибора
//        132/133 ЗЕЛЁНЫХ)
//   · взял, но не зачёл                    →  КРАСНО, 2 из 12
//   · порядок груза сведён к ПЛОТНОСТИ     →  КРАСНО, 2 из 12 (якорь еды)
//   · ступень `Returning` возвращена ПОД гейт должника  →  КРАСНО, 3 из 12
//   · балансное число срока ожидания       →  МОЛЧИТ (детектор, краснеющий
//        на всё, — не детектор)
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» ЗАКОНА 5 — у самого закона ниже.

#include "check.h"
#include <memory>

#include "macro/npc_ai.h"
#include "macro/birth_squad.h"   // birth_squad — место родится ТЕЛОМ
#include "macro/labour.h"        // settle_souls — двери душ
#include "bus/world_row.h"
#include "macro/chronicle.h"
#include "macro/currency.h"      // inventory_value, payable_row — номинал полки
#include "macro/deal.h"          // side_row, side_worth — сторона глазами ядра
#include "macro/econ_day.h"
#include "macro/economy.h"       // lot_take, lot_loss, gain_denser — ядро оценки
#include "macro/squad.h"
#include "macro/world_tick.h"    // process_world_daily_ticks — день мира, начисление
#include "bus/store.h"
#include "tables/faction.h"
#include "tables/items.h"        // item_mass_g, item_def_at — колонки строки
#include "tables/npc.h"
#include "tables/seasons.h"      // season_boundary — день начисления

#include <cstdint>
#include <cstdio>

namespace {

using namespace sm;

constexpr int kMap = 32;

// ДОЛГ ДАНИ СТАВИТСЯ ТОЙ ЖЕ ДВЕРЬЮ, КАКОЙ ЕГО СТАВИТ МИР: ребро Suzerain на
// конце ВАССАЛА, долг — колонка этого ребра (M-245). Фикстура не трогает
// память мимо двери — иначе она сторожила бы раскладку, а не закон.
void owe_tithe(MacroStore& st, int vassalId, int suzerainId,
               std::uint64_t owedValue) {
    set_relation(st, vassalId, suzerainId, Stance::Suzerain);
    const MacroHandle vh =
        macro_handle_by_spawn_id(st, std::uint32_t(vassalId));
    Interest* edge = tithe_of(st, std::uint16_t(vh.slot));
    CHECK(edge != nullptr,
          "фикстура: ребро дани поставлено дверью мира, а не записью в память");
    if (edge) edge->owedValue = owedValue;
}

// Сборщик — сквад рода Collector: дом ребром Parent, ДОЛЖНИК — КОЛОНКОЙ
// ЦЕЛИ в той же форме {клетка, Squad(ординал), 0, Working}, какой её
// поставит сам модуль, решив следующее плечо (ломоть 4 M-72).
MacroHandle make_collector(MacroStore& st, int x, int y,
                           int homeId, MacroHandle vassal, int vassalId,
                           std::uint32_t ordinal) {
    MacroHandle h = store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, kMap)};
    st.runtime[h.slot].sub = {};   // доля клетки: центр (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(NPCType::Peasant),
                                   std::uint16_t(faction_index("timaert"))};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(SquadType::Collector);
    rt.state = std::uint8_t(NPCState::Idle);
    rt.recoverySteps = 0;
    ecs::Pools pools{};
    refresh_body_from_sheet(
        pools, &rt,
        make_character_sheet(NPCType::Peasant, 3, avatar_sheet_seed(ordinal)),
        NPCType::Peasant);
    pools.sp = pools.maxSp;
    st.runtime[h.slot] = rt;
    ecs::goal_set(st.goal[h.slot], st.cell[vassal.slot].idx,
                  ecs::goal_what(ecs::GoalKind::Squad,
                                 std::uint32_t(vassalId)),
                  0, std::uint8_t(NPCState::Working));
    store_rename_ordinal(st, h, ordinal);
    h = handle_at(st, h.slot);
    set_relation(st, int(h.id), homeId, Stance::Parent);
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;
    return h;
}

// Мир фикстуры: столица-дом, деревня-должник, сборщик между ними.
struct Rig {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    std::unique_ptr<MacroStore> store = make_macro_store();
    MacroHandle home{};
    MacroHandle vassal{};
    MacroHandle man{};
    int homeId = 0;
    int vassalId = 0;
};

// `silverLots` = 0 выключает соблазн плотности: тот же прогон без серебра
// отвечает на вопрос «а доехало бы вообще хоть что-то» отдельно от вопроса
// «что выбрано первым». `vassalBornFirst` переставляет порядок рождения — и
// с ним ординалы дома и вассала (контроль перестановкой у факта дани).
void build_rig(Rig& r, int foodLots, int silverLots, std::uint64_t owed,
               bool vassalBornFirst = false) {
    r.gs.mapW = kMap;
    r.gs.mapH = kMap;
    chronicle_init(r.gs.chronicle, kMap, kMap);
    MacroStore& st = *r.store;

    const auto birth_vassal = [&] {
        r.vassal = birth_squad(r.gs, nullptr, st, SquadType::Village, 16, 10);
        r.vassalId = int(st.spawnId[r.vassal.slot].index);
    };
    if (vassalBornFirst) birth_vassal();
    r.home = birth_squad(r.gs, nullptr, st, SquadType::City, 10, 10);
    r.homeId = int(st.spawnId[r.home.slot].index);
    settle_souls(r.gs, st, r.home.slot, 50);
    // ДОМ ГОЛОДЕН СЧЁТОМ (CANON S10): «нет хлеба» есть непогашенный сезонный
    // счёт, и ровно из него модуль читает, чего дому не хватает. Полка еды
    // пуста НАМЕРЕННО — иначе прибавку не отличить от того, что уже лежало.
    st.upkeep[r.home.slot].needDebt[commodity_index("food")] =
        std::int32_t(50 * kDaysPerSeason);

    if (!vassalBornFirst) birth_vassal();
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

// ПИК СПИНЫ СНИМАЕТСЯ В ХОДЕ ПРОГОНА, А НЕ ПОСЛЕ, И ЭТО НЕ ПРИДИРКА:
// сборщик ВЫГРУЖАЕТСЯ дома, поэтому спина после прогона почти всегда пуста —
// замер по итогу назвал бы перегруз нулём и прошёл бы зелёным при любом
// дефекте. Мера берётся каждый тик.
struct BackWatch {
    std::int64_t peakMassG = 0;
    std::int64_t capAtPeakG = 0;
    int samples = 0;
};

void run_days_watching_back(Rig& r, int ticks, BackWatch& w) {
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < ticks; ++i) {
        MacroWorld mw{.gs = &r.gs, .store = r.store.get()};
        tick_macro_npc_ai(mw, rt, kAiTicks);
        MacroStore& st = *r.store;
        if (!st.valid(r.man)) continue;
        const auto* bag = body_state<ecs::NpcInventory>(st, r.man);
        if (!bag) continue;
        ++w.samples;
        const std::int64_t massG = inventory_mass_g(bag->inv);
        if (massG > w.peakMassG) {
            w.peakMassG = massG;
            w.capAtPeakG = carry_capacity_g(st.runtime[r.man.slot], bag->inv);
        }
    }
}

std::uint64_t owed_now(const Rig& r) {
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
    const std::uint64_t owedBefore = owed_now(r);
    CHECK(owedBefore > 0, "фикстура: вассал ДОЛЖЕН (условие проверки живо)");

    run_days(r, 600);

    MacroStore& st = *r.store;
    const std::uint64_t owedAfter = owed_now(r);
    const int homeFood = st.inventory[r.home.slot].inv.count("food");
    const int vassalFood = st.inventory[r.vassal.slot].inv.count("food");

    const int billPaid = home_food_bill_paid(r);

    CHECK(owedAfter < owedBefore,
          "долг ребра ПОГАШЕН взысканием — сборщик отработал");
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

// ── ЗАКОН 2: ЯКОРЬ — груз выбирает оценка дома, а не плотность стоимости ──
//
// ЗДЕСЬ СТОЯЛ ЗАКОН ДВУХ СТУПЕНЕЙ — «сначала по нужде дома, остаток по
// плотности» (вердикт 2026-09-21), — и он умер рядом M-495: механизм один,
// груз выбирает оценка дома сюзерена, а нужда дома сама поднимает его
// оценку строки. Якорь пережил смену формы без правки чисел: голодный дом
// ценит еду выше серебра на грамм, значит она едет первой и без ступени.
void test_the_tithe_loads_by_home_need_before_value_density() {
    Rig r;
    // РАСКЛАД ПОДОБРАН ТАК, ЧТО ДВА ПОРЯДКА ДАЮТ РАЗНЫЙ ОТВЕТ, И РАЗНИЦА
    // ВИДНА ОДНИМ ЧИСЛОМ. Долг 2000; еды у вассала всего 40 (стоимость 200
    // — ПЯТАЯ ЧАСТЬ долга), серебра 20 лотов (стоимость 25 600 — долг
    // закрывается им в двенадцать раз с запасом).
    //   · по оценке дома: дом голоден счётом, и его оценка еды на грамм
    //     выше серебряной — едет ВСЯ еда, остаток долга берёт серебро;
    //   · по плотности стоимости: долг закрывается ОДНИМ-ДВУМЯ лотами
    //     серебра, еда не нужна вовсе → homeFood == 0.
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
          "ГРУЗ ВЫБРАН ОЦЕНКОЙ ДОМА: голодный дом получил ЕДУ, хотя серебро "
          "в 64 раза плотнее по стоимости и закрыло бы долг одно");
    CHECK(vassalFoodLeft == 0,
          "то, что дом ценит плотнее, едет ПЕРВЫМ и ЦЕЛИКОМ: вся еда вассала "
          "уехала раньше, чем долг стал брать серебро");
    // ── ОСТАТОК ДОЛГА СПРАШИВАЕТСЯ У ИСТОЧНИКА, А НЕ У ПОЛКИ ДОМА ───────
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
          "а ОСТАТОК долга взят следующей строкой — серебро УШЛО СО СКЛАДА "
          "ВАССАЛА (иначе якорь выше не отличал бы порядок от «серебро "
          "вообще не берут»)");
    // И СТОИМОСТЬ ДОЕХАЛА — второй конец того же утверждения, уже у дома.
    // Он нарочно спрашивает СТОИМОСТЬ, а не строку: во что дом обратил
    // привезённое — его дело (монета, украшение, инструмент), а вот
    // ПРИБЫТЬ оно обязано.
    CHECK(inventory_value(st.inventory[r.home.slot].inv) > 0,
          "и доехало оно ДОМОЙ стоимостью — чем бы дом её ни сделал");
    CHECK(owed_now(r) < 2000,
          "и это был именно сбор дани, а не что-то ещё: долг упал");
}

// ── ЗАКОН 3: СПИНА НЕ ПРЕВЫШАЕТ `carry_capacity_g` НИ НА ОДНОЙ СТУПЕНИ ───
//
// ЧТО ЭТО СТЕРЕЖЁТ (M-308). У загрузки сборщика БЫЛО две ступени (до ряда
// M-495 — ниже их история; теперь механизм один, и предел спины у него —
// граница лота строки на плане плюс `roomG` сделки), и до M-308 они
// спрашивали спину ПО-РАЗНОМУ: ветка нужды дома клампила
// `carry_capacity_g - inventory_mass_g` честной дверью `deal`, а остаток
// долга по плотности звал `transfer_value_dense`, у которой параметра массы
// НЕ БЫЛО ВОВСЕ, — она встаёт на отказе СЛОТА, а `add_ref` о массе не знает
// ничего. Два закона ёмкости на один вопрос мира (DOD п.6).
//
// РАСКЛАД ПОДОБРАН ТАК, ЧТОБЫ ВТОРАЯ СТУПЕНЬ БЫЛА ВЫНУЖДЕНА ГРУЗИТЬ, И
// СЕРЕБРА ЗДЕСЬ НЕТ НАМЕРЕННО: серебро плотно по стоимости (0.32 за грамм),
// поэтому огромный долг закрылся бы парой лотов и спина осталась бы пустой —
// соблазна не возникло бы вовсе. Еда же стоит 0.005 за грамм, то есть долг
// в 10 000 стоимости есть 2000 КГ груза при спине около 240 кг. Нужда дома
// возьмёт первую спину и упрётся в свой честный кламп; долг останется
// огромным, и вторая ступень пойдёт грузить ту же еду — уже без предела,
// если его не поставить в ПОДПИСЬ.
//
// ПОЧЕМУ УТВЕРЖДАЕТСЯ ОТНОШЕНИЕ, А НЕ ЧИСЛО (§8 п.4-5). Прибор не
// пересчитывает, СКОЛЬКО дверь обязана была взять, — это была бы копия
// продакшен-формулы, проверяющая умение копировать. Он утверждает ИНВАРИАНТ
// мира: что лежит в спине, не тяжелее того, что эта спина держит. Обе
// величины берутся своими дверями, и ни одна из них не знает про вторую
// ступень. Поэтому утверждение переживает любую перекалибровку силы,
// скиллов и веса строк (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ п.5).
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» ДЛЯ ЭТОГО ЗАКОНА (обе полярности, §8 п.6).
// ЧИСЛА ЗАМЕРЕНЫ ПРОГОНОМ, А НЕ ВЫВЕДЕНЫ: первая редакция этой таблицы несла
// числа «по рассуждению» (1 759 000 против 243 000 г), и прогон опроверг ОБА —
// ёмкость этой анкеты 154 000 г, а не 243 000. Контроль — починенное дерево:
// OK, 19 проверок, пик 154 000 г при ёмкости 154 000 г (предел СВЯЗАЛ ровно).
//   · ОТКАТ ПОЧИНКИ: предел второй ступени снят (`carry_capacity_g -
//     inventory_mass_g` → `kNoCarryLimitG` у звонящего в `collect_tithe`)
//                                      →  КРАСНО, 1 из 19; пик 2 000 000 г
//        против ёмкости 154 000 г — перегруз в ТРИНАДЦАТЬ раз, то есть вся
//        еда склада уехала в одну крестьянскую спину
//   · предел подменён на ПОЛНУЮ ёмкость вместо ОСТАТКА (забыт вычет
//     `inventory_mass_g` — классическая ошибка на единицу закона)
//                                      →  КРАСНО, 1 из 19; пик 308 000 г,
//        РОВНО ВДВОЕ: спина набирается дважды, по разу на ступень
//   · балансное число: еда фикстуры 2000 → 1800 лотов
//                                      →  МОЛЧИТ, OK 19 (детектор,
//        краснеющий на всё, — не детектор)
//   · ранний выход до вердиктов этого закона (`return` после прогона)
//        →  БЕЗ ПИНА СЧЁТА прошло ЗЕЛЁНЫМ: 19 → 14 проверок, код 0. §8 п.2
//        ловит только тест с НУЛЁМ проверок, а здесь два других закона свои
//        проверки отдавали, и выпавшая ТРЕТЬ была невидима. Дыра закрыта
//        `PIN "OK collector_tithe_test .19 checks"` в `CMakeLists.txt`, и под
//        пином та же мутация →  КРАСНО (ctest). Это не довод «пин полезен
//        вообще», а замер: без него треть этого прибора можно было удалить
//        молча.
constexpr int kOverloadFood = 2000;   // 2000 кг против 154 кг спины
// Номинал всей этой еды; вассал ценит свой завал НИЖЕ номинала, поэтому долг
// не связывает — связывает спина.
constexpr std::uint64_t kOverloadOwed = 10000;

void test_the_back_never_exceeds_its_carry_capacity() {
    Rig r;
    build_rig(r, kOverloadFood, /*silverLots*/0, kOverloadOwed);

    BackWatch w;
    run_days_watching_back(r, 600, w);

    // ЦИКЛ, КОТОРЫЙ МЕРИТ, ОБЯЗАН УТВЕРДИТЬ, ЧТО ПОМЕРИЛ (§8 п.3): без
    // этого пустой прогон сообщал бы успех ровно тогда, когда охраняемое
    // сломано.
    CHECK(w.samples > 0,
          "спина СНЯТА хоть раз — иначе ноль перегрузов ниже значит ноль "
          "замеров, а не целый закон");
    // И ОБЯЗАН УТВЕРДИТЬ, ЧТО ВТОРАЯ СТУПЕНЬ ВООБЩЕ РАБОТАЛА: пустая спина
    // проходит предел тривиально, поэтому «груз был» есть ПРЕДУСЛОВИЕ
    // проверки, а не её вывод.
    CHECK(w.peakMassG > 0,
          "фикстура: сборщик РЕАЛЬНО грузился — иначе предел соблюдён "
          "пустотой и прибор ничего не стережёт");
    CHECK(w.capAtPeakG > 0,
          "фикстура: ёмкость спины положительна — иначе отношение ниже "
          "сравнивает с нулём и зелено при любом грузе");
    CHECK(w.peakMassG <= w.capAtPeakG,
          "СПИНА НЕ ПРЕВЫШАЕТ `carry_capacity_g` НИ НА ОДНОЙ СТУПЕНИ "
          "ЗАГРУЗКИ: остаток долга по плотности мерит ту же ёмкость, что "
          "ветка нужды дома, — один закон ёмкости, а не два (M-308)");
    // И ПРЕДЕЛ ОБЯЗАН БЫЛ СВЯЗАТЬ, А НЕ ПРОСТО НЕ НАРУШИТЬСЯ. Полупустая
    // спина проходит проверку выше ТРИВИАЛЬНО, поэтому утверждается, что
    // кламп реально упёрся: груз добрал хотя бы половину ёмкости. Без этого
    // прибор остался бы зелёным и у двери, которая не берёт НИЧЕГО.
    CHECK(w.peakMassG * 2 >= w.capAtPeakG,
          "предел СВЯЗАЛ: спина набралась хотя бы наполовину, значит "
          "проверка выше прошла клампом, а не пустотой");
    // ЧИСЛА ЗАМЕРА ПЕЧАТАЮТСЯ: следующий агент правит прибор, видя, ЧТО он
    // мерил, а не догадываясь (§8 п.6 — записанный контроль).
    std::printf("  спина: пик %lld г при ёмкости %lld г, замеров %d; "
                "у вассала еды %d, долг %llu\n",
                (long long)w.peakMassG, (long long)w.capAtPeakG, w.samples,
                r.store->inventory[r.vassal.slot].inv.count("food"),
                (unsigned long long)owed_now(r));
}

// ── ЗАКОН 5: ДАНЬ — БАРТЕРОМ (ряд M-495) ─────────────────────────────────
//
// Владелец, дословно: «даннь также идеёт через бартер так задумано»; разбор
// одобрен (CANON S25 «ТРИ РЕШЕНИЯ О ВЗАИМОДЕЙСТВИИ» п.2). Сборщик пришёл —
// вассал стоящий: долг мерит ЕГО оценка (наценки нет, доля ноль), груз
// выбирает оценка ДОМА СЮЗЕРЕНА; начисление — доля того, во сколько вассал
// ценит свою полку (`side_worth@src/macro/deal.h`).
//
// ЗДЕСЬ СТОЯЛ ЗАКОН СТУПЕНИ НУЖДЫ (M-488 ломоть 3): «ветка нужды дома грузит
// ТОЛЬКО недостающее» и «первой — строка, чей НЕДОСТАЮЩИЙ лот дом ценит
// выше». Его носитель — отдельная стадия «сперва по нужде дома» в глаголе
// `collect_tithe` — умер вместе с номинальным добором, поэтому снят и
// свидетель «нужда — не больше нехватки»
// (`test_the_need_stage_loads_no_more_than_the_home_lacks`): механизма, о
// котором он говорил, больше нет. Живая половина — «порядок задаёт оценка
// дома, а не словарь и не плотность стоимости» — перенесена в (б) с тем же
// контролем перестановкой.
//
// НАБЛЮДЕНИЕ — ПЕРВОЕ ВЗЫСКАНИЕ, ШАГОМ В ОДИН ТИК. Думки одного тика идут по
// ординалу (`tick_macro_npc_ai`, порядок `SquadIndex::order`), а сборщик
// фикстуры — ординал 17, позже дома и вассала: после тика, в котором его
// спина перестала быть пустой, строки вассала стоят такими, какими их
// оставила сделка. Оценка запаса — функция состояния (static_assert «ОЦЕНКА
// ЗАПАСА — ФУНКЦИЯ СОСТОЯНИЯ» в `economy.h`), поэтому во сколько вассал ценил
// отданное, свидетель спрашивает оценкой ПРИХОДА тех же единиц к его
// нынешней полке (`lot_take` строки ПОСЛЕ сделки), а не повтором
// продакшен-счёта (`lot_loss` строки ДО неё, §8 п.5).
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» ЗАКОНА 5 (прогнано вживую, 2026-11-14; контроль
// — чистое дерево: OK, 70 проверок; собран только этот таргет):
//   1) долг засчитывается НОМИНАЛОМ (`n × стоимость` на плане и в зачёте)
//                                      →  КРАСНО, 2 из 70: (а) 40 еды гасят
//        200 и 154 еды — 770, по 5 за штуку из завала и без; (д) погашено
//        больше, чем вассал отдал в своей мерке (770 против 154)
//   2) порядок строк — по оценке ВАССАЛА, а не дома сюзерена
//                                      →  КРАСНО, 1 из 70: (б), расклад, где
//        дом голоден по ткани, — первой поехала еда
//   3) снят предел долга (бюджет плана — потолок типа)
//                                      →  КРАСНО, 3 из 70: (в) при долге 100
//        вассал отдал на 154 и спина полна; (д) погашено 2000 при отданном на
//        23 873
//   4) снят предел спины (план без массы, сделка без `roomG`)
//                                      →  КРАСНО, 3 из 70: закон 3 (пик выше
//        ёмкости) и оба расклада (б) — спина взяла обе строки
//   5) начисление номиналом (`inventory_value` вместо `side_worth`)
//                                      →  КРАСНО, 1 из 70: (г) просторная и
//        тесная спины облагаются поровну, 1000 и 1000
//   6) переименование локальной `loadedG` в модуле сборщика
//                                      →  МОЛЧИТ, OK 70
//   7) субъект факта дани — сюзерен, объект — вассал (переставлены)
//                                      →  КРАСНО, 1 из 70: (е)
//   8) `side_worth` через `stock_worth(r, r.net)` — оценка СОСТОЯНИЯ, а не
//      полки                           →  КРАСНО, 2 из 70: (ж) полка под
//        непогашенным счётом ушла в минус и стала дешевле той же без счёта
struct FirstTake {
    bool seen = false;
    int food = 0;
    int cloth = 0;
    int silver = 0;
    std::uint64_t owedBefore = 0;
    std::uint64_t owedAfter = 0;
    std::int64_t vassalLoss = 0;    // во сколько вассал ценит отданное
    std::int64_t nextFoodLoss = 0;  // во сколько он ценит следующую единицу еды
    std::int64_t bagG = 0;          // масса взятого
    std::int64_t capG = 0;          // ёмкость спины сборщика в этот тик
};

// Погашено в этом взыскании — разность двух чтений долга (ниже нуля долг не
// уходит, поэтому разность неотрицательна).
std::int64_t paid(const FirstTake& t) {
    return std::int64_t(t.owedBefore) - std::int64_t(t.owedAfter);
}

std::int64_t row_lot_take(const MacroStore& st, std::uint16_t slot,
                          int idx, std::int64_t n) {
    const ItemDef* d = item_def_at(idx);
    return lot_take(side_row(st, slot, idx, d ? d->value : 0), n);
}

FirstTake watch_first_take(Rig& r, int ticks) {
    FirstTake out;
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < ticks && !out.seen; ++i) {
        const std::uint64_t owedBefore = owed_now(r);
        MacroWorld mw{.gs = &r.gs, .store = r.store.get()};
        tick_macro_npc_ai(mw, rt, 1);
        MacroStore& st = *r.store;
        if (!st.valid(r.man)) continue;
        const Inventory& bag = st.inventory[r.man.slot].inv;
        if (inventory_mass_g(bag) <= 0) continue;
        out.seen = true;
        out.owedBefore = owedBefore;
        out.owedAfter = owed_now(r);
        out.food = bag.count("food");
        out.cloth = bag.count("cloth");
        out.silver = bag.count("silver");
        out.bagG = inventory_mass_g(bag);
        out.capG = carry_capacity_g(st.runtime[r.man.slot], bag);
        std::uint16_t counted[kMaxInventorySlots];
        int rows = 0;
        for (const ItemRef& sl : bag.slots) {
            if (!payable_row(sl)) continue;
            bool dup = false;
            for (int k = 0; k < rows && !dup; ++k) dup = counted[k] == sl.def;
            if (dup) continue;
            counted[rows++] = sl.def;
            out.vassalLoss += row_lot_take(st, r.vassal.slot, int(sl.def),
                                           bag.count_of(int(sl.def)));
        }
        const int food = item_index("food");
        const ItemDef* fd = item_def_at(food);
        out.nextFoodLoss = lot_loss(
            side_row(st, r.vassal.slot, food, fd ? fd->value : 0), 1);
    }
    return out;
}

// Дом голоден ровно по двум строкам: `foodLack` еды и `clothLack` ткани.
void bill_home(Rig& r, int foodLack, int clothLack) {
    std::int32_t* debt = r.store->upkeep[r.home.slot].needDebt;
    for (int c = 0; c < kCommodityCount; ++c) debt[c] = 0;
    debt[commodity_index("food")] = foodLack;
    debt[commodity_index("cloth")] = clothLack;
}

constexpr int kWatchTicks = 600 * int(kAiTicks);
// Долг, который не связывает ни одного расклада ниже: номинал всего, что
// фикстуры кладут вассалу, меньше.
constexpr std::uint64_t kLooseOwed = 1000000;

// (а) ДОЛГ МЕРИТ ОЦЕНКА ВАССАЛА. Одна строка (еда) у того же вассала, разница
// только в ЗАВАЛЕ: сорок штук при спине его жителей почти не дешевеют, три
// тысячи — завал, и единицы на его вершине вассал ценит по полу. Утверждается
// отношение, не число: единица из завала гасит долга МЕНЬШЕ, и обе — не
// больше стоимости строки.
void test_the_debt_is_measured_by_the_vassals_valuation() {
    Rig light;
    build_rig(light, 40, 0, kLooseOwed);
    const FirstTake a = watch_first_take(light, kWatchTicks);
    Rig glut;
    build_rig(glut, 3000, 0, kLooseOwed);
    const FirstTake b = watch_first_take(glut, kWatchTicks);
    CHECK(a.seen && b.seen && a.food > 0 && b.food > 0,
          "фикстура: в обоих раскладах сборщик взял еду");
    const int idx = item_index("food");
    const std::int64_t base = item_def_at(idx) ? item_def_at(idx)->value : 0;
    CHECK(base > 0 && paid(a) > 0 && paid(b) > 0,
          "фикстура: еда чего-то стоит, и оба взыскания долг гасили");
    CHECK(paid(b) * a.food < paid(a) * b.food,
          "ДОЛГ МЕРИТ ОЦЕНКА ВАССАЛА: единица из его ЗАВАЛА гасит долга "
          "меньше, чем такая же единица без завала");
    CHECK(paid(a) <= base * a.food && paid(b) <= base * b.food,
          "и ни одна единица не гасит больше своей стоимости");
    std::printf("  (а) без завала: %d еды гасят %lld; завал: %d еды гасят "
                "%lld\n", a.food, (long long)paid(a), b.food,
                (long long)paid(b));
}

// (б) ГРУЗ ВЫБИРАЕТ ОЦЕНКА ДОМА СЮЗЕРЕНА. У вассала две строки по тысяче
// штук одной массы — еда и ткань, — а спина сборщика берёт лишь одну из них
// целиком. Дом голоден ровно по одной (счёт 1600 при пустой полке), и это
// переставляется: первой едет та, которую дом ценит плотнее, и вторую спина
// уже не берёт. Перестановка — контроль на то, что порядок задаёт оценка
// дома, а не словарь (еда в нём раньше ткани), не плотность стоимости (ткань
// вчетверо дороже на грамм) и не оценка вассала.
void test_the_load_is_chosen_by_the_suzerains_valuation() {
    struct Case { int foodLack; int clothLack; };
    const Case cases[] = {{1600, 0}, {0, 1600}};
    int foodFirst = 0, clothFirst = 0;
    for (const Case& k : cases) {
        Rig r;
        build_rig(r, 1000, 0, kLooseOwed);
        r.store->inventory[r.vassal.slot].inv.add("cloth", 1000);
        bill_home(r, k.foodLack, k.clothLack);
        // ПРЕДУСЛОВИЕ РАСКЛАДА — какую строку дом ценит плотнее на грамм
        // лота (масса строк одна, лот сравнивается равный).
        constexpr std::int64_t kLot = 100;
        const int food = item_index("food"), cloth = item_index("cloth");
        const std::int64_t massFood = kLot * item_mass_g(*item_def_at(food));
        const std::int64_t massCloth = kLot * item_mass_g(*item_def_at(cloth));
        const std::int64_t gFood =
            row_lot_take(*r.store, r.home.slot, food, kLot);
        const std::int64_t gCloth =
            row_lot_take(*r.store, r.home.slot, cloth, kLot);
        const bool homeFood = gain_denser(gFood, massFood, gCloth, massCloth);
        const bool homeCloth = gain_denser(gCloth, massCloth, gFood, massFood);
        CHECK(homeFood != homeCloth,
              "фикстура: одну из двух строк дом ценит плотнее");
        const FirstTake got = watch_first_take(r, kWatchTicks);
        CHECK(got.seen, "сборщик погрузился хоть раз — иначе судить нечего");
        CHECK(got.bagG * 2 >= got.capG,
              "фикстура: спина набралась хотя бы наполовину — вторую строку "
              "не взяла СПИНА, а не пустой долг");
        if (homeFood) {
            ++foodFirst;
            CHECK(got.food > 0 && got.cloth == 0,
                  "ПЕРВОЙ едет строка, которую дом сюзерена ценит плотнее "
                  "(еда), и спина уходит на неё");
        } else {
            ++clothFirst;
            CHECK(got.cloth > 0 && got.food == 0,
                  "ПЕРВОЙ едет строка, которую дом сюзерена ценит плотнее "
                  "(ткань), и спина уходит на неё");
        }
    }
    CHECK(foodFirst == 1 && clothFirst == 1,
          "контроль перестановки: переставленная нужда дома переставила "
          "груз — его выбирает оценка дома");
}

// (в) ВЗЯТО НЕ БОЛЬШЕ ДОЛГА В ОЦЕНКЕ ВАССАЛА. Долг мал (сто), еды у вассала
// тысяча, спина свободна: связывает ДОЛГ. Вассал в своей мерке отдал не
// больше долга, а следующая его единица в остаток уже не помещалась — предел
// связал, а не просто не нарушился. Спину держит закон 3.
void test_the_take_is_bounded_by_the_debt() {
    constexpr std::uint64_t kSmallOwed = 100;
    Rig r;
    build_rig(r, 1000, 0, kSmallOwed);
    const FirstTake got = watch_first_take(r, kWatchTicks);
    CHECK(got.seen && got.food > 0, "фикстура: сборщик взял еду");
    CHECK(got.bagG < got.capG,
          "фикстура: спина НЕ полна — связывать здесь обязан долг");
    CHECK(got.vassalLoss <= std::int64_t(kSmallOwed),
          "ВЗЯТО НЕ БОЛЬШЕ ДОЛГА: вассал в своей мерке отдал не больше, чем "
          "был должен");
    CHECK(got.owedAfter < std::uint64_t(got.nextFoodLoss),
          "и долг СВЯЗАЛ: следующая единица вассала в остаток долга уже не "
          "помещалась");
}

// (д) ПОГАШЕНО РОВНО ТО, ВО СКОЛЬКО ВАССАЛ ЦЕНИТ ОТДАННОЕ — второго счёта
// нет. Четыре расклада: малый лот, завал, две строки с серебром, малый долг.
// Цикл, который меряет, утверждает, что померил (§8 п.3).
void test_paid_is_what_the_vassal_lost() {
    struct Case { int food; int silver; std::uint64_t owed; };
    const Case cases[] = {{40, 0, kLooseOwed}, {3000, 0, kLooseOwed},
                          {40, 20, 2000}, {1000, 0, 100}};
    int samples = 0, mismatches = 0;
    for (const Case& k : cases) {
        Rig r;
        build_rig(r, k.food, k.silver, k.owed);
        const FirstTake got = watch_first_take(r, kWatchTicks);
        if (!got.seen) continue;
        ++samples;
        if (paid(got) != got.vassalLoss) {
            ++mismatches;
            std::printf("  (д) погашено %lld, вассал отдал на %lld\n",
                        (long long)paid(got), (long long)got.vassalLoss);
        }
    }
    CHECK(samples == 4, "все четыре расклада дали взыскание");
    CHECK(mismatches == 0,
          "ПОГАШЕНО РОВНО ТО, во сколько вассал ценит отданное: долг гаснет "
          "его оценкой взятого по факту, второго счёта нет");
}

// (е) ФАКТ ДАНИ — «ВАССАЛ ЗАПЛАТИЛ СЮЗЕРЕНУ»: субъект — вассал, объект —
// сюзерен (`Taxed@src/bus/fact.h`). Субъекта называет модуль, дверь факта
// решений не принимает. Контроль перестановкой: во втором раскладе вассал
// рождён ПЕРВЫМ, и его ординал меньше ординала дома, — правило, назвавшее
// бы субъектом «меньший ординал» или дом звонящего, краснеет хотя бы в
// одном из двух.
void test_the_tithe_fact_names_the_vassal_as_the_payer() {
    int facts = 0, wrong = 0, orders = 0;
    for (const bool vassalFirst : {false, true}) {
        Rig r;
        build_rig(r, 40, 0, kLooseOwed, vassalFirst);
        orders += (r.vassalId < r.homeId) == vassalFirst ? 1 : 0;
        const FirstTake got = watch_first_take(r, kWatchTicks);
        CHECK(got.seen && paid(got) > 0, "фикстура: дань взыскана");
        const Chronicle& c = r.gs.chronicle;
        for (std::uint32_t seq = 1; seq < c.nextSeq; ++seq) {
            const WorldFact* f = chronicle_fact(c, seq);
            if (!f || f->kind != std::uint16_t(FactKind::Taxed)) continue;
            ++facts;
            const bool ok =
                f->subjectKind == std::uint8_t(FactSubject::Rooted)
                && f->subject == std::uint32_t(r.vassalId)
                && f->objectKind == std::uint8_t(FactSubject::Rooted)
                && f->object == std::uint32_t(r.homeId);
            wrong += ok ? 0 : 1;
        }
    }
    CHECK(orders == 2,
          "фикстура: порядок рождения переставил ординалы дома и вассала");
    CHECK(facts >= 2, "факт дани записан в обоих раскладах");
    CHECK(wrong == 0,
          "ФАКТ ДАНИ: субъект — ВАССАЛ, объект — СЮЗЕРЕН, при любом порядке "
          "ординалов");
}

// (ж) `side_worth` МЕРИТ ТО, ЧТО ЛЕЖИТ НА ПОЛКЕ. Сквад с вещью на полке и
// НЕПОГАШЕННЫМ счётом по ней (выпуск крафта до суточного гашения) ценит
// полку неотрицательно и НЕ НИЖЕ, чем ту же полку без счёта: отдать
// нужное — значит углубить нужду. Отношение, а не число.
void test_the_shelf_under_an_unpaid_bill_is_worth_no_less() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    std::unique_ptr<MacroStore> store = make_macro_store();
    MacroStore& st = *store;
    const MacroHandle v = birth_squad(gs, nullptr, st, SquadType::Village, 8, 8);
    settle_souls(gs, st, v.slot, 30);
    st.inventory[v.slot].inv.add("cloth", 10);
    std::int32_t* debt = st.upkeep[v.slot].needDebt;
    for (int c = 0; c < kCommodityCount; ++c) debt[c] = 0;
    const std::int64_t paidBill = side_worth(st, v.slot);
    debt[commodity_index("cloth")] = 50;   // счёт выше полки, не погашен
    const std::int64_t unpaidBill = side_worth(st, v.slot);
    CHECK(paidBill > 0, "фикстура: полка без счёта чего-то стоит");
    CHECK(unpaidBill >= 0,
          "полка под непогашенным счётом оценена НЕОТРИЦАТЕЛЬНО");
    CHECK(unpaidBill >= paidBill,
          "и не ниже той же полки без счёта: лежащее нужное стороне дороже");
}

// (г) НАЧИСЛЕНИЕ — ДОЛЯ ТОГО, ВО СКОЛЬКО ВАССАЛ ЦЕНИТ ПОЛКУ. Три вассала
// одного сюзерена в одном дне мира (день 1 — граница сезона, начисление):
// пустая полка; двести серебра при спине шестидесяти жителей; те же двести
// серебра при спине пятерых — номинал тот же, а тяжёлая для малой спины
// полка в его мерке — завал. Утверждаются отношения: пустая полка не
// облагается, полка, ценимая ниже, облагается меньше при равном номинале, и
// доля не больше номинальной.
void test_the_tithe_accrues_on_what_the_vassal_values_its_shelf_at() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = kMap;
    gs.mapH = kMap;
    chronicle_init(gs.chronicle, kMap, kMap);
    std::unique_ptr<MacroStore> store = make_macro_store();
    MacroStore& st = *store;

    const MacroHandle home =
        birth_squad(gs, nullptr, st, SquadType::City, 10, 10);
    settle_souls(gs, st, home.slot, 50);
    const int homeId = int(st.spawnId[home.slot].index);
    struct Vassal { int souls; int silver; MacroHandle h{}; };
    Vassal v[3] = {{30, 0}, {60, 200}, {5, 200}};
    const int cells[3][2] = {{16, 10}, {16, 16}, {10, 16}};
    for (int i = 0; i < 3; ++i) {
        v[i].h = birth_squad(gs, nullptr, st, SquadType::Village,
                             cells[i][0], cells[i][1]);
        settle_souls(gs, st, v[i].h.slot, v[i].souls);
        if (v[i].silver > 0)
            st.inventory[v[i].h.slot].inv.add("silver", v[i].silver);
        set_relation(st, int(st.spawnId[v[i].h.slot].index), homeId,
                     Stance::Suzerain);
    }
    const Vassal& bare = v[0];
    const Vassal& roomy = v[1];
    const Vassal& cramped = v[2];
    const std::int64_t nominal =
        inventory_value(st.inventory[roomy.h.slot].inv);
    CHECK(inventory_value(st.inventory[bare.h.slot].inv) == 0,
          "фикстура: полка первого вассала пуста");
    CHECK(nominal > 0
              && inventory_value(st.inventory[cramped.h.slot].inv) == nominal,
          "фикстура: у двух других полки равного номинала");
    CHECK(side_worth(st, cramped.h.slot) < side_worth(st, roomy.h.slot),
          "фикстура: тесный вассал ценит ту же полку НИЖЕ — для его спины "
          "она завал");

    WorldTickRuntime rt{};
    reset_world_tick_runtime(rt, 7u);
    rt.pendingDailyTicks = 1;
    rt.nextDailyTickDay = 1;
    CHECK(season_boundary(1), "фикстура: день 1 — граница сезона");
    process_world_daily_ticks(gs, st, rt, 1);

    const auto owed = [&](const Vassal& x) -> std::uint64_t {
        const Interest* e = tithe_of(st, x.h.slot);
        return e ? e->owedValue : 0;
    };
    CHECK(owed(bare) == 0,
          "у вассала с ПУСТОЙ полкой дань не начисляется");
    CHECK(owed(roomy) > 0, "а у вассала с полкой — начисляется");
    CHECK(owed(cramped) < owed(roomy),
          "НАЧИСЛЕНИЕ РАСТЁТ С ТЕМ, ВО СКОЛЬКО ВАССАЛ ЦЕНИТ ПОЛКУ: равный "
          "номинал, ценимый ниже, облагается меньше");
    CHECK(owed(roomy) * 8 <= std::uint64_t(nominal),
          "доля начисления не больше доли номинала");
    std::printf("  (г) номинал %lld; долг: пустая полка %llu, просторная "
                "спина %llu, тесная %llu\n", (long long)nominal,
                (unsigned long long)owed(bare),
                (unsigned long long)owed(roomy),
                (unsigned long long)owed(cramped));
}

} // namespace

// ── ЗАКОН 4: ОСТАТОК ЧУЖОЙ ПОГОНИ В ЦЕЛИ ДОЛЖНИКОМ НЕ ЧИТАЕТСЯ (M-459) ───
// Род цели «сквад» один: должника и добычу погони называет ОДИН тег с
// ординалом. Прежде их различал сам тег (`Rooted` против `Squad`); теперь
// «из стоящих ли он» отвечает строка САМОГО названного — ось его типа,
// которую гейтит резолвер (`rooted_handle_by_ordinal@src/bus/store.h`).
// Цель — колонка общая: рефлекс угрозы пишет в неё погоню поверх рейса, а
// `goal_drop` после стычки оставляет «что я искал». Сборщик, прочитавший
// бродягу как должника, ехал бы «взыскивать» с того, кто ничего не должен,
// и настоящий долг висел бы вечно.
void test_a_leftover_chase_is_not_read_as_the_debtor() {
    Rig r;
    build_rig(r, kTitheFood, /*silverLots*/0, /*owed*/200);
    MacroStore& st = *r.store;

    // Бродяга — сквад НЕ из стоящих, своей фракции (угрозой он не станет:
    // закон под проверкой — чтение цели, а не рефлекс).
    const MacroHandle roamer =
        birth_squad(r.gs, nullptr, st, SquadType::None, 12, 12,
                    std::int16_t(faction_index("timaert")));
    CHECK(st.valid(roamer)
              && squad_archetype(SquadType(st.runtime[roamer.slot].squadType))
                     != SquadArchetype::Rooted,
          "фикстура: названный остатком погони — не стоящий сквад");
    const std::uint32_t roamerId = st.spawnId[roamer.slot].index;

    // Сборщик дома, в цели — погоня, от адреса которой он уже отказался.
    ecs::SquadGoal& g = st.goal[r.man.slot];
    ecs::goal_set(g, st.cell[roamer.slot].idx,
                  ecs::goal_what(ecs::GoalKind::Squad, roamerId), 0,
                  std::uint8_t(NPCState::Chasing));
    ecs::goal_drop(g);
    st.runtime[r.man.slot].state = std::uint8_t(NPCState::Idle);
    CHECK(ecs::goal_ordinal(g.what) == roamerId
              && ecs::goal_kind(g.what) == ecs::GoalKind::Squad,
          "фикстура: в колонке цели лежит остаток погони за бродягой");

    const std::uint64_t owedBefore = owed_now(r);
    run_days(r, 600);

    CHECK(ecs::goal_ordinal(g.what) != roamerId,
          "остаток погони ПЕРЕРЕШЁН: бродяга должником не прочитан");
    CHECK(owed_now(r) < owedBefore,
          "и настоящий долг ВЗЫСКАН — сборщик нашёл своего вассала сам");
}

int main() {
    test_the_tithe_is_collected_and_comes_home();
    test_the_tithe_loads_by_home_need_before_value_density();
    test_the_back_never_exceeds_its_carry_capacity();
    test_a_leftover_chase_is_not_read_as_the_debtor();
    test_the_debt_is_measured_by_the_vassals_valuation();
    test_the_load_is_chosen_by_the_suzerains_valuation();
    test_the_take_is_bounded_by_the_debt();
    test_paid_is_what_the_vassal_lost();
    test_the_tithe_fact_names_the_vassal_as_the_payer();
    test_the_shelf_under_an_unpaid_bill_is_worth_no_less();
    test_the_tithe_accrues_on_what_the_vassal_values_its_shelf_at();
    return sm::test::report("collector_tithe_test");
}
