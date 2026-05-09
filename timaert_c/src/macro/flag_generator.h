// Procedural heraldic flag generator. Faithful port of `flag-generator.ts`.
//
// TS uses Canvas2D for drawing on a 128×128 RGBA bitmap. This C++ port
// rasterises the same shapes onto a `std::uint8_t[128*128*4]` buffer.
// Random sequence is bit-identical to TS (Math.sin-based generator).
// Anti-aliased curves in TS are emulated with hard-edge midpoint
// rasterisers — geometry + colour choices match per seed; pixel-level
// coverage on diagonals/arcs differs slightly.

#pragma once
#include <cstdint>
#include <vector>

namespace sm {

struct FlagBitmap {
    int W = 128;
    int H = 128;
    std::vector<std::uint8_t> rgba; // W*H*4
};

// Produce a 128×128 procedural heraldic flag deterministically from `seed`.
FlagBitmap generate_flag(std::uint32_t seed);

} // namespace sm
