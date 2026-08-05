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

    // Deep inside the cell both ramps saturate → the single corner is the
    // owner; skip the hash entirely (the common case). A WATER owner falls
    // through: its submerged tiles are authored TILE_WATER, so the ground
    // fallback is only ever consulted for its DRY margin — which must read
    // as the adjacent LAND, not as brown "water bed" (the straight
    // green|brown wall on coastal cell borders).
    const Biome b00 = nbBiome[ay.i0 * 3 + ax.i0];
    const Biome b10 = nbBiome[ay.i0 * 3 + ax.i1];
    const Biome b01 = nbBiome[ay.i1 * 3 + ax.i0];
    const Biome b11 = nbBiome[ay.i1 * 3 + ax.i1];
    if (b00 == b10 && b00 == b01 && b00 == b11
        && b00 != Biome::Water) return b00;

    const float fx = ax.f, fy = ay.f;
    Biome cand[4] = {b00, b10, b01, b11};
    float w[4] = {(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                  (1.0f - fx) * fy,          fx * fy};
    // Water never contributes ground — the shoreline is authored by height
    // (TILE_WATER / TILE_SHORE), not by ground speckle. This both keeps
    // water from bleeding onto land AND makes a water cell's dry margin
    // inherit the nearest land biome.
    float total = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (cand[i] == Biome::Water) w[i] = 0.0f;
        total += w[i];
    }
    if (total <= 0.0f) {
        // No land in the blend corners (deep inside a water cell, or a
        // rare dry hump mid-ocean): fall back to the first land biome
        // anywhere in the ring, else stay Water.
        if (owner != Biome::Water) return owner;
        for (int i = 0; i < 9; ++i)
            if (nbBiome[i] != Biome::Water) return nbBiome[i];
        return Biome::Water;
    }

    const float r = tile_hash01(absX, absY) * total;
    float acc = 0.0f;
    for (int i = 0; i < 4; ++i) {
        acc += w[i];
        if (r < acc) return cand[i];
    }
    // FP tail (r ≈ total): last candidate that actually held weight.
    for (int i = 3; i >= 0; --i)
        if (w[i] > 0.0f) return cand[i];
    return owner;
}

Biome pick_ground_biome(const Biome nbBiome[9],
                        int lx, int ly, int cellSize,
                        long long absX0, long long absY0) {
    const GroundAxis ax = ground_axis_for(lx, cellSize);
    const GroundAxis ay = ground_axis_for(ly, cellSize);
    return pick_ground_biome_axis(nbBiome, ax, ay, absX0 + lx, absY0 + ly);
}

Biome apply_mountain_treeline(Biome picked, float hNorm,
                              long long absX, long long absY) {
    // Stone is a function of ALTITUDE, not of which cell's biome won the
    // pick: heights are seamless across cell borders, so keying the rock on
    // hNorm alone makes the stone line follow the iso-height contour
    // through any seam (keying it on the Mountain-biome pick drew a
    // straight wall wherever the pick's ~256-tile dither band ended —
    // the owner's peak-texture seam). Any LAND ground above the band is
    // bare rock — a high foothill of a meadow cell earns its stone too.
    if (picked == Biome::Water) return picked;
    const float t = treeline_t(hNorm);
    if (t >= 1.0f) return Biome::Mountain;
    const Biome below = (picked == Biome::Mountain) ? Biome::Meadow : picked;
    if (t <= 0.0f) return below;
    // Dither through the band with the same style of absolute-keyed hash the
    // seam dither uses — stone gains ground exactly as the trees thin.
    return treeline_is_rock(t, absX, absY) ? Biome::Mountain : below;
}

} // namespace sm::sub
