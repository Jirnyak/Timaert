// City street & house-distribution layout — data-driven tunables.
//
// The subworld city generator (sub/gens/dispatch.cpp `gen_city`) used to grow
// every interior street as a ray from the single cell centre, so road density —
// and therefore the road-gated house scatter — piled up in the middle and left a
// lopsided "one clump" city. Worse, when a city had no road-bearing neighbour
// the street axis defaulted to angle 0, so every street fanned due east.
//
// This header is the single source of truth for the radial-concentric street
// plan that replaces that starburst: a ring of radial AVENUES from the plaza to
// the rim (full 360°, guaranteeing centre-connectivity and even angular
// coverage), concentric RING ROADS tying the avenues together (block structure +
// circumferential density that counteracts the radial convergence), and short
// local STREETS fanning from every avenue×ring node so houses find frontage
// across the whole footprint.
//
// Same house idiom as seasons.h / base_generator.h BiomeConfig: one POD config +
// an inline accessor, plus pure population-response helpers so the growth curves
// live in ONE place and are unit-testable without the generator. Nothing here is
// serialized — layout is a pure function of population + cell seed — so it costs
// no kSaveVersion bump ("derive, don't store").
//
// There is one city profile today (a city's size is a continuous function of
// population, not a discrete kind), so this is a single config rather than an
// enum-indexed table; adding settlement tiers later is a table promotion, not a
// rewrite.
#pragma once

#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sm::sub {

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
// interior_household_share, CANON S28): one to three souls behind a door, and
// one more where the town is crowded rather than a hamlet. It is stated HERE
// because the GENERATOR needs the same number — a town has as many houses as
// its people have hearths, and that is the only honest answer to "how many
// houses". Two places, one law; the populator reads these same constants.
inline constexpr int kHearthSoulsMin     = 1;   // a door is never empty
inline constexpr int kHearthSoulsSpread  = 3;   // …+0..2 more
inline constexpr int kHearthCrowdedPop   = 128; // a town, not a hamlet…
inline constexpr int kHearthSoulsCrowded = 1;   // …squeezes in one more

// The most one hearth holds — which is what decides how many hearths a
// population needs.
inline constexpr int hearth_souls_max(int population) {
    return kHearthSoulsMin + (kHearthSoulsSpread - 1)
         + (population >= kHearthCrowdedPop ? kHearthSoulsCrowded : 0);
}

// What one house occupies of the town's ground: its own plot (a burgage's
// narrow street face by its depth) plus its share of the lane in front and the
// yard behind. Roughly a plot's face × three times its depth.
inline constexpr float kTownGroundPerHouse = 54.0f;

