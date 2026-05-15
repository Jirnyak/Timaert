#include "macro/macro_renderer.h"
#include "gl/helpers.h"
#include <cstdio>
#include <string>

namespace sm {

static const char* kVS = R"(#version 330 core
layout(location=0) in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// Composition pipeline (mirrors TS biome-textures.ts verbatim):
//   biomeTextureOverlay (per-biome texture + 8-neighbour shore band +
//                        land-land biome blend + climate overlay)
//   → road / dirtRoad overlay
//   → tree / mountain / landmark painter overlay
//   → zone tint
//   → cell-grid overlay (torus visibility)
//   → night tint
static const char kFS0[] = R"GLSL(#version 330 core
in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_master;     // RGBA: R=h, G=moist, B=temp, A=mask
uniform sampler2D u_featureMap; // R8: FeatureType
uniform sampler2D u_zoneMap;    // R8: zone 0..9 (optional, may be black)
uniform sampler2D u_landmarkMap;// R8: 0=none, 1=city, 2=village
uniform sampler2D u_riverMap;   // R8: river mask
uniform vec2  u_mapSize;
uniform vec2  u_cam;            // world-space pixel offset
uniform float u_zoom;
uniform vec2  u_viewSize;
uniform float u_seaLevel;
uniform float u_seed;
uniform float u_mtnThreshold;
uniform float u_timeOfDay;      // 0..1

// ── Common noise primitives (bt_ prefix; verbatim from TS biome-textures) ──
float bt_hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float bt_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = bt_hash(i);
    float b = bt_hash(i + vec2(1, 0));
    float c = bt_hash(i + vec2(0, 1));
    float d = bt_hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float bt_fbm(vec2 p, int oct) {
    float v = 0.0, a = 0.5, t = 0.0;
    for (int i = 0; i < 6; i++) { if (i >= oct) break; v += a * bt_noise(p); t += a; p *= 2.0; a *= 0.5; }
    return v / t;
}

// ── Periodic versions of bt_noise / bt_fbm. Lattice indices wrap modulo
// `period` so the field tiles seamlessly across the world wrap. Required
// because every biome's per-pixel noise lives in `wp = wpCell*16 + p`
// space, which jumps from (mapW*16) back to 0 at the world wrap; without
// periodic lattice the per-cell texture has a hard 1-pixel-wide
// discontinuity along the entire bottom and left wrap seam.
float bt_noise_p(vec2 p, vec2 period) {
    vec2 i  = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    vec2 i0 = mod(i,            period);
    vec2 i1 = mod(i + vec2(1.0), period);
    float a = bt_hash(i0);
    float b = bt_hash(vec2(i1.x, i0.y));
    float c = bt_hash(vec2(i0.x, i1.y));
    float d = bt_hash(i1);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// Period doubles per octave because input domain doubles per octave.
float bt_fbm_p(vec2 p, vec2 period, int oct) {
    float v = 0.0, a = 0.5, t = 0.0;
    for (int i = 0; i < 6; i++) {
        if (i >= oct) break;
        v += a * bt_noise_p(p, period);
        t += a;
        p      *= 2.0;
        period *= 2.0;
        a      *= 0.5;
    }
    return v / t;
}

// ── Per-biome procedural textures (verbatim from TS tundra/taiga/.../water) ──
vec3 bt_tundra(vec2 wp, float sd) {
    wp += sd * 0.17;
    vec2 P = u_mapSize * 16.0;
    float lichen = bt_fbm_p(wp * 0.06, P * 0.06, 3);
    float rock   = bt_noise_p(wp * 0.18 + 30.0, P * 0.18);
    float grain  = bt_hash(wp) * 0.015;
    float patches = smoothstep(0.35, 0.65, lichen);
    vec3 m = mix(vec3(0.88, 0.86, 0.84), vec3(0.94, 0.96, 0.88), patches);
    m += grain; m += rock * 0.06; return m;
}
vec3 bt_taiga(vec2 wp, float sd) {
    wp += sd * 0.23;
    vec2 P = u_mapSize * 16.0;
    float needles      = bt_fbm_p(wp * 0.12, P * 0.12, 2);
    float undergrowth  = bt_noise_p(wp * 0.05 + 50.0, P * 0.05);
    float bark         = bt_hash(wp) * 0.012;
    vec3 m = vec3(0.90 + needles * 0.08, 0.94 + undergrowth * 0.10, 0.88 + needles * 0.06);
    m += bark;
    float dark = smoothstep(0.55, 0.40, undergrowth);
    m *= 1.0 - dark * 0.08;
    return m;
}
vec3 bt_snow(vec2 wp, float sd) {
    wp += sd * 0.31;
    vec2 P = u_mapSize * 16.0;
    float drift   = bt_noise_p(vec2(wp.x * 0.14 + wp.y * 0.04, wp.y * 0.08) + 20.0, vec2(P.x * 0.14, P.y * 0.08));
    float detail  = bt_noise_p(wp * 0.30 + 70.0, P * 0.30);
    float sparkle = step(0.992, bt_hash(wp));
    vec3 m = vec3(0.97 + drift * 0.04, 0.97 + drift * 0.03, 0.99 + drift * 0.02);
    m -= detail * 0.03; m += sparkle * 0.06; m.b += 0.01; return m;
}
vec3 bt_valley(vec2 wp, float sd) {
    wp += sd * 0.19;
    vec2 P = u_mapSize * 16.0;
    float grass  = bt_fbm_p(wp * 0.09, P * 0.09, 3);
    float earth  = bt_noise_p(wp * 0.22 + 40.0, P * 0.22);
    float stones = step(0.95, bt_noise_p(wp * 0.45 + 15.0, P * 0.45));
    float grain  = bt_hash(wp) * 0.012;
    vec3 grassMod = vec3(0.93, 0.98, 0.88);
    vec3 earthMod = vec3(0.98, 0.93, 0.86);
    vec3 m = mix(grassMod, earthMod, smoothstep(0.4, 0.6, earth));
    m += grass * 0.06; m += grain; m -= stones * 0.06; return m;
}
vec3 bt_meadow(vec2 wp, float sd) {
    wp += sd * 0.13;
    vec2 P = u_mapSize * 16.0;
    float grass  = bt_fbm_p(wp * 0.10, P * 0.10, 3);
    float sway   = bt_noise_p(wp * 0.04 + 60.0, P * 0.04);
    float grain  = bt_hash(wp) * 0.010;
    vec3 m = vec3(0.92 + sway * 0.06, 0.97 + grass * 0.06, 0.90 + sway * 0.04);
    m += grain;
    float flower = bt_hash(wp + 99.0);
    if (flower > 0.96) {
        float kind = bt_hash(wp + 200.0);
        if (kind < 0.33)      m += vec3(0.12, 0.02, 0.0);
        else if (kind < 0.66) m += vec3(0.06, 0.06, 0.12);
        else                  m += vec3(0.12, 0.10, 0.0);
    }
    return m;
}
vec3 bt_swamp(vec2 wp, float sd) {
    wp += sd * 0.29;
    vec2 P = u_mapSize * 16.0;
    float murk  = bt_fbm_p(wp * 0.08, P * 0.08, 3);
    float pool  = smoothstep(0.42, 0.32, bt_noise_p(wp * 0.15 + 25.0, P * 0.15));
    float moss  = bt_noise_p(wp * 0.28 + 80.0, P * 0.28);
    float grain = bt_hash(wp) * 0.012;
    vec3 m = vec3(0.88 + murk * 0.08, 0.92 + moss * 0.08, 0.86 + murk * 0.05);
    m -= pool * vec3(0.06, 0.04, 0.02);
    m *= 1.0 - pool * 0.10; m += grain; return m;
}
vec3 bt_desert(vec2 wp, float sd) {
    wp += sd * 0.21;
    vec2 P = u_mapSize * 16.0;
    float ripple = bt_noise_p(vec2(wp.x * 0.12 + wp.y * 0.03, wp.y * 0.06) + 35.0, vec2(P.x * 0.12, P.y * 0.06));
    float dune   = bt_noise_p(wp * 0.04 + 90.0, P * 0.04);
    float grain  = bt_hash(wp) * 0.010;
    vec3 m = vec3(1.00 + ripple * 0.06, 0.97 + ripple * 0.04, 0.92 + dune * 0.04);
    m += dune * vec3(0.04, 0.02, 0.0);
    m += grain;
    float shadow = smoothstep(0.55, 0.45, ripple);
    m *= 1.0 - shadow * 0.04;
    return m;
}
vec3 bt_steppe(vec2 wp, float sd) {
    wp += sd * 0.37;
    vec2 P = u_mapSize * 16.0;
    float wind  = bt_noise_p(vec2(wp.x * 0.10, wp.y * 0.03) + 45.0, vec2(P.x * 0.10, P.y * 0.03));
    float tufts = bt_fbm_p(wp * 0.14, P * 0.14, 2);
    float grain = bt_hash(wp) * 0.010;
    vec3 m = vec3(0.98 + wind * 0.05, 0.96 + tufts * 0.06, 0.90 + wind * 0.03);
    m += grain;
    float bare = smoothstep(0.62, 0.68, tufts);
    m += bare * vec3(0.04, 0.02, 0.0);
    return m;
}
vec3 bt_tropics(vec2 wp, float sd) {
    wp += sd * 0.41;
    vec2 P = u_mapSize * 16.0;
    float canopy = bt_fbm_p(wp * 0.11, P * 0.11, 3);
    float gap    = smoothstep(0.58, 0.68, bt_noise_p(wp * 0.20 + 55.0, P * 0.20));
    float leaf   = bt_noise_p(wp * 0.35 + 10.0, P * 0.35);
    float grain  = bt_hash(wp) * 0.008;
    vec3 m = vec3(0.88 + canopy * 0.08, 0.94 + leaf * 0.06, 0.86 + canopy * 0.05);
    m += gap * vec3(0.08, 0.10, 0.04); m += grain; return m;
}
vec3 bt_water(vec2 wp, float sd) {
    wp += sd * 0.11;
    vec2 P = u_mapSize * 16.0;
    float caustic = bt_noise_p(wp * 0.12 +   5.0, P * 0.12) * bt_noise_p(wp * 0.18 + 65.0, P * 0.18);
    float depth   = bt_noise_p(wp * 0.03, P * 0.03);
    float ripple  = bt_noise_p(wp * 0.25 + 120.0, P * 0.25);
    vec3 m = vec3(0.94 + caustic * 0.08, 0.96 + caustic * 0.06 + depth * 0.04, 1.00 + ripple * 0.03);
    m *= 1.0 - depth * 0.04;
    return m;
}

// ── Biome classification (3×3 climate matrix; index 9 = water) ──
int bt_biome(vec2 cell) {
    vec2 uv = fract((cell + 0.5) / u_mapSize);
    vec4 m  = texture(u_master, uv);
    if (m.r < u_seaLevel) return 9;
    int row = int(clamp(m.b * 2.99, 0.0, 2.0));
    int col = int(clamp(m.g * 2.99, 0.0, 2.0));
    return row * 3 + col;
}
vec3 bt_tex(int b, vec2 wp, float sd) {
    if (b == 0) return bt_tundra (wp, sd);
    if (b == 1) return bt_taiga  (wp, sd);
    if (b == 2) return bt_snow   (wp, sd);
    if (b == 3) return bt_valley (wp, sd);
    if (b == 4) return bt_meadow (wp, sd);
    if (b == 5) return bt_swamp  (wp, sd);
    if (b == 6) return bt_desert (wp, sd);
    if (b == 7) return bt_steppe (wp, sd);
    if (b == 8) return bt_tropics(wp, sd);
    return bt_water(wp, sd);
}
vec3 bt_baseColor(int b) {
    if (b == 0) return vec3(0.50, 0.52, 0.45); // Tundra
    if (b == 1) return vec3(0.22, 0.38, 0.28); // Taiga
    if (b == 2) return vec3(0.90, 0.92, 0.96); // Snow
    if (b == 3) return vec3(0.55, 0.52, 0.32); // Valley
    if (b == 4) return vec3(0.40, 0.52, 0.28); // Meadow
    if (b == 5) return vec3(0.28, 0.38, 0.22); // Swamp
    if (b == 6) return vec3(0.82, 0.72, 0.48); // Desert
    if (b == 7) return vec3(0.68, 0.60, 0.32); // Steppe
    if (b == 8) return vec3(0.10, 0.35, 0.10); // Tropics
    return vec3(0.12, 0.22, 0.42);             // Water
}

// ── Shore palette (single source for both sides) ──
vec3 bt_sandWet() { return vec3(0.55, 0.50, 0.36); }
vec3 bt_sandDry() { return vec3(0.76, 0.70, 0.52); }

// Per-cell, per-edge noise wiggle. `cell` MUST be the toroidally-wrapped
// cell index — bt_hash is non-periodic, so passing the raw (unwrapped)
// world cell would make cell=-1 hash differently from cell=mapW-1 and
// produce a thin but visible discontinuity in the wet-sand wiggle right
// at the world wrap seam.
float bt_edgeNoise(vec2 cell, float edgeId, float coord01, float sd) {
    float s = bt_hash(cell + sd * 0.137 + edgeId * 7.31);
    return (bt_noise(vec2(coord01 * 4.7 + s * 13.0, edgeId * 3.1 + s * 7.0)) - 0.5) * 5.0;
}

// Signed-distance shore color: positive d = inside water (crisp wet
// sand band ≤4.5 px), negative d = inside land (smooth contour-less
// fade up to 12 px modulated by 2-octave noise).
vec3 bt_shoreColor(vec3 baseColor, float d, float grain, vec2 wp, float sd) {
    if (d >= 0.0) {
        if (d > 4.5) return baseColor;
        vec3 sand = mix(bt_sandWet(), bt_sandDry(), smoothstep(0.0, 4.0, d)) + grain;
        float t   = 1.0 - smoothstep(3.5, 4.5, d);
        return mix(baseColor, sand, t);
    }
    float a = -d;
    if (a > 12.0) return baseColor;
    vec2 P = u_mapSize * 16.0;
    float n   = bt_fbm_p(wp * 0.18 + sd * 0.07, P * 0.18, 3);
    float cov = clamp(smoothstep(12.0, 0.0, a) * (0.55 + n * 0.55), 0.0, 1.0);
    vec3  sand = mix(bt_sandWet(), bt_sandDry(), smoothstep(0.0, 6.0, a)) + grain;
    return mix(baseColor, sand, cov);
}

// ── Climate overlay (snow on cold land, drift ice on cold water) ──
vec3 bt_climateOverlay(vec3 col, vec2 wp, float temp01, bool isWater, float sd) {
    vec2 P = u_mapSize * 16.0;
    if (isWater) {
        float iceMask = smoothstep(0.22, 0.05, temp01);
        if (iceMask <= 0.0) return col;
        float n     = bt_fbm_p(wp * 0.07 + sd * 0.11, P * 0.07, 4);
        float crack = bt_fbm_p(wp * 0.35, P * 0.35, 2);
        float cov   = smoothstep(0.45, 0.65, n) * iceMask;
        vec3  ice   = mix(vec3(0.86, 0.92, 0.97), vec3(0.70, 0.80, 0.92), crack * 0.6);
        return mix(col, ice, cov);
    }
    float snowMask = smoothstep(0.32, 0.10, temp01);
    if (snowMask <= 0.0) return col;
    float n     = bt_fbm_p(wp * 0.10 + sd * 0.13, P * 0.10, 4);
    float frost = bt_fbm_p(wp * 0.40, P * 0.40, 2) * 0.15;
    float cov   = smoothstep(0.40, 0.70, n) * snowMask;
    vec3  snow  = vec3(0.92, 0.94, 0.97) - frost;
    return mix(col, snow, cov);
}

float bt_temperature(vec2 cell) {
    vec2 uv = fract((cell + 0.5) / u_mapSize);
    return texture(u_master, uv).b;
}

// ── Main biome overlay (verbatim port of biomeTextureOverlay) ──
vec3 biomeTextureOverlay(vec2 worldPx) {
    vec2 cell = floor(worldPx);
    vec2 f    = fract(worldPx);
    vec2 p    = floor(f * 16.0) + 0.5;     // 16×16 sub-pixel grid
    // Toroidal wrap on world-pixel coords so per-cell hash/fbm tile across
    // map boundaries with no visible seam (matches TS biome-textures.ts).
    vec2 wpCell = mod(cell, u_mapSize);
    vec2 wp   = wpCell * 16.0 + p;         // world-pixel coords (wrapped)
    float sd  = u_seed;

    // All per-cell hashes must use the wrapped cell so the world seams
    // line up. `bt_biome` already wraps via fract() internally so its
    // input may stay unwrapped, but bt_edgeNoise/grain hash do NOT wrap.
    vec2 cellW = wpCell;

    int  cb      = bt_biome(cell);
    bool isWater = (cb == 9);

    // 8 neighbours
    int nbE  = bt_biome(cell + vec2( 1, 0));
    int nbW  = bt_biome(cell + vec2(-1, 0));
    int nbN  = bt_biome(cell + vec2( 0, 1));
    int nbS  = bt_biome(cell + vec2( 0,-1));
    int nbNE = bt_biome(cell + vec2( 1, 1));
    int nbNW = bt_biome(cell + vec2(-1, 1));
    int nbSE = bt_biome(cell + vec2( 1,-1));
    int nbSW = bt_biome(cell + vec2(-1,-1));

    float grain = (bt_hash(wp + sd) - 0.5) * 0.012;

    // Signed distance to nearest water↔land boundary.
    float sgn  = isWater ? 1.0 : -1.0;
    float dist = 999.0;

    // Cardinal edges (cell-local noise; cellW is toroidally wrapped so the
    // wiggle pattern matches across the world-edge seam).
    if (isWater != (nbE == 9)) { float n = bt_edgeNoise(cellW, 0.0, p.y / 16.0, sd); dist = min(dist, abs((16.0 - p.x) - n)); }
    if (isWater != (nbW == 9)) { float n = bt_edgeNoise(cellW, 1.0, p.y / 16.0, sd); dist = min(dist, abs(p.x - n)); }
    if (isWater != (nbN == 9)) { float n = bt_edgeNoise(cellW, 2.0, p.x / 16.0, sd); dist = min(dist, abs((16.0 - p.y) - n)); }
    if (isWater != (nbS == 9)) { float n = bt_edgeNoise(cellW, 3.0, p.x / 16.0, sd); dist = min(dist, abs(p.y - n)); }

    // Diagonal corners (only when both adjacent cardinals share this cell's domain).
    if (isWater != (nbNE == 9) && (nbN == 9) == isWater && (nbE == 9) == isWater) dist = min(dist, length(p - vec2(16.0, 16.0)));
    if (isWater != (nbNW == 9) && (nbN == 9) == isWater && (nbW == 9) == isWater) dist = min(dist, length(p - vec2( 0.0, 16.0)));
    if (isWater != (nbSE == 9) && (nbS == 9) == isWater && (nbE == 9) == isWater) dist = min(dist, length(p - vec2(16.0,  0.0)));
    if (isWater != (nbSW == 9) && (nbS == 9) == isWater && (nbW == 9) == isWater) dist = min(dist, length(p - vec2( 0.0,  0.0)));

    vec3 tex = bt_baseColor(cb) * bt_tex(cb, wp, sd);

    // Land↔land biome blending at cardinal borders (5 px reach).
    if (!isWater) {
        float blendD     = 999.0;
        int   blendBiome = cb;
        if (nbE != cb && nbE != 9) { float d = 16.0 - p.x; if (d < blendD) { blendD = d; blendBiome = nbE; } }
        if (nbW != cb && nbW != 9) { float d = p.x;        if (d < blendD) { blendD = d; blendBiome = nbW; } }
        if (nbN != cb && nbN != 9) { float d = 16.0 - p.y; if (d < blendD) { blendD = d; blendBiome = nbN; } }
        if (nbS != cb && nbS != 9) { float d = p.y;        if (d < blendD) { blendD = d; blendBiome = nbS; } }
        if (blendD < 5.0) {
            float t = smoothstep(5.0, 0.0, blendD) * 0.5;
            tex = mix(tex, bt_baseColor(blendBiome) * bt_tex(blendBiome, wp, sd), t);
        }
    }

    // Shore band on top of biome texture.
    float reach = isWater ? 4.5 : 12.0;
    if (dist < reach) {
        float d = sgn * dist;
        tex = bt_shoreColor(tex, d, grain, wp, sd);
    }

    // Climate overlay.
    float temp01 = bt_temperature(cell);
    tex = bt_climateOverlay(tex, wp, temp01, isWater, sd);

    // Strength fade only when zoomed out enough that biome detail aliases.
    float strength = smoothstep(3.0, 10.0, u_zoom);
    return mix(bt_baseColor(cb), tex, strength);
}

)GLSL";
static const char kFS1[] = R"GLSL(
// =============================================================
// TS-faithful feature overlays (verbatim ports from
//   src/game/{road,dirt-road,tree,mountain}-spawner.ts).
// Renamed uniforms: u_masterTexture → u_master, u_worldSeed → u_seed.
// =============================================================

// ── Road overlay (TS road-spawner.ts ROAD_MAP_GLSL) ──
float roadHash(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}
float roadLineDist(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    return length(p - (a + ab * t));
}
bool roadAt(vec2 cell) {
    vec2 uv = mod(cell + 0.5, u_mapSize) / u_mapSize;
    float fid = texture(u_featureMap, uv).r * 255.0;
    return fid > 0.5 && fid < 1.5;
}
vec3 roadOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 pixelCoord = mapUV * u_mapSize;
    vec2 cell = floor(pixelCoord);
    vec2 cellUV = (cell + 0.5) / u_mapSize;
    float featureId = texture(u_featureMap, cellUV).r * 255.0;
    if (featureId < 0.5 || featureId > 1.5) return baseColor;

