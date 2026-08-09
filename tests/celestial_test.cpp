// Locks the DATA CONTRACT of the celestial system (macro/celestial.h).
//
// Moons and constellations are a pure derivation of world time / authored data
// — no serialized state, no kSaveVersion bump. This test proves:
//   * moon phase is a deterministic, periodic function of the absolute day and
//     wraps cleanly for non-positive days (so no caller guards the argument);
//   * illumination is a valid [0,1] fraction, 0 at new moon and 1 at full,
//     and the two moons desync (their full days differ) so the sky varies;
//   * every constellation's edges reference valid star indices (no authored
//     graph points at a star that doesn't exist);
//   * star brightness / sizes stay in range.
//
// HONEST SCOPE, so nobody reads this file as more than it is: `celestial.h` is
// currently included by NO file in `src/` (audit.md III.25). The sky renderer
// draws one always-full moon and procedural stars, so these tables are authored
// data with no consumer yet. This test guards the data against rot; it does not
// prove anything reaches the screen.
#include "check.h"

#include "macro/celestial.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace {

bool in01(float v) { return v >= 0.0f && v <= 1.0f; }

void test_moons() {
    using namespace sm;
    CHECK_OR_RETURN(int(MoonId::Count) >= 1, "the sky has at least one moon");

    for (int mi = 0; mi < int(MoonId::Count); ++mi) {
        const MoonId m = MoonId(mi);
        const MoonDef& d = moon_def(m);
        CHECK(d.id == m, "moon_def(id) returns the row it was asked for");
        CHECK(d.name != nullptr, "every moon is named");
        CHECK(d.cyclePeriodDays > 0, "a moon's cycle is a positive number of days");
        CHECK(d.baseSize > 0.0f, "a moon has a positive drawn size");
        if (d.cyclePeriodDays <= 0) continue;   // the rest would divide by it

        // Phase is in [0,1) and periodic over the moon's own cycle.
        int samples = 0, outOfRange = 0, notPeriodic = 0;
        for (int day : {1, 2, 7, 14, 28, 100, 365}) {
            ++samples;
            const float p = moon_phase01(m, day);
            if (!(p >= 0.0f && p < 1.0f)) ++outOfRange;
            if (std::fabs(p - moon_phase01(m, day + d.cyclePeriodDays)) > 1e-6f)
                ++notPeriodic;
        }
        CHECK(samples > 0 && outOfRange == 0,
              "moon phase is a fraction of its cycle: always inside [0,1)");
        CHECK(samples > 0 && notPeriodic == 0,
              "moon phase repeats exactly one cycle later");

        // Determinism worth the name: the old check compared one call to
        // ANOTHER CALL WITH THE SAME ARGUMENTS on the same line, which can only
        // fail on NaN. Walking the days forward and then backward catches what
        // that could not — hidden state that remembers the previous query.
        std::vector<float> forward;
        for (int day = 1; day <= 40; ++day) forward.push_back(moon_phase01(m, day));
        int drifted = 0;
        for (int day = 40; day >= 1; --day) {
            if (forward[std::size_t(day - 1)] != moon_phase01(m, day)) ++drifted;
        }
        CHECK(forward.size() == 40u && drifted == 0,
              "the phase of a day does not depend on which days were asked before");

        // Non-positive days must still resolve (callers never guard).
        CHECK(in01(moon_illumination01(m, 0))
                  && in01(moon_illumination01(m, -1))
                  && in01(moon_illumination01(m, 1 - 5 * d.cyclePeriodDays)),
              "days at or before zero still yield a valid illumination");

        // Day (1 - phaseOffsetDays) has t == 0 -> phase 0 -> new moon.
        const int newDay = 1 - d.phaseOffsetDays;
        const int fullDay = newDay + d.cyclePeriodDays / 2;
        CHECK(moon_illumination01(m, newDay) <= 1e-4f,
              "the new moon is dark");
        CHECK(moon_illumination01(m, fullDay) >= 0.95f,
              "half a cycle later the same moon is full");
        CHECK(moon_is_waxing(m, newDay + 1)
                  == (moon_phase01(m, newDay + 1) < 0.5f),
              "waxing means the first half of the cycle, and nothing else");
    }
}

