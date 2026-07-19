#version 450
// Macroworld synth (Phase 4c): the full data-texture pipeline on Vulkan.
// Four sampled images (master + feature + zone + river) drive a procedural
// ground: 10 per-biome bt_* textures, neighbour blend, wet-sand shore band,
// climate (snow/ice) overlay, meandering river overlay, cobblestone roads,
// compact tree/mountain marks, and a danger-zone tint. Ported from the GL
// reference (src/macro/macro_renderer.cpp kFS). Full pixel-art tree/mountain
// sprites + landmarks + night lights remain a later increment.
layout(set = 0, binding = 0) uniform sampler2D u_master;     // R=h G=moist B=temp A=mask
layout(set = 0, binding = 1) uniform sampler2D u_featureMap; // R8: FeatureType byte
layout(set = 0, binding = 2) uniform sampler2D u_zoneMap;    // R8: zone 0..9
layout(set = 0, binding = 3) uniform sampler2D u_riverMap;   // R8: river strength

layout(push_constant) uniform Push {
    vec2 resolution;
    vec2 mapSize;
    vec2 cam;        // world-pixel offset at screen centre
    vec2 viewSize;   // viewport size in pixels
    float zoom;      // pixels per world cell
    float seaLevel;
    float seed;
    float timeOfDay; // 0..1
    float nightDarken; // 0..1 night strength (TS GameScreen curve)
} pc;

layout(location = 0) out vec4 outColor;

// -- Common noise primitives --
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
// Periodic value-noise / fbm so per-cell fields tile across the world wrap.
float bt_noise_p(vec2 p, vec2 period) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    vec2 i0 = mod(i, period);
    vec2 i1 = mod(i + vec2(1.0), period);
    float a = bt_hash(i0);
    float b = bt_hash(vec2(i1.x, i0.y));
    float c = bt_hash(vec2(i0.x, i1.y));
    float d = bt_hash(i1);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float bt_fbm_p(vec2 p, vec2 period, int oct) {
    float v = 0.0, a = 0.5, t = 0.0;
    for (int i = 0; i < 6; i++) {
        if (i >= oct) break;
        v += a * bt_noise_p(p, period);
        t += a;
        p *= 2.0;
        period *= 2.0;
        a *= 0.5;
    }
    return v / t;
}

