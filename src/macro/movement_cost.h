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
// ONE — the owner's word (2026-08-24): the base is a pure level-1 number,
// balanced as data. 1 SP × the cell's weight × cells crossed; every modifier
// (travel skill, overload, terrain √) is a multiplier ON TOP, never folded
// in. A fresh level-1 walker (~110 SP) drops after ~55 meadow cells — about
// ten game hours of open country, a full day's march with an ache — and a
// night's rest (kSpRegenPctPerHour) buys it all back: the daily rhythm
// closes itself.
constexpr float kStaminaPerCell = 1.0f;

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
    return skill_mult(s, SkillId::Travel);
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
// EIGHT — the owner's word (2026-08-24, «степени двойки!»): a brisk paved
// pace at the world's own scale (cell ≈ 1 km, S1): 8 km/h on the road bed,
// /√weight elsewhere — meadow ~5.7, thicket and mountain ~3. A day's march
// lands at 30–60 km, which is what a day's march IS. The old 32 was a
// courier's gallop miscalled walking: the player crossed 125 km before the
// morning ended, and every distance in the world meant nothing.
constexpr float kMacroWalkCellsPerHour = 8.0f;

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

// ── THE step-cost law: BED + CONTRIBUTIONS (CANON S6/S7, 2026-08-24) ──────
//
// The optics idiom (macro/optics.h — the canon's exemplar table): an
// engineered FEATURE lays the bed — road 1.0 (the reference: speed =
// base/√weight, so the road IS the base march), dirt 1.5, ploughed field
// 1.8 — and where nothing is built the biome's own ground is the bed.
// CONTINUOUS contributions then ADD on top:
//
// · canopy — kCanopySpWeight × tree density (count / kMaxTreesPerCell). The
//   boolean forest-class cliff is gone: thickening woods slow the march
//   smoothly, exactly as they dim the light (optics kCanopyOpticalCost). An
//   engineered bed gates the canopy off — a road through the wood is a CUT
//   (просека), the trees stand beside it, not on it.
// · climb — kClimbSpWeight × the UPHILL height difference of the edge being
//   walked (downhill is free), priced where the step happens because a slope
//   is a fact of an EDGE, not of a cell. It makes the cost directional —
//   both A*s and the greedy squad step price it at expansion — and it obeys
//   every bed: a mountain road is honestly dearer than a valley road.
//
// It replaced a PRIORITY ladder (feature OVERRODE forest OVERRODE biome), in
// which a contribution like weather had no place to stand: under a sum, a
// new world system is one more term (S6), zero when silent.
inline float biome_sp_weight(Biome b) {
    // The bed of unimproved ground, by biome id (Tundra..Water, Mountain).
    // Recalibrated 2026-08-24 with the sum law (owner: заново, not parity):
    // open walking country 2.0, hard country 2.5–3.0, bog 4.0, the mountain
    // ground itself 5.0 (its WALL is the climb term now, not the byte),
    // water 10.0 — unpayable on foot, the ocean still drowns a lord.
    // Each row carries its own enum as a COLUMN, so a grown Biome refuses to
    // compile instead of quietly walking on a neighbour's ground.
    struct BiomeBedRow { Biome biome; float weight; };
    static constexpr BiomeBedRow kW[std::size_t(Mountain) + 1] = {
        {Tundra,  2.5f}, {Taiga,   2.5f}, {Snow,     3.0f}, {Valley, 2.0f},
        {Meadow,  2.0f}, {Swamp,   4.0f}, {Desert,   3.0f}, {Steppe, 2.0f},
        {Tropics, 2.5f}, {Water,  10.0f}, {Mountain, 5.0f},
    };
    static_assert(rows_in_enum_order(kW, &BiomeBedRow::biome),
                  "the biome bed table must mirror Biome");
    const std::size_t idx = std::size_t(b);
    return idx < std::size(kW) ? kW[idx].weight : 2.0f;
}

// The engineered beds — THE registry's column (macro/features.h kFeatureDefs;
// this was a switch with a silent 0.0 default until 2026-08-29). 0 = nothing
// built here — the biome ground is the bed (the silent zero of the law, not a
// sentinel to branch on).
inline float feature_bed_weight(FeatureType f) {
    return feature_def(f).bedWeight;
}

