// The wall — what a place puts between itself and the land.
//
// One ring model serves every walled kind, because a wall is one idea with
// dials: a closed outline, stamped as tiles AND as oriented solids, OPENED
// where a road already crosses it, towered where it turns.
//
// Two laws this file exists to keep:
//
//   · A GATE IS WHERE THE ROAD IS. It used to be where the compass said: a
//     fixed-width corridor along the pure bearing of a road-bearing neighbour,
//     cut from the cell centre outward. But the road it was cut for does not
//     run down that bearing — it runs to a jittered edge anchor up to 153
//     tiles off-axis — so the ring got an empty arch over grass AND a second
//     opening where the real road crossed, often a few tiles apart. That is
//     the "two gates side by side" the owner saw. The corridor is gone: the
//     caller carves its roads FIRST, and the ring opens exactly where it finds
//     paving under itself.
//
//   · THE RING IS DECIDED BEFORE ANYTHING IS BUILT ON IT. The wall used to be
//     stamped last, after houses and fields, and yielded to both — so a house
//     grown into an inward dip of the ring produced no wall tiles and no wall
//     solids there at all: a hole you could walk through. The outline is now
//     computed first and handed to every other placer (sub/gens/kit/outline.h),
//     so nothing is ever built where the wall is going to stand.
//
// The ring's noise amplitudes are NOT local: they live in sub/city_layout.h,
// because the citizen populator derives the ring's worst INWARD excursion from
// them to know what "inside the walls" means.
#pragma once
#include "sub/gens/kit/outline.h"
#include "sub/height.h"
#include "sub/map_data.h"

#include "core/rng.h"

namespace sm::sub::kit {

// THE GATE'S PROMISE: how much open air a gateway keeps under its lintel. A
// mounted body passes beneath it — kBodyEyeM is a walking man's eye, a rider
// sits about one body-eye higher again, and the arch clears his head, so three
// eye-heights is the span's clear rather than a number chosen to look right.
//
// Stated in the header because it is a PROMISE, not an implementation detail:
// the witness that a gate is passable on sloped ground asserts against this
// very number rather than restating one of its own (tests law 4).
inline constexpr float kGateClearM = kBodyEyeM * 3.0f;

// Perturb a shape into a BUILT ring: the two harmonics and the per-bearing
// jitter of kSettlementWallRing, then smoothed twice. The base may be a plain
// circle (a village keeps its core) or a grown, organic outline (a city's, see
// sub/gens/kit/growth.h) — the masonry's own irregularity rides on top of
// whatever shape the place actually took.
//
// Consumes `r` — hand it a stream of its own, or the wall's shape becomes a
// function of how many houses the caller happened to place first.
Outline wall_ring_noise(const Outline& base, float roughness, Rng& r);

// The circle case, spelled out: a ring of this nominal radius about (cx, cy).
Outline wall_outline(float cx, float cy, float radius, float roughness, Rng& r);

struct WallStyle {
    float height;    // curtain height, metres — courses of the wall module
    bool  towers;    // round towers where the ring turns
};

// An opening the ring left for a road, reported back so the caller can route
// to it (a track from an outlying field belongs at a gate, not at an arbitrary
// cardinal point outside the wall).
struct WallGate {
    float x, y;      // midpoint of the opening
    float angle;     // its bearing from the heart
};

// Raise the wall. Returns how many gates it cut, writing up to `maxGates` of
// them; openings beyond that are still cut, just not reported.
//
// `clip`, when given, is a boundary this ring may not cross: samples outside it
// are not built at all. That is how an inner enclosure SHARES a side with the
// curtain instead of running its own wall alongside it — the upper quarter is
// backed into the city wall, so the arc that would have stood outside the town
// is simply the city wall, and stamping it again gave the doubled wall the
// owner photographed (2026-09-13).
int stamp_wall(SubworldMapData& out, const Outline& outline,
               const WallStyle& style, WallGate* gates, int maxGates,
               const Outline* clip = nullptr);

// Settle every LIFTED span onto the ground it actually bridges.
//
// A lifted solid states its clear as a height above ONE terrain sample — the
// one under its own centre (map_data.h). That is the right seat only when the
// ground under the whole span is that height. It never is on a hillside, and a
// gate lintel is the span this matters to: promise five metres of air over the
// road, measure them from the middle of a slope, and the uphill half of the
// arch is inside the hill.
//
// It is a separate pass, and deliberately so: the ground a gateway stands on
// is STILL BEING CUT when the wall goes up — every generator's roads are
// smoothed into the relief afterwards (gens/dispatch.cpp smooth_road_heights)
// — so a seat computed at stamp time is measured against a hill that no longer
// exists. Run this once the map is final and there is exactly one place that
// knows the law.
//
// Today the overworld's only lifted-on-ground spans are gate lintels; bridge
// decks, piers and kerbs state absolute world heights (zWorld) and are left
// alone, because a span over water answers to the water.
void seat_lifted_spans(SubworldMapData& out);

} // namespace sm::sub::kit
