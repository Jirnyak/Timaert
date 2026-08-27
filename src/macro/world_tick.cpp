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
#include "macro/player_entity.h"
#include "macro/econ_day.h"
#include "macro/currency.h"
#include "macro/fauna.h"
#include "macro/macro_stock.h"
#include "macro/npc.h"
#include "macro/npc_spawn.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

// One worker at the benches per this many heads (po2) — the город's
// production labour for econ_produce_day.
constexpr int   kHeadsPerCityWorker = 8;

inline float rand01_(WorldTickRuntime& runtime) {
    return runtime.jitter.next_f01();
}

void push_history_(SettlementHistory& hist, int day, int population) {
    // One write and a wrap. The cap lives in the CONTAINER now, so nothing
    // here can forget it and nothing shifts an array to enforce it.
    hist.push(day, population);
}

// The shared tail of every landmark's day (W2b-4): consume off the
// UNIVERSAL inventory, then let ONE continuous wellbeing drive both the
// mood and the LOGISTIC population law (owner's ruling — no flat heads per
// day; K = kPopCarryingCap = the subworld's own NPC cap). Returns the
// wellbeing for callers that want it.
// One landmark fact, filed straight into the world's memory.
//
// `chronicle_record` is THE door; the app-side `record_deed` is a wrapper over
// it that additionally resolves an ENTITY and pays it renown. A landmark has
// no entity and no renown to earn — it is named the day it is founded — so it
// files directly, and there is still one door.
void record_landmark_fact(GameState& gs, FactKind kind, int landmarkId,
                          int x, int y, int amount) {
    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(kind);
    f.subjectKind = std::uint8_t(FactSubject::Landmark);
    f.subject = std::uint32_t(landmarkId < 0 ? 0 : landmarkId);
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = amount;
    chronicle_record(gs.chronicle, f);
}

template <typename Landmark>
void settle_landmark_day(Landmark& lm, int minPop,
                         bool& startedFamine, bool& startedRevolt) {
    startedFamine = false;
    startedRevolt = false;
    // Straight onto the ONE store — the twice-a-day conversion to a second
    // container (and back, through a string lookup each way) is gone with the
    // second index space it existed to bridge.
    const ConsumeOutcome o = econ_consume_day(
        lm.inventory, lm.population, lm.famineActive != 0, nullptr, nullptr);

    lm.starvedYesterday = std::uint16_t(std::min(o.starvedPop, 0xFFFF));
    lm.unmetYesterday   = std::uint16_t(std::min(o.unmetComfort, 0xFFFF));
    // The TRANSITIONS are the story, not the states. A town that has been
    // hungry for a season is one famine, not thirty-two of them, and a
    // chronicle that filed the state every day would bury the day it began.
    startedFamine = o.famineActive && lm.famineActive == 0;
    lm.famineActive = o.famineActive ? 1 : 0;

    const float wellbeing = settlement_wellbeing(o, lm.population);
    const SettlementMood was = lm.mood;
    lm.mood = SettlementMood(mood_band_from_wellbeing(wellbeing));
    startedRevolt = lm.mood == SettlementMood::Revolt
                    && was != SettlementMood::Revolt;

    lm.popGrowthCarry += population_delta_per_day(lm.population, wellbeing);
    const int whole = int(lm.popGrowthCarry);
    if (whole != 0) {
        lm.popGrowthCarry -= float(whole);
        // No ceiling (CANON S25): supply is the only cap — wellbeing already
        // turned negative growth on when the fields and the trade fell short.
        // The FLOOR is a named crutch (canon-audit B6: it mints people from
        // air): until places can die and leave ruins (S9), a cut-down village
        // never empties past it, or the world would quietly go dark with no
        // mechanic to mourn it. It leaves with the living-landmarks track.
        lm.population = std::max(lm.population + whole, minPop);
    }
}