// -- Per-biome procedural textures --
vec3 bt_tundra(vec2 wp, float sd) {
    wp += sd * 0.17;
    vec2 P = pc.mapSize * 16.0;
    float lichen = bt_fbm_p(wp * 0.06, P * 0.06, 3);
    float rock   = bt_noise_p(wp * 0.18 + 30.0, P * 0.18);
    float grain  = bt_hash(wp) * 0.04;
    float patches = smoothstep(0.35, 0.65, lichen);
    vec3 m = mix(vec3(0.88, 0.86, 0.84), vec3(0.94, 0.96, 0.88), patches);
    m += grain; m += rock * 0.06; return m;
}
vec3 bt_taiga(vec2 wp, float sd) {
    wp += sd * 0.23;
    vec2 P = pc.mapSize * 16.0;
    float needles     = bt_fbm_p(wp * 0.12, P * 0.12, 2);
    float undergrowth = bt_noise_p(wp * 0.05 + 50.0, P * 0.05);
    float bark        = bt_hash(wp) * 0.03;
    vec3 m = vec3(0.90 + needles * 0.08, 0.94 + undergrowth * 0.10, 0.88 + needles * 0.06);
    m += bark;
    float dark = smoothstep(0.55, 0.40, undergrowth);
    m *= 1.0 - dark * 0.08;
    return m;
}
vec3 bt_snow(vec2 wp, float sd) {
    wp += sd * 0.31;
    vec2 P = pc.mapSize * 16.0;
    float drift   = bt_noise_p(vec2(wp.x * 0.14 + wp.y * 0.04, wp.y * 0.08) + 20.0, vec2(P.x * 0.14, P.y * 0.08));
    float detail  = bt_noise_p(wp * 0.30 + 70.0, P * 0.30);
    float sparkle = step(0.965, bt_hash(wp));
    vec3 m = vec3(0.97 + drift * 0.04, 0.97 + drift * 0.03, 0.99 + drift * 0.02);
    m -= detail * 0.03; m += sparkle * 0.06; m.b += 0.01; return m;
}
vec3 bt_valley(vec2 wp, float sd) {
    wp += sd * 0.19;
    vec2 P = pc.mapSize * 16.0;
    float grass  = bt_fbm_p(wp * 0.09, P * 0.09, 3);
    float earth  = bt_noise_p(wp * 0.22 + 40.0, P * 0.22);
    float stones = step(0.88, bt_noise_p(wp * 0.45 + 15.0, P * 0.45));
    float grain  = bt_hash(wp) * 0.03;
    vec3 grassMod = vec3(0.93, 0.98, 0.88);
    vec3 earthMod = vec3(0.98, 0.93, 0.86);
    vec3 m = mix(grassMod, earthMod, smoothstep(0.4, 0.6, earth));
    m += grass * 0.06; m += grain; m -= stones * 0.06; return m;
}
vec3 bt_meadow(vec2 wp, float sd) {
    wp += sd * 0.13;
    vec2 P = pc.mapSize * 16.0;
    float grass = bt_fbm_p(wp * 0.10, P * 0.10, 3);
    float sway  = bt_noise_p(wp * 0.04 + 60.0, P * 0.04);
    float grain = bt_hash(wp) * 0.025;
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
    vec2 P = pc.mapSize * 16.0;
    float murk  = bt_fbm_p(wp * 0.08, P * 0.08, 3);
    float pool  = smoothstep(0.42, 0.32, bt_noise_p(wp * 0.15 + 25.0, P * 0.15));
    float moss  = bt_noise_p(wp * 0.28 + 80.0, P * 0.28);
    float grain = bt_hash(wp) * 0.03;
    vec3 m = vec3(0.88 + murk * 0.08, 0.92 + moss * 0.08, 0.86 + murk * 0.05);
    m -= pool * vec3(0.06, 0.04, 0.02);
    m *= 1.0 - pool * 0.10; m += grain; return m;
}
vec3 bt_desert(vec2 wp, float sd) {
    wp += sd * 0.21;
    vec2 P = pc.mapSize * 16.0;
    float ripple = bt_noise_p(vec2(wp.x * 0.12 + wp.y * 0.03, wp.y * 0.06) + 35.0, vec2(P.x * 0.12, P.y * 0.06));
    float dune   = bt_noise_p(wp * 0.04 + 90.0, P * 0.04);
    float grain  = bt_hash(wp) * 0.025;
    vec3 m = vec3(1.00 + ripple * 0.06, 0.97 + ripple * 0.04, 0.92 + dune * 0.04);
    m += dune * vec3(0.04, 0.02, 0.0);
    m += grain;
    float shadow = smoothstep(0.55, 0.45, ripple);
    m *= 1.0 - shadow * 0.04;
    return m;
}
vec3 bt_steppe(vec2 wp, float sd) {
    wp += sd * 0.37;
    vec2 P = pc.mapSize * 16.0;
    float wind  = bt_noise_p(vec2(wp.x * 0.10, wp.y * 0.03) + 45.0, vec2(P.x * 0.10, P.y * 0.03));
    float tufts = bt_fbm_p(wp * 0.14, P * 0.14, 2);
    float grain = bt_hash(wp) * 0.025;
    vec3 m = vec3(0.98 + wind * 0.05, 0.96 + tufts * 0.06, 0.90 + wind * 0.03);
    m += grain;
    float bare = smoothstep(0.62, 0.68, tufts);
    m += bare * vec3(0.04, 0.02, 0.0);
    return m;
}
vec3 bt_tropics(vec2 wp, float sd) {
    wp += sd * 0.41;
    vec2 P = pc.mapSize * 16.0;
    float canopy = bt_fbm_p(wp * 0.11, P * 0.11, 3);
    float gap    = smoothstep(0.58, 0.68, bt_noise_p(wp * 0.20 + 55.0, P * 0.20));
    float leaf   = bt_noise_p(wp * 0.35 + 10.0, P * 0.35);
    float grain  = bt_hash(wp) * 0.02;
    vec3 m = vec3(0.88 + canopy * 0.08, 0.94 + leaf * 0.06, 0.86 + canopy * 0.05);
    m += gap * vec3(0.08, 0.10, 0.04); m += grain; return m;
}
vec3 bt_water(vec2 wp, float sd) {
    wp += sd * 0.11;
    vec2 P = pc.mapSize * 16.0;
    float caustic = bt_noise_p(wp * 0.12 + 5.0, P * 0.12) * bt_noise_p(wp * 0.18 + 65.0, P * 0.18);
    float depth   = bt_noise_p(wp * 0.03, P * 0.03);
    float ripple  = bt_noise_p(wp * 0.25 + 120.0, P * 0.25);
    vec3 m = vec3(0.94 + caustic * 0.08, 0.96 + caustic * 0.06 + depth * 0.04, 1.00 + ripple * 0.03);
    m *= 1.0 - depth * 0.04;
    return m;
}

