// Locks the torus bearing helpers (core/torus.h) that drive the macro-road
// "doubled diagonal" fix.
//
// The road network is an MST + one redundancy edge per city (politik.cpp).
// The redundancy edge used to fan out almost parallel to a road the city
// already had — three roughly-collinear cities A–B–C (MST links A–B, B–C)
// gained a bypass A→C that visually doubled the A–B–C corridor. The fix guards
// that pass with `torus_bearings_parallel`: a candidate whose bearing shadows
// an existing road is skipped. This test encodes exactly those road scenarios
// so the geometry can't silently drift.
//
// EVERY assertion here used to be spelled `if (!x) return fail("…")` inside
// main. Three things were wrong with that and none of them were visible:
//   * `fail()` handed check.h a HARDCODED "tests/torus_geometry_test.cpp" and
//     line 0, so a failure named the file and lied about the line;
//   * on the green path nothing was counted at all — the file's only counted
//     check was the un-failable `CHECK(true, "every gate above held")` at the
//     bottom, a ritual that exists solely to satisfy "zero checks is a failure";
//   * the FIRST broken fact ended the test, so a session fixing one of them
//     learned nothing about the other eighteen.
#include "check.h"

#include "core/torus.h"

#include <cmath>
#include <cstdio>

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

constexpr int W = 100, H = 100;
// cos thresholds: kRoadFanCosThreshold in politik.cpp is 0.90 (~26°).
constexpr float kFan = 0.90f;

// ── torus_bearing: unit direction, shortest wrapped delta ──────────────────
void test_bearing_is_a_unit_direction() {
    using namespace sm;
    float ux = 0.0f, uy = 0.0f;

    CHECK_OR_RETURN(torus_bearing(10, 10, 20, 10, W, H, ux, uy),
                    "two distinct cells have a bearing between them (east)");
    CHECK(approx(ux, 1.0f) && approx(uy, 0.0f), "due east reads as (+1,0)");

    CHECK_OR_RETURN(torus_bearing(10, 10, 10, 20, W, H, ux, uy),
                    "two distinct cells have a bearing between them (south)");
    CHECK(approx(ux, 0.0f) && approx(uy, 1.0f), "due south reads as (0,+1)");

    // Perfect diagonal normalizes to (√½,√½).
    CHECK_OR_RETURN(torus_bearing(0, 0, 10, 10, W, H, ux, uy),
                    "a diagonal pair has a bearing");
    CHECK(approx(ux, 0.70710678f) && approx(uy, 0.70710678f),
          "the bearing is a UNIT vector — a perfect diagonal normalizes");
}

void test_coincident_cells_have_no_bearing() {
    using namespace sm;
    float ux = 1.0f, uy = 1.0f;   // seeded non-zero: the door must CLEAR them
    CHECK(!torus_bearing(5, 5, 5, 5, W, H, ux, uy),
          "a cell has no bearing to itself — the door says so");
    CHECK(approx(ux, 0.0f) && approx(uy, 0.0f),
          "and it zeroes the vector rather than leaving the caller's garbage");
}

// ЗАКОН АДРЕСА: мир связный тор, поэтому «далеко» через шов — это БЛИЗКО.
void test_bearing_takes_the_short_way_across_the_seam() {
    using namespace sm;
    float ux = 0.0f, uy = 0.0f;
    // From x=95 to x=2 on a width-100 torus is +7 east (the short way), NOT
    // −93 west. A world that is merely tiled would answer west here.
    CHECK_OR_RETURN(torus_bearing(95, 50, 2, 50, W, H, ux, uy),
                    "cells on opposite sides of the seam have a bearing");
    CHECK(approx(ux, 1.0f) && approx(uy, 0.0f),
          "across the seam the bearing takes the SHORT way (+east), not the "
          "long way round — the world is connected, not tiled");
}

// ── torus_bearings_parallel: the road-fan decision ─────────────────────────
void test_road_fan_suppresses_duplicates() {
    using namespace sm;
    // DOUBLED DIAGONAL (the bug): A–B–C collinear east. A already links B;
    // the redundancy candidate C sits dead ahead → suppress.
    CHECK(torus_bearings_parallel(0, 50, 10, 50, 20, 50, W, H, kFan),
          "a collinear A-B-C bypass is a duplicate road, and is suppressed");
    // A shallow fan just under the angle threshold is still a duplicate:
    // candidate (20,53) tilts a little, dot ≈ 0.989 > 0.90.
    CHECK(torus_bearings_parallel(0, 50, 20, 50, 20, 53, W, H, kFan),
          "a shallow fan inside the angle threshold is a duplicate too");
    // Wrap-aware: two roads that only LOOK far apart across the seam are still
    // parallel — the same connectedness the bearing test states above.
    CHECK(torus_bearings_parallel(95, 50, 2, 50, 6, 50, W, H, kFan),
          "duplicate roads are spotted ACROSS the seam, not only within a tile");
}

// The negative controls of the same door, and they are the half that matters:
// a guard that flags everything would pass every assertion above.
void test_road_fan_keeps_genuine_alternates() {
    using namespace sm;
    CHECK(!torus_bearings_parallel(50, 50, 60, 50, 50, 60, W, H, kFan),
          "a PERPENDICULAR redundancy edge is a genuine alternate route, kept");
    CHECK(!torus_bearings_parallel(50, 50, 60, 50, 40, 50, W, H, kFan),
          "a road going the OTHER way closes a real loop, kept");
    CHECK(!torus_bearings_parallel(0, 0, 10, 0, 10, 10, W, H, kFan),
          "a 45-degree fan (dot ~0.707, below the threshold) is a wide useful "
          "loop, kept");
    CHECK(!torus_bearings_parallel(5, 5, 5, 5, 20, 20, W, H, kFan),
          "an existing edge with no bearing is never 'parallel' to anything");
    CHECK(!torus_bearings_parallel(5, 5, 20, 20, 5, 5, W, H, kFan),
          "a candidate edge with no bearing is never 'parallel' either");
}

} // namespace

int main() {
    test_bearing_is_a_unit_direction();
    test_coincident_cells_have_no_bearing();
    test_bearing_takes_the_short_way_across_the_seam();
    test_road_fan_suppresses_duplicates();
    test_road_fan_keeps_genuine_alternates();
    return sm::test::report("torus_geometry_test");
}
