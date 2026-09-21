// The integer clock, tested as a LAW rather than as a table of numbers: what an
// advance reports must equal what the ladder itself derives, and how an advance
// is chopped up must not change where it lands.
//
// This file was green for months while asserting nothing (`int fail()` returned
// into a `bool` — every failure read as PASS). It now goes through tests/check.h,
// where a verdict is not something a function can return, and a test that runs
// zero checks fails by counting.
#include "check.h"

#include "macro/npc.h"
#include "macro/npc_ai.h"   // squad_season_window — THE boundary window
#include "macro/world_tick.h"
#include "macro/player_entity.h"
#include "macro/macro_world.h"
#include "macro/currency.h"

#include <cstdint>

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

void test_daily_processing_applies_player_upkeep_and_age() {
    sm::GameState gs{};
    // The purse rides his squad entity now; the daily tick is handed it.
    sm::ecs::World world;
    sm::ensure_macro_player_entity(gs, world);
    sm::player_inventory(world)->add("coin_empire_copper", 5);
    gs.player.ageDays = 1000;
    sm::player_sheet(world)->attributes[sm::AttributeId::Cha] = 0;
    // The player's men live on his SQUAD ENTITY now (owner, 2026-08-27), so
    // the fixture raises one — the same shape a lord's warband has — and the
    // daily tick reads his wages from it through the envelope.
    sm::SoldierSquad* army = sm::player_roster(world);
    army->push(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 77u));
    // Вторая душа — не украшение фикстуры: закон v105 взыскивает ДОЛЮ
    // ростера, и на ростере из одного «доля» неотличима от «хотя бы
    // один» — то есть от той самой отменённой кромки. Двое — минимум, на
    // котором пропорция вообще может быть измерена.
    army->push(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 79u));
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &world;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 789u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 12;   // NOT a season boundary

    const int processed = sm::process_world_daily_ticks(gs, runtime, 1, &mw);

    CHECK(processed == 1 && runtime.pendingDailyTicks == 0
              && runtime.nextDailyTickDay == 0,
          "the daily processor drains exactly the one tick that was queued");
    // The daily wage died 2026-09-17 («общее содержание — игрок == нпц»,
    // CANON S19.2): an ordinary day charges NOTHING — balances live on the
    // season boundary, through THE one squad window below.
    CHECK(sm::inventory_value((*sm::player_inventory(world))) == 5,
          "an ordinary day charges no upkeep: balances are the boundary's");
    CHECK(gs.player.ageDays == 1001,
          "one daily tick ages the player exactly one day");

    // ── THE SQUAD SEASON WINDOW, on the player himself («игрок == нпц») ──
    // The expectation is DERIVED from the same law the window pays by:
    // wage = the plain soldier-row sum × the season (no CHA haggling — the
    // discount died as a player-special), board = a season of bread per
    // roster soul whose own row is on upkeep.
    const int wageSeason =
        sm::calculate_squad_upkeep(*army) * sm::kDaysPerSeason;
    CHECK(wageSeason > 0, "the Guard row prices the roster (fixture sanity)");

    // A non-boundary day is a silent day — negative control.
    CHECK(sm::squad_season_window(mw, 12) == 0,
          "no window off the boundary: nobody deserts");
    CHECK(sm::inventory_value((*sm::player_inventory(world))) == 5,
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
    const std::size_t poolBefore = gs.deserterPool.size();
    CHECK(sm::squad_season_window(mw, 33) == 0,
          "первая граница выставляет счёт, а не взыскивает: долга не было");
    CHECK(gs.deserterPool.size() == poolBefore,
          "никто не ушёл — уходят за НЕОПЛАЧЕННОЕ, а счёт только что выписан");
    CHECK(sm::inventory_value(*sm::player_inventory(world)) == 0,
          "частичная оплата ЗАКОННА: что было в кошельке, то и ушло в счёт");
    CHECK(gs.lootPoolValue == 5,
          "уплаченная часть платы сгорает в пул лута, как и полная");

    // ВТОРАЯ граница, кошелёк пуст: счёт не погашен почти целиком, и
    // уходит ровно ЭТА доля ростера — не восьмая и не «хотя бы один».
    const int roster = army->size();
    CHECK(roster >= 2, "негативный контроль: ростеру есть кого терять");
    const int walked = sm::squad_season_window(mw, 65);
    CHECK(walked > 0, "неоплаченный сезон стоит людей");
    CHECK(walked == roster,
          "ушла ВСЯ доля неоплаченного — при пустом кошельке это весь "
          "ростер, а не назначенная восьмая");
    CHECK(gs.deserterPool.size() == poolBefore + std::size_t(walked),
          "ушедшие легли в пул дезертиров");

    // ПОКРЫТЫЙ сезон: вернуть душу, дать харч и плату — счёт гасится
    // целиком, и следующая граница не уводит никого.
    army->push(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 78u));
    sm::Inventory* purse = sm::player_inventory(world);
    purse->add("food", sm::kDaysPerSeason);
    purse->add("coin_empire_copper", wageSeason);
    const std::int64_t burnedBefore = gs.lootPoolValue;
    CHECK(sm::squad_season_window(mw, 97) == 0,
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
void test_garrison_never_exceeds_its_cap() {
    // §42 Инк 7: the ceiling is the registry TARGET (population >>
    // garrisonShift) and a day's packet is at most target >> 4 — a hole in
    // the defense heals over days, never in one morning.
    sm::GameState gs{};
    sm::Landmark s{};
    s.type = sm::LandmarkType::City;
    s.id = 1;
    s.population = 5000;
    const int target =
        sm::garrison_target_strength(s.type, s.population);   // 5000>>3 = 625
    CHECK(target == 5000 >> 3,
          "the garrison target is the registry law: population >> shift");
    // The maintenance law bleeds a SHORTED garrison at once (2026-08-31),
    // and this test is about recruiting — keep the men fed and paid.
    s.inventory.add("coin_empire_copper", 1 << 16);
    s.inventory.add("food", 1 << 13);
    // Место СТОИТ на месте, иначе оно вырастет под тестом и уведёт цель
    // гарнизона из-под проверки. Благополучие 0 — это «стоим» (владелец
    // 2026-09-19: ватерлинии нет, мера И ЕСТЬ ход роста); прежние 128 были
    // половиной хода, а не покоем, и город прибавлял по девять душ в день.
    s.seasonWellbeing = 0;
    // One below the target: exactly one recruit wanted. A standing army is
    // a GENERIC stack (CANON S4) — 624 souls is one slot, not a wall.
    s.garrison.push_stack(std::uint16_t(sm::NPCType::Guard), 1,
                          std::int32_t(target - 1));
    gs.landmarks.push_back(s);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 4242u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 3;
    sm::process_world_daily_ticks(gs, runtime, 1);

    const int after = sm::total_soldiers(gs.landmarks[0].garrison);
    CHECK(after <= sm::garrison_target_strength(
                       gs.landmarks[0].type, gs.landmarks[0].population + 1),
          "a day of recruiting never carries a garrison past its target");
    CHECK(after == target,
          "…and it does fill the last free slot — the target is a ceiling, "
          "not a veto on recruiting at all");
    // The gradualness arm: an EMPTY garrison refills by at most target>>4 a
    // day — the same slow heal desertion bleeds at (1/8), never instantly.
    sm::GameState slow{};
    sm::Landmark hollow = s;
    hollow.garrison = sm::SoldierSquad{};
    hollow.inventory = sm::Inventory{};
    hollow.inventory.add("coin_empire_copper", 1 << 16);
    hollow.inventory.add("food", 1 << 13);
    slow.landmarks.push_back(hollow);
    sm::WorldTickRuntime slowRuntime{};
    sm::reset_world_tick_runtime(slowRuntime, 4242u);
    slowRuntime.pendingDailyTicks = 1;
    slowRuntime.nextDailyTickDay = 3;
    sm::process_world_daily_ticks(slow, slowRuntime, 1);
    const int refilled = sm::total_soldiers(slow.landmarks[0].garrison);
    CHECK(refilled > 0 && refilled <= std::max(1, target >> 4),
          "a hollowed garrison heals by a day-packet, never in one morning");
}

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
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);

    // A town with mouths and no bread. Под долгом (CANON S10) голод — это
    // ВЗЫСКАНИЕ: счёт выставляется первой границей (день 1), а смерть
    // приходит второй (день 33), когда сезон прожит неоплаченным. Сорок
    // дней кроют обе границы; место без прихода умирает целиком за одно
    // взыскание, и летопись обязана записать РОВНО этот день — не тридцать
    // два дня голодания вокруг него.
    sm::Landmark s{};
    s.type = sm::LandmarkType::City;
    s.id = 1;
    s.name = "Hungry";
    s.population = 100;
    s.x = 8; s.y = 8;
    gs.landmarks.push_back(s);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 7u);
    constexpr int kDays = 40;
    runtime.pendingDailyTicks = kDays;
    runtime.nextDailyTickDay = 1;
    sm::process_world_daily_ticks(gs, runtime, 64);

    struct Count { int famines = 0; int total = 0; };
    Count c;
    sm::chronicle_near(gs.chronicle, 8, 8, /*radiusCells*/1, /*sinceDay*/0,
                       [](void* u, const sm::WorldFact& f) {
                           Count& n = *static_cast<Count*>(u);
                           ++n.total;
                           if (f.kind == std::uint16_t(sm::FactKind::Starved))
                               ++n.famines;
                       }, &c);

    CHECK(gs.landmarks[0].starvedYesterday > 0,
          "the fixture is honest: this town's bill took souls");
    CHECK(gs.landmarks[0].seasonWellbeing == 0,
          "an unpaid bill reads as zero wellbeing — the ONE measure of life");
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
    sm::Landmark lm{};
    lm.type = sm::LandmarkType::Village;
    lm.population = 3;      // a cut-down hamlet with an empty larder
    int deaths = 0;
    int dayOfDeath = -1;
    bool sawBelowOldFloor = false;
    // Days are the world's own, 1-based: day 1 is the first season boundary,
    // where the empty larder fails the window and the season turns hungry.
    for (int day = 1; day <= 2048 && dayOfDeath < 0; ++day) {
        bool famine = false, died = false;
        sm::settle_landmark_day(lm, day, famine, died);
        if (lm.population < 5) sawBelowOldFloor = true;
        if (died) { ++deaths; dayOfDeath = day; }
    }
    CHECK(dayOfDeath >= 0 && lm.population == 0,
          "a starving settlement dies honestly to zero");
    CHECK(sawBelowOldFloor,
          "population passed the old floor: no crutch is back");
    for (int day = 1; day <= 100; ++day) {
        bool famine = false, died = false;
        sm::settle_landmark_day(lm, day, famine, died);
        if (died) ++deaths;
    }
    CHECK(lm.population == 0,
          "zero population is absorbing: nobody is minted from air");
    CHECK(deaths == 1,
          "the death transition fires exactly once");
}

} // namespace

