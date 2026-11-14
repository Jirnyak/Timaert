// СВИДЕТЕЛЬ МОДУЛЯ Collector — ЗАВЕДЁН ПОТОМУ, ЧТО ЕГО НЕ БЫЛО ВОВСЕ.
//
// ПОЧЕМУ ОН СУЩЕСТВУЕТ, ЧИСЛОМ. Доделки ломтя 4 (M-72) прогнали записанную
// мутацию «`collector_think` отключён целиком» — и дерево ответило
// **132 из 133 ЗЕЛЁНЫХ**. Весь модуль сбора дани можно было вырезать, и ни
// один прибор проекта этого не замечал: у рода `Collector` свидетелей было
// РОВНО НОЛЬ.
//
// ДАНЬ ПО ВЕРДИКТУ ВЛАДЕЛЬЦА 2026-11-14 (CANON S25, «ВЕРДИКТЫ 2026-11-14»;
// ряд M-495): «сборщик просто в своем модули зебирает 1/8 стоимости склада
// деревни и всё»; «Пришёл — забрал 1/8, без книги долгов». Книги долгов нет:
// ни прохода начисления, ни средней, ни колонки долга, ни должников. Квоту
// модуль считает на месте, в момент прихода; не унесённое прощено; обход —
// раз в сезон, по одному вассалу за рейс.
//
// ЗАКОНЫ ЭТОГО ФАЙЛА:
//   1) ДАНЬ ДОЕЗЖАЕТ: у вассала убыло, дом получил, материя сохраняется.
//   2) ГРУЗ ВЫБИРАЕТ ОЦЕНКА ДОМА — голодному дому едет еда, хотя серебро
//      плотнее по стоимости (якорь отношением строк каталога).
//   3) СПИНА НЕ ПРЕВЫШАЕТ `carry_capacity_g` (M-308).
//   4) ОСТАТОК ЧУЖОЙ ПОГОНИ ЦЕЛЬЮ РЕЙСА НЕ ЧИТАЕТСЯ (M-459).
//   5) ДАНЬ — ВОСЬМАЯ ЧАСТЬ ОЦЕНКИ ПОЛКИ, НА МЕСТЕ, БЕЗ КНИГИ ДОЛГОВ:
//      (а) взято не больше восьмой части того, во сколько вассал ценил свою
//          полку в момент прихода; с богатой полки берётся больше, чем с
//          бедной;
//      (б) что грузить, выбирает оценка ДОМА СЮЗЕРЕНА — контроль
//          перестановкой нужды дома;
//      (в) остаток прощён: тесная спина унесла меньше квоты, и второго
//          визита к тому же вассалу в этом сезоне нет;
//      (г) обход: за сезон каждый из двух вассалов посещён ровно один раз, в
//          новом сезоне обход начинается снова — и в том же сезоне года год
//          спустя тоже (номер сезона — монотонный ординал);
//      (е) факт дани: субъект — вассал, объект — сюзерен, число — во сколько
//          вассал ценит отданное;
//      (ж) мера квоты — то, что лежит на полке: полка под непогашенным
//          счётом неотрицательна и не дешевле той же без счёта.
//   (Город и столица поднимают сборщика при вассале раз в сезон — закон их
//    модулей, свидетель `test_city_raises_a_collector_for_a_vassal_once_a_season`
//    в `tests/rooted_module_day_test.cpp`.)
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
// ЧТО ЭТОТ ПРИБОР НАШЁЛ В ПЕРВЫЙ ЖЕ ПРОГОН (ломоть 4 M-72) — ЖИВОЙ ДЕФЕКТ.
// Дань уезжала со склада вассала и НЕ ДОЕЗЖАЛА НИКОГДА: глагол взыскания
// перенацеливал цель на дом, обнуляя `what`, а гейт читал вассала ИЗ ЦЕЛИ — и
// гружёный сборщик на первой же думке обратного плеча уходил бродить. Замер:
// у вассала 40 → 0, дома 0, В СПИНЕ 40. Чинится в `collector_think`, ступень
// `Returning` стоит первой.
//
// ВЕЛИЧИНЫ ЗАМЕРА — ОДНА ФОРМА НА ВСЕ ЗАКОНЫ 5. Думки одного тика идут по
// ординалу (`tick_macro_npc_ai`, порядок `SquadIndex::order`), а сборщик
// фикстуры — ординал 17, позже дома и вассала: после тика, в котором его спина
// перестала быть пустой, строки вассала стоят такими, какими их оставила
// сделка. Оценка запаса — функция состояния (static_assert «ОЦЕНКА ЗАПАСА —
// ФУНКЦИЯ СОСТОЯНИЯ» в `economy.h`), поэтому во сколько вассал ценил отданное,
// свидетель спрашивает оценкой ПРИХОДА тех же единиц к его нынешней полке
// (`lot_take` строки ПОСЛЕ сделки), а оценка полки в момент прихода есть
// нынешняя оценка плюс отданное — без повтора продакшен-счёта (§8 п.5).
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ряд M-495, книга долгов снесена; прогнано вживую
// 2026-11-14; контроль — чистое дерево: OK, 63 проверки здесь и OK, 827 у
// `rooted_module_day_test`; собирались только эти два таргета):
//   1) квота — вся оценка полки вместо восьмой (`kCollectorTitheShift` 3 → 0)
//                                      →  КРАСНО, 3 из 63: (а) спина у
//        богатой полки перестала быть свободной и отдано больше восьмой части;
//        (г) первый визит унёс всю полку, и во втором сезоне брать нечего
//   2) курсор обхода не растёт после визита
//                                      →  КРАСНО, 7 из 63: законы 1 и 4 и
//        (в) — второй визит к тому же вассалу; (г) — первый посещён много раз,
//        курсор стоит на нуле
//   3) смена сезона не сбрасывает курсор
//                                      →  КРАСНО, 1 из 63: (г) в новом сезоне
//        обход не начался
//   4) город поднимает сборщика без вассала (снят вопрос `vassal_at`)
//                                      →  здесь ЗЕЛЁНЫЙ, красно у
//        `rooted_module_day_test`, 1 из 827; то же у столицы — 1 из 827
//   5) снят запрет второго сборщика за сезон (слово сезона у города)
//                                      →  здесь ЗЕЛЁНЫЙ, красно у
//        `rooted_module_day_test`, 2 из 827; то же у столицы — 2 из 827
//   6) порядок груза — по оценке ВАССАЛА, а не дома сюзерена
//                                      →  КРАСНО, 1 из 63: (б), расклад, где
//        дом голоден по ткани, — первой поехала еда
//   7) переименование локальной `loadedG` в модуле сборщика
//                                      →  МОЛЧИТ, OK 63 и OK 827
//   8) `season_ordinal` возвращает сезон ГОДА (0..3), а не ординал (прогнано
//      при 64 проверках, после плеча «год спустя»)
//                                      →  КРАСНО, 1 из 64: (г) в том же
//        сезоне следующего года обход не начался; `rooted_module_day_test`
//        молчит, OK 827

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
#include "bus/store.h"
#include "core/time.h"           // world_time_at — сезон мира ставит фикстура
#include "tables/faction.h"
#include "tables/items.h"        // item_mass_g, item_def_at — колонки строки
#include "tables/npc.h"
#include "tables/seasons.h"      // kDaysPerSeason — следующий сезон фикстуры

