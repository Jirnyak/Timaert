// The integer clock, tested as a LAW rather than as a table of numbers: what an
// advance reports must equal what the ladder itself derives, and how an advance
// is chopped up must not change where it lands.
//
// This file was green for months while asserting nothing (`int fail()` returned
// into a `bool` — every failure read as PASS). It now goes through tests/check.h,
// where a verdict is not something a function can return, and a test that runs
// zero checks fails by counting.
#include "check.h"
#include <cmath>
#include <memory>

#include "tables/npc.h"
#include "macro/labour.h"       // settle_souls / souls_home / souls_flock
#include "macro/place_birth.h"  // birth_place — место родится ТЕЛОМ
#include "macro/world_row.h"
#include "macro/npc_ai.h"   // squad_season_window — THE boundary window
#include "macro/upkeep_window.h"   // upkeep_season_window — сама дверь суда
#include "macro/world_tick.h"
#include "macro/player_entity.h"
#include "macro/macro_world.h"
#include "macro/currency.h"
#include "macro/store.h"

#include <cstdint>
#include <cstdio>

namespace {

// One game minute forward from wherever the clock stands — exact from any
// phase (core/time.h ticks_to_advance_minutes).
std::uint64_t one_minute(const sm::GameState& gs) {
    return sm::ticks_to_advance_minutes(gs.worldTime.tick, 1);
}

void test_hour_rollover() {
    sm::GameState gs{};
    gs.worldTime = sm::world_time_at(7, 6, 59);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 123u);

    const sm::WorldTickResult result =
        sm::advance_world_clock(gs, runtime, one_minute(gs));

    CHECK(result.minutesAdvanced == 1 && result.hoursAdvanced == 1
              && result.daysAdvanced == 0,
          "one minute across 06:59 reports one minute and one hour, no day");
    CHECK(gs.worldTime.day() == 7 && gs.worldTime.hour() == 7
              && gs.worldTime.minute() == 0,
          "one minute rolls 06:59 to 07:00 of the same day");
    CHECK(runtime.pendingDailyTicks == 0 && runtime.nextDailyTickDay == 0,
          "an hour rollover that is not midnight queues no daily work");
}

void test_day_rollover_queues_budgeted_daily_tick() {
    sm::GameState gs{};
    gs.worldTime = sm::world_time_at(7, 23, 59);
    gs.player.ageDays = 1000;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 456u);

    const sm::WorldTickResult result =
        sm::advance_world_clock(gs, runtime, one_minute(gs));

    CHECK(result.minutesAdvanced == 1 && result.hoursAdvanced == 1
              && result.daysAdvanced == 1,
          "one minute across midnight reports the day it crossed");
    CHECK(gs.worldTime.day() == 8 && gs.worldTime.hour() == 0
              && gs.worldTime.minute() == 0,
          "one minute rolls 23:59 into 00:00 of the next day");
    CHECK(runtime.pendingDailyTicks == 1 && runtime.nextDailyTickDay == 8,
          "midnight queues exactly one daily tick, named by the day it starts");
    CHECK(gs.player.ageDays == 1000,
          "queueing daily work does not perform it: the clock only queues");
}

