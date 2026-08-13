// Site suitability for the settled world — the owner's causality, priced:
//
//   клеточный мир → рельеф → климат → ресурсы → и только потом заселение.
//
// Resources are PRIMARY, settlement is DERIVED, so every place a city or a
// village may stand is judged by ONE function reading the resource layers
// around it: wheat potential (the ResourceField registry), fresh water
// (rivers are honest water cells), standing trees (the tree-count layer)
// and mineral deposits. Politics decides HOW MANY towns and WHOSE; this
// score decides WHERE among the candidates, HOW LARGE they live (population
// derives from the same number) and HOW MANY villages a hinterland feeds.
// The blind dice it replaces — 18 darts on a ring whose only criterion was
// "land", 1+rng%3 villages, 30+rng%90 souls — were never decisions, only
// noise; rng keeps naming things, nothing more.
//
// Weights are DATA, one row per settlement class — a village is NOT typed
// by its resource (owner): peasants work whatever stands around them, so
// one village row prices grain, water, wood and ore together and the
// profession emerges from the neighbourhood the score chose. Terms are
// integers 0..16 (discrete house style); score = Σ weight × term; a veto
// (water, a forest-massif cell, mountain rock) returns -1 — nobody builds
// there. Deterministic: pure reads, no dice, ties broken by scan order.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "macro/macro_stock.h"   // MacroWorld — the one context envelope

namespace sm {

struct DepositLayer;

enum class SettlementScoreRow : std::uint8_t {
    City = 0,     // politics picks count/owner; water and trade first
    Village,      // the economic creature: the fattest land wins
    Count,
};

struct SettlementScoreWeights {
    const char* id;
    int arable;    // wheat potential the site can plough
    int water;     // river/coast within walking reach
    int forest;    // standing trees to fell
    int deposit;   // veins to mine (iron over stone/clay)
};

// THE weights table. A row is a settlement class, not a resource type.
inline constexpr SettlementScoreWeights kSettlementScoreRows[] = {
    /* City    */ {"city",    2, 4, 1, 1},
    /* Village */ {"village", 4, 3, 2, 2},
};
static_assert(sizeof(kSettlementScoreRows) / sizeof(kSettlementScoreRows[0])
                  == std::size_t(SettlementScoreRow::Count),
              "every SettlementScoreRow needs its weights — the table IS the system");

// The working radius of a settlement, in cells (Chebyshev): the ring its
// people actually farm — the same radius stamp_field_features ploughs.
inline constexpr int kSettlementReach = 2;

// Everything the score reads. MacroWorld is the registry's own envelope
// (terrain + tree layer + scars via gs); deposits still ride their own
// carrier until the R2 migration folds them into the registry.
struct SettlementSiteContext {
    MacroWorld          w{};
    const DepositLayer* deposits = nullptr;
    std::uint8_t        seaLevel8 = 0;
};

// The one door: capacity of a WRAPPED cell as a settlement site.
// -1 = vetoed (water / forest-massif cell / mountain rock); otherwise
// Σ weight × term with every term in 0..16.
int settlement_site_score(const SettlementSiteContext& ctx,
                          SettlementScoreRow row, int x, int y);

// ── Population and village count derive from the same number ────────────
// A village's souls ARE its site score (the land feeds as many as it
// yields), floored so a border-case site is still a hamlet, not a ghost.
inline constexpr int kVillagePopFloor = 16;

// One village per this much summed hinterland capacity: the COUNT of
// villages a city's land carries is its total admissible score / quota.
inline constexpr int kVillageCapacityQuota = 16384;   // po2, calibrated below

// Guard rail for lush deltas.
inline constexpr int kMaxVillagesPerCity = 4;

// The FLOOR of the spacing between village sites (tiny maps). On a real
// hinterland the separation derives from the hinterland itself — villages
// DIVIDE the city's land, each claiming half the reach (see
// village_separation) — so the best sites of one lush pocket cannot all
// crowd into it: the k-best spread around the town instead of stacking a
// block apart (owner: "рассеяны вокруг, не плотными кластерами").
inline constexpr int kVillageSeparationFloor = 4;

inline int village_separation(int hinterlandReach) {
    return std::max(kVillageSeparationFloor, hinterlandReach / 2);
}

// Cities live by the same law (the old 4000+rng%3000 / 800+rng%1500 /
// 600+rng%1200 dice are dead): souls = per-score rate × site capacity,
// floored — a capital is the crown's seat and holds a court whatever
// the ground yields. The city score ceiling is 128 (Σ weight × 16), so
// capitals span 2048..8192 and cities 512..2048 — the old orders of
// magnitude, now earned by the land instead of rolled.
inline constexpr int kCapitalPopPerScore = 64;
inline constexpr int kCityPopPerScore = 16;
inline constexpr int kCapitalPopFloor = 2048;
inline constexpr int kCityPopFloor = 512;

inline int capital_population(int score) {
    return std::max(kCapitalPopFloor,
                    kCapitalPopPerScore * std::max(0, score));
}
inline int city_population(int score) {
    return std::max(kCityPopFloor, kCityPopPerScore * std::max(0, score));
}

} // namespace sm