#include <cstdint>
#include <cstdio>

namespace {

using namespace sm;

constexpr int kMap = 32;

// Первый день мира — весна; следующий сезон стоит ровно сезон спустя. Часы
// думками не двигаются, поэтому сезон фикстуры — то, что она поставила.
constexpr int kFirstDay = 1;
constexpr int kNextSeasonDay = kFirstDay + kDaysPerSeason;

// ФЕОД СТАВИТСЯ ТОЙ ЖЕ ДВЕРЬЮ, КАКОЙ ЕГО СТАВИТ МИР: роль зовётся с нижнего
// конца — вассал получает `Suzerain`, сюзерен зеркало `Vassal`. Долга на
// ребре нет: квоту сборщик считает на месте.
void link_vassal(MacroStore& st, int vassalId, int suzerainId) {
    set_relation(st, vassalId, suzerainId, Stance::Suzerain);
    const MacroHandle vh =
        macro_handle_by_spawn_id(st, std::uint32_t(vassalId));
    CHECK(st.valid(vh) && suzerain_of(st, vh.slot) == suzerainId,
          "фикстура: феод поставлен дверью мира, а не записью в память");
}

// Сборщик — сквад рода Collector: дом ребром Parent, стоит ДОМА без дела и без
// цели. Куда идти, он решает сам, обходом вассалов дома.
MacroHandle make_collector(MacroStore& st, int x, int y, int homeId,
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
    ecs::goal_drop(st.goal[h.slot]);
    store_rename_ordinal(st, h, ordinal);
    h = handle_at(st, h.slot);
    set_relation(st, int(h.id), homeId, Stance::Parent);
    st.sheet[h.slot] = make_character_sheet(
        NPCType::Peasant, 3, avatar_sheet_seed(st.spawnId[h.slot].index));
    pools.hp = pools.maxHp = 30;
    st.pools[h.slot] = pools;
    return h;
}

// Мир фикстуры: город-дом, деревня-вассал, сборщик дома.
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

// Что лежит на полке вассала и где он стоит. `vassalBornFirst` переставляет
// порядок рождения — и с ним ординалы дома и вассала (контроль перестановкой
// у факта дани).
struct Shelf {
    int food = 0;
    int silver = 0;
    int cloth = 0;
    bool vassalBornFirst = false;
    int vx = 16;
    int vy = 10;
};

constexpr int kHomeSouls = 50;
constexpr int kHomeFoodBill = kHomeSouls * kDaysPerSeason;

void build_rig(Rig& r, const Shelf& k) {
    r.gs.mapW = kMap;
    r.gs.mapH = kMap;
    r.gs.worldTime = world_time_at(kFirstDay, 8, 0);
    chronicle_init(r.gs.chronicle, kMap, kMap);
    MacroStore& st = *r.store;

    const auto birth_vassal = [&] {
        r.vassal = birth_squad(r.gs, nullptr, st, SquadType::Village,
                               k.vx, k.vy);
        r.vassalId = int(st.spawnId[r.vassal.slot].index);
    };
    if (k.vassalBornFirst) birth_vassal();
    r.home = birth_squad(r.gs, nullptr, st, SquadType::City, 10, 10);
    r.homeId = int(st.spawnId[r.home.slot].index);
    settle_souls(r.gs, st, r.home.slot, kHomeSouls);
    // ДОМ ГОЛОДЕН СЧЁТОМ (CANON S10): «нет хлеба» есть непогашенный сезонный
    // счёт, и ровно из него модуль читает, чего дому не хватает. Полка еды
    // пуста НАМЕРЕННО — иначе прибавку не отличить от того, что уже лежало.
    st.upkeep[r.home.slot].needDebt[commodity_index("food")] =
        std::int32_t(kHomeFoodBill);

    if (!k.vassalBornFirst) birth_vassal();
    settle_souls(r.gs, st, r.vassal.slot, 30);
    {
        Inventory& row = st.inventory[r.vassal.slot].inv;
        if (k.food > 0) row.add("food", k.food);
        if (k.silver > 0) row.add("silver", k.silver);
        if (k.cloth > 0) row.add("cloth", k.cloth);
    }
    link_vassal(st, r.vassalId, r.homeId);

    r.man = make_collector(st, 10, 10, r.homeId, 17u);
}

void run_thinks(Rig& r, int thinks) {
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < thinks; ++i) {
        MacroWorld mw{.gs = &r.gs, .store = r.store.get()};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
}

// ПИК СПИНЫ СНИМАЕТСЯ В ХОДЕ ПРОГОНА, А НЕ ПОСЛЕ: сборщик ВЫГРУЖАЕТСЯ дома,
// поэтому спина после прогона почти всегда пуста — замер по итогу назвал бы
// перегруз нулём и прошёл бы зелёным при любом дефекте.
struct BackWatch {
    std::int64_t peakMassG = 0;
    std::int64_t capAtPeakG = 0;
    int samples = 0;
};

void run_thinks_watching_back(Rig& r, int thinks, BackWatch& w) {
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 70u);
    for (int i = 0; i < thinks; ++i) {
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

// ФАКТЫ ДАНИ О ВАССАЛЕ — сколько раз он платил. Факт пишет модуль в момент
// взятия, и он есть визит, оставивший след в мире.
int taxed_facts(const GameState& gs, int vassalId) {
    int n = 0;
    const Chronicle& c = gs.chronicle;
    for (std::uint32_t seq = 1; seq < c.nextSeq; ++seq) {
        const WorldFact* f = chronicle_fact(c, seq);
        if (!f || f->kind != std::uint16_t(FactKind::Taxed)) continue;
        if (f->subject == std::uint32_t(vassalId)) ++n;
    }
    return n;
}

// ГОЛОД ДОМА — СЧЁТ, И ПРИЕХАВШЕЕ ВИДНО ПО НЕМУ, А НЕ ПО ПОЛКЕ (CANON S10):
// привезённое в голодный дом гасит его сезонный счёт и СЪЕДАЕТСЯ в дверях
// прихода — полка держит только излишек.
int home_food_bill_paid(const Rig& r) {
    return kHomeFoodBill
        - int(r.store->upkeep[r.home.slot].needDebt[commodity_index("food")]);
}

// Еда весит килограмм за единицу; сорок кило — треть спины крестьянина,
// дорога переживаема: свидетель проверяет перенос дани, а не выносливость.
constexpr int kTitheFood = 40;
// ЛОТЫ СЕРЕБРА НАЗВАНЫ ЧИСЛОМ С ИМЕНЕМ, ПОТОМУ ЧТО ИХ ЧИТАЮТ ДВОЕ: фикстура,
// которая их кладёт, и проверка убыли у ИСТОЧНИКА.
constexpr int kTitheSilverLots = 20;

constexpr int kRunThinks = 600;

// ── ЗАКОН 1: дань доезжает ───────────────────────────────────────────────
void test_the_tithe_is_collected_and_comes_home() {
    Rig r;
    build_rig(r, Shelf{.food = kTitheFood});
    run_thinks(r, kRunThinks);

    MacroStore& st = *r.store;
    const int homeFood = st.inventory[r.home.slot].inv.count("food");
    const int vassalFood = st.inventory[r.vassal.slot].inv.count("food");
    const int billPaid = home_food_bill_paid(r);

    CHECK(taxed_facts(r.gs, r.vassalId) == 1,
          "дань ВЗЯТА — один визит, один факт");
    CHECK(billPaid > 0,
          "дань ДОЕХАЛА ДОМОЙ: привезённое НАКОРМИЛО голод сюзерена — его "
          "сезонный счёт еды упал (полка держит только излишек, CANON S10)");
    CHECK(vassalFood < kTitheFood,
          "у вассала УБЫЛО ровно потому, что увезли, а не напечатали дома");
    // ЯКОРЬ СОХРАНЕНИЯ: взыскание не творит материю и не жжёт её. Спина
    // сборщика считается наравне со складами — груз в пути тоже существует.
    CHECK(st.alive[r.man.slot] != 0 && st.dead[r.man.slot] == 0,
          "сборщик дожил до конца прогона — иначе счёт спины ниже лжёт");
    const auto* bag = body_state<ecs::NpcInventory>(st, r.man);
    const int inBag = bag ? bag->inv.count("food") : 0;
    // ЧЕТЫРЕ МЕСТА, ГДЕ МОЖЕТ БЫТЬ ЕДА, И БОЛЬШЕ НИГДЕ: склад вассала,
    // полка дома, спина в пути и СЪЕДЕННОЕ ПРОТИВ СЧЁТА.
    CHECK(vassalFood + homeFood + inBag + billPaid == kTitheFood,
          "СОХРАНЕНИЕ: у вассала убыло ровно столько, сколько легло дома, "
          "съедено против счёта и едет в спине — дань переносится, а не "
          "чеканится и не исчезает");
}

// ── ЗАКОН 2: ЯКОРЬ — груз выбирает оценка дома, а не плотность стоимости ──
//
// РАСКЛАД: у вассала сорок еды и двадцать лотов серебра. Квота — восьмая часть
// оценки полки — с серебром велика, и еда в неё ложится вся. По оценке дома
// (дом голоден счётом) еда на грамм дороже серебра — едет ВСЯ еда, остаток
// квоты берёт серебро; по плотности стоимости квота закрылась бы серебром, и
// еды не приехало бы ни одной.
void test_the_tithe_loads_by_home_need_before_value_density() {
    Rig r;
    build_rig(r, Shelf{.food = kTitheFood, .silver = kTitheSilverLots});
    run_thinks(r, kRunThinks);

    MacroStore& st = *r.store;
    const int foodArrived = home_food_bill_paid(r)
        + st.inventory[r.home.slot].inv.count("food");
    const int vassalFoodLeft = st.inventory[r.vassal.slot].inv.count("food");

    CHECK(foodArrived > 0,
          "ГРУЗ ВЫБРАН ОЦЕНКОЙ ДОМА: голодный дом получил ЕДУ, хотя серебро "
          "в 64 раза плотнее по стоимости");
    CHECK(vassalFoodLeft == 0,
          "то, что дом ценит плотнее, едет ПЕРВЫМ и ЦЕЛИКОМ: вся еда вассала "
          "уехала раньше, чем квота стала брать серебро");
    // ОСТАТОК КВОТЫ СПРАШИВАЕТСЯ У ИСТОЧНИКА, А НЕ У ПОЛКИ ДОМА: дом —
    // город, его анкета знает чеканку, и доехавшее серебро уходит в монету.
    const int vassalSilverLeft =
        st.inventory[r.vassal.slot].inv.count("silver");
    CHECK(vassalSilverLeft < kTitheSilverLots,
          "а ОСТАТОК квоты взят следующей строкой — серебро УШЛО СО СКЛАДА "
          "ВАССАЛА (иначе якорь выше не отличал бы порядок от «серебро "
          "вообще не берут»)");
    CHECK(inventory_value(st.inventory[r.home.slot].inv) > 0,
          "и доехало оно ДОМОЙ стоимостью — чем бы дом её ни сделал");
}

// ── ЗАКОН 3: СПИНА НЕ ПРЕВЫШАЕТ `carry_capacity_g` ───────────────────────
//
// ЧТО ЭТО СТЕРЕЖЁТ (M-308). До M-308 у загрузки сборщика было две ступени, и
// они спрашивали спину ПО-РАЗНОМУ: ветка нужды дома клампила остатком
// ёмкости, а остаток долга по плотности звал `transfer_value_dense` без
// параметра массы — два закона ёмкости на один вопрос мира (DOD п.6).
// Теперь механизм один, и предел спины у него — граница лота строки на плане
// плюс `roomG` сделки.
//
// РАСКЛАД: две тонны еды при спине около 154 кг, и квота велика (серебро на
// полке) — связывать обязана спина.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» ЭТОГО ЗАКОНА (M-308, глагол `collect_tithe` того
// времени; контроль — OK, 19 проверок, пик 154 000 г при ёмкости 154 000 г):
//   · предел второй ступени снят       →  КРАСНО, 1 из 19; пик 2 000 000 г
//   · предел — полная ёмкость вместо ОСТАТКА →  КРАСНО, 1 из 19; пик 308 000 г
//   · балансное число: еда фикстуры 2000 → 1800 лотов → МОЛЧИТ
//   · ранний выход до вердиктов этого закона → без пина счёта ЗЕЛЁНЫЙ; дыра
//        закрыта пином счёта в `CMakeLists.txt`.
constexpr int kOverloadFood = 2000;   // 2000 кг против ~154 кг спины
constexpr int kOverloadSilver = 200;  // квота велика — связывает спина

void test_the_back_never_exceeds_its_carry_capacity() {
    Rig r;
    build_rig(r, Shelf{.food = kOverloadFood, .silver = kOverloadSilver});

    BackWatch w;
    run_thinks_watching_back(r, kRunThinks, w);

    CHECK(w.samples > 0,
          "спина СНЯТА хоть раз — иначе ноль перегрузов ниже значит ноль "
          "замеров, а не целый закон");
    CHECK(w.peakMassG > 0,
          "фикстура: сборщик РЕАЛЬНО грузился — иначе предел соблюдён "
          "пустотой и прибор ничего не стережёт");
    CHECK(w.capAtPeakG > 0,
          "фикстура: ёмкость спины положительна — иначе отношение ниже "
          "сравнивает с нулём и зелено при любом грузе");
    CHECK(w.peakMassG <= w.capAtPeakG,
          "СПИНА НЕ ПРЕВЫШАЕТ `carry_capacity_g`: один закон ёмкости, а не "
          "два (M-308)");
    CHECK(w.peakMassG * 2 >= w.capAtPeakG,
          "предел СВЯЗАЛ: спина набралась хотя бы наполовину, значит "
          "проверка выше прошла клампом, а не пустотой");
    std::printf("  спина: пик %lld г при ёмкости %lld г, замеров %d; "
                "у вассала еды %d\n",
                (long long)w.peakMassG, (long long)w.capAtPeakG, w.samples,
                r.store->inventory[r.vassal.slot].inv.count("food"));
}

// ── ЗАКОНЫ 5: КВОТА НА МЕСТЕ ─────────────────────────────────────────────
struct FirstTake {
    bool seen = false;
    int food = 0;
    int cloth = 0;
    int silver = 0;
    std::int64_t vassalLoss = 0;     // во сколько вассал ценил отданное
    std::int64_t shelfAfter = 0;     // во сколько он ценит полку после
    std::int64_t bagG = 0;           // масса взятого
    std::int64_t capG = 0;           // ёмкость спины сборщика в этот тик
};

// Оценка полки В МОМЕНТ ПРИХОДА — нынешняя плюс отданное (оценка запаса —
// функция состояния: потеря n единиц есть разность двух оценок запаса).
std::int64_t shelf_at_arrival(const FirstTake& t) {
    return t.shelfAfter + t.vassalLoss;
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
        MacroWorld mw{.gs = &r.gs, .store = r.store.get()};
        tick_macro_npc_ai(mw, rt, 1);
        MacroStore& st = *r.store;
        if (!st.valid(r.man)) continue;
        const Inventory& bag = st.inventory[r.man.slot].inv;
        if (inventory_mass_g(bag) <= 0) continue;
        out.seen = true;
        out.food = bag.count("food");
        out.cloth = bag.count("cloth");
        out.silver = bag.count("silver");
        out.bagG = inventory_mass_g(bag);
        out.capG = carry_capacity_g(st.runtime[r.man.slot], bag);
        out.shelfAfter = side_worth(st, r.vassal.slot);
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

constexpr int kWatchTicks = kRunThinks * int(kAiTicks);

// (а) ВЗЯТО НЕ БОЛЬШЕ ВОСЬМОЙ ЧАСТИ ОЦЕНКИ ПОЛКИ В МОМЕНТ ПРИХОДА, И С
// БОГАТОЙ ПОЛКИ БОЛЬШЕ, ЧЕМ С БЕДНОЙ. Две полки одной строки (еда), разница
// только в запасе; спина свободна в обоих раскладах, связывает квота.
// Утверждаются отношения, не числа.
void test_the_take_is_an_eighth_of_the_shelf_at_arrival() {
    Rig poor;
    build_rig(poor, Shelf{.food = kTitheFood});
    const FirstTake a = watch_first_take(poor, kWatchTicks);
    Rig rich;
    build_rig(rich, Shelf{.food = 5 * kTitheFood});
    const FirstTake b = watch_first_take(rich, kWatchTicks);
    CHECK(a.seen && b.seen && a.food > 0 && b.food > 0,
          "фикстура: в обоих раскладах сборщик взял еду");
    CHECK(a.bagG < a.capG && b.bagG < b.capG,
          "фикстура: спина НЕ полна — связывать здесь обязана квота");
    CHECK(8 * a.vassalLoss <= shelf_at_arrival(a)
              && 8 * b.vassalLoss <= shelf_at_arrival(b),
          "ВЗЯТО НЕ БОЛЬШЕ ВОСЬМОЙ ЧАСТИ того, во сколько вассал ценил свою "
          "полку в момент прихода");
    CHECK(b.vassalLoss > a.vassalLoss,
          "С БОГАТОЙ ПОЛКИ БЕРЁТСЯ БОЛЬШЕ: квота растёт с оценкой полки");
    std::printf("  (а) бедная полка %lld → отдано %lld (%d еды); богатая "
                "%lld → отдано %lld (%d еды)\n",
                (long long)shelf_at_arrival(a), (long long)a.vassalLoss,
                a.food, (long long)shelf_at_arrival(b),
                (long long)b.vassalLoss, b.food);
}

// (б) ГРУЗ ВЫБИРАЕТ ОЦЕНКА ДОМА СЮЗЕРЕНА. У вассала две строки по тысяче штук
// одной массы — еда и ткань, — а спина сборщика берёт лишь одну из них
// целиком; серебро на полке делает квоту большой — связывает спина. Дом
// голоден ровно по одной (счёт 1600 при пустой полке), и это переставляется:
// первой едет та, которую дом ценит плотнее, и вторую спина уже не берёт.
// Перестановка — контроль на то, что порядок задаёт оценка дома, а не словарь
// (еда в нём раньше ткани), не плотность стоимости и не оценка вассала.
void test_the_load_is_chosen_by_the_suzerains_valuation() {
    struct Case { int foodLack; int clothLack; };
    const Case cases[] = {{1600, 0}, {0, 1600}};
    int foodFirst = 0, clothFirst = 0;
    for (const Case& k : cases) {
        Rig r;
        build_rig(r, Shelf{.food = 1000, .silver = kOverloadSilver,
                           .cloth = 1000});
        bill_home(r, k.foodLack, k.clothLack);
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
              "не взяла СПИНА, а не малая квота");
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

// (в) ОСТАТОК ПРОЩЁН. Тысяча еды и серебро у вассала рядом с домом — квота
// велика, а спина сборщика тесна: унесено меньше квоты. Остаток не помнит
// никто — второго визита к тому же вассалу в этом сезоне нет, и полка его
// после первого визита больше не убывает.
void test_the_remainder_is_forgiven() {
    Rig r;
    build_rig(r, Shelf{.food = 1000, .silver = kOverloadSilver,
                       .vx = 12, .vy = 10});
    const FirstTake got = watch_first_take(r, kWatchTicks);
    CHECK(got.seen && got.food > 0, "фикстура: сборщик взял еду");
    CHECK(got.bagG * 2 >= got.capG,
          "фикстура: связала СПИНА — она набралась хотя бы наполовину");
    CHECK(8 * got.vassalLoss < shelf_at_arrival(got),
          "ТЕСНАЯ СПИНА УНЕСЛА МЕНЬШЕ КВОТЫ: отдано меньше восьмой части "
          "оценки полки в момент прихода");
    const int foodAfterVisit =
        r.store->inventory[r.vassal.slot].inv.count("food");

    run_thinks(r, 2 * kRunThinks);

    MacroStore& st = *r.store;
    CHECK(st.alive[r.man.slot] != 0 && st.dead[r.man.slot] == 0,
          "сборщик дожил до конца прогона — иначе «второго визита нет» "
          "значило бы «некому идти»");
    CHECK(taxed_facts(r.gs, r.vassalId) == 1,
          "ОСТАТОК ПРОЩЁН: второго визита к тому же вассалу в этом сезоне "
          "нет — факт дани один");
    CHECK(st.inventory[r.vassal.slot].inv.count("food") == foodAfterVisit,
          "и полка вассала после визита больше не убывала");
    CHECK(st.cell[r.man.slot].idx == st.cell[r.home.slot].idx,
          "сборщик стоит ДОМА до следующего сезона");
}

// (г) ОБХОД: ЗА СЕЗОН КАЖДЫЙ ВАССАЛ — РОВНО ОДИН РАЗ, В НОВОМ СЕЗОНЕ — СНОВА.
// Два вассала одного дома, у каждого лёгкая полка; сезон фикстуры ставят часы
// мира (думки их не двигают).
void test_the_tour_visits_each_vassal_once_a_season() {
    Rig r;
    build_rig(r, Shelf{.food = kTitheFood, .vx = 12, .vy = 10});
    MacroStore& st = *r.store;
    const MacroHandle second =
        birth_squad(r.gs, nullptr, st, SquadType::Village, 10, 12);
    settle_souls(r.gs, st, second.slot, 30);
    st.inventory[second.slot].inv.add("food", kTitheFood);
    const int secondId = int(st.spawnId[second.slot].index);
    link_vassal(st, secondId, r.homeId);

    run_thinks(r, 3 * kRunThinks);
    const int firstOnce = taxed_facts(r.gs, r.vassalId);
    const int secondOnce = taxed_facts(r.gs, secondId);
    CHECK(firstOnce == 1 && secondOnce == 1,
          "ЗА СЕЗОН КАЖДЫЙ ВАССАЛ ПОСЕЩЁН РОВНО ОДИН РАЗ: обход идёт по "
          "одному за рейс и не возвращается к посещённому");
    CHECK(st.runtime[r.man.slot].mem[kCollectorMemCursor] == 2,
          "курсор обхода насчитал двух посещённых");

    r.gs.worldTime = world_time_at(kNextSeasonDay, 8, 0);
    run_thinks(r, 3 * kRunThinks);
    const int firstTwice = taxed_facts(r.gs, r.vassalId);
    const int secondTwice = taxed_facts(r.gs, secondId);
    CHECK(firstTwice == 2 && secondTwice == 2,
          "В НОВОМ СЕЗОНЕ ОБХОД НАЧАЛСЯ СНОВА: каждый вассал посещён ещё "
          "ровно раз");

    // ТОТ ЖЕ СЕЗОН ГОДА, НО ГОД СПУСТЯ — ЭТО ДРУГОЙ СЕЗОН МИРА. Номер сезона —
    // монотонный ординал (`season_ordinal@src/tables/seasons.h`): сезон года
    // повторил бы номер прошлого обхода, и сборщик счёл бы год спустя «тем
    // же сезоном» — обхода не было бы.
    r.gs.worldTime = world_time_at(kNextSeasonDay + kDaysPerYear, 8, 0);
    run_thinks(r, 3 * kRunThinks);
    CHECK(taxed_facts(r.gs, r.vassalId) == 3
              && taxed_facts(r.gs, secondId) == 3,
          "В ТОМ ЖЕ СЕЗОНЕ СЛЕДУЮЩЕГО ГОДА ОБХОД НАЧИНАЕТСЯ СНОВА: сезон мира "
          "— ординал, и через год он не повторяется");
    std::printf("  (г) визитов: первый сезон %d и %d; второй %d и %d; тот же "
                "сезон год спустя %d и %d\n", firstOnce, secondOnce,
                firstTwice, secondTwice, taxed_facts(r.gs, r.vassalId),
                taxed_facts(r.gs, secondId));
}

// (е) ФАКТ ДАНИ — «ВАССАЛ ЗАПЛАТИЛ СЮЗЕРЕНУ»: субъект — вассал, объект —
// сюзерен (`Taxed@src/bus/fact.h`), число — во сколько вассал ценит отданное.
// Контроль перестановкой: во втором раскладе вассал рождён ПЕРВЫМ, и его
// ординал меньше ординала дома, — правило, назвавшее бы субъектом «меньший
// ординал» или дом звонящего, краснеет хотя бы в одном из двух.
void test_the_tithe_fact_names_the_vassal_as_the_payer() {
    int facts = 0, wrong = 0, orders = 0, amounts = 0;
    for (const bool vassalFirst : {false, true}) {
        Rig r;
        build_rig(r, Shelf{.food = kTitheFood, .vassalBornFirst = vassalFirst});
        orders += (r.vassalId < r.homeId) == vassalFirst ? 1 : 0;
        const FirstTake got = watch_first_take(r, kWatchTicks);
        CHECK(got.seen && got.vassalLoss > 0, "фикстура: дань взята");
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
            amounts += std::int64_t(f->amount) == got.vassalLoss ? 1 : 0;
        }
    }
    CHECK(orders == 2,
          "фикстура: порядок рождения переставил ординалы дома и вассала");
    CHECK(facts == 2, "факт дани записан в обоих раскладах, по одному");
    CHECK(wrong == 0,
          "ФАКТ ДАНИ: субъект — ВАССАЛ, объект — СЮЗЕРЕН, при любом порядке "
          "ординалов");
    CHECK(amounts == 2,
          "ЧИСЛО ФАКТА — во сколько вассал ценит отданное, второго счёта нет");
}

// (ж) `side_worth` МЕРИТ ТО, ЧТО ЛЕЖИТ НА ПОЛКЕ — мера квоты. Сквад с вещью
// на полке и НЕПОГАШЕННЫМ счётом по ней (выпуск крафта до суточного гашения)
// ценит полку неотрицательно и НЕ НИЖЕ, чем ту же полку без счёта: отдать
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

} // namespace

// ── ЗАКОН 4: ОСТАТОК ЧУЖОЙ ПОГОНИ В ЦЕЛИ ЦЕЛЬЮ РЕЙСА НЕ ЧИТАЕТСЯ (M-459) ──
// Род цели «сквад» один: вассала рейса и добычу погони называет ОДИН тег с
// ординалом; «из стоящих ли он» отвечает строка САМОГО названного — ось его
// типа, которую гейтит резолвер (`rooted_handle_by_ordinal@src/bus/store.h`).
// Цель — колонка общая: рефлекс угрозы пишет в неё погоню, а `goal_drop`
// после стычки оставляет «что я искал». Сборщик, проснувшийся НЕ дома с
// остатком погони за бродягой, обязан не поехать к бродяге, а вернуться домой
// и пойти обходом к своему вассалу.
void test_a_leftover_chase_is_not_read_as_the_vassal() {
    Rig r;
    build_rig(r, Shelf{.food = kTitheFood});
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

    // Сборщик ВНЕ ДОМА, в цели — погоня, от адреса которой он уже отказался.
    st.cell[r.man.slot] = ecs::MacroCell{ecs::cell_index(13, 13, kMap)};
    ecs::SquadGoal& g = st.goal[r.man.slot];
    ecs::goal_set(g, st.cell[roamer.slot].idx,
                  ecs::goal_what(ecs::GoalKind::Squad, roamerId), 0,
                  std::uint8_t(NPCState::Chasing));
    ecs::goal_drop(g);
    st.runtime[r.man.slot].state = std::uint8_t(NPCState::Idle);
    CHECK(ecs::goal_ordinal(g.what) == roamerId
              && ecs::goal_kind(g.what) == ecs::GoalKind::Squad,
          "фикстура: в колонке цели лежит остаток погони за бродягой");

    run_thinks(r, kRunThinks);

    CHECK(ecs::goal_ordinal(g.what) != roamerId,
          "остаток погони ПЕРЕРЕШЁН: бродяга целью рейса не прочитан");
    CHECK(taxed_facts(r.gs, r.vassalId) == 1,
          "и дань ВЗЯТА — сборщик вернулся домой и нашёл своего вассала "
          "обходом сам");
}

int main() {
    test_the_tithe_is_collected_and_comes_home();
    test_the_tithe_loads_by_home_need_before_value_density();
    test_the_back_never_exceeds_its_carry_capacity();
    test_a_leftover_chase_is_not_read_as_the_vassal();
    test_the_take_is_an_eighth_of_the_shelf_at_arrival();
    test_the_load_is_chosen_by_the_suzerains_valuation();
    test_the_remainder_is_forgiven();
    test_the_tour_visits_each_vassal_once_a_season();
    test_the_tithe_fact_names_the_vassal_as_the_payer();
    test_the_shelf_under_an_unpaid_bill_is_worth_no_less();
    return sm::test::report("collector_tithe_test");
}
