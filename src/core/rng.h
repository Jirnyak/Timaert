// Seeded xorshift32 RNG — bit-exact with src/game/rng.ts.
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

    // Float in [0,1).
    float next_f01() { return float(next_u32()) / 4294967296.0f; }

    // Int in [lo, hi).
    int next_int(int lo, int hi) {
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