    vec2 p = floor(fract(pixelCoord) * 16.0) + 0.5;
    vec2 ctr = vec2(8.0);
    float md = 999.0;
    bool connected = false;
    if (roadAt(cell + vec2( 0,-1))) { md = min(md, roadLineDist(p, ctr, vec2( 8.0,  0.0))); connected = true; }
    if (roadAt(cell + vec2( 0, 1))) { md = min(md, roadLineDist(p, ctr, vec2( 8.0, 16.0))); connected = true; }
    if (roadAt(cell + vec2( 1, 0))) { md = min(md, roadLineDist(p, ctr, vec2(16.0,  8.0))); connected = true; }
    if (roadAt(cell + vec2(-1, 0))) { md = min(md, roadLineDist(p, ctr, vec2( 0.0,  8.0))); connected = true; }
    if (roadAt(cell + vec2( 1,-1))) { md = min(md, roadLineDist(p, ctr, vec2(16.0,  0.0))); connected = true; }
    if (roadAt(cell + vec2(-1,-1))) { md = min(md, roadLineDist(p, ctr, vec2( 0.0,  0.0))); connected = true; }
    if (roadAt(cell + vec2( 1, 1))) { md = min(md, roadLineDist(p, ctr, vec2(16.0, 16.0))); connected = true; }
    if (roadAt(cell + vec2(-1, 1))) { md = min(md, roadLineDist(p, ctr, vec2( 0.0, 16.0))); connected = true; }
    if (!connected) md = length(p - ctr);