// §42: dungeon garrisons regrow by the fauna law — one soul per epoch
// while ALIVE and under the born mean; wiped clean stays dead forever.
void test_dungeon_population_regrows_like_fauna() {
    sm::GameState gs{};
    gs.mapW = 8;
    gs.mapH = 8;
    sm::Landmark ruin{};
    ruin.type = sm::LandmarkType::Ruin;
    ruin.id = 5;
    ruin.x = 1;
    ruin.y = 1;
    ruin.population = 10;
    gs.landmarks.push_back(ruin);
    sm::Landmark dead = ruin;
    dead.id = 6;
    dead.population = 0;   // cleared to the last soul
    gs.landmarks.push_back(dead);
    sm::MacroWorld w{};
    w.gs = &gs;   // no zones layer: the ruin's score is the honest zero,
                  // so its mean is the born base alone (64)

    const int dueDay = 5 % sm::kGrowthEpochDays;
    sm::regrow_dungeon_populations(w, dueDay + 1);
    CHECK(gs.landmarks[0].population == 10,
          "a landmark regrows only on its own day of the epoch");
    sm::regrow_dungeon_populations(w, dueDay);
    CHECK(gs.landmarks[0].population == 11,
          "on its due day a living garrison regrows one soul");
    sm::regrow_dungeon_populations(w, 6 % sm::kGrowthEpochDays);
    CHECK(gs.landmarks[1].population == 0,
          "wiped clean stays dead — resurrection is the S9 transition's");
    gs.landmarks[0].population = 64;   // at the born mean already
    sm::regrow_dungeon_populations(w, dueDay);
    CHECK(gs.landmarks[0].population == 64,
          "the born mean is the regrow ceiling");
}

