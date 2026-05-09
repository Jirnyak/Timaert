// Faithful port of `src/game/world-tick.ts`.
//
// Drives:
//   • Per-second elapsed → minute/hour/day rollover.
//   • On day rollover: settlement + village daily simulation
//     (economy, mood, garrison, history), trade-route settlement
//     and dispatch, player upkeep + ageing.
//
// One day of in-game time = `kRealSecondsPerDay` real seconds.
// Tunable here only — no other module reads it.

#include "macro/world_tick.h"
#include "macro/economy.h"
#include "macro/army.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>

namespace sm {

namespace {

// 100 real seconds = 1 in-game day (matches GameScreen.svelte).
constexpr float kRealSecondsPerDay = 100.0f;
constexpr float kMinutesPerSecond  = (24.0f * 60.0f) / kRealSecondsPerDay;
constexpr int   kHistoryWindow     = 30;

// Module-private RNG used for the small population jitter that TS does
// with `Math.random()`. Seeded once; not reset across days so behaviour
// matches TS's non-deterministic feel without leaking into other systems.
Rng& jitter_rng_() {
    static Rng r{0xC0FFEEu};
    return r;
}

inline int rand_range_(int lo, int hi) {
    const int span = hi - lo + 1;
    if (span <= 0) return lo;
    return lo + int(jitter_rng_().next_u32() % std::uint32_t(span));
}

inline float rand01_() { return jitter_rng_().next_f01(); }

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
void tick_settlements_(std::vector<Settlement>& settlements, int day) {
    for (auto& s : settlements) {
        produce_goods(s.eco, s.population);
        update_prices (s.eco, s.population, /*isCity=*/true);
        const auto tr = consume_and_grow(s.eco, s.population);

        const int moodChange =
            s.mood == SettlementMood::Prosperous ?  1 :
            s.mood == SettlementMood::Revolt     ? -2 : 0;

        s.population = std::max(10, s.population + tr.popDelta + moodChange + rand_range_(0, 2) - 1);
        s.population = std::min(10'000, s.population);

        update_settlement_mood_(s);

        if (s.population >= 20) {
            auto gr = generate_garrison(s.population, [] { return rand01_(); });
            if (gr.popCost > 0) {
                add_army(s.garrison, gr.garrison);
                s.population = std::max(0, s.population - gr.popCost);
            }
        }

        push_history_(s.history, day, s.population);
    }
}

// ── Village daily tick ────────────────────────────────────────
void tick_villages_(std::vector<Village>& villages, int day) {
    for (auto& v : villages) {
        gather_resources(v.eco, v.population);
        update_prices  (v.eco, v.population, /*isCity=*/false);
        const auto tr = consume_and_grow(v.eco, v.population);

        const int moodChange =
            v.mood == SettlementMood::Prosperous ?  1 :
            v.mood == SettlementMood::Tense      ? -1 : 0;

        v.population = std::max(5, v.population + tr.popDelta + moodChange + rand_range_(0, 1) - 1);
        v.population = std::min(1'000, v.population);

        update_village_mood_(v);

        push_history_(v.history, day, v.population);
    }
}

// ── Economy tick ──────────────────────────────────────────────
// 1) settle arrived caravans
// 2) villages dispatch peasant traders to their nearest city
// 3) cities dispatch caravans to other cities (and villages)
void tick_economy_(GameState& gs, int day) {
    // 1. Settle arrived routes (iterate in reverse for safe erase).
    for (int i = int(gs.activeTradeRoutes.size()) - 1; i >= 0; --i) {
        TradeRoute& route = gs.activeTradeRoutes[std::size_t(i)];
        if (day < route.arrivalDay) continue;

        EconomyState* dest = nullptr;
        EconomyState* origin = nullptr;
        for (auto& s : gs.settlements) {
            if (s.id == route.destId)   dest   = &s.eco;
            if (s.id == route.originId) origin = &s.eco;
        }
        if (!dest || !origin) {
            for (auto& v : gs.villages) {
                if (!dest   && v.id == route.destId)   dest   = &v.eco;
                if (!origin && v.id == route.originId) origin = &v.eco;
            }
        }
        if (dest && origin) {
            settle_trade_route(route, *dest, *origin);
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
        std::vector<TradeDest> dests{{city->id, float(city->x), float(city->y), &city->eco}};
        TradeRoute route = find_best_trade_route(origin, dests, /*isVillage=*/true,
                                                 day, v.lastTradeDay);
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
            dests.push_back({s.id, float(s.x), float(s.y), &s.eco});
        }
        for (auto& v : gs.villages) {
            dests.push_back({v.id, float(v.x), float(v.y), &v.eco});
        }

        const int lastDay = [&]() {
            auto it = gs.cityLastTradeDay.find(city.id);
            return it == gs.cityLastTradeDay.end() ? 0 : it->second;
        }();

        TradeOrigin origin{city.id, float(city.x), float(city.y), &city.eco};
        TradeRoute route = find_best_trade_route(origin, dests, /*isVillage=*/false,
                                                 day, lastDay);
        if (route.valid) {
            gs.activeTradeRoutes.push_back(std::move(route));
            gs.cityLastTradeDay[city.id] = day;
        }
    }
}

// ── Daily player tick (upkeep + age) ──────────────────────────
void tick_player_daily_(PlayerState& p) {
    const int upkeep = calculate_army_upkeep(p.army, p.attributes.cha);
    p.gold = std::max(0, p.gold - upkeep);
    p.ageDays += 1;
}

} // namespace

bool tick_world(GameState& gs, float dt_seconds) {
    if (dt_seconds <= 0.0f) return false;

    // Sub-minute accumulator: TS advances one minute at a time. We carry
    // fractional minutes here so an arbitrary dt does not lose time.
    static float fractional_minute_acc_ = 0.0f;
    fractional_minute_acc_ += dt_seconds * kMinutesPerSecond;
    int whole = int(fractional_minute_acc_);
    fractional_minute_acc_ -= float(whole);

    bool day_rolled = false;
    while (whole-- > 0) {
        gs.worldTime.minute += 1;
        if (gs.worldTime.minute < 60) continue;
        gs.worldTime.minute = 0;
        gs.worldTime.hour += 1;
        if (gs.worldTime.hour < 24) continue;
        gs.worldTime.hour = 0;
        gs.worldTime.day += 1;
        day_rolled = true;

        tick_settlements_(gs.settlements, gs.worldTime.day);
        tick_villages_   (gs.villages,    gs.worldTime.day);
        tick_economy_    (gs,             gs.worldTime.day);
        tick_player_daily_(gs.player);
    }
    return day_rolled;
}

bool tick_world_time_only(GameState& gs, float dt_seconds) {
    if (dt_seconds <= 0.0f) return false;
    static float fractional_minute_acc_ = 0.0f;
    fractional_minute_acc_ += dt_seconds * kMinutesPerSecond;
    int whole = int(fractional_minute_acc_);
    fractional_minute_acc_ -= float(whole);
    bool day_rolled = false;
    while (whole-- > 0) {
        gs.worldTime.minute += 1;
        if (gs.worldTime.minute < 60) continue;
        gs.worldTime.minute = 0;
        gs.worldTime.hour += 1;
        if (gs.worldTime.hour < 24) continue;
        gs.worldTime.hour = 0;
        gs.worldTime.day += 1;
        day_rolled = true;
    }
    return day_rolled;
}

} // namespace sm