    // Paved road in our build is always cobblestone (TS branched on master.a;
    // our master.a is land-mask, so we always treat feat==1 as major road).
    float hw = 3.0;
    if (md > hw) return baseColor;

    float cs = cell.x * 127.1 + cell.y * 311.7 + u_seed;
    float ph = roadHash(cs + p.x * 17.31 + p.y * 43.77);
    float edge = smoothstep(hw - 1.2, hw, md);

    vec3 stone1 = vec3(0.50, 0.48, 0.44);
    vec3 stone2 = vec3(0.45, 0.43, 0.40);
    vec3 roadColor = ph < 0.5 ? stone1 : stone2;
    vec2 sp = p;
    if (mod(floor(p.y / 3.0), 2.0) > 0.5) sp.x += 1.5;
    vec2 sc = fract(sp / 3.0);
    if (sc.x < 0.18 || sc.y < 0.18) roadColor *= 0.80;
    if (ph > 0.85) roadColor *= 0.75;

    vec3 edgeCol = vec3(0.30, 0.25, 0.18);
    roadColor = mix(roadColor, edgeCol, edge * 0.5);
    float opacity = 0.85 - edge * 0.35;
    return mix(baseColor, roadColor, opacity);
}

// ── Dirt road overlay (TS dirt-road-spawner.ts) ──
bool dirtRoadAt(vec2 cell) {
    vec2 uv = mod(cell + 0.5, u_mapSize) / u_mapSize;
    float fid = texture(u_featureMap, uv).r * 255.0;
    return (fid > 3.5 && fid < 4.5) || (fid > 0.5 && fid < 1.5);
}
vec3 dirtRoadOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 pixelCoord = mapUV * u_mapSize;
    vec2 cell = floor(pixelCoord);
    vec2 cellUV = (cell + 0.5) / u_mapSize;
    float featureId = texture(u_featureMap, cellUV).r * 255.0;
    if (featureId < 3.5 || featureId > 4.5) return baseColor;

    vec2 p = floor(fract(pixelCoord) * 16.0) + 0.5;
    vec2 ctr = vec2(8.0);
    float md = 999.0;
    bool connected = false;
    if (dirtRoadAt(cell + vec2( 0,-1))) { md = min(md, roadLineDist(p, ctr, vec2( 8.0,  0.0))); connected = true; }
    if (dirtRoadAt(cell + vec2( 0, 1))) { md = min(md, roadLineDist(p, ctr, vec2( 8.0, 16.0))); connected = true; }
    if (dirtRoadAt(cell + vec2( 1, 0))) { md = min(md, roadLineDist(p, ctr, vec2(16.0,  8.0))); connected = true; }
    if (dirtRoadAt(cell + vec2(-1, 0))) { md = min(md, roadLineDist(p, ctr, vec2( 0.0,  8.0))); connected = true; }
    if (dirtRoadAt(cell + vec2( 1,-1))) { md = min(md, roadLineDist(p, ctr, vec2(16.0,  0.0))); connected = true; }
    if (dirtRoadAt(cell + vec2(-1,-1))) { md = min(md, roadLineDist(p, ctr, vec2( 0.0,  0.0))); connected = true; }
    if (dirtRoadAt(cell + vec2( 1, 1))) { md = min(md, roadLineDist(p, ctr, vec2(16.0, 16.0))); connected = true; }
    if (dirtRoadAt(cell + vec2(-1, 1))) { md = min(md, roadLineDist(p, ctr, vec2( 0.0, 16.0))); connected = true; }
    if (!connected) md = length(p - ctr);

    float hw = 1.5;
    if (md > hw) return baseColor;

    float cs = cell.x * 127.1 + cell.y * 311.7 + u_seed;
    float ph = roadHash(cs + p.x * 17.31 + p.y * 43.77);
    float edge = smoothstep(hw - 0.8, hw, md);
    vec3 dirt = ph < 0.5 ? vec3(0.50, 0.40, 0.28) : vec3(0.46, 0.37, 0.25);
    vec3 grass = vec3(0.35, 0.50, 0.25);
    dirt = mix(dirt, grass, edge * 0.4);
    return mix(dirt, baseColor, edge * 0.3);
}

