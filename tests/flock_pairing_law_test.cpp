// ЗАКОН ПАРЫ: У ДУШИ ДВА НОСИТЕЛЯ, И ОНИ ХОДЯТ ВМЕСТЕ.
//
// Паства поселения отражена ДВАЖДЫ по построению (ЗАКОН КОНТЕЙНЕРА п.3):
// числом слоя `worked` на клетке фичи и ГОЛОВАМИ в инвентаре сквада. Это не
// второй словарь — это два ответа на РАЗНЫЕ вопросы («сколько их всего»
// против «кто стоит здесь»), — но писать их порознь запрещено. Приход сведён
// давно (`settle_souls@src/macro/labour.h`); убыль сводится здесь.
//
// ЧТО ИМЕННО УТВЕРЖДАЕТСЯ: сколько ЧЕЛОВЕЧЕСКИХ душ вышло из контейнера,
// ровно на столько падает паства его дома — какой бы дверью они ни вышли.
// Не «падает», не «падает примерно», а РОВНО: число есть ФАКТ убыли, а не
// намерение звонящего (всякая дверь вправе взять меньше запрошенного).
//
// ЦЕНА ДЫРЫ, ЧИСЛОМ — ЗАМЕР `balance_run 12345,1 × 48` ДО ПОЧИНКИ. На
// границе сезона (день 33) голодных смертей 51 322 и 51 544, а Σ`worked`
// городов и деревень ВЫРОСЛА на 1 827 и 1 840 — ровно на рождения дня, то
// есть не списалась ни на одну душу. Σ`starvedYesterday` по всем 2190 и 2213
// поселениям при этом НОЛЬ: дневная дверь не убила никого, все смерти дала
// сезонная, у которой пары не было вовсе. Мир продолжал кормить, растить,
// поднимать сквады и облагать данью 51 тысячу людей, которых у него нет.
//
// ПОЧЕМУ ЭТОГО НЕ ВИДЕЛ НИ ОДИН СВИДЕТЕЛЬ: ни один не спрашивал ДВА носителя
// в одном утверждении. Прибор печатал оба числа рядом с первого дня и
// расхождение в 275 тысяч голов никого не разбудило, потому что сравнивать
// их было не велено.
#include "check.h"

#include <cstdint>
#include <memory>

#include "macro/labour.h"        // settle_souls / souls_flock — двери паствы
#include "macro/macro_stock.h"   // квитанция боя — строка Creatures
#include "macro/macro_world.h"
#include "macro/npc_ai.h"        // squad_season_window — сезонная дверь
#include "macro/place_birth.h"   // birth_place — место родится телом
#include "macro/squad.h"
#include "macro/store.h"
#include "macro/upkeep_window.h"
#include "macro/world_row.h"
#include "tables/npc.h"
#include "tables/seasons.h"

namespace {

// ── ФИКСТУРА РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ САМА (§8 п.11) ─────────────────────
// Ни одно утверждение ниже не надеется, что генератор на каком-то сиде даст
// нужное место: мир здесь пуст, и всё, что судится, поставлено руками.
struct World {
    sm::GameState gs{};
    sm::ecs::World world;
    std::unique_ptr<sm::MacroStore> store = sm::make_macro_store();

    World() {
        // Сторона — степень двойки и квадрат (ЗАКОН АДРЕСА); мир крошечный,
        // потому что судится арифметика пары, а не размер тора.
        gs.mapW = 64;
        gs.mapH = 64;
        sm::store_attach(world, store.get());
    }

