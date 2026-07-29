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

#include "core/torus.h"

#include <cmath>
#include <cstdio>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL torus_geometry_test: %s\n", msg);
    return 1;
}

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

} // namespace

int main() {
    using namespace sm;

    const int W = 100, H = 100;
    // cos thresholds: kRoadFanCosThreshold in politik.cpp is 0.90 (~26°).
    const float kFan = 0.90f;

    // ── torus_bearing: unit direction, shortest wrapped delta ──────────────
    {
        float ux, uy;
        if (!torus_bearing(10, 10, 20, 10, W, H, ux, uy)) return fail("east bearing must exist");
        if (!approx(ux, 1.0f) || !approx(uy, 0.0f)) return fail("east bearing must be (+1,0)");

        if (!torus_bearing(10, 10, 10, 20, W, H, ux, uy)) return fail("south bearing must exist");
        if (!approx(ux, 0.0f) || !approx(uy, 1.0f)) return fail("south bearing must be (0,+1)");

        // Perfect diagonal normalizes to (√½,√½).
        if (!torus_bearing(0, 0, 10, 10, W, H, ux, uy)) return fail("diagonal bearing must exist");
        if (!approx(ux, 0.70710678f) || !approx(uy, 0.70710678f))
            return fail("diagonal bearing must normalize");

        // Coincident points have no bearing.
        if (torus_bearing(5, 5, 5, 5, W, H, ux, uy)) return fail("coincident bearing must be false");
        if (!approx(ux, 0.0f) || !approx(uy, 0.0f)) return fail("coincident must zero the vector");

        // Wrap: from x=95 to x=2 on a width-100 torus is +7 east (short way),
        // NOT −93 west. Bearing must point east.
        if (!torus_bearing(95, 50, 2, 50, W, H, ux, uy)) return fail("wrap bearing must exist");
        if (!approx(ux, 1.0f) || !approx(uy, 0.0f))
            return fail("wrap-around bearing must take the short way (+east)");
    }

    // ── torus_bearings_parallel: the road-fan decision ─────────────────────
    {
        // DOUBLED DIAGONAL (the bug): A–B–C collinear east. A already links B;
        // the redundancy candidate C sits dead ahead → suppress.
        // A=(0,50) B=(10,50) C=(20,50).
        if (!torus_bearings_parallel(0, 50, 10, 50, 20, 50, W, H, kFan))
            return fail("collinear A-B-C bypass must be flagged parallel (suppressed)");

        // A shallow fan just under the angle threshold is still a duplicate.
        // A=(0,50) existing→B=(20,50) (due east); candidate C=(20,53): tiny
        // downward tilt, dot ≈ 0.989 > 0.90 → suppress.
        if (!torus_bearings_parallel(0, 50, 20, 50, 20, 53, W, H, kFan))
            return fail("shallow fan under threshold must be flagged parallel");

        // PERPENDICULAR redundancy is a genuine alternate route → keep.
        // A=(50,50) existing→B east; candidate D south.
        if (torus_bearings_parallel(50, 50, 60, 50, 50, 60, W, H, kFan))
            return fail("perpendicular redundancy edge must NOT be flagged");

        // OPPOSITE bearing (road going the other way) is a real loop → keep.
        // A=(50,50) existing→B east(+x); candidate E west(−x). dot=−1.
        if (torus_bearings_parallel(50, 50, 60, 50, 40, 50, W, H, kFan))
            return fail("opposite-direction edge must NOT be flagged (real alternate)");

        // 45° separation (dot ≈ 0.707 < 0.90) is a wide, useful loop → keep.
        // A=(0,0) existing→B east; candidate C diagonal SE.
        if (torus_bearings_parallel(0, 0, 10, 0, 10, 10, W, H, kFan))
            return fail("45-degree fan must survive (below cos threshold)");

        // Coincident candidate / existing → no bearing → never parallel.
        if (torus_bearings_parallel(5, 5, 5, 5, 20, 20, W, H, kFan))
            return fail("coincident existing edge must not be flagged parallel");
        if (torus_bearings_parallel(5, 5, 20, 20, 5, 5, W, H, kFan))
            return fail("coincident candidate edge must not be flagged parallel");

        // Wrap-aware: two roads that only look far apart across the seam are
        // still parallel. Existing→(2,50) and candidate→(6,50) both read as
        // east across the x=95 wrap → suppress the second.
        if (!torus_bearings_parallel(95, 50, 2, 50, 6, 50, W, H, kFan))
            return fail("wrap-around parallel fan must be flagged");
    }

    std::printf("OK torus_geometry_test: bearings normalize + wrap short-way; "
                "road-fan guard flags collinear/shallow fans, keeps "
                "perpendicular/opposite/45-degree loops (cos>%.2f)\n", double(kFan));
    return 0;
}