// ДВЕ ДОЛИ НЕЗАВИСИМЫ, И ЭТО РАЗЛИЧИМО ТОЛЬКО КОГДА ОНИ РАЗНЫЕ И ОБЕ
// НЕНУЛЕВЫЕ (M-232). Свидетели ниже, гоняющие полный день, такого состояния
// не создают: у них либо непокрыто ВСЁ (доли равны 1.0), либо покрыт харч
// (доля еды 0). Отменённая формула «берётся ХУДШАЯ из двух долей» проходила
// их ЗЕЛЁНОЙ — поймано мутацией, и поэтому здесь стоит прямой суд над
// дверью, с долями 1/2 по харчу и 1/4 по плате.
//
// Долги выставляются ПРЯМО: это ВХОД судимой функции, а не подделка
// состояния мира, и другого способа получить ЧАСТИЧНУЮ недоимку у двери нет
// — её рождает только сезон, оплаченный наполовину.
void test_the_two_shares_are_independent() {
    sm::Inventory store{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        sm::creatures_push(store, sm::make_soldier(
            static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 300u + i));
    }
    CHECK(sm::creature_count(store) == 4, "фикстура: четыре души в составе");
    const sm::UpkeepBill bill = sm::upkeep_bill(store);
    CHECK(bill.board > 0 && bill.wage > 0,
          "фикстура: строка Guard несёт И рацион, И жалованье — без этого "
          "две доли неразличимы по построению");

    sm::Upkeep r{};
    const int boardOrd = sm::hunger_commodity_ordinal();
    CHECK(boardOrd >= 0, "фикстура: у мира есть голодная строка");
    r.needDebt[boardOrd] = std::int32_t(bill.board / 2);   // харч: половина
    r.wageDebt = bill.wage / 4;                            // плата: четверть

    sm::Inventory pool{};
    std::int64_t burned = 0;
    const sm::UpkeepWindowOutcome out = sm::upkeep_season_window(
        r, store, pool, burned, nullptr, nullptr);

    CHECK(out.starved == 2,
          "половина непокрытого харча = половина состава УМЕРЛА (4 × 1/2)");
    CHECK(out.walked == 1,
          "четверть непокрытой платы = четверть состава УШЛА (4 × 1/4), и "
          "доля взята от состава ДО убыли, а не от остатка");
    CHECK(sm::creature_count(pool) == 1,
          "в пул легли ТОЛЬКО неоплаченные — умершие из мира ушли совсем");
    CHECK(sm::creature_count(store) == 1,
          "сохранение: 4 = 2 умерли + 1 ушёл + 1 остался");
    // НЕГАТИВНЫЙ КОНТРОЛЬ ОТМЕНЁННОЙ ФОРМУЛЫ: «худшая из двух долей» дала бы
    // ОДИН исход на обе причины — 2 ушедших в пул и НОЛЬ умерших.
    CHECK(!(out.starved == 0 && out.walked == 2),
          "формула «худшая доля» отменена вердиктом 2026-09-22: один исход "
          "на две причины стирал разницу «мир потерял» и «сменил хозяина»");
}