// ── ПОТОЛОК ГАРНИЗОНА — ФУНКЦИЯ КОНТЕКСТА (CANON S4, 2026-09-19) ─────────
// Цель набора была только целью НАБОРА: стойло, растущее двухтактным
// обозом, не резалось вовсе (4 230 голов за 128 дней, монотонно). Потолок =
// население >> shift + «место кормит тех, кто его стоит» (богатство /
// сезон содержания стража). Излишек снимается со СЛАБЕЙШИХ; куда — решает
// СТРОКА: человек в пул дезертиров, зверь по тегу Mount под нож — мясо
// гасит долг места той же дверью гашения (S10).
void test_garrison_ceiling_trims_the_surplus() {
    sm::GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    sm::Landmark v{};
    v.type = sm::LandmarkType::Village;
    v.id = 1;
    v.name = "Stable";
    v.population = 30;   // паства 30 + 4 человека гарнизона ⇒ потолок 4
    v.x = 8; v.y = 8;
    // Богатства нет — потолок только населенческий. Стойло переполнено:
    // 4 стража (найм 30) + 8 лошадей (найм 240) при потолке 3.
    v.garrison.push_stack(std::uint16_t(sm::NPCType::Guard), 3, 4);
    v.garrison.push_stack(std::uint16_t(sm::NPCType::Horse), 1, 8);
    gs.landmarks.push_back(v);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 7u);
    runtime.pendingDailyTicks = 1;   // день 1 = граница: счёт выставлен
    runtime.nextDailyTickDay = 1;
    sm::process_world_daily_ticks(gs, runtime, 64);

    sm::Landmark& out = gs.landmarks[0];
    CHECK(sm::total_soldiers(out.garrison) == 4,
          "the surplus above the context ceiling is gone");
    // Слабейшие первыми: вся стража (30) ушла раньше первой лошади (240).
    CHECK(sm::count_soldiers_of_kind(
              out.garrison, std::uint16_t(sm::NPCType::Guard)) == 0,
          "the cheapest rows are cut first — no guard outlived a horse");
    CHECK(sm::count_soldiers_of_kind(
              out.garrison, std::uint16_t(sm::NPCType::Horse)) == 4,
          "the ceiling keeps exactly what the place is worth");
    // Люди — в пул дезертиров (плюс один харчевой ходок окна содержания —
    // голодный гарнизон терял 1/8 и до потолка, тот закон не тронут).
    CHECK(sm::count_soldiers_of_kind(
              gs.deserterPool, std::uint16_t(sm::NPCType::Guard)) == 4,
          "the cut men swell the deserter pool, they do not evaporate");
    // Зверь — под нож по своей стоимости, и мясо платит по счёту В ТУ ЖЕ
    // МИНУТУ (S10): на полке ноль, долг упал ровно на стоимость туш.
    const int breadValue = sm::item_def("food")->value;
    const int meatPerHorse =
        sm::hire_price_for(std::uint16_t(sm::NPCType::Horse), 1) / breadValue;
    const int bill = 30 * sm::kDaysPerSeason;
    const int debtNow =
        out.needDebt[sm::commodity_index("food")];
    CHECK(debtNow < bill, "the knife fed the bill");
    CHECK((bill - debtNow) % meatPerHorse == 0,
          "the bill fell by whole carcasses, valued by the one price law");
    CHECK(out.inventory.count("food") == 0,
          "meat above nothing: the hungry bill ate every unit on the spot");
}

int main() {
    test_garrison_never_exceeds_its_cap();
    test_dungeon_population_regrows_like_fauna();
    test_hour_rollover();
    test_many_small_advances_equal_one_big_one();
    test_subworld_steps_lose_nothing_when_split();
    test_day_rollover_queues_budgeted_daily_tick();
    test_daily_processing_applies_player_upkeep_and_age();
    test_a_famine_is_recorded_once_when_it_begins();
    test_garrison_ceiling_trims_the_surplus();
    test_population_dies_honestly_to_zero();
    return sm::test::report("world_tick_parity_test");
}
