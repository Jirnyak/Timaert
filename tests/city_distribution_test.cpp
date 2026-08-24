// Locks the SPATIAL DISTRIBUTION of subworld city houses (sub/gens/dispatch.cpp
// gen_city + sub/city_layout.h). The generator parity test already guards the
// counts (>=40 houses/walls, one keep, no blocked walls, edge-anchor roads);
// this test guards the property those counts can't see: that houses SPREAD
// across the whole walled footprint instead of clumping.
//
// Before the radial-concentric street plan, every interior street grew as a ray
// from the cell centre and — when no neighbour carried a road — all fanned due
// east, so houses piled into the east/south-east corner with whole quadrants
// empty. That is exactly the owner's "one clump" report. These assertions would
// have caught it: they fail on an empty angular sector and on a distribution
// pinched near the centre.
//
// The invariants are chosen to be robust to seed/population (they describe the
// SHAPE of the distribution, not exact bin counts), so this locks the behaviour
// without being brittle.

#include "check.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace sm;
using namespace sm::sub;

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/city_distribution_test.cpp", 0);
    return 1;
}

constexpr float kTwoPi = 6.28318530718f;

struct Spread {
    int   houses = 0;
    float meanR = 0.0f;
    float maxR = 0.0f;
    std::array<int, 8> sector{};   // angular histogram, 8 sectors of 45°
    float fracOuterHalf = 0.0f;    // houses beyond half of maxR
    int   emptySectors = 0;
    int   minSector = 0;
    int   maxSector = 0;
};

// Generate a city with the given seed/population on a fully road-less
// neighbourhood (the worst case for the old east-bias bug) and measure the
// house spread.
Spread measure_city(int cx, int cy, std::uint32_t seed, int population) {
    CellContext city{};
    city.cx = cx;
    city.cy = cy;
    city.macroHeight = 0.64f;
    city.biome = Meadow;
    city.feature = FT_None;
    city.landmark.id = 101;
    city.landmark.size = population;
    city.landmark.kind = LandmarkType::City;
    city.seed = seed;

    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = 0.64f;
        nbB[i] = Meadow;
        nbF[i] = std::uint8_t(FT_None);   // no road neighbours: worst case
    }

    SubworldMapData out{};
    dispatch_generate(city, nbH, nbB, nbF, out);

    const float center = float(kCellSize / 2);
    Spread s{};
    float sumR = 0.0f;
    // First pass: centroid distances + max radius + angular histogram.
    for (const Structure& st : out.structures) {
        if (st.kind != Structure::House) continue;
        ++s.houses;
        const float dx = st.x - center;
        const float dy = st.y - center;
        const float rr = std::sqrt(dx * dx + dy * dy);
        sumR += rr;
        if (rr > s.maxR) s.maxR = rr;
        float a = std::atan2(dy, dx);
        if (a < 0.0f) a += kTwoPi;
        int sec = int(a / (kTwoPi / 8.0f));
        if (sec >= 8) sec = 7;
        if (sec < 0) sec = 0;
        ++s.sector[std::size_t(sec)];
    }
    s.meanR = s.houses > 0 ? sumR / float(s.houses) : 0.0f;

    // Second pass: fraction beyond half the max radius.
    int outer = 0;
    for (const Structure& st : out.structures) {
        if (st.kind != Structure::House) continue;
        const float dx = st.x - center;
        const float dy = st.y - center;
        if (std::sqrt(dx * dx + dy * dy) >= s.maxR * 0.5f) ++outer;
    }
    s.fracOuterHalf = s.houses > 0 ? float(outer) / float(s.houses) : 0.0f;

    s.minSector = 1 << 30;
    s.maxSector = 0;
    for (int i = 0; i < 8; ++i) {
        const int c = s.sector[std::size_t(i)];
        if (c == 0) ++s.emptySectors;
        if (c < s.minSector) s.minSector = c;
        if (c > s.maxSector) s.maxSector = c;
    }
    return s;
}

} // namespace

int main() {
    struct Case { int cx, cy; std::uint32_t seed; int pop; };
    const Case cases[] = {
        { -11,  6, 0x0BADF00Du, 6000 },   // the parity-test city
        {   3, -7, 0xC0FFEE11u, 2500 },
        {  20, 14, 0x51A7E11u,  9000 },
        {  -5, -2, 0x13572468u, 1200 },   // small city, still spread
    };

    for (const Case& c : cases) {
        const Spread s = measure_city(c.cx, c.cy, c.seed, c.pop);

        if (s.houses < 40)
            return fail("city produced too few houses to assess spread");

        // (1) No empty angular sector — the direct guard against the "clump in
        // one corner" bug. The old generator left up to 4 of 8 sectors at 0.
        if (s.emptySectors != 0) {
            std::fprintf(stderr,
                "seed=0x%X pop=%d sectors: %d %d %d %d %d %d %d %d\n",
                c.seed, c.pop,
                s.sector[0], s.sector[1], s.sector[2], s.sector[3],
                s.sector[4], s.sector[5], s.sector[6], s.sector[7]);
            return fail("a whole angular sector has no houses (clumped city)");
        }

        // (2) Angular balance: the thinnest sector holds a real share of the
        // busiest one. Old min/max was 0.00 (0 vs 189); the plan gives ~0.68.
        if (s.maxSector <= 0
            || float(s.minSector) / float(s.maxSector) < 0.20f) {
            std::fprintf(stderr,
                "seed=0x%X pop=%d angular min/max=%d/%d\n",
                c.seed, c.pop, s.minSector, s.maxSector);
            return fail("houses angularly lopsided (min/max < 0.20)");
        }

        // (3) Radial spread: houses must reach out into the footprint, not pile
        // downtown. A meaningful fraction sits in the outer half of the disk.
        if (s.fracOuterHalf < 0.30f) {
            std::fprintf(stderr,
                "seed=0x%X pop=%d fracOuterHalf=%.2f meanR=%.1f maxR=%.1f\n",
                c.seed, c.pop, double(s.fracOuterHalf),
                double(s.meanR), double(s.maxR));
            return fail("houses pinched near centre (outer-half frac < 0.30)");
        }

        // (4) Mean radius is well off-centre relative to the reach.
        if (s.maxR <= 1.0f || s.meanR < s.maxR * 0.30f)
            return fail("mean house radius too close to centre");
    }

    std::printf("OK city_distribution_test: %d cities — houses fill all 8 "
                "angular sectors (no empty quadrant), angularly balanced "
                "(min/max >= 0.20) and radially spread (outer-half frac "
                ">= 0.30), across seeds/populations\n",
                int(sizeof(cases) / sizeof(cases[0])));
    CHECK(true, "every gate above held");
    return sm::test::report("city_distribution_test");
}
