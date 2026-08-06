// Movement stamina — how much a step across the world costs, and what happens
// when the body cannot pay it.
//
// ONE UNIT: the macro cell. A journey costs `terrain weight × distance in macro
// cells`, wherever it is walked. The map layer covers exactly one cell per cell;
// the subworld covers `tiles / kCellSize` of one. That is why the same crossing
// costs the same on both layers — the earlier model had two independent laws
// (10 SP per macro cell here, 10 SP per 1000 tiles there) that disagreed by
// roughly 12× once converted to game time, so the price of a road depended on
// which layer you happened to be looking at it from.
//
// Costs are FRACTIONAL and accumulate; SP is spent in whole points
// (TravelStamina below), the same fractional-carry idiom the hourly regeneration
// uses (macro/player_recovery.cpp). Nothing is lost to rounding and nothing is
// stored in the save — a load starts the carry at zero, worth at most 1 SP.
#pragma once
#include <cmath>
#include "macro/attributes.h"
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm {

// SP per weight-unit per macro cell. THE knob for how far a body can march,
// and it is calibrated in GAME HOURS against the owner's anchor (Session 21):
// a fresh, unskilled traveller BURNS HIS WHOLE BAR IN ABOUT 8 HOURS OF ROAD —
// a real day's march, camp by nightfall. 7/16 (po2 fraction, house style):
// 110 SP / (1.0 road weight × 7/16 × 32 cells/h) = 7.9 h. With the terrain
// speed law below folded in, the fresh traveller's bar buys about
//
//     road 7.9 h · meadow 5.6 h · forest 4.5 h · mountain 3.5 h · water 2.5 h
//
// The previous 0.2 was NOT a working economy at level 1: the road cost
// 6.4 SP/h against a standing recovery of ~10 SP/h, so any pause between
// clicks repaid the walk and the player could march for days without the bar
// moving — "SP не тратится вообще" (owner, in play). Now the road costs
// 14 SP/h against a fresh regen of 13.75 SP/h: an unhurried walk with rests
// breaks even ON THE ROAD ONLY — it pays in TIME, the real currency — while
// off-road marching always outruns the rest. Stamina still does not recover
// while marching (kMarchRecoveryPct), which is what makes this a budget
// instead of an allowance.
constexpr float kStaminaPerCell = 0.4375f;   // 7/16

// Fraction of the normal stamina recovery that a MARCHING body gets. Zero: legs
// in motion are not resting. HP and MP are untouched — this is about stamina.
constexpr float kMarchRecoveryPct = 0.0f;

// What the `travel` skill does, and the only thing it does: it buys down the
// stamina cost of ground, one percent per rank, under THE skill law
// (macro/attributes.h): rank == percent, capped at kMaxSkillRank. At mastery the
// terrain costs nothing — a hundred levels poured into travelling, and the world
// stops resisting you. What that does NOT buy is a free ride: an overloaded pack
// is a separate term in the cost, and the exhaustion curve is untouched.
//
// One skill, one effect. Distance is this skill's business; SPEED is `spd` and
// `athletics`. The two never fight, which is a property of pricing by CELL and
// not by time: walking faster covers the same ground for the same stamina, it
// simply takes fewer hours.
inline float travel_skill_efficiency(const Skills& s) {
    return skill_cost_mult(s.travel);
}

// Death by exhaustion, and it is DESIGN, not an accident: past zero, every
// further step costs HP equal to the WHOLE outstanding stamina debt, so the
// deeper the hole the more each step takes. Pressing on is a real gamble rather
// than a free ride on negative stamina. 1.0 = the full debt; lower is gentler.
constexpr float kExhaustionBite = 1.0f;

// How fast the macro march covers ground, in cells per GAME HOUR. Not per real
// second — that is the whole point. Stamina is priced per cell and recovery per
// game hour, so this constant IS the exchange rate between the two, and quoting
// it in game time means the length of a day can be tuned as a matter of feel
// without moving the travel economy a single point.
//
// The subworld's own walk (kSubworldWalkTilesPerSecond in main.cpp) stays in
// REAL seconds on purpose, and the difference is not an oversight: down there
// you are a body doing a thing in real time, up here you are an abstraction of
// a journey. Two different denominators for two different kinds of motion.
constexpr float kMacroWalkCellsPerHour = 32.0f;

// How much the GROUND slows the march: speed = base / √weight, derived from
// the SAME weight table that prices stamina (owner ruling, Session 21) —
// heavy ground is automatically both slower and costlier, a new biome is one
// weight row, and there is no second table to drift out of lockstep with the
// first (the target_radius lesson). √ rather than 1/weight so terrain bites
// but does not crawl: water (10×) walks at a third of road pace, not a tenth.
//
// Composition note: stamina is priced per CELL, so slowing down does not add
// SP cost — it converts part of the terrain's price from stamina into HOURS.
// Per game hour the burn is (√weight × base × kStaminaPerCell): the weight
// table's ORDER is preserved, its spread arrives as time and stamina both.
inline float terrain_speed_mult(float weight) {
    if (weight <= 1.0f) return 1.0f;
    return 1.0f / std::sqrt(weight);
}

inline float biome_sp_weight(Biome b) {
    // Indexed by Biome id: Tundra..Water (0..9), Mountain (10). Mountain carries
    // the traversal cost that used to live on the FT_Mountain feature (5.0 → the
    // old 50-SP crossing), now that mountains are a biome, not a feature.
    static const float kW[11] = {
        2.5f, 2.5f, 3.0f, 2.0f, 2.0f, 3.5f, 3.0f, 2.0f, 2.5f, 10.0f, 5.0f,
    };
    const int idx = int(b);
    if (idx < 0 || idx >= int(sizeof(kW) / sizeof(kW[0]))) {
        return 2.0f;
    }
    return kW[idx];
}

inline float feature_sp_weight(FeatureType f) {
    switch (f) {
        case FT_Road:     return 1.0f;
        case FT_DirtRoad: return 1.5f;
        case FT_Field:    return 2.0f;  // ploughed ground walks like meadow
        default:          return 0.0f;
    }
}

// Undergrowth drag of a forest-CLASS cell (macro/tree_layer.h
// is_forest_cell) — the weight FT_Tree used to carry. A road cut through a
// forest keeps its road weight: man-made features win over natural cover.
inline constexpr float kForestSpWeight = 3.0f;

inline float cell_sp_weight(Biome b, FeatureType f, bool forest = false) {
    float fw = feature_sp_weight(f);
    if (fw > 0.0f) return fw;
    if (forest) return kForestSpWeight;
    return biome_sp_weight(b);
}

// THE cost formula, for both layers: (difficulty of the ground + the burden you
// carry) × how much of a cell was crossed. `cells` is 1.0 for a macro cell step
// and tiles/kCellSize underfoot in the subworld. The overload surcharge scales
// with distance like everything else — carrying too much is paid for by the
// step, not by the bookkeeping event that happens to charge it.
// `efficiency` is the traveller's own skill at covering ground
// (travel_skill_efficiency); it discounts the TERRAIN, not the burden — what you
// carry is governed by `weightlifting` through the carry capacity, and no amount
// of pathfinding makes an overloaded pack lighter.
inline float travel_stamina_cost(float weight, float cells,
                                 int overloadCost = 0,
                                 float efficiency = 1.0f) {
    if (cells <= 0.0f) return 0.0f;
    return (weight * kStaminaPerCell * efficiency + float(overloadCost)) * cells;
}

// Charge whole SP, and let the exhaustion curve take the rest out of HP.
// Returns the HP lost (0 while stamina lasts).
//
// The body keeps its debt: stamina is NOT floored at zero, so the state is
// visible in the UI and has to be recovered before the bar refills. What the
// curve above charges is that debt, once per step.
inline int apply_stamina_cost(CombatStats& cs, int cost) {
    if (cost <= 0) return 0;
    cs.currentSp -= cost;
    if (cs.currentSp >= 0) return 0;
    const int bite = int(std::lround(float(-cs.currentSp) * kExhaustionBite));
    cs.currentHp -= bite;
    return bite;
}

// The fractional carry between steps. Runtime only (never serialised): one per
// travelling body, shared by both layers because a body has one pair of legs.
struct TravelStamina {
    float pending = 0.0f;   // SP earned by the terrain but not yet whole
};

// Spend one step's worth (travel_stamina_cost above): accumulate it, charge
// whatever whole points have built up, and apply the exhaustion curve to them.
// Returns the SP charged.
inline int spend_travel_stamina(CombatStats& cs, TravelStamina& acc,
                                float cost) {
    if (cost > 0.0f) acc.pending += cost;
    if (acc.pending < 1.0f) return 0;
    const int whole = int(acc.pending);
    acc.pending -= float(whole);
    apply_stamina_cost(cs, whole);
    return whole;
}

} // namespace sm
