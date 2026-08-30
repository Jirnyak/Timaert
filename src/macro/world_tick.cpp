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
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

// The registry column speaks EconSite ordinals (landmark_registry.h econSite
// documents this pairing); assert it where both vocabularies are visible, so
// a reordered enum cannot silently swap the towns' and the villages' tables.
static_assert(landmark_def(LandmarkType::City).econSite
                  == std::int8_t(EconSite::City)
              && landmark_def(LandmarkType::Village).econSite
                  == std::int8_t(EconSite::Village),
              "LandmarkDef::econSite must carry EconSite ordinals");

inline float rand01_(WorldTickRuntime& runtime) {
    return runtime.jitter.next_f01();
}

void push_history_(SettlementHistory& hist, int day, int population) {
    // One write and a wrap. The cap lives in the CONTAINER now, so nothing
    // here can forget it and nothing shifts an array to enforce it.
    hist.push(day, population);
}

} // namespace

// The shared tail of every landmark's day (W2b-4): consume off the
// UNIVERSAL inventory, then let ONE continuous wellbeing drive both the
// mood and the LOGISTIC population law (owner's ruling — no flat heads per
// day). At namespace scope (external linkage, the shuffled_order pattern)
// so econ_v1_test can drive a landmark to its honest death directly.
void settle_landmark_day(Landmark& lm,
                         bool& startedFamine, bool& startedRevolt,
                         bool& diedOut,
                         EconFactSink sink, void* user) {
    startedFamine = false;
    startedRevolt = false;
    diedOut = false;
    // Straight onto the ONE store — the twice-a-day conversion to a second
    // container (and back, through a string lookup each way) is gone with the
    // second index space it existed to bridge.
    const ConsumeOutcome o = econ_consume_day(
        lm.inventory, lm.population, lm.famineActive != 0, sink, user);

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
        // And NO FLOOR either (owner, 2026-08-29): the old minPop = 10/5
        // minted people from air and made every settlement immortal
        // (canon-audit B6). Population falls honestly to zero; zero is
        // absorbing by the law itself (population_delta_per_day(0) = 0),
        // and the DEATH is the story — the transition below files a Died
        // fact, so the chronicle mourns the place the crutch used to hide.
        // Turning the empty record into a Ruin is the S9-transition track.
        const int before = lm.population;
        lm.population = std::max(lm.population + whole, 0);
        diedOut = before > 0 && lm.population == 0;
    }
}

namespace {

// The pure econ steps are landmark-blind (they see one Inventory); this relay
// stamps the landmark id onto every fact on its way to the listener — the
// daily tick is the ONE caller that knows whose store it handed in.
struct EconFactRelay {
    EconFactSink sink = nullptr;
    void* user = nullptr;
    int landmarkId = -1;
};

void relay_econ_fact_(void* user, const EconFact& fact) {
    const auto* r = static_cast<const EconFactRelay*>(user);
    EconFact stamped = fact;
    stamped.landmarkId = r->landmarkId;
    r->sink(r->user, stamped);
}

// ── Settlement daily tick ─────────────────────────────────────
void tick_settlements_(GameState& gs, int day, WorldTickRuntime& runtime,
                       EconFactSink sink, void* user) {
    for (auto& s : gs.landmarks) {
        if (s.type != LandmarkType::City) continue;
        EconFactRelay relay{sink, user, s.id};
        const EconFactSink rs = sink ? &relay_econ_fact_ : nullptr;
        void* ru = sink ? static_cast<void*>(&relay) : nullptr;
        // The city CRAFTS before it eats: today's table first, then fair
        // shares (econ_day's three passes), off the same one inventory the
        // caravans stock and the market sells from.
        // Zero souls staff zero benches: max(1, …) alone minted a ghost
        // worker for an empty town — the same air-minting the pop floor did.
        // The production TABLE comes off the place's own registry row
        // (econSite column), not a hardcoded per-loop literal.
        // The mint right, v1: every CITY strikes its own faction's coin
        // (owner 2026-08-30; the right becomes a landmark column when a
        // place ever differs from its kind).
        econ_produce_day(s.inventory, EconSite(landmark_def(s.type).econSite),
                         s.population > 0
                             ? std::max(1, s.population / kHeadsPerCityWorker)
                             : 0,
                         s.population, rs, ru,
                         currency_for_faction_id(faction_id_for_index(
                             faction_index_for_kingdom(gs.politik,
                                                       s.kingdomIdx))));

        bool famine = false, revolt = false, died = false;
        const int headsBefore = s.population;
        settle_landmark_day(s, famine, revolt, died, rs, ru);
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, s.id, s.x, s.y,
                                 int(s.starvedYesterday));
        }
        if (revolt) {
            record_landmark_fact(gs, FactKind::Revolted, s.id, s.x, s.y,
                                 s.population);
        }
        if (died) {
            record_landmark_fact(gs, FactKind::Died, s.id, s.x, s.y,
                                 headsBefore);
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
void tick_villages_(GameState& gs, int day, WorldTickRuntime& runtime,
                    EconFactSink sink, void* user) {
    (void)runtime;
    for (auto& v : gs.landmarks) {
        if (v.type != LandmarkType::Village) continue;
        EconFactRelay relay{sink, user, v.id};
        const EconFactSink rs = sink ? &relay_econ_fact_ : nullptr;
        void* ru = sink ? static_cast<void*>(&relay) : nullptr;
        // The village-side half of the craft door. econ_day.h promises «a
        // village-side craft later is one row with site=Village, no code» —
        // which is only true if this call exists: today no recipe carries
        // that site, so this makes nothing, and the day the row lands it
        // works with no code here either.
        econ_produce_day(v.inventory, EconSite(landmark_def(v.type).econSite),
                         v.population > 0
                             ? std::max(1, v.population / kHeadsPerCityWorker)
                             : 0,
                         v.population, rs, ru);

        bool famine = false, revolt = false, died = false;
        const int headsBefore = v.population;
        settle_landmark_day(v, famine, revolt, died, rs, ru);
        if (famine) {
            record_landmark_fact(gs, FactKind::Starved, v.id, v.x, v.y,
                                 int(v.starvedYesterday));
        }
        if (revolt) {
            record_landmark_fact(gs, FactKind::Revolted, v.id, v.x, v.y,
                                 v.population);
        }
        if (died) {
            record_landmark_fact(gs, FactKind::Died, v.id, v.x, v.y,
                                 headsBefore);
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
        ? calculate_squad_upkeep(
              *roster, calculate_derived(p.sheet.attributes, p.sheet.skills)
                           .tradeDiscount)
        : 0;
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
    // The economy's listener rides the envelope (macro_world.h): null macro
    // or null sink both read as "nobody is listening" — the honest state of
    // the live game today; the balance harness is the first subscriber.
    const EconFactSink esink = macro ? macro->econFacts : nullptr;
    void* euser = macro ? macro->econFactsUser : nullptr;
    while (runtime.pendingDailyTicks > 0 && processed < max_daily_ticks) {
        const int day = runtime.nextDailyTickDay;
        tick_settlements_(gs, day, runtime, esink, euser);
        tick_villages_   (gs, day, runtime, esink, euser);
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
            // The fleet law (npc_spawn.h): a city without a caravan outfits
            // one from its population — losses stay permanent, the trade
            // arm regrows through the world (CANON S4).
            replenish_caravans(gs, *macro->world, *macro->terrain);
            // The labour rotation (npc_ai.h): yesterday's crews dissolve
            // into the population, today's are raised to its size.
            rotate_worker_squads(*macro, day);
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
