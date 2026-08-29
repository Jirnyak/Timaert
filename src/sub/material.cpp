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
    // Deep inside the cell both ramps saturate → the single corner is the
    // owner; skip the hash entirely (the common case).
    //
    // The ring is a GROUND ring (never Water — see the header contract), so
    // there is no water special-casing here. There used to be: water corners
    // were zeroed and re-normalised (which flat-painted the neighbour across
    // the whole band, no gradient), and when all four corners were water the
    // pick fell back to the FIRST land biome in row-major ring scan — an
    // NW-biased answer that drew razor-straight walls INSIDE a water cell
    // exactly where the band saturates, and at seams between two water cells
    // whose scan winners differed (owner report 2026-08-29, screenshots).
    // The ground alias killed the whole branch: a flooded cell's banks are
    // simply the land its climate says, blended like any land↔land pair.
    const Biome b00 = nbBiome[ay.i0 * 3 + ax.i0];
    const Biome b10 = nbBiome[ay.i0 * 3 + ax.i1];
    const Biome b01 = nbBiome[ay.i1 * 3 + ax.i0];
    const Biome b11 = nbBiome[ay.i1 * 3 + ax.i1];
    if (b00 == b10 && b00 == b01 && b00 == b11) return b00;

    const float fx = ax.f, fy = ay.f;
    const Biome cand[4] = {b00, b10, b01, b11};
    const float w[4] = {(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                        (1.0f - fx) * fy,          fx * fy};
    const float r = tile_hash01(absX, absY);
    float acc = 0.0f;
    for (int i = 0; i < 4; ++i) {
        acc += w[i];
        if (r < acc) return cand[i];
    }
    // FP tail (the weights sum to ~1, r can graze it): the last corner.
    return b11;
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
    // `picked` comes from the ground ring, so it is never Water.
    const float t = treeline_t(hNorm);
    if (t >= 1.0f) return Biome::Mountain;
    const Biome below = (picked == Biome::Mountain) ? Biome::Meadow : picked;
    if (t <= 0.0f) return below;
    // Dither through the band with the same style of absolute-keyed hash the
    // seam dither uses — stone gains ground exactly as the trees thin.
    return treeline_is_rock(t, absX, absY) ? Biome::Mountain : below;
}

} // namespace sm::sub
