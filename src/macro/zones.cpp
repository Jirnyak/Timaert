// Faithful port of `src/game/zones.ts` — danger heightmap.
//
// Three additive influences composed per cell:
//   danger = clamp01( fbm(x,y) - civPull(x,y) + mountainBoost(x,y) [+ waterBoost] [+ forestBoost] )
//
// Civ pull: BFS "max-strength wins" from cities (1.10), villages (0.55),
// roads (0.35), dirt-roads (0.22). Decay 0.06 / step (0.085 diagonal).
//
// Mountain boost: BFS depth = distance into mountain mass from non-mountain
// edge (1 ortho / 1.414 diag). Boost = 0.08 + min(0.45 - 0.08, depth*0.04).
//
// fBM: 5-octave value noise wavelength 96 cells, smoothstep, toroidally
// wrapped per octave (no seam at world edges).

#include "macro/zones.h"
#include "macro/biomes.h"
#include "macro/tree_layer.h"
#include "core/rng.h"
#include "core/math.h"
#include <cmath>
#include <limits>

namespace sm {

static constexpr float CIV_DECAY_PER_STEP    = 0.06f;
static constexpr float CIV_CITY_STRENGTH     = 1.10f;
static constexpr float CIV_VILLAGE_STRENGTH  = 0.55f;
static constexpr float CIV_ROAD_STRENGTH     = 0.35f;
static constexpr float CIV_DIRT_ROAD_STRENGTH= 0.22f;
static constexpr float MOUNTAIN_BASE_BOOST   = 0.08f;
static constexpr float MOUNTAIN_DEPTH_SCALE  = 0.04f;
static constexpr float MOUNTAIN_DEPTH_CAP    = 0.45f;
static constexpr float WATER_BOOST           = 0.05f;
static constexpr float FOREST_BOOST          = 0.04f;
// The noise lattice must close on the world, and it closes only when the
// period DIVIDES the world's width. The world is 1024 = 2^10 cells (CANON.md
// S1), so the period has to be a power of two; 96 is not, and none of the five
// octaves closed — the base octave wrapped at 1056 cells, the next at 1008, and
// so on. Measured on the shipped field: the jump across the seam was 10.9× a
// normal neighbouring step, and the quantised DANGER BAND changed on 39.9 % of
// the seam's rows against 3.7 % anywhere else. A player crossing x = 0 walked
// out of a safe haven into a war zone for no reason in the world.
// 128 is the nearest power of two to the old value: every octave (128, 64, 32,
// 16, 8) now divides 1024, and the same measurement puts the seam at 0.17× a
// normal step — which is to say there is no seam.
static constexpr int   NOISE_BASE_CELLS      = 128;
static constexpr int   NOISE_OCTAVES         = 5;

namespace {

inline float vhash(int ix, int iy, std::uint32_t salt) {
    return float(hash3(std::uint32_t(ix), std::uint32_t(iy), salt) & 0xffffffu)
         / float(0xffffffu);
}

// Toroidal value noise — period in cells (matches TS `valueNoise`).
inline float value_noise_wrap(float x, float y, float period,
                              int width, int height, std::uint32_t salt) {
    const float sx = x / period;
    const float sy = y / period;
    const int x0 = int(std::floor(sx));
    const int y0 = int(std::floor(sy));
    const float fx = smoothstep01(sx - float(x0));
    const float fy = smoothstep01(sy - float(y0));
    const int px = std::max(1, int(std::lround(float(width)  / period)));
    const int py = std::max(1, int(std::lround(float(height) / period)));
    auto wrap = [](int k, int m) { return ((k % m) + m) % m; };
    const float a = vhash(wrap(x0,     px), wrap(y0,     py), salt);
    const float b = vhash(wrap(x0 + 1, px), wrap(y0,     py), salt);
    const float c = vhash(wrap(x0,     px), wrap(y0 + 1, py), salt);
    const float d = vhash(wrap(x0 + 1, px), wrap(y0 + 1, py), salt);
    return lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
}

inline float fbm_zone(float x, float y, int width, int height, std::uint32_t seed) {
    float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int o = 0; o < NOISE_OCTAVES; ++o) {
        const float period = float(NOISE_BASE_CELLS) / freq;
        const std::uint32_t salt = seed ^ std::uint32_t(0x91E5u + o * 7919u);
        sum  += value_noise_wrap(x, y, period, width, height, salt) * amp;
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

} // namespace

ZoneLayer generate_zones(int width, int height, std::uint32_t seed,
                         const std::vector<ZoneSeed>& cities,
                         const std::vector<ZoneSeed>& villages,
                         const FeatureLayer& features,
                         const std::uint8_t* waterMaskA,
                         std::size_t waterMaskByteCount,
                         const TreeLayer* treeLayer,
                         std::vector<float>* continuousOut) {
    ZoneLayer zl;
    std::size_t total = 0;
    if (!FeatureLayer::cell_count_for(width, height, total))
        return zl;
    const std::size_t requiredWaterBytes =
        total > std::numeric_limits<std::size_t>::max() / 4u
            ? std::numeric_limits<std::size_t>::max()
            : total * 4u;
    const bool hasWaterMask = waterMaskA
        && requiredWaterBytes != std::numeric_limits<std::size_t>::max()
        && waterMaskByteCount >= requiredWaterBytes;

    const std::uint8_t *featureData =
        features.covers(width, height) ? features.data.data() : nullptr;
    auto feature_at = [&](std::size_t i) -> FeatureType {
        return featureData ? FeatureLayer::decode(featureData[i]) : FT_None;
    };

    // Mountains are the Mountain biome (elevation-classified, see biomes.h),
    // not a feature. Detect them from the terrain height (red channel) on land
    // cells — water is never a mountain regardless of height.
    auto is_mountain = [&](std::size_t i) -> bool {
        if (!hasWaterMask) return false;
        if (waterMaskA[i * 4 + 3] < 128) return false;
        return float(waterMaskA[i * 4 + 0]) / 255.0f >= kMountainBiomeLevel;
    };

    auto idx = [&](int x, int y) -> std::size_t {
        const int wx = ((x % width) + width) % width;
        const int wy = ((y % height) + height) % height;
        return std::size_t(wy) * std::size_t(width) + std::size_t(wx);
    };

    // ── 1. Civilization pull (BFS, max-strength-wins) ─────────
    std::vector<float> civPull(std::size_t(total), 0.0f);
    std::vector<std::size_t> queue;
    queue.reserve(std::size_t(total));

    auto seed_civ = [&](int x, int y, float strength) {
        const std::size_t i = idx(x, y);
        if (strength > civPull[i]) { civPull[i] = strength; queue.push_back(i); }
    };
    for (const auto& c : cities)   seed_civ(c.x, c.y, CIV_CITY_STRENGTH);
    for (const auto& v : villages) seed_civ(v.x, v.y, CIV_VILLAGE_STRENGTH);
    for (std::size_t i = 0; i < total; ++i) {
        const auto f = feature_at(i);
        const float s = (f == FT_Road)     ? CIV_ROAD_STRENGTH
                      : (f == FT_DirtRoad) ? CIV_DIRT_ROAD_STRENGTH
                                           : 0.0f;
        if (s > civPull[i]) {
            civPull[i] = s;
            queue.push_back(i);
        }
    }

    const float diagDecay = CIV_DECAY_PER_STEP * 1.41421356f;
    std::size_t head = 0;
    while (head < queue.size()) {
        const std::size_t i = queue[head++];
        const int x = int(i % std::size_t(width));
        const int y = int(i / std::size_t(width));
        const float v = civPull[i];
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const float decay = (dx == 0 || dy == 0) ? CIV_DECAY_PER_STEP : diagDecay;
                const float nv = v - decay;
                if (nv <= 0.0f) continue;
                const std::size_t j = idx(x + dx, y + dy);
                if (nv > civPull[j] + 1e-4f) {
                    civPull[j] = nv;
                    queue.push_back(j);
                }
            }
        }
    }