// ── Settlement daily tick ─────────────────────────────────────
void tick_settlements_(GameState& gs, std::vector<Settlement>& settlements,
                       int day, WorldTickRuntime& runtime) {
    for (auto& s : settlements) {
        // The city CRAFTS before it eats: today's table first, then fair
        // shares (econ_day's three passes), off the same one inventory the
        // caravans stock and the market sells from.
        econ_produce_day(s.inventory, EconSite::City,
                         std::max(1, s.population / kHeadsPerCityWorker),
                         s.population, nullptr, nullptr);

        bool famine = false, revolt = false;
        settle_landmark_day(s, /*minPop=*/10, famine, revolt);
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, s.id, s.x, s.y,
                                 int(s.starvedYesterday));
        }
        if (revolt) {
            record_landmark_fact(gs, FactKind::Revolted, s.id, s.x, s.y,
                                 s.population);
        }

        if (s.population >= 20
            && garrison_wants_recruits(total_soldiers(s.garrison))) {
            auto gr = generate_garrison(s.population,
                [&runtime] { return rand01_(runtime); },
                garrison_soldier_id_base(s.id, day));
            if (gr.popCost > 0) {
                // The cap is checked BEFORE the packet is drawn, so a garrison
                // at 63 could take a batch of ten and stand at 73 — its own
                // ceiling overshot by design of the check's placement. Take
                // only what fits, and pay POPULATION only for the men actually
                // taken: the town keeps the heads it did not give up.
                const int room = std::max(
                    0, kMaxGarrisonPerSettlement - total_soldiers(s.garrison));
                int taken = 0;
                for (const SoldierRecord& rec : gr.garrison) {
                    if (taken >= room || !s.garrison.push(rec)) break;
                    ++taken;
                }
                s.population = std::max(0, s.population - taken);
            }
        }

        push_history_(s.history, day, s.population);
    }
}

// ── Village daily tick ────────────────────────────────────────
// No gather here any more: gathering is AGENTS now — woodcutters and
// farmers hauling real units into this same inventory (npc_ai.cpp).
void tick_villages_(GameState& gs, std::vector<Village>& villages, int day,
                    WorldTickRuntime& runtime) {
    (void)runtime;
    for (auto& v : villages) {
        bool famine = false, revolt = false;
        settle_landmark_day(v, /*minPop=*/5, famine, revolt);
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, v.id, v.x, v.y,
                                 int(v.starvedYesterday));
        }
        if (revolt) {
            record_landmark_fact(gs, FactKind::Revolted, v.id, v.x, v.y,
                                 v.population);
        }
        push_history_(v.history, day, v.population);
    }
}

} // namespace

namespace {

// ── Daily player tick (upkeep + age) ──────────────────────────
void tick_player_daily_(PlayerState& p, const SoldierSquad* roster,
                        Inventory* purse) {
    // The player's men are a roster on his squad entity now, exactly like any
    // lord's — no roster (no world yet) simply means no wages.
    const int upkeep = roster
        ? calculate_squad_upkeep(*roster, p.sheet.attributes.of(AttributeId::Cha)) : 0;
    // Pay what the wallet holds; an unpaid remainder is simply unpaid today
    // (wage-debt desertion is the №3 pipeline's future rule).
    if (purse) wallet_spend_up_to(*purse, upkeep);
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
                              int max_daily_ticks, MacroWorld* macro) {
    if (max_daily_ticks <= 0) return 0;

    int processed = 0;
    while (runtime.pendingDailyTicks > 0 && processed < max_daily_ticks) {
        const int day = runtime.nextDailyTickDay;
        tick_settlements_(gs, gs.settlements, day, runtime);
        tick_villages_   (gs, gs.villages,    day, runtime);
        tick_player_daily_(
            gs.player,
            macro && macro->world ? player_roster(*macro->world) : nullptr,
            macro && macro->world ? player_inventory(*macro->world) : nullptr);

        // The ONE growth/diffusion law (R2 track): every resource field is
        // born from time and context through the same walker — the forest
        // plants the forest, beasts breed where beasts are, wheat replants
        // its fertility, and iron strikes where the world ran scarce (the
        // old bespoke W2c roll is now the Iron row's Geology domain). The
        // cadence is GAME days, so resting a month grows what a month
        // grows, however few frames it took.
        if (macro && day > 0) {
            resource_fields_daily_growth(*macro, day);
        }

        // The deserter pool's other half. Beaten armies pour INTO it
        // (macro/squad.h) and, from here, walk back OUT of it as bands — the
        // conservation law closed. The pool is an abstract count, so the day's
        // exodus is √(pool) men and the site is the field's business, not the
        // stock's (macro/npc_spawn.h).
        if (macro && macro->world && macro->terrain) {
            raise_deserter_bands(gs, *macro->world, *macro->terrain, day);
        }

        --runtime.pendingDailyTicks;
        ++runtime.nextDailyTickDay;
        ++processed;
    }
    if (runtime.pendingDailyTicks == 0) runtime.nextDailyTickDay = 0;
    return processed;
}

WorldTickResult tick_world(GameState& gs, WorldTickRuntime& runtime,
                           std::uint64_t ticks, int max_daily_ticks,
                           MacroWorld* macro) {
    WorldTickResult result = advance_world_clock(gs, runtime, ticks);
    result.dailyTicksProcessed =
        process_world_daily_ticks(gs, runtime, max_daily_ticks, macro);
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
