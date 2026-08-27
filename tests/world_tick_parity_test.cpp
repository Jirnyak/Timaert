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
    sm::player_inventory(world)->add("coin_empire", 5);
    gs.player.ageDays = 1000;
    gs.player.sheet.attributes[sm::AttributeId::Cha] = 0;
    // The player's men live on his SQUAD ENTITY now (owner, 2026-08-27), so
    // the fixture raises one — the same shape a lord's warband has — and the
    // daily tick reads his wages from it through the envelope.
    sm::SoldierSquad* army = sm::player_roster(world);
    army->push(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 77u));
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.world = &world;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 789u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 12;

    // The expectation is DERIVED from the same law the tick pays by, so the
    // upkeep table can be retuned without touching this file.
    const int expectedUpkeep =
        sm::calculate_squad_upkeep(*army, gs.player.sheet.attributes.of(sm::AttributeId::Cha));
    const int expectedGold = (5 - expectedUpkeep) > 0 ? (5 - expectedUpkeep) : 0;

    const int processed = sm::process_world_daily_ticks(gs, runtime, 1, &mw);

    CHECK(processed == 1 && runtime.pendingDailyTicks == 0
              && runtime.nextDailyTickDay == 0,
          "the daily processor drains exactly the one tick that was queued");
    CHECK(sm::wallet_value((*sm::player_inventory(world))) == expectedGold,
          "one daily tick charges exactly one day of squad upkeep");
    CHECK(gs.player.ageDays == 1001,
          "one daily tick ages the player exactly one day");
}

void test_settlement_history_keeps_a_rolling_window() {
    // More days than the ring holds — the cap is the CONTAINER's now
    // (kSettlementHistoryDays), so the test names it instead of restating a
    // number that used to live in world_tick.cpp and could drift from it.
    constexpr int kDaysRun = sm::kSettlementHistoryDays + 5;

    sm::GameState gs{};
    sm::Settlement s{};
    s.id = 1;
    s.name = "Test City";
    s.population = 10;
    s.mood = sm::SettlementMood::Stable;
    gs.settlements.push_back(s);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 321u);
    runtime.pendingDailyTicks = kDaysRun;
    runtime.nextDailyTickDay = 1;

    const int processed = sm::process_world_daily_ticks(gs, runtime, 64);
    CHECK(processed == kDaysRun && runtime.pendingDailyTicks == 0
              && runtime.nextDailyTickDay == 0,
          "a budget larger than the queue drains the whole queue");

    const sm::SettlementHistory& history = gs.settlements[0].history;
    // The window is the RING's own size now — a season (kDaysPerSeason), the
    // same span the forest grows by — and the cap lives in the container, so
    // no caller can forget it and nothing shifts an array to enforce it.
    CHECK(history.size() == sm::kSettlementHistoryDays,
          "history fills its ring and stops there");
    // Which days survive is a consequence of the ring, not a magic pair.
    CHECK(!history.empty()
              && history.day_at(history.size() - 1) == kDaysRun
              && history.day_at(0) == kDaysRun - sm::kSettlementHistoryDays + 1,
          "the ring keeps the NEWEST days, ending on the last day simulated");
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
    sm::GameState gs{};
    sm::Settlement s{};
    s.id = 1;
    s.population = 5000;                 // deep enough to want the full packet
    // Fill to one below the ceiling: the state the old check waved through.
    for (int i = 0; i < sm::kMaxGarrisonPerSettlement - 1; ++i) {
        s.garrison.push(sm::make_soldier(
            std::uint8_t(sm::NPCType::Guard), 1, std::uint32_t(1000 + i)));
    }
    gs.settlements.push_back(s);
    const int popBefore = gs.settlements[0].population;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 4242u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 3;
    sm::process_world_daily_ticks(gs, runtime, 1);

    const int after = sm::total_soldiers(gs.settlements[0].garrison);
    CHECK(after <= sm::kMaxGarrisonPerSettlement,
          "a day of recruiting never carries a garrison past its own cap");
    CHECK(after == sm::kMaxGarrisonPerSettlement,
          "…and it does fill the last free slot — the cap is a ceiling, not a "
          "veto on recruiting at all");
    // Population also moves for economic reasons on the same day, so the head
    // price is measured against a CONTROL: the same town, the same day, with a
    // garrison already full — where recruiting cannot happen at all. The gap
    // between the two populations is exactly the men taken.
    const int taken = after - (sm::kMaxGarrisonPerSettlement - 1);
    sm::GameState control{};
    sm::Settlement full = s;
    full.garrison.push(sm::make_soldier(
        std::uint8_t(sm::NPCType::Guard), 1, 9999u));   // now at the ceiling
    control.settlements.push_back(full);
    sm::WorldTickRuntime controlRuntime{};
    sm::reset_world_tick_runtime(controlRuntime, 4242u);
    controlRuntime.pendingDailyTicks = 1;
    controlRuntime.nextDailyTickDay = 3;
    sm::process_world_daily_ticks(control, controlRuntime, 1);
    CHECK(total_soldiers(control.settlements[0].garrison)
              == sm::kMaxGarrisonPerSettlement,
          "the control's full garrison recruits nobody");
    CHECK(control.settlements[0].population
              - gs.settlements[0].population == taken,
          "the town pays a head only for the men it actually took");
    (void)popBefore;
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

} // namespace

int main() {
    test_garrison_never_exceeds_its_cap();
    test_hour_rollover();
    test_many_small_advances_equal_one_big_one();
    test_subworld_steps_lose_nothing_when_split();
    test_day_rollover_queues_budgeted_daily_tick();
    test_daily_processing_applies_player_upkeep_and_age();
    test_settlement_history_keeps_a_rolling_window();
    return sm::test::report("world_tick_parity_test");
}
