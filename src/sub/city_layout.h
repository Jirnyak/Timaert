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

#include <cmath>
#include <cstdint>

namespace sm::sub {

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

    // ── Houses (count only; placement stays the road-gated scatter). ──
    float houseCountExp;    // houses ≈ pow(population, exp)
    int   houseCountMin;    // floor so even a tiny city has a hamlet's worth
    int   houseCountMax;    // ceiling so a metropolis stays within the cell

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
    /* houseCountExp      */ 0.80f,
    /* houseCountMin      */ 20,
    /* houseCountMax      */ 380,
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

// Target house count for a city of `population`. Not constexpr because std::pow
// is not portably constant-evaluable; the curve still lives here as the one
// definition the generator and tests share.
inline int city_house_target(int population) {
    const float p = float(population > 0 ? population : 0);
    int h = int(std::pow(p, kCityLayout.houseCountExp));
    if (h < kCityLayout.houseCountMin) h = kCityLayout.houseCountMin;
    if (h > kCityLayout.houseCountMax) h = kCityLayout.houseCountMax;
    return h;
}

} // namespace sm::sub
