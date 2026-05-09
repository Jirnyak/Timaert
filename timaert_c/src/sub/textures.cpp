// TS-faithful per-biome ground tile generators for the subworld atlas.
// Pixel layout matches subworld/textures.ts (TEX_SIZE=64): each biome has a
// dedicated 64×64 tile in a 64×N column atlas, sampled by `index_for(Biome)`.
//
// Visual parity is bit-faithful with TS: same per-pixel `texNoise` hash,
// same colour bases and modulation amplitudes, same `clamp255` semantics.

#include "sub/textures.h"
#include <cmath>
#include <cstdint>

namespace sm::sub {

namespace {

inline float tex_noise(int x, int y, std::uint32_t seed) {
    std::uint32_t v = (std::uint32_t(x) * 374761393u)
                    ^ (std::uint32_t(y) * 668265263u)
                    ^ (seed * 1274126177u);
    v = (v ^ (v >> 13)) * 2246822519u;
    v ^= v >> 16;
    return float(v) / 4294967295.0f;
}

inline std::uint8_t clamp255(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return std::uint8_t(std::lround(v));
}

inline void put(std::uint8_t* row, int x, float r, float g, float b, std::uint8_t a = 255) {
    std::uint8_t* px = row + x * 4;
    px[0] = clamp255(r);
    px[1] = clamp255(g);
    px[2] = clamp255(b);
    px[3] = a;
}

constexpr int N = TileAtlas::kTile;

void gen_grass(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 99) * 30.0f - 15.0f;
            bool blade = tex_noise(x * 5, y * 5, 100) > 0.7f;
            if (blade) put(row, x,  60.0f + n, 110.0f + n, 40.0f + n);
            else       put(row, x,  75.0f + n, 125.0f + n, 50.0f + n);
        }
    }
}

void gen_tundra(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 201) * 25.0f - 12.0f;
            bool moss = tex_noise(x * 3, y * 3, 202) > 0.6f;
            if (moss) put(row, x,  88.0f + n,  95.0f + n, 72.0f + n);
            else      put(row, x, 115.0f + n, 112.0f + n, 98.0f + n);
        }
    }
}

void gen_taiga(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 211) * 22.0f - 11.0f;
            bool needle = tex_noise(x * 4, y * 4, 212) > 0.65f;
            if (needle) put(row, x, 45.0f + n, 75.0f + n, 40.0f + n);
            else        put(row, x, 70.0f + n, 90.0f + n, 55.0f + n);
        }
    }
}

void gen_snow(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 221) * 12.0f - 6.0f;
            bool sparkle = tex_noise(x * 5, y * 5, 222) > 0.92f;
            float base = sparkle ? 245.0f : 228.0f;
            put(row, x, base + n, base + n - 2.0f, base + n + 2.0f);
        }
    }
}

void gen_valley(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 231) * 25.0f - 12.0f;
            bool dry = tex_noise(x * 2, y * 2, 232) > 0.5f;
            if (dry) put(row, x, 130.0f + n, 115.0f + n, 78.0f + n);
            else     put(row, x, 105.0f + n, 120.0f + n, 70.0f + n);
        }
    }
}

void gen_swamp(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 241) * 20.0f - 10.0f;
            bool mud = tex_noise(x * 3, y * 3, 242) > 0.55f;
            if (mud) put(row, x, 68.0f + n, 72.0f + n, 45.0f + n);
            else     put(row, x, 55.0f + n, 80.0f + n, 38.0f + n);
        }
    }
}

void gen_desert(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n  = tex_noise(x, y, 251) * 22.0f - 11.0f;
            float n2 = tex_noise(x * 2, y * 2, 252) * 10.0f - 5.0f;
            put(row, x, 195.0f + n + n2, 175.0f + n + n2, 130.0f + n);
        }
    }
}

void gen_steppe(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 261) * 25.0f - 12.0f;
            bool tuft = tex_noise(x * 4, y * 4, 262) > 0.7f;
            if (tuft) put(row, x, 140.0f + n, 128.0f + n, 75.0f + n);
            else      put(row, x, 160.0f + n, 145.0f + n, 88.0f + n);
        }
    }
}

void gen_tropics(std::uint8_t* out, int stride) {
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n = tex_noise(x, y, 271) * 28.0f - 14.0f;
            bool fern = tex_noise(x * 3, y * 3, 272) > 0.55f;
            if (fern) put(row, x, 30.0f + n, 105.0f + n, 28.0f + n);
            else      put(row, x, 55.0f + n, 130.0f + n, 45.0f + n);
        }
    }
}

void gen_water(std::uint8_t* out, int stride) {
    // The 3D scene has a translucent water plane at WATER_LEVEL covering
    // every submerged tile. Anything generated here is the *bed* under
    // that plane, so it must look like wet sand / silt — not blue. Keeps
    // the same colour family as gen_desert but darker / cooler so it
    // reads as damp shore when the water is shallow.
    for (int y = 0; y < N; ++y) {
        std::uint8_t* row = out + y * stride;
        for (int x = 0; x < N; ++x) {
            float n     = tex_noise(x, y, 281) * 22.0f - 11.0f;
            float pebble= tex_noise(x * 4, y * 4, 282) * 14.0f - 7.0f;
            bool   wet  = tex_noise(x * 2, y * 2, 283) > 0.55f;
            if (wet) put(row, x, 158.0f + n + pebble,
                                  140.0f + n + pebble,
                                  104.0f + n);
            else     put(row, x, 178.0f + n + pebble,
                                  158.0f + n + pebble,
                                  118.0f + n);
        }
    }
}

using GenFn = void (*)(std::uint8_t*, int);

// Atlas column index ↔ Biome enum (matches biomes.h):
//   0 Tundra, 1 Taiga, 2 Snow, 3 Valley, 4 Meadow, 5 Swamp,
//   6 Desert, 7 Steppe, 8 Tropics, 9 Water.
constexpr GenFn kGenerators[TileAtlas::kTileCount] = {
    gen_tundra, gen_taiga,  gen_snow,
    gen_valley, gen_grass,  gen_swamp,   // Meadow uses gen_grass — same as TS
    gen_desert, gen_steppe, gen_tropics,
    gen_water,
};

} // anonymous

void TileAtlas::init() {
    const int W = N * kTileCount;
    std::vector<std::uint8_t> pixels(std::size_t(W) * N * 4, 0);
    const int stride = W * 4;
    for (int t = 0; t < kTileCount; ++t) {
        kGenerators[t](pixels.data() + t * N * 4, stride);
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, N, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TileAtlas::destroy() {
    if (tex) glDeleteTextures(1, &tex);
    tex = 0;
}

int TileAtlas::index_for(Biome b) const {
    int i = int(b);
    if (i < 0 || i >= kTileCount) return 0;
    return i;
}

} // namespace sm::sub
