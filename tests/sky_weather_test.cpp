// Locks the derived-weather contract of the atmosphere submodule (sub/sky.h).
//
// Weather is a PURE function of the calendar — no state, no serialization —
// and the owner's seasonal ruling is data (kSeasonPrecip): winter snows,
// summer rains, spring may hail, storms only ride rain. This test proves the
// properties a constant or a broken hash could not satisfy: determinism,
// honest ranges, seasonal kinds, day-window smoothness (no popping), variety
// (both wet and dry days exist), and the lightning envelope's contract.
//
// Year layout (macro/seasons.h): 32 days a season — spring 1..32,
// summer 33..64, autumn 65..96, winter 97..128.
#include "check.h"

#include "sub/sky.h"

#include <cmath>
#include <initializer_list>

namespace {

bool in01(float v) { return v >= 0.0f && v <= 1.0f; }

void test_determinism_and_ranges() {
    using namespace sm;
    using namespace sm::sub;
    int samples = 0, bad = 0, nondet = 0, stormNotRain = 0;
    for (int day = 1; day <= 256; ++day) {
        for (float tod : {0.0f, 0.2f, 0.45f, 0.7f, 0.95f}) {
            ++samples;
            float p1 = -1, s1 = -1, p2 = -1, s2 = -1;
            PrecipKind k1{}, k2{};
            weather_at(day, tod, p1, k1, s1);
            weather_at(day, tod, p2, k2, s2);
            if (p1 != p2 || s1 != s2 || k1 != k2) ++nondet;
            if (!in01(p1) || !in01(s1)) ++bad;
            if (s1 > 0.0f && (k1 != PrecipKind::Rain || p1 <= 0.0f))
                ++stormNotRain;
        }
    }
    CHECK(samples > 0 && nondet == 0,
          "the same instant always has the same weather");
    CHECK(samples > 0 && bad == 0,
          "precipitation and storm are honest fractions in [0,1]");
    CHECK(samples > 0 && stormNotRain == 0,
          "thunder only rides an actual rain shower");
}

void test_seasonal_kinds() {
    using namespace sm;
    using namespace sm::sub;
    // Sample every day of each season at many hours; whenever something
    // falls, the kind must obey the owner's seasonal table.
    int winterWet = 0, winterNotSnow = 0;
    int summerWet = 0, summerNotRain = 0;
    for (int d = 0; d < 32; ++d) {
        for (float tod : {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}) {
            float p, s;
            PrecipKind k{};
            weather_at(97 + d, tod, p, k, s);       // winter
            if (p > 0.0f) {
                ++winterWet;
                if (k != PrecipKind::Snow) ++winterNotSnow;
            }
            weather_at(33 + d, tod, p, k, s);       // summer
            if (p > 0.0f) {
                ++summerWet;
                if (k != PrecipKind::Rain) ++summerNotRain;
            }
        }
    }
    CHECK(winterWet > 0 && winterNotSnow == 0, "in winter, what falls is snow");
    CHECK(summerWet > 0 && summerNotRain == 0, "in summer, what falls is rain");
}

void test_variety_and_day_constant_kind() {
    using namespace sm;
    using namespace sm::sub;
    // Within one season some days are wet and some are dry — weather is
    // weather, not a constant; and a day never switches kind mid-shower.
    int wetDays = 0, dryDays = 0, kindFlips = 0;
    for (int d = 65; d <= 96; ++d) {                // autumn
        bool wet = false;
        PrecipKind firstKind{};
        bool haveKind = false;
        for (int i = 0; i < 24; ++i) {
            float p, s;
            PrecipKind k{};
            weather_at(d, float(i) / 24.0f, p, k, s);
            if (p > 0.0f) {
                wet = true;
                if (!haveKind) { firstKind = k; haveKind = true; }
                else if (k != firstKind) ++kindFlips;
            }
        }
        (wet ? wetDays : dryDays)++;
    }
    CHECK(wetDays > 0 && dryDays > 0,
          "an autumn holds both wet days and dry days");
    CHECK(kindFlips == 0, "a day's precipitation keeps one kind all day");
}

void test_no_popping() {
    using namespace sm;
    using namespace sm::sub;
    // The shower window is eased: scanning a whole wet day in ~17-second
    // game steps, intensity never jumps. A hard on/off window fails this.
    int scanned = 0, pops = 0;
    for (int d = 65; d <= 96; ++d) {
        float prev = -1.0f;
        for (int i = 0; i <= 512; ++i) {
            float p, s;
            PrecipKind k{};
            weather_at(d, float(i) / 512.0f, p, k, s);
            if (prev >= 0.0f) {
                ++scanned;
                if (std::fabs(p - prev) > 0.04f) ++pops;
            }
            prev = p;
        }
    }
    CHECK(scanned > 0 && pops == 0,
          "a shower fades in and out — intensity never pops");
}

void test_storm_flash() {
    using namespace sm;
    using namespace sm::sub;
    // No storm — no flash, ever.
    int calmSamples = 0, calmFlashes = 0;
    for (int i = 0; i < 200; ++i) {
        ++calmSamples;
        if (storm_flash01(float(i) * 0.37f, 0.0f) != 0.0f) ++calmFlashes;
    }
    CHECK(calmSamples > 0 && calmFlashes == 0, "a calm sky never flashes");

    // A full storm: envelope stays in [0,1], is deterministic, and actually
    // flashes somewhere across a couple of minutes (a dead envelope fails).
    int samples = 0, bad = 0, nondet = 0, lit = 0;
    for (int i = 0; i < 2400; ++i) {
        const float t = float(i) * 0.05f;      // 120 s at 20 Hz
        ++samples;
        const float f = storm_flash01(t, 1.0f);
        if (!in01(f)) ++bad;
        if (f != storm_flash01(t, 1.0f)) ++nondet;
        if (f > 0.25f) ++lit;
    }
    CHECK(samples > 0 && bad == 0, "the flash envelope is a fraction in [0,1]");
    CHECK(samples > 0 && nondet == 0, "the flash envelope is deterministic");
    CHECK(lit > 0, "a two-minute thunderstorm actually flashes");
}

} // namespace

int main() {
    test_determinism_and_ranges();
    test_seasonal_kinds();
    test_variety_and_day_constant_kind();
    test_no_popping();
    test_storm_flash();
    return sm::test::report("sky_weather_test");
}
