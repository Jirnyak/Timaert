#version 450
// Stain-canvas stamp fragment stage — the gigahrush surface_marks shape
// shaders, returned to their native home. The reference (gigahrush
// src/systems/surface_marks.ts) computed these EXACT functions per pixel on
// the CPU because it was a WebGL game with a software atlas; here the same
// laws run where per-pixel work belongs. Ported verbatim: hash family,
// smooth-noise, 3-octave fbm, and the mark shapes —
//   SPLAT  — organic fluid splatter: fbm-noisy edge with 3-8 tendrils,
//            secondary blob boosts (blood / fluid hits);
//   POOL   — large irregular death pool, high inner coverage;
//   DRIP   — elongated gravity-pulled drip with a tail (wounded trail);
//   SCORCH — noisy radial burn with charred fringe (fire / explosions).
// Output = (mark colour, shapeAlpha × intensity); the pipeline's blend does
// the accumulation law (colour alpha-over, alpha adds and saturates), so a
// pixel's stored colour is the alpha-weighted mix of everything that ever
// landed there — the reference's writeSurfacePixel, in fixed function.
layout(location = 0) in vec2 vUv;         // [-1,1]² across the mark disk
layout(location = 1) in vec4 vColor;      // rgb + intensity (max alpha)
layout(location = 2) flat in float vSeed;
layout(location = 3) flat in float vType;

layout(location = 0) out vec4 outColor;

// ── Hash family (surface_marks.ts hash/hash2 — same integer mix) ──
float hash_u(uint n) {
    n = (n ^ 61u) ^ (n >> 16);
    n = n + (n << 3);
    n = n ^ (n >> 4);
    n = n * 0x27d4eb2du;
    n = n ^ (n >> 15);
    return float(n & 0x7fffffffu) / 2147483647.0;
}
float hash1(float n) { return hash_u(uint(int(n))); }
float hash2i(int x, int y, int s) {
    return hash_u(uint(x * 374761393 + y * 668265263 + s * 1274126177));
}

// Smooth noise with bilinear interpolation (surface_marks.ts snoise).
float snoise(float x, float y, int s) {
    int ix = int(floor(x)), iy = int(floor(y));
    float fx = x - float(ix), fy = y - float(iy);
    float a = hash2i(ix, iy, s);
    float b = hash2i(ix + 1, iy, s);
    float c = hash2i(ix, iy + 1, s);
    float d = hash2i(ix + 1, iy + 1, s);
    float lx = fx * fx * (3.0 - 2.0 * fx);
    float ly = fy * fy * (3.0 - 2.0 * fy);
    return a + (b - a) * lx + (c - a) * ly + (a - b - c + d) * lx * ly;
}

// Fractal brownian motion — 3 octaves (surface_marks.ts fbm).
float fbm(float x, float y, int s) {
    float v = 0.0, amp = 0.5, freq = 1.0;
    for (int i = 0; i < 3; ++i) {
        v += snoise(x * freq, y * freq, s + i * 137) * amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return v;
}

// ── Mark shapes: (u,v) in the unit disk, seed varies the organism ──

float shaderSplat(float u, float v, float seed) {
    float r = sqrt(u * u + v * v);
    if (r > 1.0) return 0.0;
    float angle = atan(v, u);
    float tendrilFreq = 3.0 + hash1(seed + 11.0) * 5.0; // 3-8 tendrils
    float tendrilAmp = 0.15 + hash1(seed + 22.0) * 0.25;
    float nEdge = fbm(angle * tendrilFreq / 6.28, r * 3.0, int(seed));
    float edgeR = 0.55 + tendrilAmp * (nEdge - 0.5) * 2.0;
    float blobs = fbm(u * 2.5 + hash1(seed + 3.0) * 10.0,
                      v * 2.5 + hash1(seed + 4.0) * 10.0, int(seed) + 77);
    float blobBoost = blobs > 0.55 ? (blobs - 0.55) * 3.0 : 0.0;
    float dist = r - edgeR - blobBoost * 0.3;
    if (dist > 0.15) return 0.0;
    if (dist > 0.0) return 1.0 - dist / 0.15;
    return 1.0;
}

float shaderPool(float u, float v, float seed) {
    float r = sqrt(u * u + v * v);
    if (r > 1.0) return 0.0;
    float n1 = fbm(u * 2.0 + hash1(seed) * 8.0,
                   v * 2.0 + hash1(seed + 1.0) * 8.0, int(seed) + 300);
    float n2 = fbm(u * 4.0, v * 4.0, int(seed) + 500);
    float edge = 0.65 + n1 * 0.25;
    float inner = 0.3 + n2 * 0.15;
    if (r > edge) return max(0.0, (1.0 - (r - edge) / 0.25) * 0.3);
    if (r < inner) return 0.9 + n2 * 0.1;
    float t = (r - inner) / (edge - inner);
    return 0.9 - t * 0.4;
}

float shaderDrip(float u, float v, float seed) {
    float su = u * 1.8;
    float sv = v * 0.7 - 0.2;
    float r = sqrt(su * su + sv * sv);
    if (r > 1.0) return 0.0;
    float n = snoise(su * 4.0, sv * 3.0, int(seed));
    float edge = 0.5 + n * 0.3;
    if (v > 0.2) {
        float tailWidth = 0.15 - (v - 0.2) * 0.12;
        if (abs(u) < tailWidth) return max(0.0, 1.0 - (v - 0.2) * 1.5);
    }
    if (r > edge) return max(0.0, (1.0 - (r - edge) / 0.3) * 0.5);
    return 1.0;
}

float shaderScorch(float u, float v, float seed) {
    float r = sqrt(u * u + v * v);
    if (r > 1.0) return 0.0;
    float n = fbm(u * 3.0 + hash1(seed) * 5.0,
                  v * 3.0 + hash1(seed + 1.0) * 5.0, int(seed) + 200);
    float edge = 0.6 + n * 0.35;
    if (r > edge) {
        float outer = (r - edge) / (1.0 - edge);
        return max(0.0, (1.0 - outer) * 0.4);
    }
    return 0.7 + (1.0 - r / edge) * 0.3;
}

void main() {
    // MarkType lockstep with sub/particles.h: 1 Splat, 2 Pool, 3 Drip,
    // 4 Scorch (0 None never reaches the GPU — the CPU drops it).
    int type = int(vType + 0.5);
    float a;
    if (type == 2)      a = shaderPool(vUv.x, vUv.y, vSeed);
    else if (type == 3) a = shaderDrip(vUv.x, vUv.y, vSeed);
    else if (type == 4) a = shaderScorch(vUv.x, vUv.y, vSeed);
    else                a = shaderSplat(vUv.x, vUv.y, vSeed);
    if (a <= 0.01) discard;
    outColor = vec4(vColor.rgb, a * vColor.a);
}