void test_daily_processing_applies_player_upkeep_and_age() {
    sm::GameState gs{};
    // The purse rides his squad entity now; the daily tick is handed it.
    sm::ecs::World world;
    auto worldStore_ = sm::make_macro_store();
    sm::store_attach(world, worldStore_.get());
    sm::ensure_macro_player_entity(gs, sm::store_of(world));
    sm::player_inventory(gs, *worldStore_)->add("coin_empire_copper", 5);
    gs.player.ageDays = 1000;
    sm::player_sheet(gs, *worldStore_)->attributes[sm::AttributeId::Cha] = 0;
    // The player's men live on his SQUAD ENTITY now (owner, 2026-08-27), so
    // the fixture raises one — the same shape a lord's warband has — and the
    // daily tick reads his wages from it through the envelope.
    sm::Inventory* army = sm::player_inventory(gs, *worldStore_);
    sm::creatures_push(*army, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 77u));
    // Вторая душа — не украшение фикстуры: закон v105 взыскивает ДОЛЮ
    // контейнера, и на контейнере из одного «доля» неотличима от «хотя бы
    // один» — то есть от той самой отменённой кромки. Двое — минимум, на
    // котором пропорция вообще может быть измерена.
    sm::creatures_push(*army, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 79u));
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &world;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 789u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 12;   // NOT a season boundary

    const int processed =
        sm::process_world_daily_ticks(gs, *worldStore_, runtime, 1, &mw);

    CHECK(processed == 1 && runtime.pendingDailyTicks == 0
              && runtime.nextDailyTickDay == 0,
          "the daily processor drains exactly the one tick that was queued");
    // The daily wage died 2026-09-17 («общее содержание — игрок == нпц»,
    // CANON S19.2): an ordinary day charges NOTHING — balances live on the
    // season boundary, through THE one squad window below.
    CHECK(sm::inventory_value((*sm::player_inventory(gs, *worldStore_))) == 5,
          "an ordinary day charges no upkeep: balances are the boundary's");
    CHECK(gs.player.ageDays == 1001,
          "one daily tick ages the player exactly one day");

    // ── THE SQUAD SEASON WINDOW, on the player himself («игрок == нпц») ──
    // The expectation is DERIVED from the same law the window pays by:
    // wage = the plain soldier-row sum × the season (no CHA haggling — the
    // discount died as a player-special), board = a season of harch per
    // creatures soul whose own row is on upkeep.
    const int wageSeason =
        sm::calculate_squad_upkeep(*army) * sm::kDaysPerSeason;
    CHECK(wageSeason > 0, "the Guard row prices the creatures (fixture sanity)");

    // A non-boundary day is a silent day — negative control.
    CHECK(sm::squad_season_window(mw, 12) == 0,
          "no window off the boundary: nobody deserts");
    CHECK(sm::inventory_value((*sm::player_inventory(gs, *worldStore_))) == 5,
          "no window off the boundary: nothing debited");

    // ── СЧЁТ, А НЕ КРОМКА (v105, CANON S10 «у всякого, кто кормит, есть
    // счёт»; владелец 2026-09-21) ───────────────────────────────────────
    // Свидетель переехал вместе с законом. Здесь пинилось ТРИ правила,
    // которые все умерли одним вердиктом: «покрыто ЦЕЛИКОМ или не
    // списывается», доля 1/8 и пол «хотя бы одна душа». Цена прежней формы
    // измерена прогоном 512 дней: мир терял три четверти населения, НЕ
    // ГОЛОДАЯ НИ ДНЯ — 154 385 душ уходили в дезертиры, потому что артель
    // в поле встречала границу с пустой сумкой.
    //
    // Новый закон — зеркало закона МЕСТ (econ_debt_boundary): на границе
    // выставляется счёт, он гасится сразу и ЧАСТИЧНО, а на СЛЕДУЮЩЕЙ
    // границе непогашенная ДОЛЯ и есть доля ушедших.

    // ПЕРВАЯ граница: счёта ещё не было, значит взыскивать нечего —
    // дезертиров ноль, но 5 монет кошелька уходят в уплату НОВОГО счёта.
    const int poolBefore = sm::creature_count(gs.deserterPool);
    CHECK(sm::squad_season_window(mw, 33) == 0,
          "первая граница выставляет счёт, а не взыскивает: долга не было");
    CHECK(sm::creature_count(gs.deserterPool) == poolBefore,
          "никто не ушёл — уходят за НЕОПЛАЧЕННОЕ, а счёт только что выписан");
    CHECK(sm::inventory_value(*sm::player_inventory(gs, *worldStore_)) == 0,
          "частичная оплата ЗАКОННА: что было в кошельке, то и ушло в счёт");
    CHECK(gs.lootPoolValue == 5,
          "уплаченная часть платы сгорает в пул лута, как и полная");

    // ── ВТОРАЯ граница, кошелёк пуст: НЕ ПОКРЫТО НИЧЕГО ─────────────────
    // ДВЕ НУЖДЫ — ДВА ИСХОДА (M-232, вердикт владельца 2026-09-22: «еда
    // голод смерть пропорционально / неуплата дезертирство пропорционально»;
    // 2026-10-06 ещё прямее: «ЕСЛИ НЕТ ЖАЛОВАНИЯ ДЕЗЕРТИРСТВО ЕСЛИ НЕТ ЕДЫ
    // УМЕР»). Здесь свидетель утверждал, что при пустом кошельке весь
    // контейнер ложится В ПУЛ ДЕЗЕРТИРОВ — он охранял ровно тот исход,
    // которого по закону нет, и у него умер НОСИТЕЛЬ (AGENTS §5 п.6).
    //
    // При обеих долях = 1.0 независимость физически невозможна (у тела одна
    // судьба), и ничья разрешена в пользу СМЕРТИ: мёртвый не дезертирует.
    // Проверка «пул не вырос» — негативный контроль старого закона.
    const int creatures = sm::creature_count(*army);
    CHECK(creatures >= 2, "негативный контроль: контейнеру есть кого терять");
    const int walked = sm::squad_season_window(mw, 65);
    CHECK(walked == 0,
          "за ХАРЧ не уходят: при непокрытой еде доля УМИРАЕТ, и уход за "
          "плату получает уже пустой контейнер");
    CHECK(sm::creature_count(*army) == 0,
          "ушла ВСЯ доля непокрытого — при пустом кошельке это весь "
          "контейнер, а не назначенная восьмая");
    CHECK(sm::creature_count(gs.deserterPool) == poolBefore,
          "НЕГАТИВНЫЙ КОНТРОЛЬ (M-232): некормленные УМЕРЛИ — из мира они "
          "ушли совсем, а не сменили хозяина");

    // ── ХАРЧ ПОКРЫТ, ПЛАТА НЕТ: ВТОРАЯ ПОЛОВИНА ЗАКОНА ──────────────────
    // Это и есть различие, которого прежний свидетель не проверял вовсе:
    // один и тот же суд обязан дать РАЗНЫЕ исходы двум строкам счёта.
    {
        sm::creatures_push(*army, sm::make_soldier(
            static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 91u));
        sm::Inventory* p = sm::player_inventory(gs, *worldStore_);
        p->add("food", sm::kDaysPerSeason);          // харч — есть
        const int heads = sm::creature_count(*army);
        const int poolWas = sm::creature_count(gs.deserterPool);
        CHECK(heads == 1 && p->count("coin_empire_copper") == 0,
              "фикстура: один человек, харч на сезон, монет НОЛЬ");
        // Граница выписывает счёт и гасит харч; плату гасить нечем.
        CHECK(sm::squad_season_window(mw, 97) == 0,
              "граница, выписавшая счёт, не взыскивает его же");
        const int left = sm::squad_season_window(mw, 129);
        CHECK(left == 1 && sm::creature_count(*army) == 0,
              "за НЕВЫПЛАТУ человек УХОДИТ, а не умирает");
        CHECK(sm::creature_count(gs.deserterPool) == poolWas + 1,
              "и ложится он именно в пул дезертиров — единственный законный "
              "путь туда");
    }

    // ПОКРЫТЫЙ сезон: вернуть душу, дать харч и плату — счёт гасится
    // целиком, и следующая граница не уводит никого.
    sm::creatures_push(*army, sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 78u));
    sm::Inventory* purse = sm::player_inventory(gs, *worldStore_);
    purse->add("food", sm::kDaysPerSeason);
    purse->add("coin_empire_copper", wageSeason);
    const std::int64_t burnedBefore = gs.lootPoolValue;
    CHECK(sm::squad_season_window(mw, 161) == 0,
          "покрытый сезон не уводит никого");
    CHECK(purse->count("food") == 0,
          "покрытый сезон съедает харч сезона разом");
    CHECK(gs.lootPoolValue > burnedBefore,
          "уплаченная плата сгорает в пул лута («жалование сгорает»)");
}


