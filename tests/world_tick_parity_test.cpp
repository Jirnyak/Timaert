#include "macro/economy.h"
#include "macro/npc.h"
#include "macro/world_tick.h"

#include <cstdio>
#include <cstdint>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "world_tick_parity_test FAIL: %s\n", msg);
    return 1;
}

// One game minute forward from wherever the clock stands — exact from any
// phase (core/time.h ticks_to_advance_minutes).
std::uint64_t one_minute(const sm::GameState& gs) {
    return sm::ticks_to_advance_minutes(gs.worldTime.tick, 1);
}

bool test_hour_rollover() {
    sm::GameState gs{};
    gs.worldTime = sm::world_time_at(7, 6, 59);

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 123u);

    const sm::WorldTickResult result =
        sm::advance_world_clock(gs, runtime, one_minute(gs));

    if (result.minutesAdvanced != 1 || result.hoursAdvanced != 1
        || result.daysAdvanced != 0) {
        return fail("single minute did not report one hour rollover");
    }
    if (gs.worldTime.day() != 7 || gs.worldTime.hour() != 7
        || gs.worldTime.minute() != 0) {
        return fail("single minute did not roll 06:59 to 07:00");
    }
    if (runtime.pendingDailyTicks != 0 || runtime.nextDailyTickDay != 0) {
        return fail("non-midnight hour rollover queued a daily tick");
    }
    return true;
}

bool test_day_rollover_queues_budgeted_daily_tick() {
    sm::GameState gs{};
    gs.worldTime = sm::world_time_at(7, 23, 59);
    gs.player.ageDays = 1000;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 456u);

    const sm::WorldTickResult result =
        sm::advance_world_clock(gs, runtime, one_minute(gs));

    if (result.minutesAdvanced != 1 || result.hoursAdvanced != 1
        || result.daysAdvanced != 1) {
        return fail("single minute did not report midnight rollover");
    }
    if (gs.worldTime.day() != 8 || gs.worldTime.hour() != 0
        || gs.worldTime.minute() != 0) {
        return fail("single minute did not roll 23:59 to next day 00:00");
    }
    if (runtime.pendingDailyTicks != 1 || runtime.nextDailyTickDay != 8) {
        return fail("midnight rollover did not queue exact next-day work");
    }
    if (gs.player.ageDays != 1000) {
        return fail("budgeted daily tick mutated player before processing");
    }
    return true;
}

bool test_daily_processing_applies_player_upkeep_and_age() {
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

    const int expectedUpkeep =
        sm::calculate_squad_upkeep(gs.player.army, gs.player.sheet.attributes.cha);
    const int processed = sm::process_world_daily_ticks(gs, runtime, 1);

    if (processed != 1 || runtime.pendingDailyTicks != 0
        || runtime.nextDailyTickDay != 0) {
        return fail("daily processor did not drain one queued tick");
    }
    const int expectedGold = (5 - expectedUpkeep) > 0 ? (5 - expectedUpkeep) : 0;
    if (gs.player.gold != expectedGold || gs.player.ageDays != 1001) {
        return fail("daily player upkeep/age did not match TS daily tick");
    }
    return true;
}

bool test_settlement_history_keeps_ts_30_day_window() {
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
    runtime.pendingDailyTicks = 35;
    runtime.nextDailyTickDay = 1;

    const int processed = sm::process_world_daily_ticks(gs, runtime, 64);
    if (processed != 35 || runtime.pendingDailyTicks != 0
        || runtime.nextDailyTickDay != 0) {
        return fail("daily processor did not drain all queued history ticks");
    }

    const sm::SettlementHistory& history = gs.settlements[0].history;
    if (history.days.size() != 30u || history.population.size() != 30u) {
        return fail("settlement history did not cap at 30 samples");
    }
    if (history.days.front() != 6 || history.days.back() != 35) {
        return fail("settlement history did not retain the newest 30 days");
    }
    return true;
}

// THE drift test the whole integer clock exists for: a thousand small advances
// and one big one must land on the same instant, report the same elapsed time,
// and queue the same daily work. Under the old float minute accumulator this
// could only ever be approximately true; now it is an equality.
bool test_many_small_advances_equal_one_big_one() {
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

    if (slow.worldTime.tick != fast.worldTime.tick) {
        return fail("small advances drifted away from one big advance");
    }
    if (slowMinutes != big.minutesAdvanced || slowHours != big.hoursAdvanced
        || slowDays != big.daysAdvanced) {
        return fail("elapsed time reported differently when split up");
    }
    if (slowRt.pendingDailyTicks != fastRt.pendingDailyTicks
        || slowRt.nextDailyTickDay != fastRt.nextDailyTickDay) {
        return fail("daily queue depends on how the advance was split");
    }
    // And the elapsed counts are the ones the ladder itself would compute.
    const std::uint64_t startTick = sm::world_time_at(3, 8, 17).tick;
    if (std::uint64_t(big.minutesAdvanced)
        != sm::absolute_minute(startTick + kTotal) - sm::absolute_minute(startTick)) {
        return fail("reported minutes disagree with the clock's own derivation");
    }
    return true;
}

// The subworld divisor keeps its remainder in the runtime, so a walk broken
// into pieces buys exactly as much daylight as one long one.
bool test_subworld_steps_lose_nothing_when_split() {
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

    if (whole.worldTime.tick != split.worldTime.tick) {
        return fail("subworld clock drifted when the steps were split up");
    }
    if (whole.worldTime.tick
        != sm::world_time_at(2, 12, 0).tick + kSteps / sm::kSubworldTickDivisor) {
        return fail("subworld steps did not buy the divisor's worth of ticks");
    }
    if (splitRt.subworldStepRemainder != kSteps % sm::kSubworldTickDivisor) {
        return fail("subworld remainder was not kept");
    }
    return true;
}

} // namespace

int main() {
    if (!test_hour_rollover()) return 1;
    if (!test_many_small_advances_equal_one_big_one()) return 1;
    if (!test_subworld_steps_lose_nothing_when_split()) return 1;
    if (!test_day_rollover_queues_budgeted_daily_tick()) return 1;
    if (!test_daily_processing_applies_player_upkeep_and_age()) return 1;
    if (!test_settlement_history_keeps_ts_30_day_window()) return 1;

    std::printf("OK world_tick_parity_test clock no_drift subworld_divisor "
                "daily_queue upkeep history\n");
    return 0;
}