// Full-thicket drag: at density 1.0 (kMaxTreesPerCell) the wood adds 2.5 on
// top of its ground — a meadow choked to full forest walks at 4.5, the old
// forest-class 3.0 sits near density ~0.4, which is what a typical massif
// interior actually carries.
inline constexpr float kCanopySpWeight = 2.5f;

// Climbing surcharge per full normalized height (h01 = height byte / 255):
// an ascent over the WHOLE world relief costs as much again as ten cells of
// open meadow (20 = 10 × meadow 2.0) — spread over however many cells the
// approach takes, and refunded by nothing on the way down.
inline constexpr float kClimbSpWeight = 20.0f;

// The CELL half of the law — bed + canopy. The climb half lives on the edge
// and is priced by the walker (A*, the greedy step, the player's charge):
//   edge cost = cell_sp_weight(to) × step + kClimbSpWeight × max(0, Δh01).
inline float cell_sp_weight(Biome b, FeatureType f, float treeDensity01 = 0.0f) {
    const float bed = feature_bed_weight(f);
    if (bed > 0.0f) return bed;             // an engineered bed is a CUT
    return biome_sp_weight(b) + kCanopySpWeight
               * (treeDensity01 < 0.0f ? 0.0f
                  : treeDensity01 > 1.0f ? 1.0f : treeDensity01);
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

// THE bite, for a body of either scale (owner's ruling, 2026-08-27:
// «истощение — это когда SP кончилось, и тогда отнимается HP от ДВИЖЕНИЯ по
// миру; остановился — отдыхаешь»). What it takes for one step in debt, given
// the debt. Zero while stamina lasts, so it can be asked unconditionally.
//
// This used to be inlined in the player's charge and hand-copied in the macro
// AI's per-think settle, where it was also gated on WATER: a squad marching
// itself into the ground on dry meadow just made camp and paid nothing, while
// the player bled for the same step. One law, one line, both scales.
inline int exhaustion_bite(int sp) {
    if (sp >= 0) return 0;
    return int(std::lround(float(-sp) * kExhaustionBite));
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
    const int bite = exhaustion_bite(cs.currentSp);
    cs.currentHp -= bite;
    return bite;
}

// THE fractional stamina carry, settled — one shape for every body on the map.
// SIGNED and BIDIRECTIONAL: a march pushes it down, a rest pushes it up, and
// whole points move to the bar in whichever direction they accumulated.
// Truncation is toward zero, so a part-point never rounds into existence.
//
// The player used to carry TWO of these, both unsigned and each blind to the
// other: a spend-only `TravelStamina::pending` that refused to act below 1.0,
// and a separate regen-only accumulator in PlayerRecoveryAccumulator that
// zeroed itself at a full bar. A macro squad carried one signed `spCarry` and
// did the same job with half the parts. Same idea, three implementations, and
// the player's pair could not even represent the state his own bar was in —
// an exhaustion debt with a fractional part owed.
//
// The bar clamps at `maxSp` going up and NOT at zero going down: the debt is
// the state the exhaustion law bills (exhaustion_bite above), so it has to be
// expressible. Returns the whole points moved — negative when spent.
inline int settle_sp_carry(int& sp, int maxSp, float& carry) {
    const int whole = int(carry);
    if (whole == 0) return 0;
    carry -= float(whole);
    sp = std::min(std::max(1, maxSp), sp + whole);
    return whole;
}

// Spend one step's worth (travel_stamina_cost above) through that carry, and
// let the exhaustion curve bill the body for the step it could not pay for.
// Returns the SP actually charged (0 while the cost is still fractional).
inline int spend_travel_stamina(CombatStats& cs, float& carry, float cost) {
    if (cost > 0.0f) carry -= cost;
    const int moved = settle_sp_carry(cs.currentSp, cs.maxSp, carry);
    if (moved >= 0) return 0;
    cs.currentHp -= exhaustion_bite(cs.currentSp);
    return -moved;
}

} // namespace sm
