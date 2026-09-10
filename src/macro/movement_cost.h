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
// uses (macro/recovery.cpp). Nothing is lost to rounding and nothing is
// stored in the save — a load starts the carry at zero, worth at most 1 SP.
#pragma once
#include <cmath>
#include "core/time.h"       // the ladder: kSubworldWalkTilesPerSecond derives from it
#include "ecs/pools.h"       // the bars this law spends and bites — one home
#include "macro/attributes.h"
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm {

// SP per weight-unit per macro cell. THE knob for how far a body can march:
// this number × the cell's weight × cells crossed, and every modifier (travel
// skill, overload, terrain √) is a multiplier ON TOP, never folded in — the
// owner's shape, 2026-08-24.
//
// TWO, and the two has a compile-time gate under it (kRoadHoursPerFreshBar,
// below the bed tables) because THIS NUMBER ALONE SAYS NOTHING. What is
// balanced is GAME HOURS of road, and the hours are the PRODUCT of this knob
// and the pace — a product with no name of its own to fail under. That is
// exactly how it drifted: the 2026-08-24 recalibration moved the pace
// 32 → 8 cells/h and this knob 7/16 → 1, the product fell 14 → 8 SP/h, and
// every quoted hour below silently grew by 1.75× while the arithmetic that
// derived them stayed in the comment. Measured in play 2026-09-09: a fresh bar
// bought 13.75 h of road — a day and a half — under a comment that said 7.9.
// The gate closes the CLASS, not the instance: move either knob and the build
// fails naming the design, instead of the balance failing in someone's hands.
//
// Priced per CELL, never per hour. Walking faster covers the same ground for
// the same stamina, which is why `travel` (distance) and `spd` (speed) never
// fight; the hours are only how the result READS.
//
// With the terrain speed law below folded in, a fresh level-1 bar (110 SP —
// attributes.h, the bare sheet) buys about
//
//     road 6.9 h · meadow 4.9 h · forest 3.2 h · mountain 3.1 h · water 2.2 h
//
// — a real day's march on the road, camp by nightfall, and a night's rest
// (kRestRegenPctPerHour) buys it all back: the daily rhythm closes itself.
//
// The number is chosen to sit ABOVE the standing regen, not on it: the road
// costs 16 SP/h against a fresh 13.75 SP/h at rest, so a stop-and-go march
// always loses ground and stamina stays a BUDGET. The 1.75 that would have
// restored the old hours exactly puts 16 → 14 against that same 13.75 — a
// knife-edge that two ranks of `marathon` flip into the free ride the owner
// already caught in play once ("SP не тратится вообще", when the road cost
// 6.4 SP/h against ~10 of regen). Under 2 that flip is EARNED, at marathon
// ~17: a road that pays for itself is what a travel-trained character is for.
// Stamina still does not recover while marching at all — a marching body
// simply never calls the rest law (structural since landing 4; the old
// kMarchRecoveryPct=0 knob had become a constant with no reader).
constexpr float kStaminaPerCell = 2.0f;

// (No kMarchRecoveryPct. «Марш не лечит НИЧЕГО» is not a rate of zero any
// more — it is the SHAPE of the callers: rest_pools has exactly two, the
// player's standing-in-camp branch (main.cpp) and the squads'
// `stopped && !moved` camp think (npc_ai.cpp), and legs in motion reach
// neither. A knob that could be set to 0.2 was a door back to the road
// healing for free.)

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
// The subworld's own walk (kSubworldWalkTilesPerSecond, below) stays in
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

// Tiles of subworld scene per macro cell. sub/map_data.h kCellSize asserts
// against THIS number, so the two scales cannot quietly disagree about how
// long a cell is (it lives here because the macro side may not include sub/).
constexpr float kSubworldTilesPerMacroCell = 1024.0f;

// Base on-foot speed in the SUBWORLD, in tiles per REAL second, before the
// character's own pace (DerivedBonuses::moveSpeedMult) and haste.
//
// DERIVED, NOT TUNED — and since 2026-09-03 derived IN CODE, because it was
// derived in a comment and the comment could not follow a knob: it is the
// same body walking the same world at the other scale, so crossing a cell's
// scene costs exactly the game minutes the macro march charges for that cell
// (the owner's parity anchor, «клетка ≈ клетка»). The arithmetic:
// kMacroWalkCellsPerHour × kSubworldTilesPerMacroCell tiles per game hour,
// over what a subworld game hour lasts in real seconds
// (kTicksPerDay/24/kTicksPerRealSecond × kSubworldTickDivisor). The TIME
// RUNG is therefore the one visual-pace knob: ÷16 gave 96 t/s (the racing
// the owner rejected), ÷64 gives 24 (canon-audit A8's «the map was
// galloping» stays honoured — the map's own 8 is untouched).
//
// It is the speed on the REFERENCE bed — the road (bed 1.0) — because the
// march it derives from is; every other ground divides it by √weight through
// terrain_speed_mult, which is the one law both scales walk by.
constexpr float kSubworldWalkTilesPerSecond =
    kMacroWalkCellsPerHour * kSubworldTilesPerMacroCell
    * float(kTicksPerRealSecond) * 24.0f
    / (float(kTicksPerDay) * float(kSubworldTickDivisor));
static_assert(kSubworldWalkTilesPerSecond == 24.0f,
              "the ÷64 rung reads as 24 tiles/s — a moved knob shows here");

// THE pace of a body, from what its row says it is against a walking man
// (army.h CombatTemplate::speedMarchMult). One scale for everything that
// moves in the subworld — man, beast and player alike.
inline constexpr float march_speed(float marchMult) {
    return kSubworldWalkTilesPerSecond * marchMult;
}

// A WALKING MAN — the row every human body is stated against, and the one the
// player wears (his own sheet's moveSpeedMult rides on top, exactly as a
// hasted guard's would). 1.0 by construction: the march IS a man walking.
inline constexpr float kHumanMarchMult = 1.0f;

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
inline constexpr float biome_sp_weight(Biome b) {
    // The bed of unimproved ground, by biome id (Tundra..Water, Mountain).
    // Recalibrated 2026-08-24 with the sum law (owner: заново, not parity):
    // open walking country 2.0, hard country 2.5–3.0, bog 4.0, the mountain
    // ground itself 5.0 (its WALL is the climb term now, not the byte),
    // water 10.0 — unpayable on foot, the ocean still drowns a lord.
    // Each row carries its own enum as a COLUMN, so a grown Biome refuses to
    // compile instead of quietly walking on a neighbour's ground.
    struct BiomeBedRow { Biome biome; float weight; };
    constexpr BiomeBedRow kW[std::size_t(Mountain) + 1] = {
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
inline constexpr float feature_bed_weight(FeatureType f) {
    return feature_def(f).bedWeight;
}

// ── THE anchor gate: where the hours and the per-cell price meet ──────────
//
// The economy is BALANCED in game hours and PRICED per cell, and until
// 2026-09-09 the arithmetic joining the two lived in a comment — which cannot
// follow a moved knob. It failed exactly that way (kStaminaPerCell's own
// epitaph): two knobs moved, their product fell 1.75×, and every test stayed
// green because every test DERIVED its expectation from the same constants it
// was meant to be guarding. A tautology guards nothing.
//
// So the design numbers are stated here as literals, and the derived ones are
// asserted against them. `kFreshBarSp` is the bare level-1 bar — 100 base plus
// the untouched sheet's END/WIL pair (attributes.h calculate_combat_stats);
// squad_travel_test pins that it still reads 110, so this literal cannot drift
// away from the sheet in silence either.
inline constexpr float kFreshBarSp = 110.0f;

// What that bar buys on the reference bed, in game hours. The one number the
// owner actually balances: "a day's march, camp by nightfall".
inline constexpr float kRoadHoursPerFreshBar =
    kFreshBarSp / (feature_bed_weight(FT_Road) * kStaminaPerCell
                   * kMacroWalkCellsPerHour);
static_assert(kRoadHoursPerFreshBar > 6.0f && kRoadHoursPerFreshBar < 9.0f,
              "a fresh bar must buy a DAY of road (6-9 game hours). Moving "
              "kStaminaPerCell or kMacroWalkCellsPerHour moves their PRODUCT, "
              "and this is the line that says so out loud");

// ...and the road has to stay dearer than standing still, or a stop-and-go
// march repays itself and the budget is an allowance again — the failure the
// owner caught in play ("SP не тратится вообще"). Marching earns nothing
// (a marching body never calls the rest law), so the comparison is
// march-hour against rest-hour.
inline constexpr float kRoadStaminaPerHour =
    feature_bed_weight(FT_Road) * kStaminaPerCell * kMacroWalkCellsPerHour;
static_assert(kRoadStaminaPerHour > kFreshBarSp * kRestRegenPctPerHour,
              "the road must outrun the rest it is measured against, with "
              "room for `marathon` to buy the free ride honestly");

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
// curve above charges is that debt, once per step. Operates on the body's
// own Pools block — the one home of every bar since landing 4.
inline int apply_stamina_cost(ecs::Pools& pools, int cost) {
    if (cost <= 0) return 0;
    pools.sp -= cost;
    const int bite = exhaustion_bite(pools.sp);
    pools.hp -= bite;
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

// Spend one step's worth (travel_stamina_cost above) through the body's OWN
// signed carry, and let the exhaustion curve bill the body for the step it
// could not pay for. Returns the SP actually charged (0 while the cost is
// still fractional). One bookkeeping for both scales since landing 4: the
// carry is Pools::spCarry — the player used to spend a CombatStats bar
// through a carry that lived on a different struct, while the macro AI spent
// the same shape through its own per-think settle.
inline int spend_travel_stamina(ecs::Pools& pools, float cost) {
    if (cost > 0.0f) pools.spCarry -= cost;
    const int moved = settle_sp_carry(pools.sp, pools.maxSp, pools.spCarry);
    if (moved >= 0) return 0;
    pools.hp -= exhaustion_bite(pools.sp);
    return -moved;
}

} // namespace sm
