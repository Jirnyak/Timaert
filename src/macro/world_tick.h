// World tick: advances clock plus budgeted daily settlement simulation.
#pragma once
#include <cstdint>
#include "core/rng.h"
#include "macro/state.h"
#include "macro/deposit_layer.h"

namespace sm {

// How long a day is, how a tick relates to a real second, how much slower the
// clock runs underground — all of that lives in core/time.h now. This file
// only moves the clock forward and bills the world for the days that passed.
// WorldTickRuntime itself lives in macro/state.h since v24: the queue, the
// remainder and the jitter stream are STATE and ride the save.

struct WorldTickResult {
    // Whole world ticks this advance bought. On the macro layer that is one per
    // simulation step; underground it is one per kSubworldTickDivisor steps, so
    // anything that wants to run on WORLD time (rather than wall time) reads
    // this instead of counting frames.
    int  ticksAdvanced = 0;
    int  minutesAdvanced = 0;
    int  hoursAdvanced = 0;
    int  daysAdvanced = 0;
    int  dailyTicksProcessed = 0;
    bool dailyBudgetExhausted = false;
};

// A settlement's garrison recruits daily but nothing ever drains it; without
// a ceiling it crossed the save-format guard (kMaxSoldiers=8192) around game
// day 820 and silently broke the WHOLE save (audit II.4). Hard cap, po2.
inline constexpr int kMaxGarrisonPerSettlement = 64;
inline bool garrison_wants_recruits(int currentSoldiers) {
    return currentSoldiers < kMaxGarrisonPerSettlement;
}


void reset_world_tick_runtime(WorldTickRuntime& runtime, std::uint32_t seed);

// Advance the clock by whole world ticks and queue one daily simulation tick
// per day rollover. Nothing here takes seconds: the caller decides how many
// ticks a simulation step is worth (one on the macro layer, one per
// kSubworldTickDivisor steps underground), and the clock only ever moves by
// integers, so it cannot drift and a save states the instant exactly.
WorldTickResult advance_world_clock(GameState& gs, WorldTickRuntime& runtime,
                                    std::uint64_t ticks);

// Process queued settlement/village/player daily ticks. `macro` (the
// registry's world envelope — state, tree grid, deposit layer, terrain)
// enables the daily resource-growth step (the ONE law: forest growth,
// fauna breeding, wheat healing, iron discovery); nullptr = the world's
// fields sleep (tests, headless drivers).
struct MacroWorld;
int process_world_daily_ticks(GameState& gs, WorldTickRuntime& runtime,
                              int max_daily_ticks,
                              MacroWorld* macro = nullptr);

// Macro-view path: advance time and process queued daily ticks immediately.
WorldTickResult tick_world(GameState& gs, WorldTickRuntime& runtime,
                           std::uint64_t ticks, int max_daily_ticks = 32,
                           MacroWorld* macro = nullptr);

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
