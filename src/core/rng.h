// Seeded xorshift32 RNG. (TS parity is dead — this is the sole authority.)
#pragma once
#include <cstdint>

namespace sm {

struct Rng {
    std::uint32_t state;

    explicit Rng(std::uint32_t seed) : state(seed ? seed : 1u) {}

    // Returns next raw u32.
    std::uint32_t next_u32() {
        std::uint32_t s = state;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        state = s;
        return s;
    }

    // Float in [0,1). Top 24 bits on the 2^-24 grid — every value is exactly
    // representable, the max draw is 1 - 2^-24, never 1.0. (The old
    // float(u32)/2^32 rounded the top ~2^8 codes UP to exactly 1.0f, so every
    // `int(next_f01() * N)` consumer could index one past the end.)
    float next_f01() { return float(next_u32() >> 8) * 0x1.0p-24f; }

    // Int in [lo, hi); a degenerate or inverted range collapses to lo
    // (hi == lo used to be modulo by zero — UB).
    int next_int(int lo, int hi) {
        if (hi - lo <= 0) return lo;
        int r = int(next_u32() & 0x7fffffff);
        return lo + (r % (hi - lo));
    }
};

// Deterministic 32-bit hash (mix of x,y,seed). Used for procedural noise seeding.
inline std::uint32_t hash3(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
    std::uint32_t v = (x * 374761393u) ^ (y * 668265263u) ^ (seed * 2246822519u);
    v = (v ^ (v >> 13)) * 1274126177u;
    v ^= v >> 16;
    return v;
}

} // namespace sm
