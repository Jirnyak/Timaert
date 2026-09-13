// Value noise over the GLOBAL tile lattice — the kit's one hash-noise door.
//
// Every open-air module that wants "a number for this place" asks here, and it
// asks in ABSOLUTE tile coordinates: that is the whole point. A glade, a crop
// stand, a scorch mottle and a furrow all straddle cell seams, and two
// neighbouring cells only agree about a feature crossing their shared edge if
// both derive it from the same global lattice with the same hash. Seeding off
// a cell-local coordinate re-rolls the pattern at every seam — the failure the
// glade scan was written to avoid in the first place.
#pragma once
#include "core/rng.h"

#include <cmath>
#include <cstdint>

namespace sm::sub::kit {

// One lattice node, one number in [0, 1). Integer coordinates are GLOBAL
// (cx * kCellSize + local), so the same node answers the same on both sides
// of a seam.
inline float noise01(int x, int y, std::uint32_t seed) {
    return float(hash3(std::uint32_t(x), std::uint32_t(y), seed)) / 4294967295.0f;
}

// Bilinear interpolation of the same lattice — a continuous field rather than
// a per-node coin, for anything that must not show the lattice (scorch edges,
// ploughland massifs, rock mottle). The interpolant is smoothstepped so the
// field is C¹ at the nodes: a straight lerp leaves a visible crease along
// every lattice line.
inline float smooth_noise01(float x, float y, std::uint32_t seed) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = noise01(ix,     iy,     seed);
    const float n10 = noise01(ix + 1, iy,     seed);
    const float n01 = noise01(ix,     iy + 1, seed);
    const float n11 = noise01(ix + 1, iy + 1, seed);
    return n00 * (1.0f - sx) * (1.0f - sy)
         + n10 * sx * (1.0f - sy)
         + n01 * (1.0f - sx) * sy
         + n11 * sx * sy;
}

} // namespace sm::sub::kit
