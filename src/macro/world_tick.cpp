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
#include "macro/economy.h"
#include "macro/npc.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

constexpr int   kHistoryWindow     = 30;

inline int rand_range_(WorldTickRuntime& runtime, int lo, int hi) {
    const int span = hi - lo + 1;
    if (span <= 0) return lo;
    return lo + int(runtime.jitter.next_u32() % std::uint32_t(span));
}

inline float rand01_(WorldTickRuntime& runtime) {
    return runtime.jitter.next_f01();
}

void update_settlement_mood_(Settlement& s) {
    const float h = s.eco.happiness;
    if      (h > 0.70f) s.mood = SettlementMood::Prosperous;
    else if (h > 0.50f) s.mood = SettlementMood::Stable;
    else if (h > 0.30f) s.mood = SettlementMood::Tense;
    else if (h > 0.15f) s.mood = SettlementMood::Unrest;
    else                s.mood = SettlementMood::Revolt;
}

void update_village_mood_(Village& v) {
    const float h = v.eco.happiness;
    if      (h > 0.60f) v.mood = SettlementMood::Prosperous;
    else if (h > 0.35f) v.mood = SettlementMood::Stable;
    else                v.mood = SettlementMood::Tense;
}

void push_history_(SettlementHistory& hist, int day, int population) {
    hist.days.push_back(day);
    hist.population.push_back(population);
    if (int(hist.days.size()) > kHistoryWindow) {
        hist.days.erase(hist.days.begin());
        hist.population.erase(hist.population.begin());
    }
}

// ── Settlement daily tick ─────────────────────────────────────
void tick_settlements_(std::vector<Settlement>& settlements, int day,
                       WorldTickRuntime& runtime) {
    for (auto& s : settlements) {
        produce_goods(s.eco, s.population);
        update_prices (s.eco, s.population, /*isCity=*/true);
        const auto tr = consume_and_grow(s.eco, s.population);

        const int moodChange =
            s.mood == SettlementMood::Prosperous ?  1 :
            s.mood == SettlementMood::Revolt     ? -2 : 0;

        s.population = std::max(10, s.population + tr.popDelta
            + moodChange + rand_range_(runtime, 0, 2) - 1);
        s.population = std::min(10'000, s.population);

        update_settlement_mood_(s);

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
void tick_villages_(std::vector<Village>& villages, int day,
                    WorldTickRuntime& runtime) {
    for (auto& v : villages) {
        gather_resources(v.eco, v.population);
        update_prices  (v.eco, v.population, /*isCity=*/false);
        const auto tr = consume_and_grow(v.eco, v.population);

        const int moodChange =
            v.mood == SettlementMood::Prosperous ?  1 :
            v.mood == SettlementMood::Tense      ? -1 : 0;

        v.population = std::max(5, v.population + tr.popDelta
            + moodChange + rand_range_(runtime, 0, 1) - 1);
        v.population = std::min(1'000, v.population);

        update_village_mood_(v);

        push_history_(v.history, day, v.population);
    }
}

// ── Economy tick ──────────────────────────────────────────────
// 1) settle arrived caravans
// 2) villages dispatch peasant traders to their nearest city
// 3) cities dispatch caravans to other cities (and villages)
} // namespace

RouteParties resolve_route_parties(GameState& gs, const TradeRoute& route) {
    RouteParties out{};
    if (route.originIsVillage) {
        for (auto& v : gs.villages)
            if (v.id == route.originId) { out.origin = &v.eco; break; }
    } else {
        for (auto& s : gs.settlements)
            if (s.id == route.originId) { out.origin = &s.eco; break; }
    }
    if (route.destIsVillage) {
        for (auto& v : gs.villages)
            if (v.id == route.destId) { out.dest = &v.eco; break; }
    } else {
        for (auto& s : gs.settlements)
            if (s.id == route.destId) { out.dest = &s.eco; break; }
    }
    return out;
}

namespace {

void tick_economy_(GameState& gs, int day) {
    // 1. Settle arrived routes (iterate in reverse for safe erase).
    for (int i = int(gs.activeTradeRoutes.size()) - 1; i >= 0; --i) {
        TradeRoute& route = gs.activeTradeRoutes[std::size_t(i)];
        if (day < route.arrivalDay) continue;

        const RouteParties parties = resolve_route_parties(gs, route);
        if (parties.dest && parties.origin) {
            settle_trade_route(route, *parties.dest, *parties.origin);
        }
        gs.activeTradeRoutes.erase(gs.activeTradeRoutes.begin() + i);
    }

    // 2. Villages → nearest city.
    for (auto& v : gs.villages) {
        Settlement* city = nullptr;
        for (auto& s : gs.settlements) {
            if (s.id == v.nearestCityId) { city = &s; break; }
        }
        if (!city) continue;

        TradeOrigin origin{v.id, float(v.x), float(v.y), &v.eco};
        std::vector<TradeDest> dests{{city->id, float(city->x), float(city->y),
                                      &city->eco, /*isVillage=*/false}};
        TradeRoute route = find_best_trade_route(origin, dests, /*isVillage=*/true,
                                                 day, v.lastTradeDay,
                                                 gs.mapW, gs.mapH);
        if (route.valid) {
            gs.activeTradeRoutes.push_back(std::move(route));
            v.lastTradeDay = day;
        }
    }

    // 3. Cities → other cities + villages.
    for (auto& city : gs.settlements) {
        std::vector<TradeDest> dests;
        dests.reserve(gs.settlements.size() + gs.villages.size());
        for (auto& s : gs.settlements) {
            if (s.id == city.id) continue;
            dests.push_back({s.id, float(s.x), float(s.y), &s.eco,
                             /*isVillage=*/false});
        }
        for (auto& v : gs.villages) {
            dests.push_back({v.id, float(v.x), float(v.y), &v.eco,
                             /*isVillage=*/true});
        }

        const int lastDay = [&]() {
            auto it = gs.cityLastTradeDay.find(city.id);
            return it == gs.cityLastTradeDay.end() ? 0 : it->second;
        }();

        TradeOrigin origin{city.id, float(city.x), float(city.y), &city.eco};
        TradeRoute route = find_best_trade_route(origin, dests, /*isVillage=*/false,
                                                 day, lastDay,
                                                 gs.mapW, gs.mapH);
        if (route.valid) {
            gs.activeTradeRoutes.push_back(std::move(route));
            gs.cityLastTradeDay[city.id] = day;
        }
    }
}

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
        tick_economy_    (gs,             day);
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
