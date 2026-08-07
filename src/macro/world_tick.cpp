// The world's clock, and the daily simulation it pays for.
//
// Drives:
//   • Ticks elapsed → minute/hour/day rollover (core/time.h owns the ladder).
//   • On day rollover: settlement + village daily simulation
//     (economy, mood, garrison, history), trade-route settlement
//     and dispatch, player upkeep + ageing.
//
// This file no longer knows how long a day is in real seconds, or how much
// slower the clock runs underground. It is handed whole ticks and moves the
// world by exactly that many.

#include "macro/world_tick.h"
#include "macro/econ_day.h"
#include "macro/npc.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

constexpr int   kHistoryWindow     = 30;
// One worker at the benches per this many heads (po2) — the город's
// production labour for econ_produce_day.
constexpr int   kHeadsPerCityWorker = 8;

inline float rand01_(WorldTickRuntime& runtime) {
    return runtime.jitter.next_f01();
}

void push_history_(SettlementHistory& hist, int day, int population) {
    hist.days.push_back(day);
    hist.population.push_back(population);
    if (int(hist.days.size()) > kHistoryWindow) {
        hist.days.erase(hist.days.begin());
        hist.population.erase(hist.population.begin());
    }
}

// The shared tail of every landmark's day (W2b-4): consume off the
// UNIVERSAL inventory, then let ONE continuous wellbeing drive both the
// mood and the LOGISTIC population law (owner's ruling — no flat heads per
// day; K = kPopCarryingCap = the subworld's own NPC cap). Returns the
// wellbeing for callers that want it.
template <typename Landmark>
void settle_landmark_day(Landmark& lm, int minPop) {
    Stockpile store = stockpile_from_inventory(lm.inventory);
    const ConsumeOutcome o = econ_consume_day(
        store, lm.population, lm.famineActive != 0, nullptr, nullptr);
    apply_stockpile_to_inventory(store, lm.inventory);

    lm.starvedYesterday = std::uint16_t(std::min(o.starvedPop, 0xFFFF));
    lm.unmetYesterday   = std::uint16_t(std::min(o.unmetComfort, 0xFFFF));
    lm.famineActive     = o.famineActive ? 1 : 0;

    const float wellbeing = settlement_wellbeing(o, lm.population);
    lm.mood = SettlementMood(mood_band_from_wellbeing(wellbeing));

    lm.popGrowthCarry += population_delta_per_day(lm.population, wellbeing);
    const int whole = int(lm.popGrowthCarry);
    if (whole != 0) {
        lm.popGrowthCarry -= float(whole);
        lm.population = std::clamp(lm.population + whole,
                                   minPop, kPopCarryingCap);
    }
}

// ── Settlement daily tick ─────────────────────────────────────
void tick_settlements_(std::vector<Settlement>& settlements, int day,
                       WorldTickRuntime& runtime) {
    for (auto& s : settlements) {
        // The city CRAFTS before it eats: today's table first, then fair
        // shares (econ_day's three passes), off the same one inventory the
        // caravans stock and the market sells from.
        Stockpile store = stockpile_from_inventory(s.inventory);
        econ_produce_day(store, EconSite::City,
                         std::max(1, s.population / kHeadsPerCityWorker),
                         s.population, nullptr, nullptr);
        apply_stockpile_to_inventory(store, s.inventory);

        settle_landmark_day(s, /*minPop=*/10);

        if (s.population >= 20
            && garrison_wants_recruits(total_soldiers(s.garrison))) {
            auto gr = generate_garrison(s.population,
                [&runtime] { return rand01_(runtime); },
                garrison_soldier_id_base(s.id, day));
            if (gr.popCost > 0) {
                add_squad(s.garrison, gr.garrison);
                s.population = std::max(0, s.population - gr.popCost);
            }
        }

        push_history_(s.history, day, s.population);
    }
}

// ── Village daily tick ────────────────────────────────────────
// No gather here any more: gathering is AGENTS now — woodcutters and
// farmers hauling real units into this same inventory (npc_ai.cpp).
void tick_villages_(std::vector<Village>& villages, int day,
                    WorldTickRuntime& runtime) {
    (void)runtime;
    for (auto& v : villages) {
        settle_landmark_day(v, /*minPop=*/5);
        push_history_(v.history, day, v.population);
    }
}

} // namespace