    // ── 2. Mountain depth BFS (only into mountain cells) ──────
    constexpr float kInf = 1e9f;
    std::vector<float> mtnDepth(std::size_t(total), kInf);
    std::vector<std::size_t> mq;
    mq.reserve(std::size_t(total));
    for (std::size_t i = 0; i < total; ++i) {
        if (!is_mountain(i)) {
            mtnDepth[i] = 0.0f;
            mq.push_back(i);
        }
    }
    head = 0;
    while (head < mq.size()) {
        const std::size_t i = mq[head++];
        const int x = int(i % std::size_t(width));
        const int y = int(i / std::size_t(width));
        const float d = mtnDepth[i];
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const std::size_t j = idx(x + dx, y + dy);
                if (!is_mountain(j)) continue;
                const float nd = d + ((dx == 0 || dy == 0) ? 1.0f : 1.41421356f);
                if (nd < mtnDepth[j] - 1e-4f) {
                    mtnDepth[j] = nd;
                    mq.push_back(j);
                }
            }
        }
    }

    // ── 3. Compose noise + boosts - civ pull ──────────────────
    zl.width = width; zl.height = height;
    zl.data .assign(total, 0);
    if (continuousOut) continuousOut->assign(total, 0.0f);

    // Decorrelate fBM seed from generic world seed (matches TS xorshift32).
    Rng nrng{seed ^ 0x5A17E5u};
    const std::uint32_t noiseSeed = nrng.next_u32();

    const float mtnCap = MOUNTAIN_DEPTH_CAP - MOUNTAIN_BASE_BOOST;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(width) + std::size_t(x);
            float z = fbm_zone(float(x), float(y), width, height, noiseSeed);
            z -= civPull[i];

            if (is_mountain(i)) {
                const float d = mtnDepth[i] >= kInf ? 0.0f : mtnDepth[i];
                z += MOUNTAIN_BASE_BOOST + std::min(mtnCap, d * MOUNTAIN_DEPTH_SCALE);
            } else if (treeLayer && treeLayer->has_complete_storage()
                       && i < treeLayer->data.size()) {
                // Forest danger scales with the actual tree count: a deep
                // massif carries the full old FT_Tree boost, thin ambience
                // almost none.
                z += FOREST_BOOST * (float(treeLayer->data[i])
                                     / float(kMaxTreesPerCell));
            }

            if (hasWaterMask && waterMaskA[i * 4 + 3] < 128) {
                z += WATER_BOOST;
            }

            z = clamp01(z);
            if (continuousOut) (*continuousOut)[i] = z;
            int q = int(std::floor(z * float(kZoneCount)));
            if (q >= kZoneCount) q = kZoneCount - 1;
            if (q < 0) q = 0;
            zl.data[i] = std::uint8_t(q);
        }
    }

    return zl;
}

} // namespace sm
