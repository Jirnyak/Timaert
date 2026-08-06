// The integer clock, tested as a LAW rather than as a table of numbers: what an
// advance reports must equal what the ladder itself derives, and how an advance
// is chopped up must not change where it lands.
//
// This file was green for months while asserting nothing (`int fail()` returned
// into a `bool` — every failure read as PASS). It now goes through tests/check.h,
// where a verdict is not something a function can return, and a test that runs
// zero checks fails by counting.
#include "check.h"

#include "macro/economy.h"
#include "macro/npc.h"
#include "macro/world_tick.h"

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
    gs.player.gold = 5;
    gs.player.ageDays = 1000;
    gs.player.sheet.attributes.cha = 0;
    gs.player.army.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 77u));

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 789u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 12;

    // The expectation is DERIVED from the same law the tick pays by, so the
    // upkeep table can be retuned without touching this file.
    const int expectedUpkeep =
        sm::calculate_squad_upkeep(gs.player.army, gs.player.sheet.attributes.cha);
    const int expectedGold = (5 - expectedUpkeep) > 0 ? (5 - expectedUpkeep) : 0;

    const int processed = sm::process_world_daily_ticks(gs, runtime, 1);

    CHECK(processed == 1 && runtime.pendingDailyTicks == 0
              && runtime.nextDailyTickDay == 0,
          "the daily processor drains exactly the one tick that was queued");
    CHECK(gs.player.gold == expectedGold,
          "one daily tick charges exactly one day of squad upkeep");
    CHECK(gs.player.ageDays == 1001,
          "one daily tick ages the player exactly one day");
}

void test_settlement_history_keeps_a_rolling_window() {
    constexpr int kDaysRun = 35;    // more days than the window holds
    constexpr int kWindow  = 30;    // world_tick.cpp kHistoryWindow

    sm::GameState gs{};
    sm::Settlement s{};
    s.id = 1;
    s.name = "Test City";
    s.population = 10;
    s.mood = sm::SettlementMood::Stable;
    s.eco = sm::create_economy_state();
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
    CHECK(int(history.days.size()) == kWindow
              && int(history.population.size()) == kWindow,
          "history caps at the rolling window and both columns stay in step");
    // Which days survive is a consequence of the window, not a magic pair.
    CHECK(!history.days.empty() && history.days.back() == kDaysRun
              && history.days.front() == kDaysRun - kWindow + 1,
          "the window keeps the NEWEST days, ending on the last day simulated");
}

// THE drift test the whole integer clock exists for: a thousand small advances
// and one big one must land on the same instant, report the same elapsed time,
// and queue the same daily work. Under the old float minute accumulator this
// could only ever be approximately true; now it is an equality.
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
    test_hour_rollover();
    test_many_small_advances_equal_one_big_one();
    test_subworld_steps_lose_nothing_when_split();
    test_day_rollover_queues_budgeted_daily_tick();
    test_daily_processing_applies_player_upkeep_and_age();
    test_settlement_history_keeps_a_rolling_window();
    return sm::test::report("world_tick_parity_test");
}
