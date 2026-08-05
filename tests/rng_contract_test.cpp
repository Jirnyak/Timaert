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

#include "core/rng.h"

#include <cstdint>
#include <cstdio>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL rng_contract_test: %s\n", msg);
    return 1;
}

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

} // namespace

int main() {
    using namespace sm;

    // Sanity: the inverse really is the inverse, across the value range.
    const std::uint32_t kProbes[4] = {0xFFFFFFFFu, 0x80000000u, 0x00C0FFEEu, 1u};
    for (std::uint32_t y : kProbes) {
        Rng r(xorshift32_preimage(y));
        if (r.next_u32() != y) return fail("xorshift32 preimage sanity");
    }

    // 1) Every draw in the top band that used to round to 1.0f stays below it.
    for (std::uint32_t y = 0xFFFFFF80u; y != 0u; ++y) {
        Rng r(xorshift32_preimage(y));
        const float v = r.next_f01();
        if (!(v >= 0.0f && v < 1.0f)) return fail("next_f01 escaped [0,1) on a top-band draw");
    }

    // 2) Degenerate ranges must not divide by zero: hi == lo and hi < lo
    //    both collapse to lo.
    {
        Rng r(12345u);
        for (int i = 0; i < 4; ++i)
            if (r.next_int(5, 5) != 5) return fail("next_int(5,5) must return 5");
        if (r.next_int(7, 3) != 7) return fail("next_int(7,3) must return 7");
    }

    // 3) Range law on a sample of live draws.
    {
        Rng r(999u);
        for (int i = 0; i < 100000; ++i) {
            const int v = r.next_int(-3, 11);
            if (v < -3 || v >= 11) return fail("next_int left [lo,hi)");
        }
        Rng q(31337u);
        for (int i = 0; i < 100000; ++i) {
            const float f = q.next_f01();
            if (!(f >= 0.0f && f < 1.0f)) return fail("next_f01 left [0,1)");
        }
    }

    std::printf("rng_contract_test: preimage=ok top_band=ok degenerate=ok range=ok\n");
    return 0;
}
