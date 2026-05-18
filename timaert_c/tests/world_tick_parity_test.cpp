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

float one_world_minute_seconds() {
    return 1.0f / sm::kWorldMinutesPerSecond;
}

bool test_hour_rollover_matches_ts_minute_tick() {
    sm::GameState gs{};
    gs.worldTime = sm::WorldTime{7, 6, 59};

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 123u);

    const sm::WorldTickResult result =
        sm::advance_world_clock(gs, runtime, one_world_minute_seconds());

    if (result.minutesAdvanced != 1 || result.hoursAdvanced != 1
        || result.daysAdvanced != 0) {
        return fail("single minute did not report one hour rollover");
    }
    if (gs.worldTime.day != 7 || gs.worldTime.hour != 7
        || gs.worldTime.minute != 0) {
        return fail("single minute did not roll 06:59 to 07:00");
    }
    if (runtime.pendingDailyTicks != 0 || runtime.nextDailyTickDay != 0) {
        return fail("non-midnight hour rollover queued a daily tick");
    }
    return true;
}

bool test_day_rollover_queues_budgeted_daily_tick() {
    sm::GameState gs{};
    gs.worldTime = sm::WorldTime{7, 23, 59};
    gs.player.ageDays = 1000;

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 456u);

    const sm::WorldTickResult result =
        sm::advance_world_clock(gs, runtime, one_world_minute_seconds());

    if (result.minutesAdvanced != 1 || result.hoursAdvanced != 1
        || result.daysAdvanced != 1) {
        return fail("single minute did not report midnight rollover");
    }
    if (gs.worldTime.day != 8 || gs.worldTime.hour != 0
        || gs.worldTime.minute != 0) {
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
    gs.player.attributes.cha = 0;
    gs.player.army.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1, 77u));

    sm::WorldTickRuntime runtime{};
    sm::reset_world_tick_runtime(runtime, 789u);
    runtime.pendingDailyTicks = 1;
    runtime.nextDailyTickDay = 12;

    const int expectedUpkeep =
        sm::calculate_squad_upkeep(gs.player.army, gs.player.attributes.cha);
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

} // namespace

int main() {
    if (!test_hour_rollover_matches_ts_minute_tick()) return 1;
    if (!test_day_rollover_queues_budgeted_daily_tick()) return 1;
    if (!test_daily_processing_applies_player_upkeep_and_age()) return 1;
    if (!test_settlement_history_keeps_ts_30_day_window()) return 1;

    std::printf("OK world_tick_parity_test clock daily_queue upkeep history\n");
    return 0;
}