// Radius of a city's outer wall, tiles from the cell centre. Integer because
// the generator stamps the ring on an integer radius.
inline int city_wall_radius(int population) {
    const SettlementFootprint& F = kSettlementFootprint;
    const float p = float(std::max(F.cityPopFloor, population));
    return int(std::min(F.cityMax,
        std::max(F.cityMin, F.cityBase + std::sqrt(p) * F.cityPerSqrtPop)));
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

// The area a city of this population covers, tiles². The growth stops here,
// so the shape is free but the SIZE is not: a population always gets the room
// it needs and never more, however the ground lets it sprawl.
inline float city_target_area(int population) {
    const float r = float(city_wall_radius(population));
    return 3.14159265f * r * r;
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
    const int hearths = p / hearth_souls_max(p);
    const int room = int(village_target_area(p) / kTownGroundPerHouse);
    return std::min(hearths, room);
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

inline constexpr float city_wall_roughness(int ring) {
    return kSettlementWallRing.cityRoughness
         + float(ring) * kSettlementWallRing.cityRoughnessPerRing;
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

// Concentric wall rings of a city. A big city keeps its older cores as inner
// walls; the OUTERMOST ring is the one that encloses the town.
inline constexpr int city_wall_rings(int population) {
    return 1 + (population >= 2000  ? 1 : 0)
             + (population >= 5000  ? 1 : 0)
             + (population >= 10000 ? 1 : 0)
             + (population >= 20000 ? 1 : 0);
}

// Nominal radius of city wall ring `ring` (0 = innermost).
inline float city_ring_wall_radius(int population, int ring) {
    const int rings = city_wall_rings(std::max(kSettlementFootprint.cityPopFloor,
                                               population));
    const int r = std::clamp(ring, 0, rings - 1);
    const float fraction = float(r + 1) / float(rings);
    return float(city_wall_radius(population)) * fraction + float(r) * 8.0f;
}

// THE query the citizen spawner asks: within what radius of the cell centre are
// this settlement's inhabitants born? The built-up disk, clipped to what the
// enclosing wall actually guarantees — never the whole macro cell. A hamlet with
// no wall is bounded by its house core alone.
inline float settlement_population_radius(bool city, int population) {
    if (city) {
        const int outer = city_wall_rings(
            std::max(kSettlementFootprint.cityPopFloor, population)) - 1;
        // The GUARANTEED part of the town, since this function answers with a
        // scalar and the town is no longer a disk. The outer ring is the core
        // scaled by that ring's own share of the footprint, then reduced by
        // the ring noise's worst inward excursion.
        //
        // The cost of answering conservatively is real and known: the lobes a
        // city grows down its tract hold houses and streets but no crowd, so
        // the rim reads quieter than the heart. Closing that means handing the
        // populator the town's actual outline instead of a number — the
        // generator already computes one (sub/gens/kit/outline.h) and the
        // subworld map is never serialized, so it can simply carry it. That is
        // the next increment, not a hidden debt.
        const float ringShare = city_ring_wall_radius(population, outer)
                              / std::max(1.0f, float(city_wall_radius(population)));
        return std::min(city_house_radius(population),
                        wall_inner_bound(city_core_radius(population) * ringShare,
                                         city_wall_roughness(outer)));
    }
    const float core = village_core_radius(population);
    if (!village_is_walled(population)) return core;
    return std::min(core, wall_inner_bound(village_wall_radius(population),
                                           kSettlementWallRing.villageRoughness));
}

struct CityLayout {
    // ── Radial avenues: plaza → rim, evenly spaced over the full circle. ──
    int   avenuesBase;      // radial avenues at low population
    int   avenuesPer;       // +1 avenue for every this-many population
    int   avenuesMax;       // cap on avenues (bounds the road work)

    // ── Concentric ring roads tying the avenues together. ──
    int   ringsBase;        // ring roads at low population
    int   ringsPer;         // +1 ring for every this-many population
    int   ringsMax;         // cap on rings
    float ringInnerFrac;    // innermost ring radius as a fraction of usableR
    float ringOuterFrac;    // outermost ring radius as a fraction of usableR

    // ── Local streets fanning from each avenue×ring node (house frontage). ──
    int   streetsPerNodeBase;// streets per node at low population
    int   streetsPerNodePer; // +1 street per node for every this-many population
    int   streetsPerNodeMax; // cap on streets per node
    float streetLenMin;      // shortest local street, tiles
    float streetLenMax;      // longest local street, tiles

    // ── Houses ──
    // Nothing. The COUNT is derived from the hearth law (city_house_target
    // below): a town has as many houses as its people have hearths, bounded
    // only by the ground it stands on. There is no floor and no ceiling to
    // configure — a bracket like the old [20, 380] is not a model of anything,
    // and the 380 made every city above 1 750 souls identical.

    // ── Geometry. ──
    float streetWallInset;  // usableR = wallR − inset; keeps streets inside walls
};

// The one canonical city profile. Tuned so a mid-size city (~6k pop) lays a
// half-dozen-plus avenues and a few ring roads blanketing the disk, with local
// frontage streets so the road-gated house scatter spreads across the whole
// footprint instead of clumping downtown — while still leaving clear blocks for
// the houses themselves.
inline constexpr CityLayout kCityLayout = {
    /* avenuesBase        */ 6,
    /* avenuesPer         */ 1200,
    /* avenuesMax         */ 14,
    /* ringsBase          */ 2,
    /* ringsPer           */ 4000,
    /* ringsMax           */ 5,
    /* ringInnerFrac      */ 0.30f,
    /* ringOuterFrac      */ 0.92f,
    /* streetsPerNodeBase */ 1,
    /* streetsPerNodePer  */ 6000,
    /* streetsPerNodeMax  */ 3,
    /* streetLenMin       */ 22.0f,
    /* streetLenMax       */ 54.0f,
    /* streetWallInset    */ 14.0f,
};

inline constexpr const CityLayout& city_layout() { return kCityLayout; }

// ── Pure population-response curves (single source of truth, testable). ──

// Number of radial avenues for a city of `population`. Always even-spaced over
// the full circle, so a city is symmetric in every direction regardless of which
// neighbours carry roads (the old code fanned everything east when none did).
inline constexpr int city_avenues(int population) {
    int a = kCityLayout.avenuesBase
          + (population > 0 ? population / kCityLayout.avenuesPer : 0);
    if (a < kCityLayout.avenuesBase) a = kCityLayout.avenuesBase;
    if (a > kCityLayout.avenuesMax) a = kCityLayout.avenuesMax;
    return a;
}

// Number of concentric ring roads for a city of `population`.
inline constexpr int city_rings(int population) {
    int r = kCityLayout.ringsBase
          + (population > 0 ? population / kCityLayout.ringsPer : 0);
    if (r < 1) r = 1;
    if (r > kCityLayout.ringsMax) r = kCityLayout.ringsMax;
    return r;
}

// Radius of ring `ring` (0 = innermost) as an absolute tile distance, given the
// usable radius (wallR − inset). Rings are spread from ringInnerFrac to
// ringOuterFrac of usableR.
inline float city_ring_radius(int ring, int rings, float usableR) {
    if (rings <= 1) {
        return usableR * 0.5f * (kCityLayout.ringInnerFrac + kCityLayout.ringOuterFrac);
    }
    if (ring < 0) ring = 0;
    if (ring > rings - 1) ring = rings - 1;
    const float t = float(ring) / float(rings - 1);
    return usableR * (kCityLayout.ringInnerFrac
        + (kCityLayout.ringOuterFrac - kCityLayout.ringInnerFrac) * t);
}

// Local streets fanning from each avenue×ring node for a city of `population`.
inline constexpr int city_streets_per_node(int population) {
    int s = kCityLayout.streetsPerNodeBase
          + (population > 0 ? population / kCityLayout.streetsPerNodePer : 0);
    if (s < kCityLayout.streetsPerNodeBase) s = kCityLayout.streetsPerNodeBase;
    if (s > kCityLayout.streetsPerNodeMax) s = kCityLayout.streetsPerNodeMax;
    return s;
}

// Target house count for a city of `population`: EVERY SOUL HAS A HEARTH.
//
// What this replaces: pow(population, 0.80) clamped to 380. The exponent came
// from nowhere, and the cap bound at 1 750 souls — so every city in the world
// above that size, market town and capital alike, had exactly 380 houses. A
// number that stops distinguishing the things it is about has stopped being a
// model of them.
//
// The ceiling that remains is the GROUND's, not a design knob: a town cannot
// hold more houses than its own footprint has room for.
inline int city_house_target(int population) {
    const int p = population > 0 ? population : 0;
    const int hearths = p / hearth_souls_max(p);
    // The only bound is the GROUND's: a town cannot raise more houses than its
    // own footprint has room for. That is a fact about the place, not a knob.
    const int room = int(city_target_area(p) / kTownGroundPerHouse);
    return std::min(hearths, room);
}

} // namespace sm::sub
