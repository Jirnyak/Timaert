// The honest economy day-loop — костяк №1 (work_vector, owner-approved form).
//
// THE LAW this module exists to uphold: RESOURCE IS CONSERVED. Nothing is
// created from population formulas; every unit is gathered from a deposit by
// worker-days, transformed by a recipe, or consumed by a need. The self-play
// test (tests/econ_v1_test.cpp) proves the ledger balances to the unit —
// that test, run over years of game time, is the balancing arbiter.
//
// Anchor unit: the PERSON-DAY. 1 bread feeds 1 pop for 1 day; a gatherer
// pulls kGatherPerWorkerDay raw units a day; each recipe names its
// output-per-worker-day. Round po2-family numbers by house style.
//
// FACTS: every noteworthy happening is reported through EconFactSink — the
// «система обязана объявить свои факты» invariant. This is a POD callback,
// NOT the EventBus: macro is L1 and may not include events (L3); the app
// layer forwards the facts it wants onto the bus when this loop is wired in.
//
// NOT WIRED into world_tick yet — deliberately additive. Wiring replaces
// EconomyState's twin float arrays with Stockpile and is its own increment.
#pragma once
#include <array>
#include <cstdint>
#include "macro/commodity.h"
#include "macro/seasons.h"
#include "macro/items.h"

