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

// The garrison's TARGET strength (§42 Инк 7, owner: «гарнизон = армия
// ландмарка», «у городов должны быть сотни»): population >> the registry
// row's own garrisonShift; a kind whose column is 0xFF keeps none. The
// old ceiling — kMaxGarrisonPerSettlement = 64, and the √pop×0.3-cap-10
// packet under it — sized a tavern recruit pool, not a defense force. The
// target is population-bound by construction (a shift of a bounded
// number), so the runaway that once crossed the save guard (audit II.4)
// cannot recur; the roster's own capacity (kMaxSquadMembers) is the one
// physical wall left, and push refuses out loud at it.
inline int garrison_target_strength(LandmarkType type, int population) {
    const std::uint8_t shift = landmark_def(type).garrisonShift;
    if (shift == 0xFFu || population <= 0) return 0;
    return population >> shift;
}
inline bool garrison_wants_recruits(LandmarkType type, int population,
                                    int currentSoldiers) {
    return currentSoldiers < garrison_target_strength(type, population);
}


// The economy's fact channel (econ_day.h owns the record; the envelope of
// macro_world.h carries the pointer the same way).
struct EconFact;
using EconFactSink = void (*)(void* user, const EconFact& fact);

// One landmark's daily tail: consume off the universal inventory, band the
// mood, run the LOGISTIC population law. No floor and no ceiling (CANON S25
// + owner 2026-08-29): supply is the only cap, and population falls honestly
// to an absorbing zero — the out-flags fire exactly on the transitions
// (famine began / revolt began / the place died out), which is what the
// chronicle files. Public so econ_v1_test can drive a landmark to its death.
// `sink` receives the day's Consumed/Starved/Famine facts (null = silence).
void settle_landmark_day(Landmark& lm,
                         bool& startedFamine, bool& startedRevolt,
                         bool& diedOut,
                         EconFactSink sink = nullptr, void* user = nullptr);

// The dungeon garrisons' regrowth (§42, owner: «как фауна — медленно,
// выбитое подчистую мертво»): one soul per kGrowthEpochDays (32 days, the
// fauna epoch) while the place still LIVES (population > 0) and stands
// below its born MEAN (recomputed from the registry's born columns × the
// kind's context score — nothing stored, nothing to fold into the save).
// A place cleared to zero never regrows — resurrection belongs to the S9
// transition. Each landmark regrows on its own day of the epoch
// (id-staggered, the growth_cell_due pattern), so the world never pulses
// in lockstep. Public so the genesis test can witness the law directly.
void regrow_dungeon_populations(const MacroWorld& w, int day);

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
