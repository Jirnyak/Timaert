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
#include "core/rng.h"
#include "core/math.h"
#include <cmath>

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
static constexpr int   NOISE_BASE_CELLS      = 96;
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
                         const std::uint8_t* waterMaskA) {
    const int total = width * height;

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
    for (int i = 0; i < total; ++i) {
        const auto f = FeatureType(features.data[std::size_t(i)]);
        const float s = (f == FT_Road)     ? CIV_ROAD_STRENGTH
                      : (f == FT_DirtRoad) ? CIV_DIRT_ROAD_STRENGTH
                                           : 0.0f;
        if (s > civPull[std::size_t(i)]) {
            civPull[std::size_t(i)] = s;
            queue.push_back(std::size_t(i));
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
    for (int i = 0; i < total; ++i) {
        if (FeatureType(features.data[std::size_t(i)]) != FT_Mountain) {
            mtnDepth[std::size_t(i)] = 0.0f;
            mq.push_back(std::size_t(i));
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
                if (FeatureType(features.data[j]) != FT_Mountain) continue;
                const float nd = d + ((dx == 0 || dy == 0) ? 1.0f : 1.41421356f);
                if (nd < mtnDepth[j] - 1e-4f) {
                    mtnDepth[j] = nd;
                    mq.push_back(j);
                }
            }
        }
    }

    // ── 3. Compose noise + boosts - civ pull ──────────────────
    ZoneLayer zl;
    zl.width = width; zl.height = height;
    zl.data .assign(std::size_t(total), 0);
    zl.field.assign(std::size_t(total), 0.0f);

    // Decorrelate fBM seed from generic world seed (matches TS xorshift32).
    Rng nrng{seed ^ 0x5A17E5u};
    const std::uint32_t noiseSeed = nrng.next_u32();

    const float mtnCap = MOUNTAIN_DEPTH_CAP - MOUNTAIN_BASE_BOOST;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(width) + std::size_t(x);
            float z = fbm_zone(float(x), float(y), width, height, noiseSeed);
            z -= civPull[i];

            const auto f = FeatureType(features.data[i]);
            if (f == FT_Mountain) {
                const float d = mtnDepth[i] >= kInf ? 0.0f : mtnDepth[i];
                z += MOUNTAIN_BASE_BOOST + std::min(mtnCap, d * MOUNTAIN_DEPTH_SCALE);
            } else if (f == FT_Tree) {
                z += FOREST_BOOST;
            }

            if (waterMaskA && waterMaskA[i * 4 + 3] < 128) {
                z += WATER_BOOST;
            }

            z = clamp01(z);
            zl.field[i] = z;
            int q = int(std::floor(z * float(kZoneCount)));
            if (q >= kZoneCount) q = kZoneCount - 1;
            if (q < 0) q = 0;
            zl.data[i] = std::uint8_t(q);
        }
    }

    return zl;
}

} // namespace sm