// THE drift test the whole integer clock exists for: a thousand small advances
// and one big one must land on the same instant, report the same elapsed time,
// and queue the same daily work. Under the old float minute accumulator this
// could only ever be approximately true; now it is an equality.
// The garrison's own ceiling, kept by the DAY — not merely asked about before
// the day begins. The check `wants_recruits(n) = n < 64` sat before a packet
// of up to ten was drawn, so a garrison at 63 stood at 73 by nightfall: its
// own cap overshot by nine, by the placement of the question. A town that
// cannot take a recruit must also not pay a head for him.
// (`test_garrison_never_exceeds_its_cap` и
// `test_garrison_ceiling_trims_the_surplus` УМЕРЛИ 2026-09-30 вместе со своей
// подсистемой, v122: цель набора `garrison_target_strength`, потолок
// `garrison_cap_`, пакет `garrison_recruit_` и нож `garrison_trim_` снесены
// вердиктом владельца «раздел гарнизон/мирные умирает; оборона места = вся
// толпа». Свидетель без предмета не охраняет закон — он охраняет память о
// нём (§8 п.5), поэтому оба сняты, а не подогнаны.
//
// ЧТО УШЛО ВМЕСТЕ С НИМИ И НАЗВАНО ВСЛУХ: предел ТАБУНА места. Нож резал
// излишек скота в мясо, и без него лошади копятся без предела (замер до
// ножа: 4 230 голов за 128 дней, монотонно). Возвращать потолок НЕЛЬЗЯ —
// ЗАКОН КЛАМПА прямо запрещает «потолок численности» как костыль; предел
// обязан наступать обратной связью, то есть скот должен ЕСТЬ (`souls_home`
// считает только людей, лошадь сегодня не ест вовсе). Это экономика —
// наряд, а не хвост этого ломтика.)