void test_moons_desync() {
    using namespace sm;
    if (int(MoonId::Count) < 2) return;   // nothing to desync against
    // Two moons with different periods must actually drift apart, or the night
    // sky is one moon drawn twice.
    int days = 0;
    bool differ = false;
    for (int day = 1; day <= 400 && !differ; ++day) {
        ++days;
        if (std::fabs(moon_illumination01(MoonId::Pale, day)
                      - moon_illumination01(MoonId::Crimson, day)) > 0.25f)
            differ = true;
    }
    CHECK(days > 0 && differ,
          "the two moons drift apart within a year: the sky is never static");
}

// The procedural orbit: position derives from the phase, so the classic
// "full moon is anti-solar / new moon hides in the sun's glare" facts must
// EMERGE from the math — they are no longer pinned by a -sunDir hardcode.
void test_moon_orbits() {
    using namespace sm;

    // Whole-day phase must be the float formula sampled at the integer — one
    // master formula, the delegation can never drift.
    int mismatches = 0, samples = 0;
    for (int mi = 0; mi < int(MoonId::Count); ++mi) {
        for (int day : {-3, 0, 1, 14, 141, 365}) {
            ++samples;
            if (moon_phase01(MoonId(mi), day)
                != moon_phase01f(MoonId(mi), float(day))) ++mismatches;
        }
    }
    CHECK(samples > 0 && mismatches == 0,
          "whole-day phase IS the continuous phase sampled at the integer");

    int notUnit = 0, poses = 0;
    for (int mi = 0; mi < int(MoonId::Count); ++mi) {
        for (int day : {1, 15, 141}) {
            for (float tod : {0.0f, 0.3f, 0.62f, 0.97f}) {
                ++poses;
                const SkyDir d = moon_dir(MoonId(mi), day, tod);
                const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (std::fabs(len - 1.0f) > 1e-4f) ++notUnit;
            }
        }
    }
    CHECK(poses > 0 && notUnit == 0,
          "a moon direction is a unit vector on the dome");

    // Pale moon: offset 0, period 28 → phase 0.5 exactly at day 15, tod 0.
    // Full ⇒ anti-solar (dot ≈ -1 up to the small orbit tilt), and it stands
    // HIGH at midnight — the emergent behaviour the renderer will rely on.
    {
        const SkyDir sun  = sun_dir(0.0f);
        const SkyDir full = moon_dir(MoonId::Pale, 15, 0.0f);
        const float dt = full.x * sun.x + full.y * sun.y + full.z * sun.z;
        CHECK(dt < -0.9f, "a full moon rises opposite the sun");
        CHECK(full.y > 0.9f, "a full moon stands high at midnight");
        CHECK(moon_illumination01f(MoonId::Pale, 15.0f) > 0.99f,
              "and that same instant it is fully lit — position and phase agree");
    }
    // New moon (day 1) travels WITH the sun: same bearing, dot ≈ +1.
    {
        const SkyDir sun = sun_dir(0.0f);
        const SkyDir nw  = moon_dir(MoonId::Pale, 1, 0.0f);
        const float dt = nw.x * sun.x + nw.y * sun.y + nw.z * sun.z;
        CHECK(dt > 0.9f, "a new moon hides in the sun's glare");
    }

    // The lag SIGN: a waxing first-quarter moon TRAILS the sun by 90°, so it
    // stands highest at sunset (the evening moon everyone knows). At full/new
    // phase ±π lands on the same bearing, so only a quarter phase can tell a
    // trailing moon from a leading one — this is the check that pins the sign.
    // Pale quarter: phase 0.25 near day 8 (offset 0, period 28).
    {
        const SkyDir q = moon_dir(MoonId::Pale, 8, 0.75f);
        CHECK(q.y > 0.5f,
              "a waxing quarter moon stands high at sunset (trails the sun)");
    }

    // No midnight pop: the last instant of day N and the first of day N+1 are
    // the same sky. This is what the fractional-day phase buys.
    int popped = 0, crossings = 0;
    for (int mi = 0; mi < int(MoonId::Count); ++mi) {
        for (int day : {1, 14, 140}) {
            ++crossings;
            const SkyDir a = moon_dir(MoonId(mi), day, 0.9999f);
            const SkyDir b = moon_dir(MoonId(mi), day + 1, 0.0f);
            if (std::fabs(a.x - b.x) > 0.05f || std::fabs(a.y - b.y) > 0.05f
                || std::fabs(a.z - b.z) > 0.05f) ++popped;
        }
    }
    CHECK(crossings > 0 && popped == 0,
          "the moon glides through midnight — no per-day position jump");
}

