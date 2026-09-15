// THE AIR HAS A HEIGHT — the law of CANON.md S18.1, asserted as relations.
//
// The air is the only thing standing between the player and 1049 km of world,
// and the canon spends four paragraphs on it: the plain must dissolve on its
// own, the summit must outlive it on its own, and NO distance may be chosen by
// anybody. All three are statements about a function, so this file measures
// that function instead of trusting the picture.
//
// What it does NOT do is restate the numbers. Every check below is a RELATION
// ("a ray to the crest is thinner than the ray along the valley beside it",
// "the plain's air contains the plain"), derived from the same named constants
// the code reads, so a retune of D0 or H moves the numbers and the law still
// holds — or the law breaks and this reddens, which is the point.
//
// The function under test is `sub::air_optical_depth`, the CPU mirror of
// shaders/lighting.glsl `aerial_perspective` — the same arrangement `biome_at`
// has with the shader's `bt_biome`. The mirror is what makes the law testable
// at all; keeping the two in step is a hand duty, and the numbers here are
// small enough that a drift shows up as a wrong frame immediately.
#include "check.h"
#include "sub/lighting.h"

#include <cmath>

using namespace sm;
using sm::sub::air_optical_depth;
using sm::sub::air_transmittance;
using sm::sub::kAirEFoldM;
using sm::sub::kAirScaleHeightM;
using sm::sub::kHeightScaleM;
using sm::sub::kSeaLevelM;

namespace {

// The world's own altitudes, every one of them derived from a named constant
// rather than typed in. These ARE the ladder the air is scaled against.
const float kMountainLineM = kMountainBiomeLevel * kHeightScaleM;   // 1125 m
const float kSummitM       = 1.04f * kHeightScaleM;                 // the
    // generator's hard crest clamp (sub/base_generator.cpp peakHeight) — the
    // tallest ground this world will ever make, 1560 m.
const float kPlainM        = kSeaLevelM + 200.0f;  // a typical lowland stance

} // namespace

