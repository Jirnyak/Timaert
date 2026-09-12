#include "sub/material.h"

#include <array>
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

GroundCorners ground_corners(const Biome nbBiome[9],
                             const GroundAxis& ax, const GroundAxis& ay) {
    GroundCorners c{};
    c.b00 = nbBiome[ay.i0 * 3 + ax.i0];
    c.b10 = nbBiome[ay.i0 * 3 + ax.i1];
    c.b01 = nbBiome[ay.i1 * 3 + ax.i0];
    c.b11 = nbBiome[ay.i1 * 3 + ax.i1];
    c.uniform = c.b00 == c.b10 && c.b00 == c.b01 && c.b00 == c.b11;
    return c;
}

const std::uint8_t* biome_ground_materials() {
    // Built from terrain_material_for itself, with a tile the authored branch
    // ignores — so this is that door's own answer, tabulated, and it cannot
    // drift from it.
    static const std::array<std::uint8_t, 11> table = [] {
        std::array<std::uint8_t, 11> t{};
        for (int i = 0; i < 11; ++i)
            t[std::size_t(i)] = static_cast<std::uint8_t>(
                terrain_material_for(TILE_EMPTY, static_cast<Biome>(i)));
        return t;
    }();
    return table.data();
}

Biome pick_ground_biome_axis(const Biome nbBiome[9],
                             const GroundAxis& ax, const GroundAxis& ay,
                             long long absX, long long absY) {
    // The one-shot form: stand a one-row walker up and answer through the
    // bulk body, so there is one body and not two. Its 32 divisions are paid
    // by the map preview and by tests, never by the million-tile fill.
    GroundDitherRow row;
    row.begin(absY);
    return pick_ground_biome_axis(nbBiome, ax, ay, row, absX);
}

// THE BULK BODY: the caller brings the boundary field walked along its row
// (material.h GroundDitherRow) and the tile's absolute x. The one-shot
// overload above is this one with a one-row walker stood up on the spot, so
// the law has one body and the million-tile fill copies nothing.
Biome pick_ground_biome_axis(const Biome nbBiome[9],
                             const GroundAxis& ax, const GroundAxis& ay,
                             GroundDitherRow& row, long long absX) {
    return pick_ground_biome_corners(ground_corners(nbBiome, ax, ay),
                                     ax.f, ay.f, row, absX);
}

Biome pick_ground_biome_corners(const GroundCorners& c, float fx, float fy,
                                GroundDitherRow& row, long long absX) {
    // Deep inside the cell both ramps saturate → the single corner is the
    // owner; skip the field entirely (the common case, and the reason the
    // field is drawn from the ROW rather than handed in: handing it in made
    // this early-out pay for a field it never uses, and doubled the fill).
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
    if (c.uniform) return c.b00;

    const Biome cand[4] = {c.b00, c.b10, c.b01, c.b11};
    const float w[4] = {(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                        (1.0f - fx) * fy,          fx * fy};
    // THE ground-boundary law (material.h GroundDitherRow): the same field
    // the treeline consults, so a boundary is one act with one answer — and
    // drawn HERE, after the early-out above, never before it.
    const float r = row.at(absX);
    float acc = 0.0f;
    for (int i = 0; i < 4; ++i) {
        acc += w[i];
        if (r < acc) return cand[i];
    }
    // FP tail (the weights sum to ~1, r can graze it): the last corner.
    return c.b11;
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
    // Draw the field here, answer through the row form: one body.
    return apply_mountain_treeline_at(
        picked, hNorm, ground_dither01(absX + 9973, absY - 7919));
}

Biome apply_mountain_treeline_row(Biome picked, float hNorm,
                                  GroundDitherRow& row, long long absX) {
    // The band test first, the field only inside it — the same laziness the
    // coin had, kept where the law lives instead of in the caller.
    const float t = treeline_t(hNorm);
    if (t >= 1.0f) return Biome::Mountain;
    const Biome below = (picked == Biome::Mountain) ? Biome::Meadow : picked;
    if (t <= 0.0f) return below;
    return treeline_is_rock_at(t, row.at(absX + 9973)) ? Biome::Mountain
                                                       : below;
}

Biome apply_mountain_treeline_at(Biome picked, float hNorm, float dither) {
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
    return treeline_is_rock_at(t, dither) ? Biome::Mountain : below;
}

} // namespace sm::sub
