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
#include "macro/items.h"

namespace sm {

// ── State ────────────────────────────────────────────────────────────────

// Integer units per commodity — discrete by house style; signed so a
// bookkeeping bug shows up as a negative number, not a silent wrap.
struct Stockpile {
    std::array<int, kCommodityCount> qty{};
};

// A source of raw units in the world: a grain field, a clay pit, a stand of
// timber. Finite — gathering DRAINS it (regrowth is the tree-layer-style
// years-toward-baseline pass, a later increment; the ledger already treats
// deposits as part of the conserved total).
struct Deposit {
    int commodity = 0;   // must be a Raw-tier index
    int remaining = 0;
};

// ── Data tables ──────────────────────────────────────────────────────────

// v1: buildings are ABSTRACT (owner ruling) — a recipe names the KIND of
// place it runs in, not a building.
enum class EconSite : std::uint8_t { Village = 0, City = 1 };

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
inline constexpr RecipeDef kRecipes[] = {
    {"bread",     {{"grain", 1}, {nullptr, 0}}, 8, EconSite::City},
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

inline constexpr int kGatherPerWorkerDay = 8;

// ── Facts ────────────────────────────────────────────────────────────────

struct EconFact {
    enum class Kind : std::uint8_t {
        Gathered = 0,       // commodity, amount
        Produced = 1,       // commodity, amount
        FamineStarted = 2,  // amount = starved pops today
        FamineEnded = 3,
        Starved = 4,        // amount = pops that went unfed today
    };
    Kind kind{};
    int commodity = -1;
    int amount = 0;
};
using EconFactSink = void (*)(void* user, const EconFact& fact);

// ── The day, in three pure steps ─────────────────────────────────────────

// Workers pull raw units out of deposits (in deposit order) into the store.
// Returns total gathered. Conservation: Σdeposits shrinks by exactly the
// amount the stockpile grows.
int econ_gather_day(Stockpile& store, Deposit* deposits, int depositCount,
                    int workers, EconFactSink sink, void* user);

// Workers run the site's recipes in three passes: today's TABLE first (each
// consumed output staffed up to the town's daily demand, table order — bread
// can never be starved by a fair share), then FAIR SHARES of the remaining
// workers across recipes with inputs (the surplus), then leftovers in table
// order. Returns total units produced. Conservation: inputs leave the store
// as outputs enter.
int econ_produce_day(Stockpile& store, EconSite site, int workers,
                     int population, EconFactSink sink, void* user);

struct ConsumeOutcome {
    int fedPop = 0;         // pops whose vital need was met today
    int starvedPop = 0;     // pops that went without bread today
    int unmetComfort = 0;   // non-daily units short of demand
    int comfortDemand = 0;  // total non-daily units demanded (unmet's scale)
    bool famineActive = false;
};

// Population eats down the needs ladder. `famineWasActive` carries yesterday's
// state so FamineStarted/FamineEnded fire exactly on the transitions.
ConsumeOutcome econ_consume_day(Stockpile& store, int population,
                                bool famineWasActive,
                                EconFactSink sink, void* user);

// ── Population and mood (owner's law, W2b-4) ─────────────────────────────
//
// LOGISTIC growth, not flat heads per day: dP = r·P·(1−P/K)·drive, where
// drive ∈ [−1, +1] comes from continuous WELLBEING (fed fraction, softened
// by comfort shortfall). K = 16384 — 2^14, the very cap the subworld holds
// for NPCs, so no town can outgrow the world that must embody it. The
// logistic damp applies to GROWTH only: starvation is not softened by being
// near the cap. Small towns grow slowly because r·P is small — the owner's
// point that "+1 a day" was absurd for a hamlet.
inline constexpr int   kPopCarryingCap     = 16384;         // 2^14
inline constexpr float kPopGrowthRatePerDay = 1.0f / 256.0f; // po2 rate

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
    const float damp = drive > 0.0f
        ? 1.0f - float(population) / float(kPopCarryingCap)
        : 1.0f;
    return kPopGrowthRatePerDay * float(population) * damp * drive;
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

// ── Inventory adapter ────────────────────────────────────────────────────
// The landmark's persistent truth is its universal Inventory (owner's
// ruling); the day-loop works a flat Stockpile. Convert at the day boundary
// — non-commodity stacks (potions, weapons the player sold) ride untouched.
Stockpile stockpile_from_inventory(const Inventory& inv);
void apply_stockpile_to_inventory(const Stockpile& s, Inventory& inv);

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
void seed_landmark_inventory(Inventory& inv, int population, EconSite site);

} // namespace sm