void test_many_small_advances_equal_one_big_one() {
    constexpr std::uint64_t kTotal = 10000;   // ~2.5 hours of world time

    sm::GameState slow{};
    sm::GameState fast{};
    slow.worldTime = sm::world_time_at(3, 8, 17);
    fast.worldTime = slow.worldTime;

    sm::WorldTickRuntime slowRt{};
    sm::WorldTickRuntime fastRt{};
    sm::reset_world_tick_runtime(slowRt, 999u);
    sm::reset_world_tick_runtime(fastRt, 999u);

    int slowMinutes = 0, slowHours = 0, slowDays = 0;
    for (std::uint64_t i = 0; i < kTotal; ++i) {
        const sm::WorldTickResult r = sm::advance_world_clock(slow, slowRt, 1);
        slowMinutes += r.minutesAdvanced;
        slowHours += r.hoursAdvanced;
        slowDays += r.daysAdvanced;
    }
    const sm::WorldTickResult big =
        sm::advance_world_clock(fast, fastRt, kTotal);

    CHECK(slow.worldTime.tick == fast.worldTime.tick,
          "ten thousand one-tick advances land on the same instant as one jump");
    CHECK(slowMinutes == big.minutesAdvanced && slowHours == big.hoursAdvanced
              && slowDays == big.daysAdvanced,
          "elapsed time is reported the same however the advance is split");
    CHECK(slowRt.pendingDailyTicks == fastRt.pendingDailyTicks
              && slowRt.nextDailyTickDay == fastRt.nextDailyTickDay,
          "the daily queue does not depend on how the advance was split");
    // And the elapsed counts are the ones the ladder itself would compute.
    const std::uint64_t startTick = sm::world_time_at(3, 8, 17).tick;
    CHECK(std::uint64_t(big.minutesAdvanced)
              == sm::absolute_minute(startTick + kTotal)
                     - sm::absolute_minute(startTick),
          "reported minutes equal the clock's own derivation, not a count");
}

