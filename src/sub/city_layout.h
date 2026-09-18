// City street & house-distribution layout — data-driven tunables.
//
// WHAT THIS HEADER IS TODAY: the size laws of a town — how much GROUND its
// houses need (kTownGroundPerHouse and the plot it is spelled out of), how
// big its market must be for the sellers it holds, how many hearths its
// people keep, where its wall stands and how far inside it the building line
// falls. Pure functions of population, unit-testable without the generator,
// nothing serialized (layout is a pure function of population + cell seed, so
// it costs no kSaveVersion bump — "derive, don't store").
//
// WHAT IT IS NOT, any more: the street PLAN. This file used to open by
// declaring itself «the single source of truth for the radial-concentric
// street plan» — avenues from the plaza to the rim, concentric ring roads,
// local streets fanning from every avenue×ring node — and it carried the
// four curves that sized them. That plan has not drawn a street since the
// lane kit landed: a town's streets GROW (sub/gens/kit/lanes.h — branching
// from the tract and the market, dying on ground another lane already
// serves), and its shape follows the price of land. The four functions
// (city_avenues / city_rings / city_ring_radius / city_streets_per_node) and
// the eleven CityLayout knobs they read had ZERO consumers on 2026-09-18 and
// were deleted; what survived the sweep is what the generator actually asks
// for — streetWallInset and streetLenMin.
//
// The cost of leaving them was not the hundred lines. A header that CALLS
// ITSELF the source of truth for a plan the game does not draw is a map of a
// road that is not there: the next reader tunes `avenuesPer`, sees nothing
// move, and goes looking for the bug in the generator.
//
// There is one city profile today (a city's size is a continuous function of
// population, not a discrete kind), so the remaining geometry is a single
// config rather than an enum-indexed table; adding settlement tiers later is
// a table promotion, not a rewrite.
#pragma once

