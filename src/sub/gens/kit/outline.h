// THE shape of a place — one radius per bearing, about one heart.
//
// Everything a settlement lays down has to agree about where it ENDS: the wall
// is raised on that boundary, the streets must stop short of it, the houses
// must stand inside it, the fields begin beyond it, the trees are cleared up to
// it. Until 2026-09-13 each of those read a different number — the wall used a
// noisy ring, the streets used `wallR - 14`, the houses used `wallR - 10`, the
// fields used `wallR * 1.08`, the tree scatter used `wallR + 16` — and all four
// were derived from the ring's MEAN radius while the built ring wandered ±8 %
// around it. So the ring dipped inward onto a house and emitted nothing there
// (a hole in the wall you could walk through), an avenue ran out through the
// masonry, and the trees stood inside a four-ring city.
//
// One outline fixes all of that by construction: whoever wants to know where
// the town ends asks the same object the wall was built from.
//
// Why a radius per bearing rather than a mask: a settlement is star-shaped
// about its heart — every point of it is reachable from the centre without
// leaving it — which is exactly what a medieval town IS, and what makes
// "inside" answerable in constant time by everyone who asks.
//
// THE KIT IS GEOMETRY AND NOTHING ELSE (owner's ruling, 2026-09-13). A ring's
// SHAPE is the same question whichever kind of wall is raised on it — masonry,
// a stockade, a ruin's stub — so it is shared; what is BUILT on that shape is
// each settlement module's own business and lives with that module.
#pragma once

#include "core/rng.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sm::sub::kit {

struct Outline {
    // 64 bearings, 5.625° apart. Power of two so the bearing of a point is a
    // shift rather than a division, and the same count the wall-profile test
    // bins azimuth into — a town's shape and the assertion about it are then
    // sampled at the same resolution.
    static constexpr int kBearings = 64;
    static constexpr float kTwoPi = 6.28318530718f;

    float cx = 0.0f, cy = 0.0f;
    std::array<float, kBearings> r{};

    // Bearing index (and the fraction into the next) of a direction.
    void bin_of(float angle, int& i0, int& i1, float& t) const {
        float a = angle / kTwoPi;
        a -= std::floor(a);                       // wrap to [0, 1)
        const float f = a * float(kBearings);
        i0 = int(f) % kBearings;
        i1 = (i0 + 1) % kBearings;
        t = f - std::floor(f);
    }

    // The radius at a bearing, linearly interpolated between bins.
    float at(float angle) const {
        int i0, i1; float t;
        bin_of(angle, i0, i1, t);
        return r[std::size_t(i0)] * (1.0f - t) + r[std::size_t(i1)] * t;
    }

    // Is this point inside the outline pulled in by `inset`? Negative inset
    // means "inside the outline pushed OUT" — how the fields ask.
    bool contains(float x, float y, float inset) const {
        const float dx = x - cx, dy = y - cy;
        const float d2 = dx * dx + dy * dy;
        const float bound = at(std::atan2(dy, dx)) - inset;
        if (bound <= 0.0f) return false;
        return d2 <= bound * bound;
    }

    float min_radius() const { return *std::min_element(r.begin(), r.end()); }
    float max_radius() const { return *std::max_element(r.begin(), r.end()); }

    // A plain disk — the shape a place has before anything has made it
    // interesting, and the only shape an unwalled hamlet ever gets.
    static Outline disk(float cx, float cy, float radius) {
        Outline o;
        o.cx = cx;
        o.cy = cy;
        o.r.fill(radius);
        return o;
    }

    // The same shape, larger or smaller. A town's older cores stood on the
    // same ground and were bent by the same hills, so an inner ring is this
    // ring scaled — not an independent circle drawn inside an organic town.
    Outline scaled(float factor) const {
        Outline o = *this;
        for (float& v : o.r) v *= factor;
        return o;
    }
};

// An opening a ring left for a road, reported back so the caller can route to
// it (a track from an outlying field belongs at a gate, not at an arbitrary
// cardinal point outside the wall). Geometry, not masonry: a city's arch and a
// village's timber frame are both THIS to everyone downstream.
struct WallGate {
    float x, y;      // midpoint of the opening
    float angle;     // its bearing from the heart
    float span;      // how wide it is, jamb to jamb — a gateway's own measure
                     // of itself, and therefore the measure of the room it
                     // needs on either side (a way through is at least as
                     // deep as it is wide).
};

// Perturb a shape into a BUILT ring: the two harmonics and the per-bearing
// jitter of kSettlementWallRing, then smoothed twice. The base may be a plain
// circle (a village keeps its core) or a grown, organic outline (a city's, see
// kit/growth.h) — the ring's own irregularity rides on top of whatever shape
// the place actually took.
//
// Consumes `r` — hand it a stream of its own, or the ring's shape becomes a
// function of how many houses the caller happened to place first.
Outline wall_ring_noise(const Outline& base, float roughness, Rng& r);

// The circle case, spelled out: a ring of this nominal radius about (cx, cy).
Outline wall_outline(float cx, float cy, float radius, float roughness, Rng& r);

} // namespace sm::sub::kit