// The subworld divisor keeps its remainder in the runtime, so a walk broken
// into pieces buys exactly as much daylight as one long one.
void test_subworld_steps_lose_nothing_when_split() {
    sm::GameState whole{};
    sm::GameState split{};
    whole.worldTime = sm::world_time_at(2, 12, 0);
    split.worldTime = whole.worldTime;

    sm::WorldTickRuntime wholeRt{};
    sm::WorldTickRuntime splitRt{};
    sm::reset_world_tick_runtime(wholeRt, 4242u);
    sm::reset_world_tick_runtime(splitRt, 4242u);

    constexpr std::uint64_t kSteps = 1000;
    sm::tick_world_subworld_steps(whole, wholeRt, kSteps);
    for (std::uint64_t i = 0; i < kSteps; ++i) {
        sm::tick_world_subworld_steps(split, splitRt, 1);
    }

    CHECK(whole.worldTime.tick == split.worldTime.tick,
          "the subworld clock does not drift when its steps are split up");
    CHECK(whole.worldTime.tick
              == sm::world_time_at(2, 12, 0).tick
                     + kSteps / sm::kSubworldTickDivisor,
          "N subworld steps buy exactly N/divisor ticks of world time");
    CHECK(splitRt.subworldStepRemainder == kSteps % sm::kSubworldTickDivisor,
          "the leftover steps are kept whole in the runtime, not dropped");
}

// ── A town's TRANSITIONS become facts of the world ───────────────────────
// The chronicle records the day a famine BEGAN, not the thirty-two days it
// lasted: a town hungry for a season is one famine, and a chronicle that filed
// the state every day would bury the day it started under the days it
// continued.
void test_a_famine_is_recorded_once_when_it_begins() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    // Плечо места — колонки ТЕЛА (M-90 шаг 5): store стоит до первого места.
    auto storePtr = sm::make_macro_store();
    sm::MacroStore& st = *storePtr;
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);

    // A town with mouths and no food. Под долгом (CANON S10) голод — это
    // ВЗЫСКАНИЕ: счёт выставляется первой границей (день 1), а смерть
    // приходит второй (день 33), когда сезон прожит неоплаченным. Сорок
    // дней кроют обе границы; место без прихода умирает целиком за одно
    // взыскание, и летопись обязана записать РОВНО этот день — не тридцать
    // два дня голодания вокруг него.
    const sm::MacroHandle town =
        sm::birth_place(gs, st, sm::SquadType::City, 8, 8, -1, "Hungry");
    // Души селятся ДВЕРЬЮ МИРА (labour.h settle_souls): паства в worked-число
    // фичи, головы в инвентарь — тем же законом, что генезис.
    sm::settle_souls(gs, st, town.slot, 100);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 7u);
    constexpr int kDays = 40;
    runtime.pendingDailyTicks = kDays;
    runtime.nextDailyTickDay = 1;
    sm::process_world_daily_ticks(gs, st, runtime, 64);

    struct Count { int famines = 0; int total = 0; };
    Count c;
    sm::chronicle_near(gs.chronicle, 8, 8, /*radiusCells*/1, /*sinceDay*/0,
                       [](void* u, const sm::WorldFact& f) {
                           Count& n = *static_cast<Count*>(u);
                           ++n.total;
                           if (f.kind == std::uint16_t(sm::FactKind::Starved))
                               ++n.famines;
                       }, &c);

    CHECK(st.wellbeing[town.slot].starvedYesterday > 0,
          "the fixture is honest: this town's bill took souls");
    CHECK(sm::souls_flock(gs, st, town.slot) < 100,
          "паства упала на съеденных: число фичи и головы идут ПАРОЙ");
    // БЛАГОПОЛУЧИЕ ЕСТЬ ДОЛЯ ОПЛАЧЕННОГО, и здесь утверждается ИМЕННО это,
    // а не круглый ноль. Прежде тут стояло `== 0`, и ноль держался на
    // ПОБОЧНОМ ЭФФЕКТЕ снесённой подсистемы: `garrison_recruit_` уводил
    // души из населения, поэтому к взысканию их было не больше выставленного
    // счёта и умирали ВСЕ. Сегодня место успевает вырасти за первый сезон,
    // горстка выживает — и мир честно докладывает эту горстку долей, а не
    // нулём. Число выводится из тех же данных, что его породили (§8 п.4).
    {
        const std::uint16_t l = town.slot;
        const sm::Wellbeing& wb = st.wellbeing[l];
        // Утверждаются СВОЙСТВА, а не число: благополучие есть произведение
        // доли еды на долю комфорта, и пересчитать его здесь значило бы
        // написать вторую копию продакшен-формулы как «ожидаемое» (§8 п.5 —
        // ровно этот дубль сюита и поймала на первой попытке).
        CHECK(wb.seasonWellbeing < 255 / 8,
              "неоплаченный сезон рушит благополучие почти в ноль");
        CHECK(int(wb.starvedYesterday) > sm::souls_home(st, l),
              "взыскание забрало БОЛЬШЕ душ, чем осталось: место обезлюдело, "
              "а не поголодало");
        CHECK(sm::souls_flock(gs, st, l) == sm::souls_home(st, l),
              "паства упала вместе с головами: два носителя не расходятся "
              "(в поле этот город никого не держит)");
    }
    CHECK(c.famines == 1,
          "the boundary that took the souls is ONE fact, not thirty-two");
    CHECK(c.total >= 1, "the world remembered something about this place");

    // A landmark has a NAME the day it is founded, but figure-ness is
    // RENOWN (owner, 2026-08-28): this fresh fixture town has done and
    // suffered nothing the world was told of before, so its misfortune is
    // weather — the ring holds it (asserted above), the annals do not.
    // History begins when the place is somebody.
    CHECK(gs.chronicle.annals.empty(),
          "a weightless town's misfortune is weather, not history");
}

