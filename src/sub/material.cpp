#include "sub/material.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

float terrain_material_for(std::uint8_t tile, Biome biome) {
    switch (tile) {
        case TILE_FIELD:  return float(TM_Field);
        case TILE_SHORE:  return float(TM_Shore);
        case TILE_ROCK:   return float(TM_Rock);
        case TILE_ROAD:
        case TILE_SQUARE: return float(TM_Road);
        case TILE_WATER:  return float(TM_Water);
        default: break;
    }
    switch (biome) {
        case Biome::Tundra:  return float(TM_Tundra);
        case Biome::Taiga:   return float(TM_Taiga);
        case Biome::Snow:    return float(TM_Snow);
        case Biome::Valley:  return float(TM_Valley);
        case Biome::Swamp:   return float(TM_Swamp);
        case Biome::Desert:  return float(TM_Desert);
        case Biome::Steppe:  return float(TM_Steppe);
        case Biome::Tropics: return float(TM_Tropics);
        case Biome::Water:   return float(TM_Water);
        case Biome::Mountain: return float(TM_Rock);  // bare ledges read as rock
        case Biome::Meadow:
        default:             return float(TM_Meadow);
    }
}

bool material_is_authored(std::uint8_t tile) {
    switch (tile) {
        case TILE_FIELD:
        case TILE_SHORE:
        case TILE_ROCK:
        case TILE_ROAD:
        case TILE_SQUARE:
        case TILE_WATER:
            return true;
        default:
            return false;
    }
}

namespace {

// Sharpening of the bilinear coordinate: the raw weight ramps over a full
// cell (1024 tiles) — a taiga cell would show meadow speckle 400 tiles in.
// Compressing the ramp to |t-0.5| <= 0.5/k keeps the mixed band ~cell/k
// (~256 tiles) centred on the seam, matching how wide the height manifold
// visually transitions.
constexpr float kSeamBlendSharpness = 4.0f;

inline float sharpen(float t) {
    t = std::clamp((t - 0.5f) * kSeamBlendSharpness + 0.5f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Deterministic per-tile hash → [0,1). Keyed to ABSOLUTE tile coords so the
// dither pattern is a property of the world, not of the current 3×3 window
// (the GPU seam shift relocates baked bytes — they must stay valid).
inline float tile_hash01(long long ax, long long ay) {
    std::uint64_t h = std::uint64_t(ax) * 0x9E3779B97F4A7C15ull
                    ^ std::uint64_t(ay) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 27; h *= 0x94D049BB133111EBull;
    h ^= h >> 31;
    return float(h >> 40) * (1.0f / 16777216.0f);
}

} // namespace

// One axis entry: which two 3×3 columns (or rows) local coord `l` blends,
// and the sharpened fraction between them. Same convention as
// generate_heightmap: cell centres at grid coords 0.5/1.5/2.5, the owning
// cell spanning [1,2).
static GroundAxis ground_axis_for(int l, int cellSize) {
    const float g = (float(l) + 0.5f) / float(cellSize) + 1.0f;
    const int i0 = std::clamp(int(std::floor(g - 0.5f)), 0, 2);
    const int i1 = std::min(2, i0 + 1);
    const float f = sharpen(std::clamp((g - 0.5f) - float(i0), 0.0f, 1.0f));
    return {std::uint8_t(i0), std::uint8_t(i1), f};
}

void ground_axis_table(int cellSize, GroundAxis* out) {
    for (int l = 0; l < cellSize; ++l) out[l] = ground_axis_for(l, cellSize);
}

Biome pick_ground_biome_axis(const Biome nbBiome[9],
                             const GroundAxis& ax, const GroundAxis& ay,
                             long long absX, long long absY) {
    const Biome owner = nbBiome[4];
    if (owner == Biome::Water) return owner;

    // Deep inside the cell both ramps saturate → the single corner is the
    // owner; skip the hash entirely (the common case).
    const Biome b00 = nbBiome[ay.i0 * 3 + ax.i0];
    const Biome b10 = nbBiome[ay.i0 * 3 + ax.i1];
    const Biome b01 = nbBiome[ay.i1 * 3 + ax.i0];
    const Biome b11 = nbBiome[ay.i1 * 3 + ax.i1];
    if (b00 == b10 && b00 == b01 && b00 == b11) return b00;

    const float fx = ax.f, fy = ay.f;
    Biome cand[4] = {b00, b10, b01, b11};
    float w[4] = {(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                  (1.0f - fx) * fy,          fx * fy};
    // Water never dithers onto land — the shoreline is authored by height
    // (TILE_WATER / TILE_SHORE), not by ground speckle.
    float total = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (cand[i] == Biome::Water) w[i] = 0.0f;
        total += w[i];
    }
    if (total <= 0.0f) return owner;

    const float r = tile_hash01(absX, absY) * total;
    float acc = 0.0f;
    for (int i = 0; i < 4; ++i) {
        acc += w[i];
        if (r < acc) return cand[i];
    }
    return cand[3] == Biome::Water ? owner : cand[3];
}

Biome pick_ground_biome(const Biome nbBiome[9],
                        int lx, int ly, int cellSize,
                        long long absX0, long long absY0) {
    const GroundAxis ax = ground_axis_for(lx, cellSize);
    const GroundAxis ay = ground_axis_for(ly, cellSize);
    return pick_ground_biome_axis(nbBiome, ax, ay, absX0 + lx, absY0 + ly);
}

} // namespace sm::sub
