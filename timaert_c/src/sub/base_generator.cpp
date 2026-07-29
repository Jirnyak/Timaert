#include "sub/base_generator.h"
#include "core/rng.h"
#include "core/math.h"
#include <algorithm>
#include <cmath>

namespace sm::sub {

static const BiomeConfig kConfigs[11] = {
    /* Tundra   */ {0.018f, 6, 2, 3, 0.8f, WATER_LEVEL, false, false},
    /* Taiga    */ {0.20f,  3, 2, 4, 1.0f, WATER_LEVEL, false, false},
    /* Snow     */ {0.025f, 5, 2, 3, 0.9f, WATER_LEVEL, false, false},
    /* Valley   */ {0.050f, 4, 2, 3, 1.0f, WATER_LEVEL, false, false},
    /* Meadow   */ {0.035f, 4, 2, 3, 1.0f, WATER_LEVEL, false, false},
    /* Swamp    */ {0.080f, 3, 2, 4, 0.3f, WATER_LEVEL, true,  false},
    /* Desert   */ {0.004f, 8, 2, 3, 0.6f, WATER_LEVEL, false, true},
    /* Steppe   */ {0.018f, 5, 2, 3, 0.8f, WATER_LEVEL, false, false},
    /* Tropics  */ {0.25f,  2, 2, 5, 1.0f, WATER_LEVEL, false, false},
    /* Water    */ {0.0f,   16,2, 3, 0.5f, WATER_LEVEL, false, false},
    /* Mountain */ {0.02f,  6, 2, 3, 1.0f, WATER_LEVEL, false, false},
};
const BiomeConfig& biome_config(Biome b) {
    const int i = int(b);
    return kConfigs[(i >= 0 && i < int(sizeof(kConfigs) / sizeof(kConfigs[0]))) ? i : 0];
}

// Per-feature subworld height amplifier removed — TS-faithful generator
// derives mountain influence from biome+feature directly.

// TS-faithful integer hash noise (mirrors `terrainNoise` in
// subworld/base-generator.ts). Used by smooth_noise_ts for biome dune /
// swamp layers so terrain matches TS reference shape.
static float terrain_noise_ts(int x, int y, std::uint32_t seed) {
    std::uint32_t v = std::uint32_t(x * 374761393) ^ std::uint32_t(y * 668265263)
                    ^ (seed * 2246822519u);
    v = (v ^ (v >> 13)) * 1274126177u;
    v ^= v >> 16;
    return float(v) / 4294967295.0f;
}

// Single-octave smoothstep value noise on the TS hash. Mirrors
// `smoothTerrainNoise` from base-generator.ts.
static float smooth_noise_ts(float x, float y, std::uint32_t seed) {
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float n00 = terrain_noise_ts(ix,     iy,     seed);
    float n10 = terrain_noise_ts(ix + 1, iy,     seed);
    float n01 = terrain_noise_ts(ix,     iy + 1, seed);
    float n11 = terrain_noise_ts(ix + 1, iy + 1, seed);
    return n00 * (1 - sx) * (1 - sy)
         + n10 * sx * (1 - sy)
         + n01 * (1 - sx) * sy
         + n11 * sx * sy;
}

// Sea-level / water-plane constants live in base_generator.h as the
// single source of truth. We alias here for readability.
constexpr float kWaterLevel = WATER_LEVEL;
constexpr float kSeaLevel   = kMacroSeaLevel;
constexpr float kLandFloor  = WATER_LEVEL + kLandMargin;

// Domain-warped 2-octave ridged multifractal. Kept deliberately
// LOW-FREQUENCY (wavelengths ~250 and ~110 tiles) so mountains read as
// smooth coherent massifs. Higher-frequency ridge octaves (≥0.02) aliased
// badly on the 16-tile-spaced terrain mesh, producing the "chaotic spiky
// peaks" the minimap never showed (the minimap low-passes the heightmap).
// Keeping the ridge content itself below the mesh Nyquist was necessary but
// NOT sufficient: the crest shaping function also matters. The classic ridged
// fold (1−|2s−1|)² has a slope discontinuity at every noise 0.5-crossing, and
// that corner synthesises high-frequency harmonics of the (low-frequency) base
// field that alias back onto the mesh. We use a C1 smooth crest instead — see
// the octave loop below — which is what actually made the 3D relief match the
// smooth shaded relief on the map.
//   The ridge ceiling is blended from the same 3×3 macro context as the base
//   terrain.  Peaks may rise slightly above 1.0, but a soft compression avoids
//   both the old over-tall walls and the later flat 1.0 plateau.
static float soft_compress_peak(float h) {
    if (h <= 1.0f) return h;
    const float excess = h - 1.0f;
    return 1.0f + 0.20f * (1.0f - std::exp(-excess / 0.20f));
}

static float apply_mountain_ridges(float h, int gx, int gy, float macroH,
                                   float peakTarget, float rw) {
    if (rw <= 0.01f) return h;
    constexpr std::uint32_t kRidgeSeed = 0xD37A115u;
    const float wx = float(gx)
        + (smooth_noise_ts(float(gx) * 0.002f + 71.7f,
                           float(gy) * 0.002f, kRidgeSeed) - 0.5f) * 90.0f;
    const float wy = float(gy)
        + (smooth_noise_ts(float(gx) * 0.002f,
                           float(gy) * 0.002f + 31.1f, kRidgeSeed) - 0.5f) * 90.0f;
    constexpr float kFreqs[2] = {0.004f, 0.009f};
    float ridge = 0.0f, amp = 1.0f, wt = 1.0f;
    for (int o = 0; o < 2; ++o) {
        float sig = smooth_noise_ts(wx * kFreqs[o], wy * kFreqs[o], kRidgeSeed);
        // Smooth crest 4·s·(1−s) instead of the classic ridged fold
        // (1−|2s−1|)²: both are 0→1→0 humps peaking on the ridge line, but the
        // |·| fold has a slope discontinuity (a C0 corner) at every 0.5-crossing
        // of the noise. That corner manufactures high-frequency harmonics of the
        // base ridge field — so even with the octaves themselves kept below the
        // mesh Nyquist, the corner's 3rd/4th harmonic folds back onto the
        // 16-tile terrain mesh and aliases into the pervasive "spiky peaks". The
        // parabola is C1 (a smooth maximum, zero derivative at the crest), so the
        // massif keeps its height and prominence while the mesh-scale curvature
        // drops ~70 % (median 16.5 → 4.7 m of kink per 16 m quad, mean −58 %,
        // range preserved to <0.5 %). Measured across 5 seeds/heights.
        sig = 4.0f * sig * (1.0f - sig);
        sig = std::min(sig * wt, 1.0f);
        wt = std::min(1.0f, sig * 1.6f);
        ridge += sig * amp;
        amp  *= 0.5f;
    }
    ridge = std::min(1.0f, ridge * 0.78f);
    // Valley floor must track the surrounding macro altitude, not collapse
    // to half of it. The original `macroH * 0.5f` produced a 400+ m trench
    // around mountain features whenever neighbour land cells sat above
    // ~0.55 macroH (their land-remap put them well above the mountain's
    // off-ridge floor). 0.92 keeps mountain valleys gently lower than the
    // surrounding plain (≈ 8 % drop) so ridges still rise visibly above
    // the basin without creating a moat at the foot of the wall.
    const float valleyFloor = std::max(kWaterLevel + 0.08f, macroH * 0.90f);
    const float peak        = std::max(valleyFloor + 0.05f, peakTarget);
    const float mtnH        = soft_compress_peak(valleyFloor + ridge * (peak - valleyFloor));
    const float blend       = std::min(1.0f, rw * 1.5f);
    return h * (1.0f - blend) + mtnH * blend;
}

void generate_heightmap(std::vector<float>& out, int cellSize,
                        const float nbHeights[9],
                        const Biome nbBiome[9],
                        Biome biome, std::uint32_t seed,
                        int globalOffsetX, int globalOffsetY) {
    out.assign(std::size_t(cellSize) * cellSize, 0.0f);
    const float invCS = 1.0f / float(cellSize);

    // ── Per-cell traits (TS parity) ──
    // Mountain influence: 0.3 in mountain cells, 0.1 + 0.15·adjMtn elsewhere.
    // Ridge weight: 1 in mountain cells, 0 elsewhere — drives apply_mountain_ridges.
    // Macro gradient: max 4-conn |Δh| in macro space — boosts relief at biome
    // boundaries (steep shores get rougher noise than flat plains).
    // Height scales / dune / swamp flags: from per-neighbour BiomeConfig.
    // Remapped macro height: water cells → squared deep-ocean curve, land
    // cells → linear lift. Bilinear blend of the remapped values produces
    // the smooth heightmap manifold (natural shorelines, river banks for
    // a single-cell water tile, plain → foothill → peak gradients).
    float mountainScale[9];
    float ridgeWeight[9];
    float macroGradient[9];
    float heightScale[9];
    float duneFactor[9];
    float swampFactor[9];
    float remapped[9];
    float peakHeight[9];
    bool needsDune = false, needsSwamp = false;
    // Land remap: shoreline (mh = kSeaLevel) maps to kLandFloor (= WATER_LEVEL
    // + kLandMargin), peak (mh = 1) maps to 1. Compress the upper end so
    // mountain ceiling stays at 1.0 — `apply_mountain_ridges` adds the
    // extra rise above 1.0 from its own per-cell `peakHeight`.
    const float landScale = (1.0f - kLandFloor) / (1.0f - kSeaLevel);

    for (int i = 0; i < 9; ++i) {
        // Mountains are a biome now (elevation-classified), so ridge/peak
        // amplification keys off the neighbour biome, not a feature byte.
        const bool isMtn = nbBiome[i] == Biome::Mountain;
        int adjMtn = 0;
        const int cx = i % 3, cy = i / 3;
        if (cx > 0 && nbBiome[i - 1] == Biome::Mountain) ++adjMtn;
        if (cx < 2 && nbBiome[i + 1] == Biome::Mountain) ++adjMtn;
        if (cy > 0 && nbBiome[i - 3] == Biome::Mountain) ++adjMtn;
        if (cy < 2 && nbBiome[i + 3] == Biome::Mountain) ++adjMtn;
        mountainScale[i] = isMtn ? 0.3f : (0.1f + adjMtn * 0.15f);
        ridgeWeight  [i] = isMtn ? 1.0f : 0.0f;

        float maxDiff = 0.0f;
        const float mh = nbHeights[i];
        if (cx > 0) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i - 1]));
        if (cx < 2) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i + 1]));
        if (cy > 0) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i - 3]));
        if (cy < 2) maxDiff = std::max(maxDiff, std::fabs(mh - nbHeights[i + 3]));
        macroGradient[i] = maxDiff;

        const auto& bc = biome_config(nbBiome[i]);
        heightScale[i] = bc.heightScale;
        duneFactor [i] = bc.duneNoise  ? 1.0f : 0.0f;
        swampFactor[i] = bc.swampPools ? 1.0f : 0.0f;
        if (bc.duneNoise)  needsDune  = true;
        if (bc.swampPools) needsSwamp = true;

        if (nbBiome[i] == Biome::Water) {
            // Water cell: t=1 at shoreline (mh=seaLevel), t=0 at deep ocean.
            // Squared curve drops the bed quickly so deep water sits well
            // below WATER_LEVEL → global water plane covers it cleanly.
            const float t = std::max(0.0f, mh / kSeaLevel);
            remapped[i] = t * t * kWaterLevel;
        } else {
            // Land cell: lift macroHeight from kLandFloor (at shoreline)
            // up to 1.0 (at peak) so even the just-above-sea land cell
            // sits safely above the water plane after bilinear blend.
            remapped[i] = kLandFloor + (mh - kSeaLevel) * landScale;
        }

        const int cellGX = globalOffsetX / cellSize + cx;
        const int cellGY = globalOffsetY / cellSize + cy;
        const float jitter = terrain_noise_ts(cellGX, cellGY, seed ^ 0x5A17u) - 0.5f;
        if (isMtn) {
            peakHeight[i] = std::clamp(0.86f + mh * 0.20f + adjMtn * 0.025f
                                      + jitter * 0.06f, 0.85f, 1.18f);
        } else {
            peakHeight[i] = std::clamp(remapped[i] + 0.07f + adjMtn * 0.015f
                                      + jitter * 0.03f, kWaterLevel + 0.10f, 1.05f);
        }
    }

    for (int y = 0; y < cellSize; ++y) {
        // Bilinear sample weights: u,v ∈ [0,1] over the centre cell map
        // to gx,gy ∈ [0.5..1.5] in 3×3 grid space (cell centres at 0.5,
        // 1.5, 2.5). Same convention as TS.
        const float v   = float(y) * invCS;
        const float gy  = v + 1.0f;
        const int   y0  = std::clamp(int(std::floor(gy - 0.5f)), 0, 2);
        const int   y1  = std::min(2, y0 + 1);
        const float fy  = std::clamp((gy - 0.5f) - float(y0), 0.0f, 1.0f);
        for (int x = 0; x < cellSize; ++x) {
            const float u  = float(x) * invCS;
            const float gx = u + 1.0f;
            const int   x0 = std::clamp(int(std::floor(gx - 0.5f)), 0, 2);
            const int   x1 = std::min(2, x0 + 1);
            const float fx = std::clamp((gx - 0.5f) - float(x0), 0.0f, 1.0f);

            const float w00 = (1 - fx) * (1 - fy);
            const float w10 = fx       * (1 - fy);
            const float w01 = (1 - fx) * fy;
            const float w11 = fx       * fy;

            auto blend = [&](const float* tbl) {
                return tbl[y0 * 3 + x0] * w00 + tbl[y0 * 3 + x1] * w10
                     + tbl[y1 * 3 + x0] * w01 + tbl[y1 * 3 + x1] * w11;
            };

            float macroH = blend(remapped);
            const float localHS  = blend(heightScale);
            const float localGrd = blend(macroGradient);
            const float localMtn  = blend(mountainScale);
            const float localPeak = blend(peakHeight);
            const float rw        = blend(ridgeWeight);

            const float sf = needsSwamp ? blend(swampFactor) : 0.0f;
            if (sf > 0.01f) {
                // Per-pixel swamp flatten BEFORE noise so neighbour heights
                // don't lift swamp rims into a crater wall.
                const float swampTarget = kWaterLevel + 0.06f;
                macroH += (swampTarget - macroH) * sf * 0.85f;
            }

            // Multi-octave terrain noise in global tile coords with a fixed
            // world seed → continuous across cell boundaries.
            const int gxi = globalOffsetX + x;
            const int gyi = globalOffsetY + y;
            constexpr std::uint32_t kDetailSeed = 0xD37A115u;
            // Detail octaves must stay above the 3D mesh Nyquist (~32-tile
            // wavelength on the 16-tile-spaced terrain mesh). The old 0.06
            // octave (~16-tile wavelength) sat *at* Nyquist and aliased into
            // the "chaotic spiky peaks" in 3D that the low-passing minimap
            // never showed — worst on mountain foothills where mountainScale
            // amplifies it. Dropping it makes the 3D relief match the smooth
            // shaded relief on the map.
            float noise = 0.0f;
            noise += smooth_noise_ts(float(gxi) * 0.008f, float(gyi) * 0.008f, kDetailSeed) * 0.5f;
            noise += smooth_noise_ts(float(gxi) * 0.02f,  float(gyi) * 0.02f,  kDetailSeed) * 0.25f;
            noise = std::clamp(noise / 0.75f, 0.0f, 1.0f);

            // Smooth manifold: relief = macroH² + gradient. Macro height
            // squared concentrates noise on hills/peaks and keeps lowlands
            // calm; gradient lifts noise at biome edges so transitions
            // look natural.
            const float relief = macroH * macroH + localGrd;
            float h = macroH + (noise - 0.5f) * relief * localHS * localMtn;

            if (rw > 0.0f) {
                h = apply_mountain_ridges(h, gxi, gyi, macroH, localPeak, rw);
            }

            if (needsDune) {
                const float duneF = blend(duneFactor);
                if (duneF > 0.01f) {
                    h += (smooth_noise_ts(float(gxi) * 0.012f,
                                          float(gyi) * 0.018f, kDetailSeed) - 0.5f)
                       * 0.15f * duneF;
                }
            }

            if (sf > 0.01f) {
                const float lowland = smooth_noise_ts(
                    float(gxi) * 0.006f + 200.0f,
                    float(gyi) * 0.006f + 200.0f, kDetailSeed);
                const float bog = smooth_noise_ts(
                    float(gxi) * 0.025f, float(gyi) * 0.025f, kDetailSeed);
                const float dip = (1.0f - lowland) * 0.05f
                                + (1.0f - bog) * 0.04f;
                h -= dip * sf;
            }

            // Clamp broad for safety; mountain ridge output itself is kept
            // near the TS 0..1 relief range, while water/swamp/plain logic
            // remains on the same smooth manifold.
            out[std::size_t(y) * cellSize + x] = std::clamp(h, 0.0f, 2.0f);
        }
    }
    (void)biome;
}