    sm::MacroWorld envelope() {
        return sm::MacroWorld{.gs = &gs, .world = &world, .store = store.get()};
    }
};

// Место с ПАСТВОЙ: `settle_souls` — единственная дверь появления души, и она
// пишет ОБА носителя, поэтому фикстура согласована по построению.
std::uint16_t born_place(World& w, sm::SquadType kind, int x, int y,
                         int souls) {
    const sm::MacroHandle h =
        sm::birth_place(w.gs, *w.store, kind, x, y, 0, "Fixture");
    sm::settle_souls(w.gs, *w.store, h.slot, souls);
    return h.slot;
}

int flock_of(World& w, std::uint16_t slot) {
    return sm::souls_flock(w.gs, *w.store, slot);
}
int folk_of(World& w, std::uint16_t slot) {
    return sm::count_human_souls(w.store->inventory[slot].inv);
}

// ── 1. СЕЗОННАЯ ДВЕРЬ: ГОЛОД СНИМАЕТ ОБА НОСИТЕЛЯ ────────────────────────
// Это тот самый путь, что давал 51 322 призрака за границу. Недоимка
// выставляется ПРЯМО — это ВХОД судимой функции, и другого способа получить
// ЧАСТИЧНУЮ долю у двери нет: её рождает только наполовину оплаченный сезон.
void test_the_season_window_pays_the_flock() {
    World w;
    const std::uint16_t slot = born_place(w, sm::SquadType::City, 10, 10, 8);
    CHECK(flock_of(w, slot) == 8 && folk_of(w, slot) == 8,
          "фикстура: оба носителя согласны — паства 8 и голов 8");

    const int boardOrd = sm::hunger_commodity_ordinal();
    CHECK(boardOrd >= 0, "фикстура: у мира есть голодная строка");
    const sm::UpkeepBill bill =
        sm::upkeep_bill(w.store->inventory[slot].inv);
    CHECK(bill.board > 0,
          "фикстура: состав из людей несёт рацион — без этого доля голода "
          "неразличима по построению");
    // Половина харча не покрыта ⇒ половина состава обязана умереть.
    w.store->upkeep[slot].needDebt[boardOrd] = std::int32_t(bill.board / 2);

    sm::MacroWorld mw = w.envelope();
    // День ГРАНИЦЫ: окно гейтится `season_boundary`, и вне её оно no-op.
    const int day = 1 + sm::kDaysPerSeason;
    CHECK(sm::season_boundary(day), "фикстура: день 33 есть граница сезона");
    sm::squad_season_window(mw, day);

    const int folkAfter = folk_of(w, slot);
    const int flockAfter = flock_of(w, slot);
    CHECK(folkAfter == 4,
          "половина непокрытого харча = половина состава умерла (8 × 1/2)");
    CHECK(flockAfter == folkAfter,
          "ЗАКОН ПАРЫ: паства упала РОВНО на умерших — оба носителя сошлись");
    // НЕГАТИВНЫЙ КОНТРОЛЬ ДЕФЕКТА, КОТОРЫЙ ЭТОТ ФАЙЛ ЗАКРЫВАЕТ: до починки
    // паства не падала вовсе, то есть осталась бы восьмёркой при четырёх
    // головах. Утверждение названо отдельно от равенства выше, чтобы провал
    // печатал ИМЕННО эту болезнь, а не «числа не равны».
    CHECK(flockAfter != 8,
          "паства, не упавшая на границе ни на одну душу, и есть дефект "
          "51 322 призраков — мир кормил бы людей, которых нет");
}

// ── 2. КВИТАНЦИЯ БОЯ: ТА ЖЕ ПАРА У СТРОКИ `Creatures` ────────────────────
// Строка субъекта не фильтрует род, и квитанции авто-боя приходят на
// поселения: набег на деревню снимал головы и паству не трогал.
void test_the_creature_receipt_pays_the_flock() {
    World w;
    const std::uint16_t slot = born_place(w, sm::SquadType::Village, 20, 20, 5);
    const int id = int(w.store->spawnId[slot].index);
    CHECK(flock_of(w, slot) == 5 && folk_of(w, slot) == 5,
          "фикстура: паства 5 и голов 5");

    sm::MacroWorld mw = w.envelope();
    // Генерик называется ПАРОЙ {род, уровень} — у стака нет entityId.
    sm::MacroStockKey key{id, 0, 0, -1,
                          std::uint16_t(sm::NPCType::Peasant),
                          std::int16_t(sm::npc_def(sm::NPCType::Peasant).baseLevel)};
    sm::macro_stock_apply(mw, sm::MacroStock::Creatures, key, -2);

    CHECK(folk_of(w, slot) == 3, "квитанция на двоих сняла ровно двоих");
    CHECK(flock_of(w, slot) == 3,
          "ЗАКОН ПАРЫ: паства упала на тех же двоих");
}

// ── 3. КОНЬ — ИМУЩЕСТВО, А НЕ ДУША ───────────────────────────────────────
// Вторая половина дефекта, и она про ЧИСЛО, а не про наличие записи: до
// починки из паствы вычиталась ЛЮБАЯ павшая голова, потому что счёт шёл
// `creature_count`/`starvedPop`, а приход в паству идёт строкой Peasant.
// Сегодня латентно — табун у мест нулевой, — поэтому свидетель ставит его
// себе сам.
void test_a_dead_horse_is_not_a_dead_soul() {
    World w;
    const std::uint16_t slot = born_place(w, sm::SquadType::Village, 30, 30, 4);
    sm::Inventory& inv = w.store->inventory[slot].inv;
    CHECK(sm::creatures_push_stack(inv, sm::NPCType::Horse,
                                   sm::npc_def(sm::NPCType::Horse).baseLevel, 3),
          "фикстура: в стойле места стоят три коня");
    CHECK(flock_of(w, slot) == 4 && sm::creature_count(inv) == 7,
          "фикстура: паства людская (4), а голов в контейнере семь");

    sm::MacroWorld mw = w.envelope();
    const int id = int(w.store->spawnId[slot].index);
    sm::MacroStockKey horseKey{id, 0, 0, -1,
                               std::uint16_t(sm::NPCType::Horse),
                               std::int16_t(sm::npc_def(sm::NPCType::Horse).baseLevel)};
    sm::macro_stock_apply(mw, sm::MacroStock::Creatures, horseKey, -3);

    CHECK(sm::creature_count(inv) == 4, "три коня сняты с контейнера");
    CHECK(flock_of(w, slot) == 4,
          "падёж табуна человеческую паству не трогает: конь — имущество, "
          "и в worked его никогда не вносили");
}

// ── 4. У ДАНЖА ПАРЫ НЕТ, И ЭТО ЗНАЧЕНИЕ, А НЕ ИСКЛЮЧЕНИЕ ─────────────────
// Паства данжа И ЕСТЬ головы его толпы, а `worked` под его клеткой занят
// чужим числом (под FT_Spire живёт спелл). Дверь пары уходит оттуда пустой
// по КОЛОНКЕ `bornPopBase`, без единой ветки по роду у звонящих.
void test_a_dungeon_has_no_second_carrier() {
    World w;
    const std::uint16_t slot = born_place(w, sm::SquadType::Spire, 40, 40, 6);
    CHECK(sm::landmark_def(sm::SquadType::Spire).bornPopBase != 0,
          "фикстура: шпиль есть данж по колонке строки, а не по имени");
    // Чужое число на клетке шпиля — ровно то, что дверь обязана не трогать.
    sm::worked_write(w.gs, 40, 40, 7);
    const int before = sm::worked_read(w.gs, 40, 40);

    sm::MacroWorld mw = w.envelope();
    const int id = int(w.store->spawnId[slot].index);
    const sm::NPCType crowd = sm::weakest_crowd_kind(sm::SquadType::Spire);
    CHECK(crowd != sm::NPCType::Count,
          "фикстура: у полосы толпы шпиля есть слабейшая строка");
    sm::MacroStockKey key{id, 0, 0, -1, std::uint16_t(crowd),
                          std::int16_t(sm::npc_def(crowd).baseLevel)};
    sm::macro_stock_apply(mw, sm::MacroStock::Creatures, key, -2);

    CHECK(sm::worked_read(w.gs, 40, 40) == before,
          "душа данжа ушла, а чужое число клетки не шевельнулось — списать "
          "там значило бы гасить спелл шпиля");

    // ── И ВТОРОЙ ПУТЬ К ТОЙ ЖЕ КЛЕТКЕ, БЕЗ КОТОРОГО УТВЕРЖДЕНИЕ ВЫШЕ СЛЕПО
    // Толпа шпиля НЕ ЧЕЛОВЕЧЕСКАЯ (слабейшая строка полосы — Imp), поэтому
    // счёт людей у неё ноль с обоих концов, и до выхода по колонке
    // `bornPopBase` дело не доходит вовсе: мутация «выход снят» прошла
    // ЗЕЛЁНОЙ. А путь, который этот выход правда охраняет, другой — СКВАД С
    // ЧЕЛОВЕЧЕСКИМИ ДУШАМИ, ЧЕЙ ДОМ ЕСТЬ ДАНЖ (банда из логова: «банды это
    // население ландмарка логово бандитов»). Его убыль человеческая, дом
    // разрешается, и только колонка не пускает списание в чужое число.
    const sm::MacroHandle band =
        sm::birth_place(w.gs, *w.store, sm::SquadType::Artel, 41, 41, 0,
                        "Band");
    w.store->runtime[band.slot].homeSettlementId = id;   // дом банды — шпиль
    CHECK(sm::creatures_push_stack(
              w.store->inventory[band.slot].inv, sm::NPCType::Peasant,
              sm::npc_def(sm::NPCType::Peasant).baseLevel, 3),
          "фикстура: в банде три ЧЕЛОВЕЧЕСКИЕ души");
    const int spellBefore = sm::worked_read(w.gs, 40, 40);
    sm::MacroStockKey bandKey{int(w.store->spawnId[band.slot].index), 0, 0, -1,
                              std::uint16_t(sm::NPCType::Peasant),
                              std::int16_t(sm::npc_def(
                                  sm::NPCType::Peasant).baseLevel)};
    sm::macro_stock_apply(mw, sm::MacroStock::Creatures, bandKey, -3);
    CHECK(sm::count_human_souls(w.store->inventory[band.slot].inv) == 0,
          "фикстура: банда действительно потеряла всех трёх — иначе списывать "
          "было бы нечего и утверждение ниже проверяло бы не то");
    CHECK(sm::worked_read(w.gs, 40, 40) == spellBefore,
          "человеческие души банды вышли из мира, а число клетки ДАНЖА не "
          "двинулось: под FT_Spire живёт спелл, и паствы там нет вовсе");
}

// ── 5. ОТКАЗ ПРИЁМНИКА — ЭТО ФАКТ, И ПАСТВА ЧИТАЕТ ФАКТ ──────────────────
// Ходок, которого пул не принял, ВОЗВРАЩАЕТСЯ в состав. Списать с паствы
// «намерение» значило бы вычесть человека, который остался стоять, — ровно
// то, от чего измерение до/после и защищает.
void test_a_refused_walker_stays_in_the_flock() {
    sm::Inventory store{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        sm::creatures_push(store, sm::make_soldier(
            std::uint8_t(sm::NPCType::Guard), 1, 500u + i));
    }
    const sm::UpkeepBill bill = sm::upkeep_bill(store);
    CHECK(bill.wage > 0,
          "фикстура: строка Guard несёт жалованье — без него исход «уход» "
          "недостижим вовсе");
    sm::Upkeep r{};
    r.wageDebt = bill.wage;            // не плачено НИЧЕГО ⇒ уходят все
    const int boardOrd = sm::hunger_commodity_ordinal();
    r.needDebt[boardOrd] = 0;          // харч покрыт: смерть не вмешивается

    // Пул, который НЕ МОЖЕТ принять: область существ забита доверху.
    //
    // НАБИВАЕТСЯ ИМЕННЫМИ, И ЭТО НЕ ПРИДИРКА К ФИКСТУРЕ, А СВОЙСТВО
    // КОНТЕЙНЕРА: генерик сливается в ОДИН слот стаком (`add_ref`), поэтому
    // пулом генериков этот кап недостижим вовсе — первая редакция свидетеля
    // набила 1025 крестьян в один слот и объявила пул полным, которым он не
    // был. Именная душа берёт слот себе (`entityId != 0`), и только ею кап
    // области и выражается.
    sm::Inventory pool{};
    int packed = 0;
    while (sm::creatures_push(pool, sm::make_soldier(
               std::uint8_t(sm::NPCType::Peasant), 1,
               std::uint32_t(1000 + packed)))) {
        ++packed;
    }
    CHECK(packed > 0 && !sm::creatures_push(pool, sm::make_soldier(
              std::uint8_t(sm::NPCType::Peasant), 1, 999999u)),
          "фикстура: пул действительно полон — иначе отказ недостижим и "
          "утверждение ниже проверяло бы не то");

    std::int64_t burned = 0;
    const int before = sm::creature_count(store);
    const sm::UpkeepWindowOutcome out =
        sm::upkeep_season_window(r, store, pool, burned, nullptr, nullptr);
    CHECK(out.walked == 0,
          "пул отказал — ушедших НОЛЬ, и это исход, а не ошибка");
    CHECK(sm::creature_count(store) == before,
          "человек, которого некуда положить, остался в составе целиком: "
          "за капом никого не уничтожают (CANON S26)");
}

}   // namespace

int main() {
    test_the_season_window_pays_the_flock();
    test_the_creature_receipt_pays_the_flock();
    test_a_dead_horse_is_not_a_dead_soul();
    test_a_dungeon_has_no_second_carrier();
    test_a_refused_walker_stays_in_the_flock();
    return sm::test::report("flock_pairing_law_test");
}