// ── Tree overlay (TS tree-spawner.ts TREE_MAP_GLSL — verbatim port) ──
float treeHash(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}
float treeHash2D(vec2 cell, float offset) {
    vec2 p = cell + offset;
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float treeValueNoise(vec2 p, float sd) {
    vec2 i = floor(p); vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = treeHash2D(i, sd);
    float b = treeHash2D(i + vec2(1.0, 0.0), sd);
    float c = treeHash2D(i + vec2(0.0, 1.0), sd);
    float d = treeHash2D(i + vec2(1.0, 1.0), sd);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
vec4 treeDraw(vec2 cell, vec2 localUV, vec3 baseColor) {
    vec2 cellUV = (cell + 0.5) / u_mapSize;
    float featureId = texture(u_featureMap, cellUV).r * 255.0;
    if (featureId < 1.5 || featureId > 2.5) return vec4(baseColor, 0.0);

    float v1 = treeHash2D(cell, u_seed + 1.0);
    float v2 = treeHash2D(cell, u_seed + 2.0);
    float temp = texture(u_master, cellUV).b;
    float vn = treeValueNoise(cell / 8.0, u_seed + 77.0);

    int tp;
    if (temp < 0.2)       tp = 4;
    else if (temp < 0.35) tp = vn < 0.45 ? 4 : 2;
    else if (temp < 0.5)  tp = vn < 0.45 ? 2 : 3;
    else if (temp < 0.65) tp = vn < 0.4 ? 0 : (vn < 0.7 ? 3 : 5);
    else if (temp < 0.8)  tp = vn < 0.35 ? 1 : (vn < 0.65 ? 0 : 5);
    else                  tp = 6;

    vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
    float cs = treeHash2D(cell, u_seed) * 1e3;
    float ph = treeHash(cs + p.x * 17.1 + p.y * 31.7);
    float cx = 7.0 + floor((v1 - 0.5) * 2.0);

    vec3 bark1, bark2, leaf1, leaf2, leaf3;
    if (tp == 0) {
        bark1 = vec3(79,56,41)/255.0;  bark2 = vec3(101,67,33)/255.0;
        leaf1 = vec3(30,120,30)/255.0; leaf2 = vec3(50,160,50)/255.0; leaf3 = vec3(75,105,42)/255.0;
    } else if (tp == 1) {
        bark1 = vec3(60,40,30)/255.0;   bark2 = vec3(85,55,40)/255.0;
        leaf1 = vec3(255,160,180)/255.0; leaf2 = vec3(255,120,165)/255.0; leaf3 = vec3(225,105,145)/255.0;
    } else if (tp == 2) {
        bark1 = vec3(195,195,190)/255.0; bark2 = vec3(240,240,235)/255.0;
        leaf1 = vec3(105,195,85)/255.0; leaf2 = vec3(135,215,105)/255.0; leaf3 = vec3(85,165,65)/255.0;
    } else if (tp == 3) {
        bark1 = vec3(70,50,40)/255.0;   bark2 = vec3(95,68,48)/255.0;
        leaf1 = vec3(235,125,10)/255.0; leaf2 = vec3(225,65,10)/255.0; leaf3 = vec3(245,200,15)/255.0;
    } else if (tp == 4) {
        bark1 = vec3(88,58,38)/255.0; bark2 = vec3(105,72,52)/255.0;
        leaf1 = vec3(12,82,12)/255.0; leaf2 = vec3(32,115,32)/255.0; leaf3 = vec3(18,68,18)/255.0;
    } else if (tp == 5) {
        bark1 = vec3(88,62,48)/255.0;  bark2 = vec3(105,72,38)/255.0;
        leaf1 = vec3(125,190,45)/255.0; leaf2 = vec3(105,170,35)/255.0; leaf3 = vec3(145,205,55)/255.0;
    } else {
        bark1 = vec3(62,45,30)/255.0; bark2 = vec3(80,55,35)/255.0;
        leaf1 = vec3(15,95,20)/255.0; leaf2 = vec3(25,130,30)/255.0; leaf3 = vec3(10,75,15)/255.0;
    }

    vec3 bk = ph < 0.5 ? bark1 : bark2;
    vec3 lf = ph < 0.33 ? leaf1 : (ph < 0.66 ? leaf2 : leaf3);

    vec3 col = baseColor;
    float drawn = 0.0;

    if (tp == 4) {
        // PINE
        float trT = 10.0 - floor(v2);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float baseY = 1.0 + floor(v1 * 2.0);
        for (int i = 0; i < 3; i++) {
            float tT = baseY + float(i) * 3.0;
            float tB = tT + 3.0;
            if (p.y >= tT && p.y <= tB) {
                float frac = (p.y - tT) / 3.0;
                float halfW = 0.5 + frac * (2.2 + float(i) * 0.7);
                float eN = (treeHash(cs + p.y * 7.1 + float(i) * 97.0) - 0.5) * 0.7;
                if (abs(p.x - cx) <= halfW + eN) {
                    vec3 lc = lf;
                    if (p.y < tT + 1.0) lc *= 1.18;
                    else if (p.y >= tB) lc *= 0.72;
                    col = lc; drawn = 1.0;
                }
            }
        }
    } else if (tp == 2) {
        // BIRCH
        float trT = 5.0 - floor(v2);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) {
            col = bk;
            if (mod(p.y + floor(v1 * 3.0), 3.0) < 1.0 && ph > 0.4) col = vec3(0.22,0.22,0.20);
            drawn = 1.0;
        }
        if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float cY = trT - 2.5;
        float rX = 2.5 + v1 * 1.2;
        float rY = 3.5 + v2 * 1.5;
        vec2 dd = (p - vec2(cx, cY)) / vec2(rX, rY);
        float eN = (treeHash(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.25;
        if (dot(dd, dd) <= 1.0 + eN) {
            vec3 lc = lf;
            if (dd.y < -0.35) lc *= 1.18;
            else if (dd.y > 0.35) lc *= 0.78;
            col = lc; drawn = 1.0;
        }
    } else if (tp == 5) {
        // WILLOW
        float trT = 7.0;
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float cY = 4.5;
        float cR = 4.5 + v1;
        float d = length(p - vec2(cx, cY));
        float eN = (treeHash(cs + p.x * 13.3 + p.y * 23.7) - 0.5) * 1.0;
        if (d <= cR + eN) {
            vec3 lc = lf;
            if (p.y < cY - cR * 0.3) lc *= 1.15;
            else if (p.y > cY + cR * 0.15) lc *= 0.82;
            col = lc; drawn = 1.0;
        }

        for (int i = 0; i < 6; i++) {
            float vs = cs + float(i) * 7.3;
            if (treeHash(vs) > 0.55) continue;
            float vx = cx - 3.0 + float(i) * 1.2 + treeHash(vs + 1.0) * 0.5;
            float vineStart = cY + cR * 0.5;
            float vineLen = 2.0 + treeHash(vs + 2.0) * 2.5;
            if (abs(p.x - floor(vx)) < 1.0 && p.y >= vineStart && p.y < vineStart + vineLen) {
                col = lf * 0.82; drawn = 1.0;
            }
        }
    } else if (tp == 6) {
        // JUNGLE
        float trT = 7.0;
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) <= 1.0) { col = bk; drawn = 1.0; }
        if (p.y >= 13.0 && p.y <= 15.0) {
            float rootW = 2.5 - (15.0 - p.y) * 0.5;
            if (abs(p.x - cx) <= rootW && abs(p.x - cx) > 1.0) {
                col = bk * 0.85; drawn = 1.0;
            }
        }
        if (p.y == 15.0 && abs(p.x - cx) <= 3.0) { col = vec3(0.06,0.10,0.03); drawn = 0.45; }

        float cY1 = 3.5;
        float rX1 = 5.5 + v1 * 1.5;
        float rY1 = 4.0 + v2;
        vec2 dd1 = (p - vec2(cx, cY1)) / vec2(rX1, rY1);
        float eN1 = (treeHash(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.35;
        if (dot(dd1, dd1) <= 1.0 + eN1) {
            vec3 lc = lf;
            if (dd1.y < -0.3) lc *= 1.15;
            else if (dd1.y > 0.3) lc *= 0.75;
            if (dot(dd1, dd1) > 0.7 + eN1) lc *= 0.85;
            col = lc; drawn = 1.0;
        }

        float cx2 = cx + (v1 < 0.5 ? -2.0 : 2.0);
        float cY2 = 2.0 + v2;
        float rC2 = 3.0 + v1 * 0.8;
        float d2 = length(p - vec2(cx2, cY2));
        float eN2 = (treeHash(cs + p.x * 9.1 + p.y * 15.3) - 0.5) * 0.5;
        if (d2 <= rC2 + eN2) {
            vec3 lc = leaf2;
            if (p.y < cY2 - rC2 * 0.3) lc *= 1.12;
            else if (p.y > cY2 + rC2 * 0.2) lc *= 0.78;
            col = lc; drawn = 1.0;
        }

        for (int i = 0; i < 7; i++) {
            float vs = cs + float(i) * 5.7;
            if (treeHash(vs) > 0.5) continue;
            float vx = cx - 4.0 + float(i) * 1.3 + treeHash(vs + 1.0) * 0.5;
            float vineStart = cY1 + rY1 * 0.5;
            float vineLen = 2.5 + treeHash(vs + 2.0) * 3.0;
            if (abs(p.x - floor(vx)) < 1.0 && p.y >= vineStart && p.y < vineStart + vineLen) {
                col = leaf3 * 0.9; drawn = 1.0;
            }
        }
    } else {
        // OAK / CHERRY / AUTUMN — round canopy
        float trT = 9.0 - floor(v2 * 2.0);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float cR = 4.5 + v1 * 1.5;
        float cY = trT - cR + 1.5;
        float d = length(p - vec2(cx, cY));
        float eN = (treeHash(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 1.0;
        if (d <= cR + eN) {
            vec3 lc = lf;
            if (p.y < cY - cR * 0.3) lc *= 1.22;
            else if (p.y > cY + cR * 0.3) lc *= 0.72;
            if (d > cR + eN - 1.2) lc *= 0.88;
            col = lc; drawn = 1.0;
            if (tp == 1 && ph > 0.82) col = vec3(1.0, 0.96, 0.98);
        }
    }

    return vec4(col, drawn);
}
)GLSL";
static const char kFS2[] = R"GLSL(
// ── Mountain overlay (TS mountain-spawner.ts MOUNTAIN_MAP_GLSL — verbatim port) ──
float mtnHash2D(vec2 cell, float offset) {
    vec2 p = cell + offset;
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float mtnHash(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}
vec4 mtnDraw(vec2 cell, vec2 localUV, vec3 baseColor) {
    vec2 cellUV = (cell + 0.5) / u_mapSize;
    float featureId = texture(u_featureMap, cellUV).r * 255.0;
    if (featureId < 2.5 || featureId > 3.5) return vec4(baseColor, 0.0);

    float height = texture(u_master, cellUV).r;
    float hParam = clamp((height - u_mtnThreshold) / (1.0 - u_mtnThreshold), 0.0, 1.0);

    float v1 = mtnHash2D(cell, u_seed + 1.0);
    float v2 = mtnHash2D(cell, u_seed + 2.0);
    float v3 = mtnHash2D(cell, u_seed + 3.0);
    int mtype = int(mtnHash2D(cell, u_seed + 7.0) * 4.0);

    vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
    float cs = mtnHash2D(cell, u_seed) * 1e3;
    float ph = mtnHash(cs + p.x * 17.1 + p.y * 31.7);
    float cx = 8.0 + floor((v1 - 0.5) * 2.0);

    vec3 rock1, rock2, rock3, snow1, snow2, shadow;
    if (mtype == 0) {
        rock1 = vec3(128,118,105)/255.0; rock2 = vec3(150,140,128)/255.0; rock3 = vec3(105,95,85)/255.0;
        snow1 = vec3(240,245,250)/255.0; snow2 = vec3(210,220,235)/255.0; shadow = vec3(75,70,65)/255.0;
    } else if (mtype == 1) {
        rock1 = vec3(155,130,100)/255.0; rock2 = vec3(175,150,115)/255.0; rock3 = vec3(130,108,82)/255.0;
        snow1 = vec3(238,242,248)/255.0; snow2 = vec3(215,225,235)/255.0; shadow = vec3(95,78,58)/255.0;
    } else if (mtype == 2) {
        rock1 = vec3(72,72,78)/255.0;    rock2 = vec3(95,92,98)/255.0;    rock3 = vec3(55,55,62)/255.0;
        snow1 = vec3(235,240,248)/255.0; snow2 = vec3(200,210,225)/255.0; shadow = vec3(38,38,45)/255.0;
    } else {
        rock1 = vec3(145,88,68)/255.0;   rock2 = vec3(168,105,78)/255.0;  rock3 = vec3(120,72,55)/255.0;
        snow1 = vec3(242,240,238)/255.0; snow2 = vec3(220,215,210)/255.0; shadow = vec3(85,52,38)/255.0;
    }

    float peakH = (1.0 - hParam) * 9.0;
    float baseY = 12.0;
    float snowLine = peakH + (baseY - peakH) * (0.15 + 0.15 * (1.0 - hParam));

    vec3 col = baseColor;
    float drawn = 0.0;

    if (p.y >= peakH && p.y <= baseY) {
        float frac = (p.y - peakH) / (baseY - peakH);
        float halfW = 0.5 + frac * (4.0 + v2 * 2.0);
        float edgeNoise = (mtnHash(cs + p.y * 13.1 + 47.0) - 0.5) * 1.2;

        if (abs(p.x - cx) <= halfW + edgeNoise) {
            vec3 rc = ph < 0.33 ? rock1 : (ph < 0.66 ? rock2 : rock3);
            float side = (p.x - cx) / max(halfW, 0.01);
            if (side < -0.3) rc *= 1.12;
            else if (side > 0.3) rc *= 0.78;
            rc *= 1.0 - frac * 0.2;

            float ridgeN = mtnHash(cs + p.x * 23.7 + p.y * 11.3);
            if (ridgeN > 0.88) rc *= 1.25;
            if (abs(p.x - cx) > halfW + edgeNoise - 1.0) rc = shadow;

            col = rc; drawn = 1.0;

            if (hParam > 0.4 && p.y < snowLine) {
                float snowN = (mtnHash(cs + p.x * 31.3 + p.y * 7.7) - 0.5) * 1.5;
                if (p.y < snowLine + snowN) {
                    col = ph < 0.5 ? snow1 : snow2;
                    if (side > 0.3) col *= 0.88;
                }
            }
        }
    }

    if (p.y == 13.0 && drawn < 0.5) {
        float shadowW = 1.5 + hParam * 2.5;
        if (abs(p.x - cx) <= shadowW) {
            col = mix(baseColor, vec3(0.0), 0.25);
            drawn = 1.0;
        }
    }

    if (hParam > 0.55) {
        float cx2 = cx + 3.0 * (v3 > 0.5 ? 1.0 : -1.0);
        float peakH2 = peakH + 2.0 + v3 * 2.0;
        float baseY2 = baseY - 1.0;
        if (p.y >= peakH2 && p.y <= baseY2) {
            float frac2 = (p.y - peakH2) / (baseY2 - peakH2);
            float halfW2 = 0.3 + frac2 * (2.0 + v3);
            float edgeN2 = (mtnHash(cs + p.y * 19.3 + 91.0) - 0.5) * 0.8;
            if (abs(p.x - cx2) <= halfW2 + edgeN2) {
                vec3 rc = ph < 0.5 ? rock1 : rock2;
                float side2 = (p.x - cx2) / max(halfW2, 0.01);
                if (side2 < -0.3) rc *= 1.10;
                else if (side2 > 0.3) rc *= 0.80;
                rc *= 1.0 - frac2 * 0.18;
                if (abs(p.x - cx2) > halfW2 + edgeN2 - 1.0) rc = shadow;
                col = rc; drawn = 1.0;

                float snowLine2 = peakH2 + (baseY2 - peakH2) * 0.25;
                if (hParam > 0.65 && p.y < snowLine2) {
                    col = ph < 0.5 ? snow1 : snow2;
                    if (side2 > 0.3) col *= 0.88;
                }
            }
        }
    }

    return vec4(col, drawn);
}
// ── Landmark overlay: procedural pixel-art cities & villages ──
// landmarkMap: 0=none, 1=city, 2=village. Per-cell hash drives variation
// (palette, gable side, banner colour). 16×16 sub-cell pixel grid;
// landmarks anchor at cell+0.5 with a 2×2 footprint so roofs overlap into
// the cell above (painter's order: dy=1 first, dy=0 last).
float lmHash(vec2 cell, float salt) {
    vec2 p = cell + salt;
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
vec4 cityDraw(vec2 cell, vec2 localUV, vec3 baseColor) {
    // 16×16 pixel grid, p.x left→right, p.y bottom→top
    vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
    float h0 = lmHash(cell, u_seed + 11.0);
    float h1 = lmHash(cell, u_seed + 23.0);

    // Palette (warm sandstone vs grey stone vs red-tile)
    vec3 wall, wallDk, roof, roofDk, gold, win;
    int pal = int(h0 * 3.0);
    if (pal == 0) { // sandstone + red roof
        wall   = vec3(212, 188, 148) / 255.0;
        wallDk = vec3(168, 142, 102) / 255.0;
        roof   = vec3(168,  64,  48) / 255.0;
        roofDk = vec3(120,  42,  32) / 255.0;
    } else if (pal == 1) { // grey stone + slate
        wall   = vec3(186, 178, 168) / 255.0;
        wallDk = vec3(132, 124, 116) / 255.0;
        roof   = vec3( 92,  84,  78) / 255.0;
        roofDk = vec3( 58,  52,  48) / 255.0;
    } else { // tan + dark roof
        wall   = vec3(196, 170, 132) / 255.0;
        wallDk = vec3(150, 124,  92) / 255.0;
        roof   = vec3(108,  72,  52) / 255.0;
        roofDk = vec3( 72,  48,  34) / 255.0;
    }
    gold = vec3(248, 208,  72) / 255.0;
    win  = vec3( 90,  68,  40) / 255.0;

    // Layout: a small keep with two side houses + a banner.
    // Keep: x in [6,10], main wall y in [4,9], roof triangle y 9..12.
    // Side wing left: x in [2,5], y in [5,8], roof y 8..10.
    // Side wing right: x in [11,14], y in [5,8], roof y 8..10.
    // Ground/path: y == 3 across [3..13].
    vec3 col = baseColor;
    float drawn = 0.0;

    // Path / plinth
    if (p.y == 3.0 && p.x >= 3.0 && p.x <= 13.0) {
        col = mix(wallDk, vec3(0.10, 0.08, 0.06), 0.35);
        drawn = 1.0;
    }

    // Left wing wall
    if (p.x >= 2.0 && p.x <= 5.0 && p.y >= 4.0 && p.y <= 7.0) {
        col = (mod(p.x + p.y, 2.0) < 0.5) ? wall : wallDk;
        // Window
        if (p.x == 3.0 && p.y == 6.0) col = win;
        drawn = 1.0;
    }
    // Left wing roof (triangle)
    if (p.y >= 8.0 && p.y <= 10.0) {
        float ay = p.y - 8.0;            // 0..2
        float halfW = 2.0 - ay;
        if (p.x >= 3.5 - halfW && p.x <= 3.5 + halfW) {
            col = (p.x > 3.5) ? roofDk : roof;
            drawn = 1.0;
        }
    }

    // Right wing wall
    if (p.x >= 10.0 && p.x <= 13.0 && p.y >= 4.0 && p.y <= 7.0) {
        col = (mod(p.x + p.y, 2.0) < 0.5) ? wall : wallDk;
        if (p.x == 12.0 && p.y == 6.0) col = win;
        drawn = 1.0;
    }
    // Right wing roof
    if (p.y >= 8.0 && p.y <= 10.0) {
        float ay = p.y - 8.0;
        float halfW = 2.0 - ay;
        if (p.x >= 11.5 - halfW && p.x <= 11.5 + halfW) {
            col = (p.x > 11.5) ? roofDk : roof;
            drawn = 1.0;
        }
    }

    // Central keep wall
    if (p.x >= 6.0 && p.x <= 9.0 && p.y >= 4.0 && p.y <= 9.0) {
        col = (mod(p.x + p.y, 2.0) < 0.5) ? wall : wallDk;
        // Gate
        if ((p.x == 7.0 || p.x == 8.0) && p.y == 4.0) col = vec3(0.18, 0.12, 0.08);
        // Window row
        if ((p.x == 6.0 || p.x == 9.0) && p.y == 7.0) col = win;
        drawn = 1.0;
    }
    // Keep roof (taller triangle)
    if (p.y >= 10.0 && p.y <= 12.0) {
        float ay = p.y - 10.0;
        float halfW = 2.5 - ay;
        if (p.x >= 7.5 - halfW && p.x <= 7.5 + halfW) {
            col = (p.x > 7.5) ? roofDk : roof;
            drawn = 1.0;
        }
    }
    // Banner pole + flag (gold)
    if (p.x == 8.0 && p.y >= 12.0 && p.y <= 14.0) { col = vec3(0.15, 0.12, 0.08); drawn = 1.0; }
    if (p.y == 13.0 && (p.x == 9.0 || p.x == 10.0)) {
        col = (h1 < 0.33) ? vec3(0.85, 0.18, 0.18)
            : (h1 < 0.66) ? gold
            :              vec3(0.20, 0.50, 0.85);
        drawn = 1.0;
    }

    // Side shadow on ground
    if (p.y == 3.0 && drawn < 0.5 && p.x >= 4.0 && p.x <= 12.0) {
        col = mix(baseColor, vec3(0.0), 0.15);
        drawn = 1.0;
    }

    return vec4(col, drawn);
}
vec4 villageDraw(vec2 cell, vec2 localUV, vec3 baseColor) {
    vec2 p = floor(vec2(localUV.x, 1.0 - localUV.y) * 16.0);
    float h0 = lmHash(cell, u_seed + 13.0);
    float h1 = lmHash(cell, u_seed + 29.0);

    vec3 wall, wallDk, roof, roofDk, win;
    if (h0 < 0.5) {
        wall   = vec3(208, 184, 144) / 255.0;
        wallDk = vec3(160, 134,  96) / 255.0;
        roof   = vec3(118,  78,  52) / 255.0;
        roofDk = vec3( 78,  52,  36) / 255.0;
    } else {
        wall   = vec3(196, 178, 152) / 255.0;
        wallDk = vec3(150, 132, 108) / 255.0;
        roof   = vec3( 92,  72,  56) / 255.0;
        roofDk = vec3( 58,  46,  36) / 255.0;
    }
    win = vec3(80, 56, 32) / 255.0;

    vec3 col = baseColor;
    float drawn = 0.0;

    // Three small huts: left (x 3..5), centre (x 7..9), right (x 11..13).
    // Walls y in [4..6], roofs y in [6..8] (triangles).
    // Add slight offset per-cell so villages don't all look identical.
    float jx = floor(h1 * 2.0); // 0 or 1
    for (int i = 0; i < 3; i++) {
        float cx = 4.0 + float(i) * 4.0 + (i == 1 ? jx : 0.0);
        // Wall
        if (p.x >= cx - 1.0 && p.x <= cx + 1.0 && p.y >= 4.0 && p.y <= 6.0) {
            col = (mod(p.x + p.y, 2.0) < 0.5) ? wall : wallDk;
            if (p.x == cx && p.y == 5.0) col = win;
            if (p.y == 4.0 && p.x == cx) col = vec3(0.16, 0.10, 0.06); // door
            drawn = 1.0;
        }
        // Roof triangle
        if (p.y >= 6.0 && p.y <= 8.0) {
            float ay = p.y - 6.0;
            float halfW = 2.0 - ay;
            if (p.x >= cx - halfW && p.x <= cx + halfW) {
                col = (p.x > cx) ? roofDk : roof;
                drawn = 1.0;
            }
        }
    }
    // Path
    if (p.y == 3.0 && p.x >= 3.0 && p.x <= 13.0) {
        col = mix(wallDk, vec3(0.10, 0.08, 0.06), 0.30);
        drawn = 1.0;
    }
    return vec4(col, drawn);
}
void landmarkDraw(vec2 cell, vec2 worldPos, inout vec3 col) {
    vec2 cellUV = (cell + 0.5) / u_mapSize;
    float lid = floor(texture(u_landmarkMap, cellUV).r * 255.0 + 0.5);
    if (lid < 0.5) return;

    vec2 diff = worldPos - (cell + 0.5);
    if (diff.x >  u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
    if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
    if (diff.y >  u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
    if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;

    vec2 localUV = (diff + 1.0) / 2.0;
    if (localUV.x < 0.0 || localUV.x >= 1.0
        || localUV.y < 0.0 || localUV.y >= 1.0) return;

    vec4 r;
    if (lid < 1.5)      r = cityDraw(cell, localUV, col);
    else if (lid < 2.5) r = villageDraw(cell, localUV, col);
    else return;
    if (r.a > 0.5) col = r.rgb;
}

void decorationOverlay(vec2 mapUV, inout vec3 color) {
    vec2 worldPos = mapUV * u_mapSize;
    vec2 cc = floor(worldPos);
    for (int dy = 1; dy >= -1; dy--) {
        for (int dx = -1; dx <= 1; dx++) {
            vec2 cell = mod(cc + vec2(float(dx), float(dy)), u_mapSize);

            {
                vec2 diff = worldPos - (cell + vec2(0.5, 1.0));
                if (diff.x >  u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
                if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
                if (diff.y >  u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
                if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;
                vec2 luv = (diff + 1.0) / 2.0;
                if (luv.x >= 0.0 && luv.x < 1.0
                    && luv.y >= 0.0 && luv.y < 1.0) {
                    vec4 t = treeDraw(cell, luv, color);
                    if (t.a > 0.5) color = t.rgb;
                }
            }

            {
                vec2 diff = worldPos - (cell + 0.5);
                if (diff.x >  u_mapSize.x * 0.5) diff.x -= u_mapSize.x;
                if (diff.x < -u_mapSize.x * 0.5) diff.x += u_mapSize.x;
                if (diff.y >  u_mapSize.y * 0.5) diff.y -= u_mapSize.y;
                if (diff.y < -u_mapSize.y * 0.5) diff.y += u_mapSize.y;
                vec2 luv = (diff + 1.0) / 2.0;
                if (luv.x >= 0.0 && luv.x < 1.0
                    && luv.y >= 0.0 && luv.y < 1.0) {
                    vec4 m = mtnDraw(cell, luv, color);
                    if (m.a > 0.5) color = m.rgb;
                }
            }

            landmarkDraw(cell, worldPos, color);
        }
    }
}

vec3 riverOverlay(vec2 mapUV, vec3 baseColor) {
    float riverVal = texture(u_riverMap, mapUV).r;
    if (riverVal <= 0.02) return baseColor;

    float height = texture(u_master, mapUV).r;
    if (height < u_seaLevel) return baseColor;

    float riverStrength = smoothstep(0.02, 0.40, riverVal);
    vec3 riverColor = mix(vec3(0.12, 0.35, 0.52),
                          vec3(0.06, 0.22, 0.40),
                          riverStrength);
    return mix(baseColor, riverColor, riverStrength * 0.85);
}

vec3 zoneTintOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 cell = floor(mapUV * u_mapSize);
    vec2 cellUV = (cell + 0.5) / u_mapSize;
    float zone = texture(u_zoneMap, cellUV).r * 255.0;
    if (zone < 4.5) return baseColor;

    float t = clamp((zone - 4.0) / 5.0, 0.0, 1.0);
    vec3 hazard = mix(vec3(0.72, 0.50, 0.18), vec3(0.58, 0.08, 0.06), t);
    float opacity = mix(0.055, 0.13, t);
    return mix(baseColor, hazard, opacity);
}

void main() {
    // Map screen pixel → world pixel.
    vec2 worldPx = (v_uv - 0.5) * u_viewSize / u_zoom + u_cam;
    // Continuous (per-pixel) toroidal mapUV so feature overlays can resolve
    // sub-cell pixel-art detail. Cell-snapped sampling is done inside each
    // overlay where needed (e.g. featureMap lookup at cell centre).
    vec2 mapUV   = fract(worldPx / u_mapSize);

    vec3 col = biomeTextureOverlay(worldPx);

    col = riverOverlay(mapUV, col);

    // ── Feature overlays (TS-faithful order: roads, then painter decorations) ──
    col = roadOverlay(mapUV, col);
    col = dirtRoadOverlay(mapUV, col);
    decorationOverlay(mapUV, col);
    col = zoneTintOverlay(mapUV, col);

    // ── Cell-grid overlay (torus structure) — fades in at high zoom ──
    {
        vec2 fpx  = fract(worldPx);
        float edge = min(min(fpx.x, fpx.y), min(1.0 - fpx.x, 1.0 - fpx.y));
        float gridFade = smoothstep(6.0, 16.0, u_zoom);
        float inside   = smoothstep(0.0, 0.06, edge);
        col *= 1.0 - 0.30 * gridFade * (1.0 - inside);
    }

    // ── Night tint ──
    float night = clamp(abs(u_timeOfDay - 0.5) * 2.0 - 0.4, 0.0, 1.0);
    col *= mix(vec3(1.0), vec3(0.30, 0.35, 0.55), night);

    fragColor = vec4(col, 1.0);
}
)GLSL";

static const std::string kFS =
    std::string(kFS0) + kFS1 + kFS2;

bool MacroRenderer::init() {
    prog = gl_link(kVS, kFS.c_str());
    if (!prog) return false;
    static const float verts[] = {-1, -1, 3, -1, -1, 3};
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    return true;
}
void MacroRenderer::destroy() {
    if (prog) glDeleteProgram(prog);
    if (featureTex) glDeleteTextures(1, &featureTex);
    if (zoneTex) glDeleteTextures(1, &zoneTex);
    if (landmarkTex) glDeleteTextures(1, &landmarkTex);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    *this = {};
}
void MacroRenderer::upload_features(const FeatureLayer& fl) {
    static constexpr std::uint8_t kBlankFeaturePixel = 0;
    if (featureTex) glDeleteTextures(1, &featureTex);
    const std::uint8_t* uploadData = &kBlankFeaturePixel;
    int uploadW = 1;
    int uploadH = 1;
    const std::uint8_t* featureData =
        fl.complete_cells_or_sanitized(featureUploadScratch);
    if (featureData) {
        uploadW = fl.width;
        uploadH = fl.height;
        uploadData = featureData;
    }
    featureTex = gl_make_texture_r8(uploadW, uploadH, uploadData,
                                    GL_NEAREST, GL_REPEAT);
}
void MacroRenderer::upload_zones(const ZoneLayer& zl) {
    static constexpr std::uint8_t kBlankZonePixel = 0;
    if (zoneTex) glDeleteTextures(1, &zoneTex);
    const bool valid = zl.has_data_storage();
    const std::uint8_t* uploadData = &kBlankZonePixel;
    int uploadW = 1;
    int uploadH = 1;
    if (valid) {
        uploadW = zl.width;
        uploadH = zl.height;
        uploadData = zl.data.data();
        const std::size_t total = zl.cell_count();
        for (std::size_t i = 0; i < total; ++i) {
            if (zl.data[i] >= std::uint8_t(kZoneCount)) {
                zoneUploadScratch.resize(total);
                for (std::size_t j = 0; j < total; ++j) {
                    zoneUploadScratch[j] = ZoneLayer::decode(zl.data[j]);
                }
                uploadData = zoneUploadScratch.data();
                break;
            }
        }
    }
    zoneTex = gl_make_texture_r8(uploadW, uploadH, uploadData, GL_NEAREST, GL_REPEAT);
}
void MacroRenderer::upload_landmarks(int w, int h, const std::uint8_t* data) {
    static constexpr std::uint8_t kBlankLandmarkPixel = 0;
    if (landmarkTex) glDeleteTextures(1, &landmarkTex);
    std::size_t total = 0;
    const bool valid = data && FeatureLayer::cell_count_for(w, h, total);
    const std::uint8_t* uploadData = &kBlankLandmarkPixel;
    int uploadW = 1;
    int uploadH = 1;
    if (valid) {
        uploadW = w;
        uploadH = h;
        uploadData = data;
        for (std::size_t i = 0; i < total; ++i) {
            if (data[i] > 2u) {
                landmarkUploadScratch.resize(total);
                for (std::size_t j = 0; j < total; ++j) {
                    landmarkUploadScratch[j] = data[j] <= 2u ? data[j] : 0u;
                }
                uploadData = landmarkUploadScratch.data();
                break;
            }
        }
    }
    landmarkW = uploadW; landmarkH = uploadH;
    landmarkTex = gl_make_texture_r8(uploadW, uploadH, uploadData, GL_NEAREST, GL_REPEAT);
}
void MacroRenderer::rebuild_landmarks(const GameState& gs) {
    std::size_t total = 0;
    if (!FeatureLayer::cell_count_for(gs.mapW, gs.mapH, total)) {
        upload_landmarks(1, 1, nullptr);
        return;
    }
    std::vector<std::uint8_t> grid(total, 0u);
    auto stamp = [&](int x, int y, std::uint8_t v) {
        int wx = ((x % gs.mapW) + gs.mapW) % gs.mapW;
        int wy = ((y % gs.mapH) + gs.mapH) % gs.mapH;
        grid[std::size_t(wy) * gs.mapW + wx] = v;
    };
    // Villages first; cities take precedence on collision.
    for (const auto& v : gs.villages) stamp(v.x, v.y, 2u);
    for (const auto& s : gs.settlements) stamp(s.x, s.y, 1u);
    upload_landmarks(gs.mapW, gs.mapH, grid.data());
}
void MacroRenderer::draw(const TerrainData& td, float camX, float camY, float zoom,
                         int viewW, int viewH, const WorldTime& time,
                         float seaLevel) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, viewW, viewH);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, td.texture);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, featureTex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, zoneTex);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, landmarkTex);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, td.riverTexture);
    glUniform1i(glGetUniformLocation(prog, "u_master"),     0);
    glUniform1i(glGetUniformLocation(prog, "u_featureMap"), 1);
    glUniform1i(glGetUniformLocation(prog, "u_zoneMap"),    2);
    glUniform1i(glGetUniformLocation(prog, "u_landmarkMap"),3);
    glUniform1i(glGetUniformLocation(prog, "u_riverMap"),   4);
    glUniform2f(glGetUniformLocation(prog, "u_mapSize"),    float(td.width), float(td.height));
    glUniform2f(glGetUniformLocation(prog, "u_cam"),        camX, camY);
    glUniform1f(glGetUniformLocation(prog, "u_zoom"),       zoom);
    glUniform2f(glGetUniformLocation(prog, "u_viewSize"),   float(viewW), float(viewH));
    glUniform1f(glGetUniformLocation(prog, "u_seaLevel"),   seaLevel);
    glUniform1f(glGetUniformLocation(prog, "u_seed"),       1.0f);
    glUniform1f(glGetUniformLocation(prog, "u_mtnThreshold"),
                kDefaultFeatureMountainThreshold);
    float tod = (time.hour * 60 + time.minute) / (24.0f * 60.0f);
    glUniform1f(glGetUniformLocation(prog, "u_timeOfDay"),  tod);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

} // namespace sm