// Population falls honestly to ZERO — the floor is dead (owner, 2026-08-29).
// minPop = 10/5 minted people from air and made every settlement immortal
// (canon-audit B6). The honest law: starvation drives the logistic decline
// all the way down; zero is absorbing (population_delta_per_day(0) = 0); the
// death fires ONCE through the diedOut transition — the Died fact the daily
// tick files. NEGATIVE CONTROL against reintroduction: under the old law
// population < 5 was unreachable, so the sawBelowOldFloor gate reddens the
// moment any floor returns.
void test_population_dies_honestly_to_zero() {
    // GameState НА КУЧЕ: он ~0.84 МиБ, и больше двух стековых локалов в одной
    // функции рвут стек (грабля ломтика C).
    auto gsp = std::make_unique<sm::GameState>();
    sm::GameState& gs = *gsp;
    auto storePtr = sm::make_macro_store();
    sm::MacroStore& st = *storePtr;
    gs.mapW = 64;
    gs.mapH = 64;
    const std::uint16_t v =
        sm::birth_place(gs, st, sm::SquadType::Village, 4, 4).slot;
    // Хутор с пустым амбаром: три души ДВЕРЬЮ МИРА (паства + головы).
    sm::settle_souls(gs, st, v, 3);
    int deaths = 0;
    int dayOfDeath = -1;
    bool sawBelowOldFloor = false;
    // Days are the world's own, 1-based: day 1 is the first season boundary,
    // where the empty larder fails the window and the season turns hungry.
    for (int day = 1; day <= 2048 && dayOfDeath < 0; ++day) {
        bool famine = false, died = false;
        sm::settle_landmark_day(gs, st, v, day, famine, died);
        if (sm::souls_flock(gs, st, v) < 5) sawBelowOldFloor = true;
        if (died) { ++deaths; dayOfDeath = day; }
    }
    CHECK(dayOfDeath >= 0 && sm::souls_flock(gs, st, v) == 0,
          "a starving settlement dies honestly to zero");
    CHECK(sm::souls_home(st, v) == 0,
          "…и головы ушли вместе с числом: пара носителей не расходится");
    CHECK(sawBelowOldFloor,
          "population passed the old floor: no crutch is back");
    for (int day = 1; day <= 100; ++day) {
        bool famine = false, died = false;
        sm::settle_landmark_day(gs, st, v, day, famine, died);
        if (died) ++deaths;
    }
    CHECK(sm::souls_flock(gs, st, v) == 0,
          "zero population is absorbing: nobody is minted from air");
    CHECK(deaths == 1,
          "the death transition fires exactly once");
}

} // namespace

