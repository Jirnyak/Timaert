// Locks the sm::Rng numeric contract (core/rng.h).
//
// Two holes existed and both were reachable:
//   1. next_f01() promised [0, 1) but divided float(u32) by 2^32 — the float
//      mantissa is 24 bits, so the top ~2^8 draw codes rounded UP to exactly
//      1.0f. Every `int(next_f01() * N)` consumer could index one past the end
//      (the Fisher-Yates OOB in content/quests/procedural.cpp was this hole).
//   2. next_int(lo, hi) computed `r % (hi - lo)` with no guard — hi == lo is
//      modulo by zero, UB (on ARM it silently returns garbage, on x86 SIGFPE).
//
// The test drives the REAL generator into the failing draws: xorshift32 is a
// bijection on nonzero u32, so we invert it to find the exact seed state that
// produces any chosen output word, then assert the contract on that draw.
//
// Every sweep below counts what it examined and asserts that count, so a loop
// that stops sampling (a bound that goes empty, a range that inverts) fails
// instead of passing quietly.
#include "check.h"

#include "core/rng.h"

#include <cstdint>

namespace {

// Invert one xorshift step. y = x ^ (x << k)  =>  recover x.
std::uint32_t unshift_left(std::uint32_t y, int k) {
    std::uint32_t x = y;
    for (int i = 0; i < 32 / k + 1; ++i) x = y ^ (x << k);
    return x;
}
std::uint32_t unshift_right(std::uint32_t y, int k) {
    std::uint32_t x = y;
    for (int i = 0; i < 32 / k + 1; ++i) x = y ^ (x >> k);
    return x;
}

// State that makes the NEXT next_u32() return exactly `target`.
std::uint32_t xorshift32_preimage(std::uint32_t target) {
    std::uint32_t x = unshift_left(target, 5);
    x = unshift_right(x, 17);
    x = unshift_left(x, 13);
    return x;
}

// The tooling this test steers with has to be right, or every assertion below
// is aimed at the wrong draw.
void test_preimage_really_inverts() {
    const std::uint32_t kProbes[4] = {0xFFFFFFFFu, 0x80000000u, 0x00C0FFEEu, 1u};
    int probes = 0, wrong = 0;
    for (std::uint32_t y : kProbes) {
        ++probes;
        sm::Rng r(xorshift32_preimage(y));
        if (r.next_u32() != y) ++wrong;
    }
    CHECK(probes == 4 && wrong == 0,
          "the preimage lands the generator on exactly the draw we asked for");
}

// The whole band that used to round up to 1.0f, drawn for real.
void test_top_band_stays_below_one() {
    int drawn = 0, escaped = 0;
    for (std::uint32_t y = 0xFFFFFF80u; y != 0u; ++y) {
        ++drawn;
        sm::Rng r(xorshift32_preimage(y));
        const float v = r.next_f01();
        if (!(v >= 0.0f && v < 1.0f)) ++escaped;
    }
    CHECK(drawn == 128 && escaped == 0,
          "the 128 highest draws stay inside [0,1): int(f * N) can never reach N");
}

void test_degenerate_ranges_collapse_to_lo() {
    sm::Rng r(12345u);
    int calls = 0, wrong = 0;
    for (int i = 0; i < 4; ++i) {
        ++calls;
        if (r.next_int(5, 5) != 5) ++wrong;
    }
    CHECK(calls == 4 && wrong == 0,
          "an empty range returns its bound instead of dividing by zero");
    CHECK(r.next_int(7, 3) == 7,
          "an inverted range returns its bound instead of dividing by zero");
}

void test_range_law_on_live_draws() {
    sm::Rng r(999u);
    int ints = 0, outside = 0;
    for (int i = 0; i < 100000; ++i) {
        ++ints;
        const int v = r.next_int(-3, 11);
        if (v < -3 || v >= 11) ++outside;
    }
    CHECK(ints == 100000 && outside == 0,
          "next_int stays in [lo, hi) across a hundred thousand live draws");

    sm::Rng q(31337u);
    int floats = 0, escaped = 0;
    for (int i = 0; i < 100000; ++i) {
        ++floats;
        const float f = q.next_f01();
        if (!(f >= 0.0f && f < 1.0f)) ++escaped;
    }
    CHECK(floats == 100000 && escaped == 0,
          "next_f01 stays in [0,1) across a hundred thousand live draws");
}

} // namespace

int main() {
    test_preimage_really_inverts();
    test_top_band_stays_below_one();
    test_degenerate_ranges_collapse_to_lo();
    test_range_law_on_live_draws();
    return sm::test::report("rng_contract_test");
}