namespace {

// ── Daily player tick (upkeep + age) ──────────────────────────
void tick_player_daily_(PlayerState& p) {
    const int upkeep = calculate_squad_upkeep(p.army, p.sheet.attributes.cha);
    p.gold = std::max(0, p.gold - upkeep);
    p.ageDays += 1;
}

} // namespace

void reset_world_tick_runtime(WorldTickRuntime& runtime, std::uint32_t seed) {
    runtime = WorldTickRuntime{};
    runtime.jitter = Rng{seed ^ 0xC0FFEEu};
}

WorldTickResult advance_world_clock(GameState& gs, WorldTickRuntime& runtime,
                                    std::uint64_t ticks) {
    WorldTickResult result{};
    if (ticks == 0) return result;

    // The clock used to be walked forward one minute at a time so it could
    // count the rollovers on the way. It does not have to be: minutes, hours
    // and days are all linear in the tick (core/time.h), so what an advance
    // covered is a subtraction, however large the jump. A month of resting and
    // a single frame take the same three lines and the same instant lands on
    // the same tick either way — that is the drift test's whole claim.
    const std::uint64_t before = gs.worldTime.tick;
    gs.worldTime.tick = before + ticks;
    const std::uint64_t after = gs.worldTime.tick;

    result.ticksAdvanced   = int(ticks);
    result.minutesAdvanced = int(absolute_minute(after) - absolute_minute(before));
    result.hoursAdvanced   = int(absolute_hour(after)   - absolute_hour(before));
    result.daysAdvanced    = day_of(after) - day_of(before);

    // One queued daily simulation tick per day that rolled over, whether the
    // advance crossed one midnight or forty.
    for (int i = 0; i < result.daysAdvanced; ++i) {
        if (runtime.pendingDailyTicks == 0) {
            runtime.nextDailyTickDay = day_of(before) + 1 + i;
        }
        ++runtime.pendingDailyTicks;
    }
    return result;
}

int process_world_daily_ticks(GameState& gs, WorldTickRuntime& runtime,
                              int max_daily_ticks) {
    if (max_daily_ticks <= 0) return 0;

    int processed = 0;
    while (runtime.pendingDailyTicks > 0 && processed < max_daily_ticks) {
        const int day = runtime.nextDailyTickDay;
        tick_settlements_(gs.settlements, day, runtime);
        tick_villages_   (gs.villages,    day, runtime);
        tick_player_daily_(gs.player);

        --runtime.pendingDailyTicks;
        ++runtime.nextDailyTickDay;
        ++processed;
    }
    if (runtime.pendingDailyTicks == 0) runtime.nextDailyTickDay = 0;
    return processed;
}

WorldTickResult tick_world(GameState& gs, WorldTickRuntime& runtime,
                           std::uint64_t ticks, int max_daily_ticks) {
    WorldTickResult result = advance_world_clock(gs, runtime, ticks);
    result.dailyTicksProcessed =
        process_world_daily_ticks(gs, runtime, max_daily_ticks);
    result.dailyBudgetExhausted = runtime.pendingDailyTicks > 0;
    return result;
}

WorldTickResult tick_world_time_only(GameState& gs, WorldTickRuntime& runtime,
                                     std::uint64_t ticks) {
    WorldTickResult result = advance_world_clock(gs, runtime, ticks);
    result.dailyBudgetExhausted = runtime.pendingDailyTicks > 0;
    return result;
}

WorldTickResult tick_world_subworld_steps(GameState& gs,
                                          WorldTickRuntime& runtime,
                                          std::uint64_t steps) {
    // Underground the day stretches: kSubworldTickDivisor simulation steps buy
    // one tick of world time. The leftover steps stay in the runtime as a whole
    // number, so pausing, saving or walking out mid-divisor loses nothing.
    const std::uint64_t total = runtime.subworldStepRemainder + steps;
    runtime.subworldStepRemainder = total % kSubworldTickDivisor;
    return tick_world_time_only(gs, runtime, total / kSubworldTickDivisor);
}

} // namespace sm
