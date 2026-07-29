// Locks the DATA CONTRACT of the celestial system (macro/celestial.h).
//
// Moons and constellations are a pure derivation of world time / authored data
// — no serialized state, no kSaveVersion bump. This test proves:
//   * moon phase is a pure, periodic function of the absolute day and wraps
//     cleanly for non-positive days (so no caller guards the argument);
//   * illumination is a valid [0,1] fraction, 0 at new moon and 1 at full,
//     and the two moons desync (their full days differ) so the sky varies;
//   * every constellation's edges reference valid star indices (no authored
//     graph points at a star that doesn't exist);
//   * star brightness / sizes stay in range.
// If any of these regress, the night sky silently breaks even though it still
// compiles, and the renderer reading these tables would draw garbage.

#include "macro/celestial.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL celestial_test: %s\n", msg);
    return 1;
}

bool in01(float v) { return v >= 0.0f && v <= 1.0f; }

} // namespace

int main() {
    using namespace sm;

    // ── Moons ────────────────────────────────────────────────────────────
    if (int(MoonId::Count) < 1) return fail("need at least one moon");

    for (int mi = 0; mi < int(MoonId::Count); ++mi) {
        const MoonId m = MoonId(mi);
        const MoonDef& d = moon_def(m);
        if (d.id != m) return fail("moon_def id mismatch");
        if (d.name == nullptr) return fail("moon missing name");
        if (d.cyclePeriodDays <= 0) return fail("moon cycle must be positive");
        if (d.baseSize <= 0.0f) return fail("moon baseSize must be positive");

        // Phase is in [0,1), pure, and periodic over the moon's own cycle.
        for (int day : {1, 2, 7, 14, 28, 100, 365}) {
            const float p = moon_phase01(m, day);
            if (!(p >= 0.0f && p < 1.0f)) return fail("phase out of [0,1)");
            if (moon_phase01(m, day) != moon_phase01(m, day))
                return fail("phase not pure");
            if (std::fabs(moon_phase01(m, day)
                          - moon_phase01(m, day + d.cyclePeriodDays)) > 1e-6f)
                return fail("phase not periodic over its cycle");
        }

        // Non-positive days must still resolve (callers never guard).
        if (!in01(moon_illumination01(m, 0))) return fail("day 0 illum out of range");
        if (!in01(moon_illumination01(m, -1))) return fail("day -1 illum out of range");
        if (!in01(moon_illumination01(m, 1 - 5 * d.cyclePeriodDays)))
            return fail("far-negative day illum out of range");

        // New moon (phase 0) is dark; the mid-cycle day is full (illum ~1).
        // Find the day whose phase is 0 and the day nearest phase 0.5.
        // Day (1 - phaseOffsetDays) has t==0 -> phase 0 -> new moon.
        const int newDay = 1 - d.phaseOffsetDays;
        if (moon_illumination01(m, newDay) > 1e-4f)
            return fail("new-moon day must be dark (illum ~0)");
        const int fullDay = newDay + d.cyclePeriodDays / 2;
        if (moon_illumination01(m, fullDay) < 0.95f)
            return fail("full-moon day must be bright (illum ~1)");

        // Waxing flag agrees with phase halves.
        if (moon_is_waxing(m, newDay + 1) != (moon_phase01(m, newDay + 1) < 0.5f))
            return fail("waxing flag disagrees with phase");
    }

    // Two moons must actually desync: with different periods/offsets there is
    // at least one day where their illuminations differ meaningfully.
    if (int(MoonId::Count) >= 2) {
        bool differ = false;
        for (int day = 1; day <= 400 && !differ; ++day) {
            if (std::fabs(moon_illumination01(MoonId::Pale, day)
                          - moon_illumination01(MoonId::Crimson, day)) > 0.25f)
                differ = true;
        }
        if (!differ) return fail("moons never desync — sky would look static");
    }

    // ── Constellations ──────────────────────────────────────────────────
    if (kConstellationCount < 1) return fail("need at least one constellation");

    for (int ci = 0; ci < kConstellationCount; ++ci) {
        const ConstellationDef& c = constellation_def(ci);
        if (c.name == nullptr) return fail("constellation missing name");
        if (c.starCount < 1) return fail("constellation has no stars");
        if (c.stars == nullptr) return fail("constellation stars null");

        for (int s = 0; s < c.starCount; ++s) {
            const StarDef& st = c.stars[s];
            if (st.name == nullptr) return fail("star missing name");
            if (!(st.az >= 0.0f && st.az < 360.0f)) return fail("star az out of [0,360)");
            if (!(st.el >= 0.0f && st.el <= 90.0f)) return fail("star el out of [0,90]");
            if (!in01(st.brightness)) return fail("star brightness out of [0,1]");
        }

        // Every edge must reference stars that exist — the core invariant a
        // hand-authored star-graph can violate.
        for (int e = 0; e < c.edgeCount; ++e) {
            const StarEdge& ed = c.edges[e];
            if (ed.a >= c.starCount || ed.b >= c.starCount)
                return fail("constellation edge references a nonexistent star");
            if (ed.a == ed.b) return fail("constellation edge is a self-loop");
        }
    }

    // Star-size seam sanity.
    if (!(kSkyStarSizeScale > 0.0f && kSkyStarSizeScale <= 1.0f))
        return fail("star size scale must be in (0,1]");
    if (!(kSkyStarSizeMin > 0.0f)) return fail("star size floor must be positive");

    std::printf("OK celestial_test: %d moons phase-pure+periodic (new dark, full "
                "bright, desynced), %d constellations with valid star-graph edges, "
                "star-size seam sane (scale=%.2f)\n",
                int(MoonId::Count), kConstellationCount, double(kSkyStarSizeScale));
    return 0;
}