// -- Biome classification (3x3 climate matrix; 9 = water) --
int bt_biome(vec2 cell) {
    vec2 uv = fract((cell + 0.5) / pc.mapSize);
    vec4 m  = texture(u_master, uv);
    if (m.r < pc.seaLevel) return 9;
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
    if (b == 0) return vec3(0.50, 0.52, 0.45);
    if (b == 1) return vec3(0.22, 0.38, 0.28);
    if (b == 2) return vec3(0.90, 0.92, 0.96);
    if (b == 3) return vec3(0.55, 0.52, 0.32);
    if (b == 4) return vec3(0.40, 0.52, 0.28);
    if (b == 5) return vec3(0.28, 0.38, 0.22);
    if (b == 6) return vec3(0.82, 0.72, 0.48);
    if (b == 7) return vec3(0.68, 0.60, 0.32);
    if (b == 8) return vec3(0.10, 0.35, 0.10);
    return vec3(0.12, 0.22, 0.42);
}

// -- Shore band --
vec3 bt_sandWet() { return vec3(0.55, 0.50, 0.36); }
vec3 bt_sandDry() { return vec3(0.76, 0.70, 0.52); }
float bt_edgeNoise(vec2 cell, float edgeId, float coord01, float sd) {
    float s = bt_hash(cell + sd * 0.137 + edgeId * 7.31);
    return (bt_noise(vec2(coord01 * 4.7 + s * 13.0, edgeId * 3.1 + s * 7.0)) - 0.5) * 5.0;
}
vec3 bt_shoreColor(vec3 baseColor, float d, float grain, vec2 wp, float sd) {
    if (d >= 0.0) {
        if (d > 4.5) return baseColor;
        vec3 sand = mix(bt_sandWet(), bt_sandDry(), smoothstep(0.0, 4.0, d)) + grain;
        float t   = 1.0 - smoothstep(3.5, 4.5, d);
        return mix(baseColor, sand, t);
    }
    float a = -d;
    if (a > 12.0) return baseColor;
    vec2 P = pc.mapSize * 16.0;
    float n   = bt_fbm_p(wp * 0.18 + sd * 0.07, P * 0.18, 3);
    float cov = clamp(smoothstep(12.0, 0.0, a) * (0.55 + n * 0.55), 0.0, 1.0);
    vec3  sand = mix(bt_sandWet(), bt_sandDry(), smoothstep(0.0, 6.0, a)) + grain;
    return mix(baseColor, sand, cov);
}

// -- Climate overlay --
vec3 bt_climateOverlay(vec3 col, vec2 wp, float temp01, bool isWater, float sd) {
    vec2 P = pc.mapSize * 16.0;
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
    vec2 uv = fract((cell + 0.5) / pc.mapSize);
    return texture(u_master, uv).b;
}