#include "sub/map_data.h"
#include "sub/sky.h"       // time_of_day01 — the one clock the sun reads

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sm::sub {

// ── THE DAY'S PUMP: how a town's people move between street and hearth ────
//
// A settlement's population is a CONSERVED quantity that lives in two vessels.
// Nothing is created at dawn and nothing dies at dusk: the same souls move,
// and what moves them is the light. So there is one number here, it is a pure
// function of the world clock, and everything else follows from it.
//
// It is the SUN ITSELF, at full swing. `sun_dir(tod).y` is already the height
// of the sun over the horizon — +1 at noon, 0 at sunrise and at sunset, −1 at
// midnight — so mapping that range onto [0, 1] costs no invented number at
// all, and the curve is continuous across every minute of the day.
//
// What it deliberately is NOT: a clamp. `max(0, sunY)` was the first cut and
// it is a ceiling wearing a floor's clothes — it pins the value flat for half
// the day, so from dusk to dawn there is no gradient left to pump anything
// with, and the town changes by a switch instead of by degrees.
//
// Seasons come free the day the sun's arc learns about them, because this
// reads the arc rather than the hour.
inline float crowd_outdoor_share01(const WorldTime& t) {
    return (sun_dir(time_of_day01(t)).y + 1.0f) * 0.5f;
}

// How many of a hearth's `souls` are BEHIND THE DOOR at this hour.
//
// The arithmetic is `souls × (1 − share)`, and the interesting part is what
// happens to the fraction. Rounded, every house in a town flips at the same
// minute and a city of fourteen hundred doors empties like a switch thrown.
// So the fraction is resolved by the DOOR'S OWN PHASE — a constant drawn from
// its seed, uniform in [0, 1) — which is dithering, the same trick the ground
// uses to resolve a fractional shade without banding.
//
// What that buys, and why it is the whole point:
//   · the expectation over doors is exactly `souls × (1 − share)`, so the
//     partition law is not approximated — it is exact in the aggregate;
//   · every house has its OWN threshold, so one household is up at first light
//     and its neighbour sits until dark. A town's windows light one by one;
//   · it is deterministic and cheap — one multiply, one add, one floor. There
//     is no loop over people anywhere: the law reads a COUNT, as it should
//     with thousands of them.
inline int hearth_indoors_now(int souls, std::uint32_t doorSeed,
                              const WorldTime& t) {
    if (souls <= 0) return 0;
    // The door's phase: 24 bits of its seed as a fraction of one. Taken from
    // the high end because the low bits of a hash are the ones a stepping
    // ordinal correlates through.
    const float phase = float((doorSeed >> 8) & 0xFFFFFFu) / 16777216.0f;
    const float indoors = float(souls) * (1.0f - crowd_outdoor_share01(t))
                        + phase;
    return std::clamp(int(indoors), 0, souls);
}

// ── Settlement footprint — where the town physically IS ────────────────────
//
// The wall / core radius used to live as two magic-number expressions buried
// inside gen_city and gen_village, invisible to every other system. That is
// exactly how the citizen spawner (sub/spawn.cpp) came to scatter a town's
// whole population uniformly over the 1024×1024 macro cell: a 200-soul city
// walls a disk of ~4 % of that cell, a 60-soul village ~1 %, so 92–99 % of a
// settlement's people were born in the wilderness outside their own gates and
// the streets stood empty.
//
// The footprint now has ONE definition, read by the generator that stamps the
// walls AND by the populator that fills them, so the two cannot drift apart.
// (Same lesson as body_radius(): "keep these in lockstep" is a hope, not a
// mechanism.) Pure functions of population — nothing serialized, same
// "derive, don't store" rule as the rest of this header.
struct SettlementFootprint {
    // City wall: clamp(base + perSqrtPop·√pop, min, max), pop floored below.
    float cityBase;
    float cityPerSqrtPop;
    float cityMin;
    float cityMax;
    int   cityPopFloor;      // gen_city treats anything smaller as this
    float cityHouseInset;    // houses — and people — keep this far off the wall

    // Village core: min(maxCellFrac · kCellSize, base + perSqrtPop·√pop).
    float villageBase;
    float villagePerSqrtPop;
    float villageMaxCellFrac;
    int   villagePopFloor;
    int   villageWallPop;    // a village at least this big gets a wall
    float villageWallPad;    // …that sits this far outside the core…
    float villageWallMin;    // …and never closer than this to the centre
};

inline constexpr SettlementFootprint kSettlementFootprint = {
    /* cityBase           */ 70.0f,
    /* cityPerSqrtPop     */ 3.0f,
    /* cityMin            */ 90.0f,
    /* cityMax            */ 360.0f,
    /* cityPopFloor       */ 50,
    /* cityHouseInset     */ 10.0f,
    /* villageBase        */ 30.0f,
    /* villagePerSqrtPop  */ 3.0f,
    /* villageMaxCellFrac */ 0.07f,
    /* villagePopFloor    */ 10,
    /* villageWallPop     */ 50,
    /* villageWallPad     */ 6.0f,
    /* villageWallMin     */ 15.0f,
};

// ── THE HEARTH: how many souls one door holds ─────────────────────────────
// This law was written inline in the interior populator (sub/spawn.cpp
// interior_household_share, CANON S28) and is stated HERE because the
// GENERATOR needs the same number: a town has as many houses as its people
// have hearths, and that is the only honest answer to "how many houses".
//
// THE NUMBER BELOW IS A MEAN, and that correction is load-bearing (owner,
// 2026-09-13). It used to be a CEILING: the house count divided the population
// by the largest a hearth could be, while the populator rolled each door
// somewhere between one soul and that ceiling — so the doors of a town held,
// between them, about five eighths of its people and the rest had no door at
// all. Invisible while everyone stood outside anyway; the day the sun started
// sending them home it showed as a city that stayed half full at midnight,
// because four hundred and fifty of its twelve hundred had nowhere to go.
//
// So the roll is now centred on this number instead of topped by it — a
// household is this many souls ON AVERAGE and may be half that or twice it.
// Which is also the honest medieval figure: four under one roof is a small
// household, not a full one.
inline constexpr int kHearthSoulsMean    = 3;   // a hall, its family, its help
inline constexpr int kHearthCrowdedPop   = 128; // a town, not a hamlet…
inline constexpr int kHearthSoulsCrowded = 1;   // …packs one more in per door

// The souls one hearth holds ON AVERAGE — which is what decides how many
// hearths a population needs, and what the per-door roll is centred on.
inline constexpr int hearth_souls_mean(int population) {
    return kHearthSoulsMean
         + (population >= kHearthCrowdedPop ? kHearthSoulsCrowded : 0);
}

// The widest a single household gets. The roll is uniform over
// [1, 2·mean − 1]: symmetric about the mean, so the doors of a town hold its
// people EXACTLY in expectation — which is the property the house count
// depends on and the old ceiling law could not give. A crowded town therefore
// runs one to seven behind a door, four being ordinary.
inline constexpr int hearth_souls_span(int population) {
    return 2 * hearth_souls_mean(population) - 1;
}


// Target house count for a city of `population`: EVERY SOUL HAS A HEARTH.
//
// What this replaces: pow(population, 0.80) clamped to 380. The exponent came
// from nowhere, and the cap bound at 1 750 souls — so every city in the world
// above that size, market town and capital alike, had exactly 380 houses. A
// number that stops distinguishing the things it is about has stopped being a
// model of them.
//
// There is no ceiling and no floor: the town's SIZE follows from this number
// (city_target_area below), so a place with few souls is simply a small place.
inline int city_house_target(int population) {
    const int p = population > 0 ? population : 0;
    return p / hearth_souls_mean(p);
}

// ── WHAT ONE HOUSE OCCUPIES OF THE TOWN ───────────────────────────────────
// A town is as big as the ground its houses need, and this is that ground. It
// is spelled out in its three parts because each is a fact about the plots the
// generator actually lays (sub/gens/kit/plots.h FrontagePlan):
//
//   · the HOUSE itself — its street face by its depth;
//   · its share of the LANE it fronts — the lane serves both its sides, so a
//     house pays for half the width across its own frontage;
//   · its YARD — a burgage plot runs back three times the house's own depth:
//     the garden, the pig, the woodpile and the midden.
//
// This closes the defect the owner photographed as "empty lots": the town's
// AREA came from `70 + 3·√population` (a formula from nowhere) while its
// HOUSES came from the hearth law, so their ratio — the density — was an
// accident that happened to look right at one population and nowhere else.
// Now there is one number and the area follows from it.
inline constexpr float kPlotFrontage  = 4.5f;   // mean of the frontage band
inline constexpr float kPlotDepth     = 6.0f;   // mean building depth
inline constexpr float kPlotLaneWidth = 4.0f;   // the lane it fronts
inline constexpr float kPlotYardDepth = kPlotDepth * 3.0f;

inline constexpr float kTownGroundPerHouse =
      kPlotFrontage * kPlotDepth                    // the house       ~27
    + kPlotFrontage * kPlotLaneWidth * 0.5f         // its half of the lane ~9
    + kPlotFrontage * kPlotYardDepth;               // its yard        ~81

// ── THE MARKET ────────────────────────────────────────────────────────────
// A market square is not decoration and not a 6×6 stamp: it is the room the
// town's sellers need on market day. One household in ten keeps a stall; a
// stall with the aisle in front of it takes eight tiles; and half the square
// again is the room for everyone else to move between them.
inline constexpr int   kHearthsPerStall = 10;
inline constexpr float kStallGround     = 8.0f;
inline constexpr float kMarketAisleShare = 1.5f;

inline float city_market_area(int hearths) {
    const float stalls = float(std::max(1, hearths / kHearthsPerStall));
    return stalls * kStallGround * kMarketAisleShare;
}

// The area a city of this population covers, tiles²: its houses' ground plus
// its market. The growth stops here, so the shape is free but the SIZE is not
// — a population always gets the room it needs and never more, however the
// ground lets it sprawl.
inline float city_target_area(int population) {
    const int houses = city_house_target(population);
    return float(houses) * kTownGroundPerHouse + city_market_area(houses);
}

// Radius of a city's wall, tiles from its heart — the circle of the area its
// houses and its market need. Integer because the generator stamps the ring on
// an integer radius. The ONE bound is the ground's: a town cannot outgrow the
// cell it stands in, and it must leave room for its own fields beyond the
// walls.
inline int city_wall_radius(int population) {
    constexpr float kPi = 3.14159265f;
    const float r = std::sqrt(city_target_area(population) / kPi);
    return int(std::min(float(kCellSize) * 0.30f, std::max(8.0f, r)));
}

// Radius within which a city's houses are stamped — the wall pulled in by the
// house inset, so a building (or a citizen) never sits in the masonry.
inline float city_house_radius(int population) {
    return std::max(1.0f,
        float(city_wall_radius(population)) - kSettlementFootprint.cityHouseInset);
}

// ── The CORE: the ground a city holds whatever the land says ──────────────
// A city's outline is no longer a circle. It is GROWN outward from this disk,
// taking the cheapest ground first — down its tract, along the flat, never
// into the marsh (sub/gens/kit/growth.h) — until it has the AREA a circle of
// `city_wall_radius` would have had. So the town is the same size as before
// and a completely different shape, long where the road runs and pinched
// where the hill is.
//
// That leaves one thing everyone else needs to know: how much of the town is
// guaranteed, independent of terrain. This is it, and the factor is not a
// taste: 1/√2 is the radius of the disk holding HALF the target area. Half is
// the honest split — the heart of a town is a real place that the ground does
// not get a vote on, and the other half is exactly the part the ground shapes.
//
// Everything that must be right without consulting the terrain — above all
// the citizen populator, which knows only a radius — reads this.
inline float city_core_radius(int population) {
    constexpr float kHalfAreaRadius = 0.70710678f;   // 1/√2
    return float(city_wall_radius(population)) * kHalfAreaRadius;
}

// How far a city may reach along its best bearing. A town that ran the whole
// cell would meet its own fields and its neighbour's seam; half again its
// nominal radius is as long as a place can be and still read as one town.
inline float city_max_radius(int population) {
    return std::min(float(kCellSize) * 0.42f,
                    float(city_wall_radius(population)) * 1.5f);
}

// Radius of a village's built-up core (the disk its houses line).
inline float village_core_radius(int population) {
    const SettlementFootprint& F = kSettlementFootprint;
    const float p = float(std::max(F.villagePopFloor, population));
    return std::min(float(kCellSize) * F.villageMaxCellFrac,
                    F.villageBase + std::sqrt(p) * F.villagePerSqrtPop);
}

// A village takes its ground the same way a city does — grown outward from a
// guaranteed core over the cheapest land, which is what makes a hamlet on a
// tract a RIBBON rather than a blob. Same three laws, same half-the-area
// split; only the scale differs.
inline float village_core_radius_guaranteed(int population) {
    constexpr float kHalfAreaRadius = 0.70710678f;   // 1/√2
    return village_core_radius(population) * kHalfAreaRadius;
}
inline float village_target_area(int population) {
    const float r = village_core_radius(population);
    return 3.14159265f * r * r;
}
inline float village_max_radius(int population) {
    return std::min(float(kCellSize) * 0.20f,
                    village_core_radius(population) * 1.5f);
}

// A village counts its houses by the same law a city does — every soul has a
// hearth — bounded by its own ground. The old `min(120, pop/5)` had both an
// undivined divisor and a cap that flattened every village above 600 souls.
inline int village_house_target(int population) {
    const int p = population > 0 ? population : 0;
    return p / hearth_souls_mean(p);
}

// Does a village of this size raise a wall at all? Hamlets do not.
inline bool village_is_walled(int population) {
    return std::max(kSettlementFootprint.villagePopFloor, population)
        >= kSettlementFootprint.villageWallPop;
}

// Radius of a village's wall ring (meaningful only when village_is_walled).
inline float village_wall_radius(int population) {
    return std::max(kSettlementFootprint.villageWallMin,
                    village_core_radius(population)
                        + kSettlementFootprint.villageWallPad);
}

// ── Wall ring noise model ──────────────────────────────────────────────────
// stamp_settlement_wall does not lay a circle: it perturbs the ring by two
// harmonics plus a per-segment jitter, all scaled by `roughness`, so the built
// wall wanders inside and outside its nominal radius by up to
// roughness·(harmonics + jitter). Those amplitudes live here because the
// populator needs them: "inside the walls" is only guaranteed inside the ring's
// WORST INWARD excursion, not inside its mean radius. Anyone reading only the
// mean would place citizens in the dips — outside their own town.
struct SettlementWallRing {
    float harmonic3Amp;         // 3rd harmonic, fraction of radius·roughness
    float harmonic5Amp;         // 5th harmonic, same units
    float jitterAmp;            // per-segment jitter, same units
    float halfThickness;        // masonry half-thickness, tiles
    float cityRoughness;        // roughness of a city's innermost ring
    float cityRoughnessPerRing; // …plus this per ring outward
    float villageRoughness;     // a village's single ring
};

inline constexpr SettlementWallRing kSettlementWallRing = {
    /* harmonic3Amp         */ 0.32f,
    /* harmonic5Amp         */ 0.18f,
    /* jitterAmp            */ 0.18f,
    /* halfThickness        */ 1.1f,
    /* cityRoughness        */ 0.12f,
    /* cityRoughnessPerRing */ 0.025f,
    /* villageRoughness     */ 0.12f,
};

inline constexpr float city_wall_roughness() {
    return kSettlementWallRing.cityRoughness;
}

// The innermost tile a ring of this nominal radius can ever reach: mean radius
// minus the worst inward excursion of the noise, minus half the masonry. A
// conservative bound — the generator's two smoothing passes only pull the
// extremes back in — and that is exactly what it is for.
inline float wall_inner_bound(float radius, float roughness) {
    const SettlementWallRing& W = kSettlementWallRing;
    const float worst = roughness
        * (W.harmonic3Amp + W.harmonic5Amp + W.jitterAmp);
    return std::max(1.0f, radius * (1.0f - worst) - W.halfThickness);
}

// ── THE UPPER QUARTER ─────────────────────────────────────────────────────
// Not a castle-sized ring stuck to the wall — that was the first cut, and the
// owner called it what it was (2026-09-13). The upper town is a COMPARTMENT:
// a walled district inside the city, with its own gates, its own streets and
// its own houses, holding the keep.
//
// Who lives there decides how big it is, and that number is already in the
// world: a place's GARRISON is `population >> garrisonShift` (macro/
// landmark_registry.h — «гарнизон = армия ландмарка», §42). The garrison and
// its lord are the upper town's population; its hearths follow by the same
// hearth law as the city's, and its ground by the same ground-per-house. So a
// quarter is a fraction of the city only as a CONSEQUENCE — never as a chosen
// share.
inline int city_upper_population(int population) {
    const LandmarkDef& def = landmark_def(LandmarkType::City);
    if (def.garrisonShift == 0xFF) return 0;          // a kind with no garrison
    const int p = population > 0 ? population : 0;
    return p >> def.garrisonShift;
}

inline int city_upper_houses(int population) {
    const int soldiers = city_upper_population(population);
    return soldiers / hearth_souls_mean(std::max(1, soldiers));
}

inline float city_upper_area(int population) {
    const int houses = city_upper_houses(population);
    return float(houses) * kTownGroundPerHouse;
}

inline float city_upper_radius(int population) {
    constexpr float kPi = 3.14159265f;
    return std::sqrt(std::max(1.0f, city_upper_area(population)) / kPi);
}

// ── ONE CURTAIN, and a CASTLE that is not a ring ──────────────────────────
// A city used to raise up to five concentric rings, one per population step,
// each at a fraction of the footprint radius. Concentric circles are not what
// a town is; they are a pattern. What real towns had were TWO DIFFERENT
// THINGS: one city curtain (moved outward as the town grew, the old one
// demolished or built into the houses), and a castle's own small enceinte —
// sized to what it encloses, not to a share of the city.
//
// So there is one curtain, and the UPPER QUARTER above is the other object.
// The owner's eye caught the pattern from the air (2026-09-13); the streets
// running "under" the inner rings were the same defect seen from the ground,
// because those rings were walls nothing was allowed to cross and nothing had
// asked permission to.



// THE query the citizen spawner asks: within what radius of the cell centre are
// this settlement's inhabitants born? The built-up disk, clipped to what the
// enclosing wall actually guarantees — never the whole macro cell. A hamlet with
// no wall is bounded by its house core alone.
inline float settlement_population_radius(bool city, int population) {
    if (city) {
        // The GUARANTEED part of the town, since this function answers with a
        // scalar and the town is no longer a disk: the core, reduced by the
        // ring noise's worst inward excursion.
        //
        // The cost of answering conservatively is real and known: the lobes a
        // city grows down its tract hold houses and streets but no crowd, so
        // the rim reads quieter than the heart. Closing that means handing the
        // populator the town's actual outline instead of a number — the
        // generator already computes one (sub/gens/kit/outline.h) and the
        // subworld map is never serialized, so it can simply carry it. That is
        // the next increment, not a hidden debt.
        return std::min(city_house_radius(population),
                        wall_inner_bound(city_core_radius(population),
                                         city_wall_roughness()));
    }
    const float core = village_core_radius(population);
    if (!village_is_walled(population)) return core;
    return std::min(core, wall_inner_bound(village_wall_radius(population),
                                           kSettlementWallRing.villageRoughness));
}

// The two numbers the LANE KIT asks this file for. Everything else that stood
// in this struct described the dead radial-concentric plan and was deleted
// 2026-09-18 with the four curves that read it (see the header note).
struct CityLayout {
    // The shortest local street, tiles — the lane kit's block scale
    // (kit/growth.cpp: how far a lane runs before it may branch again).
    float streetLenMin;
    // usableR = wallR − inset: how far inside the curtain the building line
    // falls, so a lane never grows into the masonry (gens/city.cpp, and its
    // half for the upper quarter's own ring).
    float streetWallInset;
};

// The one canonical city profile. There is no second one: a city's size is a
// continuous function of its population, not a discrete kind.
inline constexpr CityLayout kCityLayout = {
    /* streetLenMin       */ 22.0f,
    /* streetWallInset    */ 14.0f,
};

inline constexpr const CityLayout& city_layout() { return kCityLayout; }

} // namespace sm::sub
