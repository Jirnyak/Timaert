// World tick: advances clock plus budgeted daily settlement simulation.
#pragma once
#include <cstdint>
#include "core/rng.h"
#include "macro/state.h"

namespace sm {

// How long a day is, how a tick relates to a real second, how much slower the
// clock runs underground — all of that lives in core/time.h now. This file
// only moves the clock forward and bills the world for the days that passed.
struct WorldTickRuntime {
    int pendingDailyTicks = 0;
    int nextDailyTickDay = 0;
    // Subworld only: the clock there advances one tick per kSubworldTickDivisor
    // simulation steps, and this integer counts the steps not yet spent. An
    // integer remainder is the whole point — the old float scale could not
    // survive a save or a pause without losing a sliver of a day.
    std::uint64_t subworldStepRemainder = 0;
    Rng jitter{0xC0FFEEu};
};

struct WorldTickResult {
    int  minutesAdvanced = 0;
    int  hoursAdvanced = 0;
    int  daysAdvanced = 0;
    int  dailyTicksProcessed = 0;
    bool dailyBudgetExhausted = false;
};

void reset_world_tick_runtime(WorldTickRuntime& runtime, std::uint32_t seed);

// Advance the clock by whole world ticks and queue one daily simulation tick
// per day rollover. Nothing here takes seconds: the caller decides how many
// ticks a simulation step is worth (one on the macro layer, one per
// kSubworldTickDivisor steps underground), and the clock only ever moves by
// integers, so it cannot drift and a save states the instant exactly.
WorldTickResult advance_world_clock(GameState& gs, WorldTickRuntime& runtime,
                                    std::uint64_t ticks);

// Process queued settlement/village/economy/player daily ticks.
int process_world_daily_ticks(GameState& gs, WorldTickRuntime& runtime,
                              int max_daily_ticks);

// Macro-view path: advance time and process queued daily ticks immediately.
WorldTickResult tick_world(GameState& gs, WorldTickRuntime& runtime,
                           std::uint64_t ticks, int max_daily_ticks = 32);

// Clock-only path: advances the clock and queues daily work, but does not
// process it.
WorldTickResult tick_world_time_only(GameState& gs, WorldTickRuntime& runtime,
                                     std::uint64_t ticks);

// Subworld path: N simulation steps buy a whole tick only every
// kSubworldTickDivisor of them; the remainder is kept in the runtime. Returns
// the ticks actually spent so callers can see the clock crawl.
WorldTickResult tick_world_subworld_steps(GameState& gs,
                                          WorldTickRuntime& runtime,
                                          std::uint64_t steps);

} // namespace sm