void fill_base_tiles(std::vector<std::uint8_t>& tiles, int cellSize,
                     Biome biome, std::uint32_t seed) {
    tiles.assign(std::size_t(cellSize) * cellSize, std::uint8_t(TILE_GRASS));
    const auto& cfg = biome_config(biome);
    if (biome == Biome::Water) {
        std::fill(tiles.begin(), tiles.end(), std::uint8_t(TILE_WATER));
        return;
    }
    Rng r(seed);
    for (int y = 0; y < cellSize; y += cfg.treeStep) {
        for (int x = 0; x < cellSize; x += cfg.treeStep) {
            if (r.next_f01() < cfg.treeDensity) {
                int ix = x + int(r.next_u32() % std::uint32_t(cfg.treeStep));
                int iy = y + int(r.next_u32() % std::uint32_t(cfg.treeStep));
                if (ix < cellSize && iy < cellSize)
                    tiles[std::size_t(iy) * cellSize + ix] = TILE_TREE_DECOR;
            }
        }
    }
}

void scatter_universal_trees(SubworldMapData& out,
                             int cellSize,
                             int globalOffsetX, int globalOffsetY,
                             Biome biome,
                             bool forestBoost,
                             int clearRadius,
                             std::uint32_t seed) {
    if (biome == Biome::Water) return;
    const auto& cfg = biome_config(biome);
    const float baseDensity = forestBoost
        ? std::max(cfg.treeDensity, 0.16f)
        : cfg.treeDensity;
    if (baseDensity <= 0.0f) return;

    const int step = forestBoost
        ? std::max(2, cfg.treeStep - 1)
        : cfg.treeStep;
    const int gox = globalOffsetX, goy = globalOffsetY;

    // Align scan start to the nearest greater multiple of `step` so adjacent
    // cells use the same world-aligned grid (no seams).
    auto align_start = [&](int g) {
        int rem = ((g % step) + step) % step;
        return g + ((step - rem) % step);
    };
    const int gStartX = align_start(gox);
    const int gStartY = align_start(goy);
    const int gEndX   = gox + cellSize;
    const int gEndY   = goy + cellSize;

    const int cx = cellSize / 2, cy = cellSize / 2;
    const int clearSq = clearRadius * clearRadius;

    Rng sizeRng(seed ^ 0xA17EE5u);

    for (int gy = gStartY; gy < gEndY; gy += step) {
        for (int gx = gStartX; gx < gEndX; gx += step) {
            const int x = gx - gox;
            const int y = gy - goy;
            if (x < 0 || y < 0 || x >= cellSize || y >= cellSize) continue;
            const std::size_t idx = std::size_t(y) * cellSize + x;
            const std::uint8_t tile = out.tiles[idx];
            // Only place on empty / biome ground tiles. Keep walls, roads,
            // houses, water, etc. untouched.
            if (tile == TILE_ROAD || tile == TILE_HOUSE || tile == TILE_WALL
             || tile == TILE_FIELD || tile == TILE_WATER || tile == TILE_SHORE
             || tile == TILE_SQUARE || tile == TILE_TREE_DECOR) continue;

            // Suppress urban centre.
            if (clearRadius > 0) {
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy < clearSq) continue;
            }

            // FBM cluster gate (global coords, seamless across cells).
            const float n1 = smooth_noise_ts(float(gx) * 0.015f,
                                             float(gy) * 0.015f, seed);
            const float n2 = smooth_noise_ts(float(gx) * 0.04f,
                                             float(gy) * 0.04f, seed + 7u);
            const float fbm = n1 * 0.7f + n2 * 0.3f;
            const float ns = std::clamp((fbm - 0.2f) / 0.5f, 0.0f, 1.0f);
            float density = baseDensity * ns;

            // Position-based hash for placement decision (deterministic per
            // global tile so adjacent cells produce identical trees).
            const float posHash = terrain_noise_ts(gx * 7 + 12345,
                                                   gy * 13 + 67890, seed);
            if (posHash >= density) continue;

            // Smooth alpine treeline. Heightmap is normalised against the
            // 1500 m kHeightScale used by the 3D renderer; trees thin out
            // from h=0.80 (1200 m) and stop at h=1.33 (2000 m). Sampled
            // from the cell heightmap if available so the cap follows the
            // actual rendered relief.
            if (!out.heightmap.empty()) {
                const float hNorm = out.heightmap[idx];
                const float t = std::clamp((hNorm - 0.80f) / (1.33f - 0.80f),
                                           0.0f, 1.0f);
                const float treelineSurvive = 1.0f - t * t * (3.0f - 2.0f * t);
                if (treelineSurvive < 1e-3f) continue;
                // Per-tile reroll against the smoothstep survival prob.
                const float survHash = terrain_noise_ts(gx * 31 + 99991,
                                                        gy * 37 + 88883, seed);
                if (survHash > treelineSurvive) continue;
            }

            const float treeR = cfg.treeMinSize
                + sizeRng.next_f01() * (cfg.treeMaxSize - cfg.treeMinSize);
            // Trees scaled against the C++ kHeightScale=1500 m peak
            // ceiling. 22-44 m crown reads at roughly 1/40 of a peak —
            // matches mature mixed-conifer / hardwood scale (typical
            // 25-40 m, occasional taller specimens) without dwarfing
            // the surrounding relief.
            const float treeH = 22.0f + sizeRng.next_f01() * 22.0f;

            Structure s{};
            s.kind   = Structure::Tree;
            s.x      = float(x) + 0.5f;
            s.y      = float(y) + 0.5f;
            s.radius = treeR;
            s.height = treeH;
            out.structures.push_back(s);
            out.tiles[idx] = TILE_TREE_DECOR;
        }
    }
}