// -- Universal biome ground synth --
vec3 biomeTextureOverlay(vec2 worldPx) {
    vec2 cell = floor(worldPx);
    vec2 f    = fract(worldPx);
    vec2 p    = floor(f * 16.0) + 0.5;
    vec2 wpCell = mod(cell, pc.mapSize);
    vec2 wp   = wpCell * 16.0 + p;
    float sd  = pc.seed;
    vec2 cellW = wpCell;

    int  cb      = bt_biome(cell);
    bool isWater = (cb == 9);

    int nbE  = bt_biome(cell + vec2( 1, 0));
    int nbW  = bt_biome(cell + vec2(-1, 0));
    int nbN  = bt_biome(cell + vec2( 0, 1));
    int nbS  = bt_biome(cell + vec2( 0,-1));
    int nbNE = bt_biome(cell + vec2( 1, 1));
    int nbNW = bt_biome(cell + vec2(-1, 1));
    int nbSE = bt_biome(cell + vec2( 1,-1));
    int nbSW = bt_biome(cell + vec2(-1,-1));

    float grain = (bt_hash(wp + sd) - 0.5) * 0.03;

    float sgn  = isWater ? 1.0 : -1.0;
    float dist = 999.0;
    if (isWater != (nbE == 9)) { float n = bt_edgeNoise(cellW, 0.0, p.y / 16.0, sd); dist = min(dist, abs((16.0 - p.x) - n)); }
    if (isWater != (nbW == 9)) { float n = bt_edgeNoise(cellW, 1.0, p.y / 16.0, sd); dist = min(dist, abs(p.x - n)); }
    if (isWater != (nbN == 9)) { float n = bt_edgeNoise(cellW, 2.0, p.x / 16.0, sd); dist = min(dist, abs((16.0 - p.y) - n)); }
    if (isWater != (nbS == 9)) { float n = bt_edgeNoise(cellW, 3.0, p.x / 16.0, sd); dist = min(dist, abs(p.y - n)); }
    if (isWater != (nbNE == 9) && (nbN == 9) == isWater && (nbE == 9) == isWater) dist = min(dist, length(p - vec2(16.0, 16.0)));
    if (isWater != (nbNW == 9) && (nbN == 9) == isWater && (nbW == 9) == isWater) dist = min(dist, length(p - vec2( 0.0, 16.0)));
    if (isWater != (nbSE == 9) && (nbS == 9) == isWater && (nbE == 9) == isWater) dist = min(dist, length(p - vec2(16.0,  0.0)));
    if (isWater != (nbSW == 9) && (nbS == 9) == isWater && (nbW == 9) == isWater) dist = min(dist, length(p - vec2( 0.0,  0.0)));

    vec3 tex = bt_baseColor(cb) * bt_tex(cb, wp, sd);

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

    float reach = isWater ? 4.5 : 12.0;
    if (dist < reach) {
        float d = sgn * dist;
        tex = bt_shoreColor(tex, d, grain, wp, sd);
    }

    float temp01 = bt_temperature(cell);
    tex = bt_climateOverlay(tex, wp, temp01, isWater, sd);
    return tex;
}

// -- River overlay --
float riverVisualValue(vec2 mapUV) {
    vec2 texel = 1.0 / pc.mapSize;
    vec2 wp = mapUV * pc.mapSize;
    float wx = (bt_noise(wp * 0.21 + vec2(pc.seed * 0.17, 13.0)) - 0.5) * 0.35;
    float wy = (bt_noise(wp * 0.19 + vec2(31.0, pc.seed * 0.11)) - 0.5) * 0.35;
    vec2 uv = fract(mapUV + vec2(wx, wy) * texel);
    float v = texture(u_riverMap, uv).r;
    v = max(v, texture(u_riverMap, fract(uv + vec2( texel.x, 0.0))).r * 0.58);
    v = max(v, texture(u_riverMap, fract(uv + vec2(-texel.x, 0.0))).r * 0.58);
    v = max(v, texture(u_riverMap, fract(uv + vec2(0.0,  texel.y))).r * 0.58);
    v = max(v, texture(u_riverMap, fract(uv + vec2(0.0, -texel.y))).r * 0.58);
    v = max(v, texture(u_riverMap, fract(uv + vec2( texel.x,  texel.y))).r * 0.28);
    v = max(v, texture(u_riverMap, fract(uv + vec2(-texel.x,  texel.y))).r * 0.28);
    v = max(v, texture(u_riverMap, fract(uv + vec2( texel.x, -texel.y))).r * 0.28);
    v = max(v, texture(u_riverMap, fract(uv + vec2(-texel.x, -texel.y))).r * 0.28);
    return v;
}
vec3 riverOverlay(vec2 mapUV, vec3 baseColor) {
    float riverVal = riverVisualValue(mapUV);
    if (riverVal <= 0.02) return baseColor;
    float height = texture(u_master, mapUV).r;
    if (height < pc.seaLevel) return baseColor;
    float riverStrength = smoothstep(0.02, 0.48, riverVal);
    float bank = smoothstep(0.04, 0.22, riverVal) * (1.0 - smoothstep(0.30, 0.56, riverVal));
    float glint = bt_noise(mapUV * pc.mapSize * 0.38 + pc.seed * 0.013);
    vec3 riverColor = mix(vec3(0.12, 0.35, 0.52), vec3(0.06, 0.22, 0.40), riverStrength);
    riverColor = mix(riverColor, vec3(0.17, 0.42, 0.58), bank * (0.25 + glint * 0.18));
    return mix(baseColor, riverColor, riverStrength * 0.82);
}

// -- Road overlay (cobblestone) --
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
    vec2 uv = mod(cell + 0.5, pc.mapSize) / pc.mapSize;
    float fid = texture(u_featureMap, uv).r * 255.0;
    return fid > 0.5 && fid < 1.5;
}
vec3 roadOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 pixelCoord = mapUV * pc.mapSize;
    vec2 cell = floor(pixelCoord);
    vec2 cellUV = (cell + 0.5) / pc.mapSize;
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

    float hw = 3.0;
    if (md > hw) return baseColor;

    float cs = cell.x * 127.1 + cell.y * 311.7 + pc.seed;
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