// §42: dungeon garrisons regrow by the fauna law — one soul per epoch
// while ALIVE and under the born mean; wiped clean stays dead forever.
void test_dungeon_population_regrows_like_fauna() {
    sm::GameState gs{};
    auto storePtr = sm::make_macro_store();
    sm::MacroStore& st = *storePtr;
    gs.mapW = 8;
    gs.mapH = 8;
    const sm::MacroHandle live =
        sm::birth_place(gs, st, sm::SquadType::Ruin, 1, 1);
    // Души данжа — ГОЛОВЫ его толпы (v122, вердикт 3): род НЕ
    // выдумывается, его даёт полоса crowdHabitat этого рода мест
    // (руина → слабейшая строка).
    sm::settle_souls(gs, st, live.slot, 10);
    // выбита до последней души: ноль голов
    const sm::MacroHandle dead =
        sm::birth_place(gs, st, sm::SquadType::Ruin, 2, 2);
    sm::MacroWorld w{};
    w.gs = &gs;   // no zones layer: the ruin's score is the honest zero,
                  // so its mean is the born base alone (64)

    // ДЕНЬ ОТРОСТА — ФУНКЦИЯ ОРДИНАЛА (world_tick.cpp: id % эпоха), а
    // ординал выдаёт эмитент (ломтик F) — он и спрашивается, вместо прежнего
    // рукописного `5`.
    const int liveId = int(st.spawnId[live.slot].index);
    const int deadId = int(st.spawnId[dead.slot].index);
    const int dueDay = liveId % sm::kGrowthEpochDays;
    sm::regrow_dungeon_populations(w, st, dueDay + 1);
    CHECK(sm::souls_flock(gs, st, live.slot) == 10,
          "a landmark regrows only on its own day of the epoch");
    sm::regrow_dungeon_populations(w, st, dueDay);
    CHECK(sm::souls_flock(gs, st, live.slot) == 11,
          "on its due day a living garrison regrows one soul");
    sm::regrow_dungeon_populations(w, st, deadId % sm::kGrowthEpochDays);
    CHECK(sm::souls_flock(gs, st, dead.slot) == 0,
          "wiped clean stays dead — resurrection is the S9 transition's");
    sm::settle_souls(gs, st, live.slot, 53);   // 11 + 53 = born mean 64
    sm::regrow_dungeon_populations(w, st, dueDay);
    CHECK(sm::souls_flock(gs, st, live.slot) == 64,
          "the born mean is the regrow ceiling");
}

// ── ПОТОЛОК ГАРНИЗОНА — ФУНКЦИЯ КОНТЕКСТА (CANON S4, 2026-09-19) ─────────
// Цель набора была только целью НАБОРА: стойло, растущее двухтактным
// обозом, не резалось вовсе (4 230 голов за 128 дней, монотонно). Потолок =
// население >> shift + «место кормит тех, кто его стоит» (богатство /
// сезон содержания стража). Излишек снимается со СЛАБЕЙШИХ; куда — решает
// СТРОКА: человек в пул дезертиров, зверь по тегу Mount под нож — мясо
// гасит долг места той же дверью гашения (S10).
int main() {
    test_dungeon_population_regrows_like_fauna();
    test_hour_rollover();
    test_many_small_advances_equal_one_big_one();
    test_subworld_steps_lose_nothing_when_split();
    test_day_rollover_queues_budgeted_daily_tick();
    test_the_two_shares_are_independent();
    test_daily_processing_applies_player_upkeep_and_age();
    test_a_famine_is_recorded_once_when_it_begins();
    test_population_dies_honestly_to_zero();
    return sm::test::report("world_tick_parity_test");
}