// Subworld road flattening — make road / square tiles look like flat planar
// pieces laid on the terrain, locally curvature-free but still tracking the
// large-scale relief slope. Pipeline:
//
//   1. Each road tile takes a wide 9×9 (r=4) box average from a snapshot of
//      the heightmap so neighbouring road tiles do not feed back into each
//      other within the same pass. This kills the high-frequency bumps the
//      generator stamped through the road corridor.
//
//   2. 12 iterations of pure Laplacian smoothing — `h = (h±1 + h±W) / 4` —
//      restricted to road / square tiles. Discrete Laplacian smoothing
//      converges to a harmonic function: zero local curvature → road tiles
//      become essentially planar over their connected corridor while still
//      following the long-wavelength slope. This is what "flat tiles laid
//      on the relief" reduces to mathematically.
//
//   3. Single shoulder pass — one ring of non-road neighbours is pulled 35 %
//      toward the average of its road-tile neighbours so the road edge does
//      not produce a visible cliff against the surrounding terrain.
//
// Smoothing is sparse: callers provide sorted road/square indices so the
// 3072x3072 composite does not need a full tile-grid scan on worker jobs.
void smooth_road_heights_indexed(std::vector<float>& hm,
                                 const std::vector<std::int32_t>& roadIdx,
                                 int width, int height) {
    if (hm.size() != std::size_t(width) * std::size_t(height)) return;
    if (roadIdx.empty()) return;

    // Pass 1: wide 25x25 (r=12) box average over road / square tiles,
    // sourced from a snapshot. Use an integral image so async composite
    // smoothing stays O(map area + road tiles), not O(road tiles * r^2).
    {
        const std::vector<float> src(hm);
        const int stride = width + 1;
        std::vector<float> integral(std::size_t(stride) * std::size_t(height + 1), 0.0f);
        for (int y = 0; y < height; ++y) {
            float rowSum = 0.0f;
            const std::size_t srcRow = std::size_t(y) * width;
            const std::size_t prevRow = std::size_t(y) * stride;
            const std::size_t dstRow = std::size_t(y + 1) * stride;
            for (int x = 0; x < width; ++x) {
                rowSum += src[srcRow + std::size_t(x)];
                integral[dstRow + std::size_t(x + 1)] =
                    integral[prevRow + std::size_t(x + 1)] + rowSum;
            }
        }

        constexpr int r = 12;
        for (std::int32_t i : roadIdx) {
            const int x = i % width;
            const int y = i / width;
            const int y0 = std::max(0, y - r), y1 = std::min(height - 1, y + r);
            const int x0 = std::max(0, x - r), x1 = std::min(width  - 1, x + r);
            const std::size_t a = std::size_t(y0) * stride + std::size_t(x0);
            const std::size_t b = std::size_t(y0) * stride + std::size_t(x1 + 1);
            const std::size_t c = std::size_t(y1 + 1) * stride + std::size_t(x0);
            const std::size_t d = std::size_t(y1 + 1) * stride + std::size_t(x1 + 1);
            const float sum = integral[d] - integral[b] - integral[c] + integral[a];
            const int cnt = (x1 - x0 + 1) * (y1 - y0 + 1);
            hm[std::size_t(i)] = cnt > 0 ? sum / float(cnt) : 0.5f;
        }
    }

    // Pass 2: 80 iterations of pure Laplacian smoothing on road tiles
    // only. No centre weight so curvature collapses fastest; iteration
    // count is high enough that the surface converges to harmonic over
    // the whole connected road component, not just locally. End result:
    // each road tile is essentially a flat planar piece sitting on the
    // long-wavelength terrain slope.
    for (int it = 0; it < 80; ++it) {
        for (std::int32_t i : roadIdx) {
            const int x = i % width;
            const int y = i / width;
            if (x <= 0 || x >= width - 1 || y <= 0 || y >= height - 1) continue;
            hm[i] = (hm[i - 1] + hm[i + 1]
                   + hm[i - width] + hm[i + width]) * 0.25f;
        }
    }

    // Pass 3: shoulder. For every NON-road tile that touches at least one
    // road neighbour, pull its height 55 % toward the average of its
    // road-tile neighbours. Eliminates the visible cliff at the road edge.
    // Keep the accumulation sparse and deterministic: mark road membership in
    // one byte per tile, collect shoulder samples, then sort/group them.
    {
        static constexpr int dx8[8] = {-1, 0, 1,-1, 1,-1, 0, 1};
        static constexpr int dy8[8] = {-1,-1,-1, 0, 0, 1, 1, 1};
        struct ShoulderSample {
            std::int32_t idx;
            float roadH;
        };
        std::vector<std::uint8_t> roadMask(hm.size(), 0);
        for (std::int32_t i : roadIdx) {
            roadMask[std::size_t(i)] = 1;
        }
        std::vector<ShoulderSample> shoulder;
        shoulder.reserve(roadIdx.size() * 4);
        for (std::int32_t i : roadIdx) {
            const int x = i % width;
            const int y = i / width;
            const float roadH = hm[std::size_t(i)];
            for (int k = 0; k < 8; ++k) {
                const int xn = x + dx8[k], yn = y + dy8[k];
                if (xn <= 0 || xn >= width - 1 || yn <= 0 || yn >= height - 1) continue;
                const std::int32_t j = yn * width + xn;
                if (roadMask[std::size_t(j)] != 0) continue;
                shoulder.push_back(ShoulderSample{j, roadH});
            }
        }
        std::sort(shoulder.begin(), shoulder.end(),
            [](const ShoulderSample& a, const ShoulderSample& b) {
                return a.idx < b.idx;
            });
        std::size_t pos = 0;
        while (pos < shoulder.size()) {
            const std::int32_t idx = shoulder[pos].idx;
            float sum = 0.0f;
            int cnt = 0;
            do {
                sum += shoulder[pos].roadH;
                ++cnt;
                ++pos;
            } while (pos < shoulder.size() && shoulder[pos].idx == idx);

            const float roadAvg = sum / float(cnt);
            const float orig = hm[std::size_t(idx)];
            hm[std::size_t(idx)] = orig + (roadAvg - orig) * 0.55f;
        }
    }
}

void smooth_road_heights(std::vector<float>& hm,
                         const std::vector<std::uint8_t>& tiles,
                         int width, int height) {
    if (hm.size() != std::size_t(width) * std::size_t(height)) return;
    if (tiles.size() != hm.size()) return;

    std::vector<std::int32_t> roadIdx;
    roadIdx.reserve(tiles.size() / 64);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const std::uint8_t tile = tiles[i];
        if (tile == TILE_ROAD || tile == TILE_SQUARE) {
            roadIdx.push_back(std::int32_t(i));
        }
    }
    smooth_road_heights_indexed(hm, roadIdx, width, height);
}

} // namespace sm::sub
