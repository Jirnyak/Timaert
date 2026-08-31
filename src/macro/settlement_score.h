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
#include <vector>

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

// The working radius of a settlement, in cells (Chebyshev): THE home-field
// box — the ±3 ring find_home_field harvests, the plough prospects
// (npc_ai.cpp) and the genesis stamp seeds (spawners.cpp). ONE radius,
// three consumers; it was 2 here while the crews' box was a drifted
// hardcoded 3 — unified 2026-08-31 with the field-birth law.
inline constexpr int kSettlementReach = 3;

// Everything the score reads. MacroWorld is the registry's own envelope —
// terrain, tree layer, deposit layer and scars all ride it, and every term
// prices cells through the ONE registry door.
struct SettlementSiteContext {
    MacroWorld   w{};
    std::uint8_t seaLevel8 = 0;
    // The deposit-reach FIELD (owner 2026-08-31, «полевой подход»): the
    // sparse veins splatted once per world to the crews' working reach so
    // the score sees what the hands actually mine — a per-candidate 33²
    // box scan over four hash maps priced a billion lookups per worldgen.
    // Built by build_deposit_reach_field below; the vector is the caller's,
    // width×height, value = the deposit term 0..16 at that cell. Null =
    // the term reads 0 (a context without geology prices none — the same
    // fail-closed zero every absent layer answers).
    const std::uint16_t* depositReach = nullptr;
};

// Splat every live vein's worth ladder into a flat field: value at a cell =
// max over veins in reach of (siteWorth >> (d / (kSettlementReach + 1))) —
// the same po2 ladder village_pressure walks, clamped to the term contract
// 0..16. One law of distance for working, pricing and pressing.
std::vector<std::uint16_t> build_deposit_reach_field(const DepositLayer& dl,
                                                     int mapW, int mapH);

// The one door: capacity of a WRAPPED cell as a settlement site.
// -1 = vetoed (water / forest-massif cell / mountain rock); otherwise
// Σ weight × term with every term in 0..16.
int settlement_site_score(const SettlementSiteContext& ctx,
                          SettlementScoreRow row, int x, int y);

// The raw terms behind the score — same reads, same vetoes (vetoed ground
// answers all-zero terms and the score door answers -1). Exposed so birth
// gates can ask "CAN this place feed itself" without a second term
// implementation drifting beside the score's.
struct SettlementSiteTerms {
    int arable = 0, water = 0, forest = 0, deposit = 0;
};
SettlementSiteTerms settlement_site_terms(const SettlementSiteContext& ctx,
                                          int x, int y);

// The SELF-FEEDING GATE of village birth (owner's field law, 2026-08-31):
// beyond each city's forced first hamlet, a site must either plough enough
// to feed the owner's-scale souls — arable ≥ 4: the top parcels at a mean
// of ~1024 stands turn over ~128 grain/day per season-regrow, the born
// hundred fed by its own box — or sit on ore worth the trade: deposit ≥ 8
// is a prize vein (worth 16) within half the working reach, the mining
// village the owner legalized, living on bought bread. Without this gate
// the relative quality bar let an empty steppe breed hamlets as freely as
// a river delta (measured by the placement test's lush>dry control).
inline constexpr int kVillageArableGate  = 4;
inline constexpr int kVillageDepositGate = 8;

// ── Birth by the settlement FIELD (owner 2026-08-31, «полевой подход») ───
// Candidates are priced by the ONE score; villages OCCUPY best-first, and
// every placed village PRESSES the field around itself — «поставленная
// деревня сразу давит поле, следующая берёт нового лучшего». The old
// separation rule, the capacity quota and the per-city cap all died into
// this law: their work is done by the field itself.
//
// The press of a placed village of score S on a candidate at Chebyshev
// distance d: the home-field box (d ≤ kSettlementReach) is CLAIMED whole —
// the full S, so no later candidate inside another village's farmland can
// survive the relative-quality bar below — and beyond it the press halves
// per box-width of distance, the same po2 ladder the deposit reach walks.
inline int village_pressure(int placedScore, int d) {
    if (d <= kSettlementReach) return placedScore;
    return placedScore >> (d / (kSettlementReach + 1));
}

// Placement stops when the best PRESSED candidate falls under HALF the
// hinterland's own first-best: «занимаются от самых дорогих», and the tail
// is cut by the land's own quality bar — no absolute constant, a lush
// delta feeds many villages and a dry steppe few, emergently.

// Born souls = the OWNER'S SCALE, never the score (CANON S25, 2026-08-31:
// «скор — закон места, души — замысел масштаба»): a village is a
// hundred-odd souls — base + a seed roll of the spread lands [64..191],
// the «крупная деревня ~200» tail included. The score decides only WHERE
// and in what ORDER.
inline constexpr int kVillageBornBase   = 64;
inline constexpr int kVillageBornSpread = 128;

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
