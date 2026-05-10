// World tick: advances clock plus budgeted daily settlement simulation.
#pragma once
#include <cstdint>
#include "core/rng.h"
#include "macro/state.h"

namespace sm {

inline constexpr float kWorldRealSecondsPerDay = 100.0f;
inline constexpr float kWorldMinutesPerSecond =
    (24.0f * 60.0f) / kWorldRealSecondsPerDay;

struct WorldTickRuntime {
    float fractionalMinute = 0.0f;
    int   pendingDailyTicks = 0;
    int   nextDailyTickDay = 0;
    Rng   jitter{0xC0FFEEu};
};

struct WorldTickResult {
    int  minutesAdvanced = 0;
    int  hoursAdvanced = 0;
    int  daysAdvanced = 0;
    int  dailyTicksProcessed = 0;
    bool dailyBudgetExhausted = false;
};

void reset_world_tick_runtime(WorldTickRuntime& runtime, std::uint32_t seed);

// Advance the clock and queue one daily simulation tick per day rollover.
WorldTickResult advance_world_clock(GameState& gs, WorldTickRuntime& runtime,
                                    float dt_seconds);

// Process queued settlement/village/economy/player daily ticks.
int process_world_daily_ticks(GameState& gs, WorldTickRuntime& runtime,
                              int max_daily_ticks);

// Macro-view path: advance time and process queued daily ticks immediately.
WorldTickResult tick_world(GameState& gs, WorldTickRuntime& runtime,
                           float dt_seconds, int max_daily_ticks = 32);

// Clock-only path: advances minute/hour/day and queues daily work, but does
// not process it.
WorldTickResult tick_world_time_only(GameState& gs, WorldTickRuntime& runtime,
                                     float dt_seconds);

} // namespace sm