int main() {
    using namespace sm::test;

    // ── 1. THE AIR IS A LAYER THE LOWLAND LIVES INSIDE ────────────────────
    // H is the mountain band by construction; the consequence that has to be
    // true for "the ridge floats over a sea of haze" is that the lowland sits
    // inside the first scale height while a mountain does not.
    CHECK(kPlainM - kSeaLevelM < kAirScaleHeightM,
          "a lowland stance is inside one scale height — it breathes dense air");
    CHECK(kMountainLineM - kSeaLevelM > kAirScaleHeightM,
          "the line where land becomes mountain is ABOVE the dense layer");
    CHECK(kSummitM - kSeaLevelM > 2.0f * kAirScaleHeightM,
          "a summit stands above two scale heights — it is out of the haze");

    // ── 2. THE INTEGRAL IS AN INTEGRAL ────────────────────────────────────
    // Properties no correct implementation can miss, and that the old uniform
    // exp(-d/D) failed by construction.
    {
        // Distance alone: longer ray, more air, always.
        float worst = 0.0f;
        int samples = 0;
        float prev = -1.0f;
        for (float d = 100.0f; d <= 200000.0f; d *= 1.5f) {
            const float t = air_optical_depth(d, kPlainM, kPlainM);
            if (t <= prev) worst += 1.0f;
            prev = t;
            ++samples;
        }
        CHECK(samples > 8 && worst == 0.0f,
              "optical depth rises strictly with distance");
    }
    {
        // Altitude alone: the SAME ray length, aimed higher, carries less air.
        // This is the whole difference from the uniform model, which returned
        // one number here no matter where the ray pointed.
        const float d = 20000.0f;
        float prev = 1e9f;
        int samples = 0, breaks = 0;
        for (float h = kSeaLevelM; h <= kSummitM; h += 60.0f) {
            const float t = air_optical_depth(d, kPlainM, h);
            if (t >= prev) ++breaks;
            prev = t;
            ++samples;
        }
        CHECK(samples > 8 && breaks == 0,
              "a ray of fixed length carries LESS air the higher it reaches");
    }
    {
        // The segment mean does not care which end you call the eye.
        float mismatches = 0.0f;
        int samples = 0;
        for (float h = kSeaLevelM; h <= kSummitM; h += 120.0f) {
            const float up   = air_optical_depth(5000.0f, kPlainM, h);
            const float down = air_optical_depth(5000.0f, h, kPlainM);
            if (std::fabs(up - down) > 1e-4f) mismatches += 1.0f;
            ++samples;
        }
        CHECK(samples > 4 && mismatches == 0.0f,
              "looking up and looking down along one ray cost the same air");
    }
    {
        // The float guard must not be a seam: the limit branch and the
        // quotient branch have to agree where they meet. (The guard exists
        // because the quotient's numerator cancels; below it the limit is the
        // more accurate of the two, not a different answer.)
        const float eps = 1e-3f * kAirScaleHeightM;   // the guard, in metres
        const float below = air_optical_depth(9000.0f, kPlainM,
                                              kPlainM + eps * 0.5f);
        const float above = air_optical_depth(9000.0f, kPlainM,
                                              kPlainM + eps * 2.0f);
        CHECK(std::fabs(below - above) < 1e-3f * above,
              "the limit branch and the quotient branch agree at the guard");
    }

    // ── 3. THE CANON'S TWO PROMISES ───────────────────────────────────────
    // «Равнина растворяется сама, вершина сама доживает дальше. Ни одно из
    // этих расстояний никем не выбрано.» Stated as the inequality it is: at
    // one and the same distance the plain must be gone and the summit must
    // not be. The distance is not a constant of the code — it is read off the
    // air itself, as the range where the plain has been taken.
    {
        // Where the lowland has lost 9/10 of itself.
        float dPlainGone = 0.0f;
        for (float d = 1000.0f; d <= 400000.0f; d += 1000.0f) {
            if (air_transmittance(d, kPlainM, kPlainM) < 0.10f) {
                dPlainGone = d;
                break;
            }
        }
        CHECK(dPlainGone > 0.0f, "the lowland does dissolve at a finite range");
        const float summit = air_transmittance(dPlainGone, kPlainM, kSummitM);
        CHECK(summit > 0.25f,
              "at the range that swallowed the plain, the summit is still there");
        // ...and it is not there forever either, or the air would not be air.
        CHECK(air_transmittance(6.0f * dPlainGone, kPlainM, kSummitM) < 0.10f,
              "far enough, the summit goes too — no draw distance, just air");
    }

    // ── 4. CLIMBING OPENS THE WORLD ───────────────────────────────────────
    // The canon's emergent reward: the same far summit, seen from the plain
    // and seen from a summit. No code implements this — it must fall out.
    {
        const float d = 100000.0f;                     // 100 macro cells
        const float fromPlain  = air_transmittance(d, kPlainM, kSummitM);
        const float fromSummit = air_transmittance(d, kSummitM, kSummitM);
        CHECK(fromSummit > 2.0f * fromPlain,
              "climbing a summit MORE THAN DOUBLES what the far ridge shows");
    }

    // ── 5. THE OFF STATE, AND THE NEGATIVE CONTROL ────────────────────────
    // A ray of no length takes no air — the identity the harness leans on.
    CHECK(air_transmittance(0.0f, kPlainM, kPlainM) > 0.999f,
          "zero distance takes nothing");
    {
        // The detector itself: a UNIFORM air (the model this replaced) fails
        // check 2's altitude test, and this is what proves check 2 can see it.
        // Uniform means f == 1 regardless of altitude, i.e. tau = d/D0.
        const float d = 20000.0f;
        const float uniformLow  = d / kAirEFoldM;
        const float uniformHigh = d / kAirEFoldM;
        CHECK(uniformLow == uniformHigh,
              "control: uniform air is blind to altitude (what we replaced)");
        CHECK(air_optical_depth(d, kPlainM, kSummitM)
                  < air_optical_depth(d, kPlainM, kPlainM),
              "control: the new air is NOT blind to it");
    }

    return report("air_law_test");
}