namespace sm {

// ── State ────────────────────────────────────────────────────────────────

// Integer units per commodity — discrete by house style; signed so a
// bookkeeping bug shows up as a negative number, not a silent wrap.
// THE store of a place is its INVENTORY — there is no second container and no
// second index space (owner's ruling, 2026-08-27: полное слияние). A flat
// `Stockpile` of 14 commodity counts used to live here and be converted to and
// from the landmark's inventory TWICE PER GAME DAY per landmark, through a
// string lookup each way, because the same noun was addressed by two different
// ordinals. `bread` is one row of one catalog now.
//
// The day's steps stay PURE (owner: «шаги остаются ЧИСТЫМИ»): they take data
// and tables, never the world.
//
// A commodity's CATALOG ordinal, resolved once and cached — the day loop asks
// this instead of carrying a second numbering of the same things.
int commodity_item_index(int commodityIdx);

// ── Data tables ──────────────────────────────────────────────────────────

// v1: buildings are ABSTRACT (owner ruling) — a recipe names the KIND of
// place it runs in, not a building.
// Any (owner 2026-08-30, CANON S10): a recipe every settled place can run —
// the bread row moved here, because «деревня печёт хуже уже потому, что её
// меньше»: the population-efficiency law below prices the difference, not a
// second recipe and not a site wall.
enum class EconSite : std::uint8_t { Village = 0, City = 1, Any = 2 };

inline bool recipe_runs_at(EconSite recipeSite, EconSite here) {
    return recipeSite == EconSite::Any || recipeSite == here;
}

struct RecipeInput {
    const char* id;
    int qty;
};

struct RecipeDef {
    const char* output;          // commodity id
    RecipeInput inputs[2];       // unused slot: {nullptr, 0}
    int outputPerWorkerDay;      // units one worker makes per day
    EconSite site;
};

// Производство «обычно в городе» (owner) — v1 keeps every craft in the City;
// a village-side craft later is one row with site=Village, no code.
// The MINT output marker: not a commodity row — the produce scheduler
// resolves it into the town's own faction coin (CANON S10: чеканка = рецепт;
// выход монет из единицы металла = стоимость металла по единой таблице цен,
// «таблица цен и есть монетный двор»; сеньораж эмерджентен из рыночного
// спреда серебра).
inline constexpr const char* kMintOutput = "coin";

inline constexpr RecipeDef kRecipes[] = {
    {"bread",     {{"grain", 1}, {nullptr, 0}}, 8, EconSite::Any},
    // ЧЕКАНКА: все города (site City = право v1); 4 металла на рабочий-день
    // — балансовая крутилка темпа эмиссии.
    {kMintOutput, {{"silver", 1}, {nullptr, 0}}, 4, EconSite::City},
    {"bricks",    {{"clay", 1},  {nullptr, 0}}, 8, EconSite::City},
    {"cloth",     {{"grain", 2}, {nullptr, 0}}, 4, EconSite::City},
    {"tools",     {{"iron", 1},  {"wood", 1}},  2, EconSite::City},
    {"furniture", {{"wood", 2},  {nullptr, 0}}, 2, EconSite::City},
    {"wagon",     {{"wood", 4},  {"iron", 1}},  1, EconSite::City},
    {"jewelry",   {{"iron", 1},  {"stone", 1}}, 1, EconSite::City},
    {"carving",   {{"wood", 1},  {nullptr, 0}}, 2, EconSite::City},
    {"statue",    {{"stone", 8}, {nullptr, 0}}, 1, EconSite::City},
};
inline constexpr int kRecipeCount = int(sizeof(kRecipes) / sizeof(kRecipes[0]));

// The needs ladder (потребности по ярусам). popPerUnitDay: one unit serves
// that many pop-days (po2 → the daily demand is a shift). Vital shortfall
// STARVES; Instrument/Luxury shortfall only goes unmet (mood/growth math
// consumes these counts at wiring time).
struct NeedDef {
    const char* commodity;
    int popPerUnitDay;
};
inline constexpr NeedDef kNeeds[] = {
    {"bread",     1},    // 1 хлеб = 1 житель-день; ЕДИНСТВЕННАЯ голодная нужда v1
    {"cloth",     32},   // одежда изнашивается: 1 на 32 жителе-дня
    {"bricks",    64},   // поддержание жилья
    {"tools",     16},
    {"furniture", 64},
    {"jewelry",   32},
    {"carving",   32},
    {"statue",    512},
};
inline constexpr int kNeedCount = int(sizeof(kNeeds) / sizeof(kNeeds[0]));

inline constexpr int kGatherPerWorkerDay = 32;

// One worker per this many heads (po2) — THE labour quota: the same share
// of hands staffs the benches (econ_produce_day) and walks out with the
// daily crews (npc_ai.h rotate_worker_squads). One number, one law of who
// works; it moved here from world_tick.cpp when the crews were born
// (2026-08-30) so the two consumers cannot drift.
inline constexpr int kHeadsPerCityWorker = 8;

// The working rhythm (owner 2026-08-30, CANON S10/S14: work burns the SAME
// SP the march does — no second labour law). One work cycle costs a quarter
// of the squad's full bar, so a rested worker BESIDE his parcel makes four
// hauls a day: kGatherPerWorkerDay is now the DERIVED day of such a worker
// (4 cycles × the per-cycle take below), and a far vein pays part of the
// bar to the road and honestly loses hauls. Headcount multiplies the yield,
// never the price — the bar belongs to the squad.
inline constexpr int kWorkCyclesPerBar = 4;
static_assert(kGatherPerWorkerDay % kWorkCyclesPerBar == 0,
              "the person-day must divide into whole cycle takes");
inline constexpr int kGatherPerCycle = kGatherPerWorkerDay / kWorkCyclesPerBar;

// ── Facts ────────────────────────────────────────────────────────────────

struct EconFact {
    enum class Kind : std::uint8_t {
        Gathered = 0,       // commodity, amount
        Produced = 1,       // commodity, amount
        FamineStarted = 2,  // amount = starved pops today
        FamineEnded = 3,
        Starved = 4,        // amount = pops that went unfed today
        Consumed = 5,       // commodity, amount — the needs ladder's take
        Minted = 6,         // amount = coins struck (commodity = silver row)
    };
    Kind kind{};
    int commodity = -1;
    int amount = 0;
    // Which landmark's day this was. The pure steps are landmark-BLIND (they
    // see one Inventory); the id is stamped by the relay in world_tick.cpp —
    // the one caller that knows whose store it handed in. -1 = unattributed
    // (a direct pure-step call, e.g. econ_v1_test).
    int landmarkId = -1;
};
using EconFactSink = void (*)(void* user, const EconFact& fact);

// ── The day, in three pure steps ─────────────────────────────────────────

// Workers run the site's recipes in three passes: today's TABLE first (each
// consumed output staffed up to the town's daily demand, table order — bread
// can never be starved by a fair share), then FAIR SHARES of the remaining
// workers across recipes with inputs (the surplus), then leftovers in table
// order. Returns total units produced. Conservation: inputs leave the store
// as outputs enter.
// `mintCurrencyId`: the faction coin the kMintOutput recipe strikes —
// null = this place has no mint right and the row simply does not run.
int econ_produce_day(Inventory& store, EconSite site, int workers,
                     int population, EconFactSink sink, void* user,
                     const char* mintCurrencyId = nullptr);

struct ConsumeOutcome {
    int fedPop = 0;         // pops whose vital need was met today
    int starvedPop = 0;     // pops that went without bread today
    int unmetComfort = 0;   // non-daily units short of demand
    int comfortDemand = 0;  // total non-daily units demanded (unmet's scale)
    bool famineActive = false;
};

// Population eats down the needs ladder. `famineWasActive` carries yesterday's
// state so FamineStarted/FamineEnded fire exactly on the transitions.
ConsumeOutcome econ_consume_day(Inventory& store, int population,
                                bool famineWasActive,
                                EconFactSink sink, void* user);

// ── Population and mood (owner's law, W2b-4) ─────────────────────────────
//
// LOGISTIC growth, not flat heads per day: dP = r·P·(1−P/K)·drive, where
// drive ∈ [−1, +1] comes from continuous WELLBEING (fed fraction, softened
// by comfort shortfall).
//
// NO CARRYING CAP (CANON S25, the owner's word): «ни константы-потолка, ни
// ёмкости как крышки быть не должно». The ceiling EMERGES from supply — a
// town that outgrows its fields and its trade sees wellbeing fall and stops;
// famine turns it around. A town on a crossroads may outgrow one on black
// earth with no roads, and that is the right world. (The old kPopCarryingCap
// = 16384 was one lid for every town on the map — canon-audit III.6.)
//
// The rate is quoted PER SEASON (owner 2026-08-24): a month IS a season here
// — 32 days, the same epoch the forest grows by (kGrowthEpochDays) — and in
// a fantasy town of a thousand souls a birth is an event, not daily noise.
// The daily tick only ACCRUES the season rate into the fractional carry;
// whole people appear when the carry crosses one.
inline constexpr float kPopGrowthPerSeason = 1.0f / 8.0f;  // po2: +12.5%/season fully fed
inline constexpr float kPopGrowthRatePerDay =
    kPopGrowthPerSeason / float(kDaysPerSeason);

// Continuous wellbeing in [0, 1]: the fed fraction, softened by how much of
// the comfort ladder went unmet. 0.5 is the waterline — above it the town
// grows, below it shrinks.
inline float settlement_wellbeing(const ConsumeOutcome& o, int population) {
    if (population <= 0) return 0.5f;
    const float fedFrac = float(o.fedPop) / float(population);
    const float comfortFrac = o.comfortDemand > 0
        ? 1.0f - float(o.unmetComfort) / float(o.comfortDemand)
        : 1.0f;
    return fedFrac * (0.5f + 0.5f * comfortFrac);
}

inline float population_delta_per_day(int population, float wellbeing) {
    if (population <= 0) return 0.0f;
    const float drive = (wellbeing - 0.5f) * 2.0f;   // [-1, +1]
    // Growth and decline ride the same season-quoted rate; nothing damps
    // starvation and nothing caps plenty — supply is the only ceiling (S25).
    return kPopGrowthRatePerDay * float(population) * drive;
}

// Mood is the SAME wellbeing banded for the eye (0 = Prosperous …
// 4 = Revolt, matching SettlementMood's order) — context, not a second law.
inline int mood_band_from_wellbeing(float wellbeing) {
    if (wellbeing >= 0.8f) return 0;
    if (wellbeing >= 0.6f) return 1;
    if (wellbeing >= 0.4f) return 2;
    if (wellbeing >= 0.2f) return 3;
    return 4;
}


// ── Birth stocks ─────────────────────────────────────────────────────────

// A landmark is born MID-LIFE, not at hour zero (owner's ruling): worldgen
// seeds its universal Inventory as if it had been living for years, so the
// market has wares on day one and nobody starves while the first caravans
// find their legs. One law, deterministic from population:
//   · the daily-vital row (bread) holds kSeedVitalDays of the table — a
//     larder, not a warehouse;
//   · every other need row holds a stretch of its daily demand — a season
//     in a crafting City, days in a gathering Village;
//   · raw stocks are a production buffer per head — doubled in a Village,
//     whose whole business is raw.
void seed_landmark_inventory(Inventory& inv, int population, EconSite site,
                             const char* currencyId);

} // namespace sm