void test_night_light() {
    using namespace sm;

    // Full Pale moon at midnight (day 15) → a strong light from that moon.
    {
        const NightLight n = night_light(15, 0.0f);
        CHECK(n.moonIndex == int(MoonId::Pale),
              "on the Pale moon's full night, the Pale moon dominates");
        CHECK(n.strength01 > 0.9f, "a full moon overhead lights the night");
        CHECK(n.dir.y > 0.5f, "the light comes from up in the sky");
        CHECK(in01(n.rgb[0]) && in01(n.rgb[1]) && in01(n.rgb[2])
                  && (n.rgb[0] + n.rgb[1] + n.rgb[2]) > 0.0f,
              "the light carries the moon's authored tint");
    }
    // Day 141: BOTH moons are new (141 ≡ 1 mod 28 and 141-1+3 ≡ 0 mod 11) —
    // the honest dark night. The context feature, not a bug.
    {
        const NightLight n = night_light(141, 0.0f);
        CHECK(n.strength01 < 0.05f,
              "when every moon is new, the night honestly goes dark");
    }
    // Strength stays a valid fraction across a sweep of nights and hours.
    int bad = 0, sampled = 0;
    for (int day = 1; day <= 60; ++day) {
        for (float tod : {0.0f, 0.1f, 0.5f, 0.85f}) {
            ++sampled;
            if (!in01(night_light(day, tod).strength01)) ++bad;
        }
    }
    CHECK(sampled > 0 && bad == 0, "night-light strength is always in [0,1]");
}

void test_constellations() {
    using namespace sm;
    CHECK_OR_RETURN(kConstellationCount >= 1, "at least one constellation exists");

    int starsSeen = 0, badStars = 0;
    int edgesSeen = 0, danglingEdges = 0, selfLoops = 0;
    for (int ci = 0; ci < kConstellationCount; ++ci) {
        const ConstellationDef& c = constellation_def(ci);
        CHECK(c.name != nullptr, "every constellation is named");
        CHECK(c.starCount >= 1 && c.stars != nullptr,
              "a constellation is made of stars");
        if (c.starCount < 1 || c.stars == nullptr) continue;

        for (int s = 0; s < c.starCount; ++s) {
            const StarDef& st = c.stars[s];
            ++starsSeen;
            const bool ok = st.name != nullptr
                         && st.az >= 0.0f && st.az < 360.0f
                         && st.el >= 0.0f && st.el <= 90.0f
                         && in01(st.brightness);
            if (!ok) ++badStars;
        }
        for (int e = 0; e < c.edgeCount; ++e) {
            const StarEdge& ed = c.edges[e];
            ++edgesSeen;
            if (ed.a >= c.starCount || ed.b >= c.starCount) ++danglingEdges;
            if (ed.a == ed.b) ++selfLoops;
        }
    }
    // The counts are asserted, not just the failures: an authored table that
    // silently became empty would otherwise pass every loop below.
    CHECK(starsSeen > 0 && badStars == 0,
          "every star sits in the sky at a real bearing, elevation and brightness");
    CHECK(edgesSeen > 0 && danglingEdges == 0,
          "every drawn line joins two stars that exist");
    CHECK(edgesSeen > 0 && selfLoops == 0,
          "no line joins a star to itself");
}

void test_star_size_seam() {
    using namespace sm;
    CHECK(kSkyStarSizeScale > 0.0f && kSkyStarSizeScale <= 1.0f,
          "the star-size scale shrinks stars, never grows or erases them");
    CHECK(kSkyStarSizeMin > 0.0f, "the smallest star is still visible");
}

} // namespace

int main() {
    test_moons();
    test_moons_desync();
    test_moon_orbits();
    test_night_light();
    test_constellations();
    test_star_size_seam();
    return sm::test::report("celestial_test");
}