// -- Compact tree / mountain marks (feature 2 = tree, 3 = mountain).
// Full TS pixel-art sprites + landmarks land in a later increment. --
vec3 featureDecor(vec2 worldPx, vec3 col) {
    vec2 cell = floor(worldPx);
    vec2 cellUV = fract((cell + 0.5) / pc.mapSize);
    float fid = texture(u_featureMap, cellUV).r * 255.0;
    vec2 p = floor(fract(worldPx) * 16.0) + 0.5;
    if (fid > 1.5 && fid < 2.5) {
        float cx = 8.0 + (bt_hash(cell + pc.seed) - 0.5) * 4.0;
        float cy = 8.0 + (bt_hash(cell + pc.seed * 1.7) - 0.5) * 4.0;
        float r  = 4.0 + bt_hash(cell + 3.0) * 2.0;
        float d  = length(p - vec2(cx, cy)) + (bt_hash(p + cell) - 0.5) * 1.6;
        if (d < r) {
            vec3 leaf = mix(vec3(0.10, 0.32, 0.12), vec3(0.16, 0.44, 0.16), bt_hash(p + cell));
            if (p.y < cy - r * 0.3) leaf *= 1.15;
            else if (p.y > cy + r * 0.3) leaf *= 0.80;
            col = mix(col, leaf, 0.92);
        }
    } else if (fid > 2.5 && fid < 3.5) {
        float h = texture(u_master, cellUV).r;
        float hParam = clamp((h - pc.seaLevel) / (1.0 - pc.seaLevel), 0.0, 1.0);
        float peakH = mix(9.0, 2.0, hParam);
        float cx = 8.0;
        if (p.y >= peakH && p.y <= 13.0) {
            float frac = (p.y - peakH) / (13.0 - peakH);
            float halfW = 0.5 + frac * 5.0;
            float en = (bt_hash(cell + p.y * 1.3) - 0.5) * 1.4;
            if (abs(p.x - cx) <= halfW + en) {
                vec3 rock = mix(vec3(0.42, 0.40, 0.38), vec3(0.30, 0.29, 0.30), bt_hash(p + cell));
                float side = (p.x - cx) / max(halfW, 0.01);
                rock *= side < -0.3 ? 1.15 : (side > 0.3 ? 0.80 : 1.0);
                if (hParam > 0.35 && p.y < peakH + 2.5) rock = vec3(0.92, 0.94, 0.97);
                col = rock;
            }
        }
    }
    return col;
}

// -- Danger-zone tint --
vec3 zoneTintOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 cell = floor(mapUV * pc.mapSize);
    vec2 cellUV = (cell + 0.5) / pc.mapSize;
    float zone = texture(u_zoneMap, cellUV).r * 255.0;
    if (zone < 4.5) return baseColor;
    float t = clamp((zone - 4.0) / 5.0, 0.0, 1.0);
    vec3 hazard = mix(vec3(0.72, 0.50, 0.18), vec3(0.58, 0.08, 0.06), t);
    float opacity = mix(0.055, 0.13, t);
    return mix(baseColor, hazard, opacity);
}

void main() {
    // Screen pixel -> world pixel, matching the GL macro renderer:
    //   worldPx = (uv - 0.5) * viewSize / zoom + cam
    // Vulkan gl_FragCoord.y is top-down; flip so the map keeps the same
    // vertical orientation the GL path produced.
    vec2 uv = gl_FragCoord.xy / pc.resolution;
    uv.y = 1.0 - uv.y;
    vec2 worldPx = (uv - 0.5) * pc.viewSize / pc.zoom + pc.cam;
    vec2 mapUV = fract(worldPx / pc.mapSize);

    vec3 col = biomeTextureOverlay(worldPx);
    col = riverOverlay(mapUV, col);
    col = roadOverlay(mapUV, col);
    col = featureDecor(worldPx, col);
    col = zoneTintOverlay(mapUV, col);

    // Night tint (TS renderer.ts night pass). Landmark glow deferred.
    if (pc.nightDarken > 0.0) {
        col = mix(col, vec3(0.05, 0.05, 0.15), pc.nightDarken * 0.82);
    }
    outColor = vec4(col, 1.0);
}
