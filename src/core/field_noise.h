// THE noise stack of the world's fields (v72) — torus-tiling fBM over
// periodic Perlin, verbatim the mathematics the climate master has always
// been synthesized with (it lived as map_generator.cpp's private inline
// until the geology field law needed the SAME noise: one mathematics for
// every field of the world, owner 2026-08-31). Header-inline so every
// consumer links it for free — the map generator, the deposit layer, and
// whatever field comes next.
//
// Keep `period` a WHOLE number of tiles across the torus, and every octave
// of it whole too, or the noise cuts a seam the seed hides inside the map —
// the mountain-cliff scar (map_generator.cpp synth_master) is the measured
// lesson.
#pragma once
#include <cmath>

namespace sm {

// Ken Perlin's classic 256-entry permutation table, doubled to 512 to avoid a
// modulo on indices.
inline constexpr int kFieldNoisePerm[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,
    117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,
    165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,
    105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,
    187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,
    3,64,52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,
    227,47,16,58,17,182,189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,
    221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,
    178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,
    241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
    176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180
};

inline float pn_fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float pn_mod(float x, float y) { return x - y * std::floor(x / y); }
inline float pn_fract(float x) { return x - std::floor(x); }
inline float pn_mix(float a, float b, float t) { return a + t * (b - a); }

inline float pn_grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0f * v : 2.0f * v);
}

// Periodic Perlin noise -- lattice indices wrap modulo `period` so the field
// repeats every `period` units, tiling seamlessly across uv[0,1].
inline float periodic_noise(float pX, float pY, float period, float seed) {
    pX += seed;
    pY += seed;
    const float px = pn_mod(pX, period);
    const float py = pn_mod(pY, period);
    const int xi  = int(std::floor(px)) & 255;
    const int yi  = int(std::floor(py)) & 255;
    const int xi1 = int(pn_mod(std::floor(px) + 1.0f, period)) & 255;
    const int yi1 = int(pn_mod(std::floor(py) + 1.0f, period)) & 255;
    const float xf = pn_fract(px);
    const float yf = pn_fract(py);
    const float u  = pn_fade(xf);
    const float v  = pn_fade(yf);
    const int aa = kFieldNoisePerm[kFieldNoisePerm[xi]  + yi];
    const int ab = kFieldNoisePerm[kFieldNoisePerm[xi]  + yi1];
    const int ba = kFieldNoisePerm[kFieldNoisePerm[xi1] + yi];
    const int bb = kFieldNoisePerm[kFieldNoisePerm[xi1] + yi1];
    const float x1 = pn_mix(pn_grad(aa, xf, yf),        pn_grad(ba, xf - 1.0f, yf),        u);
    const float x2 = pn_mix(pn_grad(ab, xf, yf - 1.0f), pn_grad(bb, xf - 1.0f, yf - 1.0f), u);
    return pn_mix(x1, x2, v);
}

// fBM over periodic Perlin -- same octave / persistence / period semantics as
// the former shader (capped at 8 octaves).
inline float terrain_fbm(float pX, float pY, int oct, float persistence,
                         float period, float seed) {
    float value = 0.0f, amp = 1.0f, freq = 1.0f, total = 0.0f;
    for (int i = 0; i < 8; ++i) {
        if (i >= oct) break;
        value += amp * periodic_noise(pX * freq, pY * freq,
                                      period * freq, seed + float(i) * 100.0f);
        total += amp;
        amp   *= persistence;
        freq  *= 2.0f;
    }
    return value / total;
}

} // namespace sm
