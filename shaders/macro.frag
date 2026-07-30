#version 450
// Macroworld synth (Phase 4c): the full data-texture pipeline on Vulkan.
// Four sampled images (master + feature + zone + river) drive a procedural
// ground: 10 per-biome bt_* textures, neighbour blend, wet-sand shore band,
// climate (snow/ice) overlay, cobblestone roads,
// compact tree marks, a hillshaded relief massif for mountains, and a
// danger-zone tint. Ported from the GL reference (src/macro/macro_renderer.cpp
// kFS); pixel-art tree sprites + landmarks + night lights remain a later step.
layout(set = 0, binding = 0) uniform sampler2D u_master;     // R=h G=moist B=temp A=mask
layout(set = 0, binding = 1) uniform sampler2D u_featureMap; // R8: FeatureType byte
layout(set = 0, binding = 2) uniform sampler2D u_zoneMap;    // R8: zone 0..9
layout(set = 0, binding = 3) uniform sampler2D u_riverMap;   // R8 river mask (gameplay state; no longer sampled for the ground render -- rivers render as Biome::Water)
layout(set = 0, binding = 4) uniform sampler2D u_lightField; // RGB night glow (macro_lighting bake)
layout(set = 0, binding = 5) uniform sampler2D u_treeMap;    // R8: tree count / 16384 (macro/tree_layer.h)

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
    float elapsed;   // real seconds — drives haze flow / water shimmer
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

// -- Mountain ground texture (biome base under the relief massif) --
// Neutral mottled stone; multiplied by the Mountain base colour in bt_tex.
// This is the massif FOOT/base that blends with neighbour biomes; the lit
// relief (facets, terraces, snow) is composited on top in mountainOverlay().
vec3 bt_mountain(vec2 wp, float sd) {
    wp += sd * 0.21;
    vec2 P = pc.mapSize * 16.0;
    float coarse = bt_fbm_p(wp * 0.07, P * 0.07, 3);        // broad rock mottling
    float grit   = bt_noise_p(wp * 0.26 + 60.0, P * 0.26);  // finer scree grain
    float grain  = bt_hash(wp) * 0.04;
    vec3 m = mix(vec3(0.86, 0.85, 0.83), vec3(1.02, 1.01, 1.00),
                 smoothstep(0.40, 0.70, coarse));
    m -= grit * 0.05; m += grain; return m;
}

// -- Biome classification (3x3 climate matrix; 9 = water, 10 = mountain) --
// Mountain is an elevation override OUTSIDE the climate matrix, mirroring how
// water is classified by being below sea level (see biomes.h biome_at). Keeping
// it here (a biome, before the feature overlays) is what dissolves the old hard
// mountain border: neighbour-blended biome ground grades cleanly into the foot.
const float MTN_LEVEL = 0.75;   // == sm::kMountainBiomeLevel
int bt_biome(vec2 cell) {
    vec2 uv = fract((cell + 0.5) / pc.mapSize);
    vec4 m  = texture(u_master, uv);
    if (m.r < pc.seaLevel) return 9;
    if (m.r >= MTN_LEVEL)  return 10;
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
    if (b == 10) return bt_mountain(wp, sd);
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
    if (b == 10) return vec3(0.55, 0.53, 0.50);   // == kBiomes[Mountain]
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

// -- Rivers --
// Rivers are NOT a separate overlay any more. Generation carves every river
// cell below sea level (map_generator.cpp generate_river_data), so bt_biome()
// classifies it as water (id 9) and biomeTextureOverlay() renders it through
// the EXACT same path as the sea: blue water + the wet-sand SDF shore band.
// This is the "rivers are honest water cells" model -- a 1-cell river reads as
// a thin sea inlet with crisp banks, with none of the old linear-bled speckle
// that the removed riverOverlay() produced on the adjacent land cells.

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
    // A road is a road for connectivity: cobble (FT_Road=1) and dirt (FT_DirtRoad=2)
    // link into one network; the current cell's byte picks the surface style.
    return fid > 0.5 && fid < 2.5;
}
// Forest sprite density — BINARY at the forest-class line, procedural above
// it. 0.0 below u_treeMap 0.5 (kForestClassTreeCount = 8192, the same
// threshold that makes the cell Forest-mode in the subworld) — the ЧЁТКАЯ
// граница condition: no fades, a cell either grows canopy or it does not.
// Above the line the count remaps to (0,1] and drives HOW MUCH canopy the
// cell sources (crown count + crown size in featureDecor) — the procedural
// density. Man-made cells (roads, and the settlement cells that always sit
// on one) source nothing: the canopy recedes into an organic clearing
// around them, shaped by the neighbours' overhanging crowns.
float forestSpriteDensity(vec2 cell) {
    vec2 uv = mod(cell + 0.5, pc.mapSize) / pc.mapSize;
    float d = texture(u_treeMap, uv).r;
    if (d < 0.5) return 0.0;
    if (texture(u_featureMap, uv).r * 255.0 > 0.5) return 0.0;
    return (d - 0.5) * 2.0;
}
vec3 forestLeafColor(float temp01, float h) {
    vec3 oak    = vec3(0.10, 0.32, 0.12);
    vec3 cherry = vec3(0.34, 0.45, 0.20);
    vec3 birch  = vec3(0.17, 0.40, 0.17);
    vec3 autumn = vec3(0.52, 0.31, 0.10);
    vec3 pine   = vec3(0.06, 0.23, 0.15);
    vec3 willow = vec3(0.13, 0.34, 0.17);
    vec3 jungle = vec3(0.04, 0.29, 0.08);
    if (temp01 < 0.20) return pine;
    if (temp01 < 0.35) return h < 0.45 ? pine : birch;
    if (temp01 < 0.50) return h < 0.45 ? birch : autumn;
    if (temp01 < 0.65) return h < 0.40 ? oak : (h < 0.70 ? autumn : willow);
    if (temp01 < 0.80) return h < 0.35 ? cherry : (h < 0.65 ? oak : willow);
    return jungle;
}
vec4 forestBlob(vec2 srcCell, vec2 p, vec2 ctr, float r, float alphaMul) {
    float rough = (bt_hash(floor(p) + srcCell * 13.7) - 0.5) * 1.6;
    float d = length(p - ctr) + rough;
    if (d >= r) return vec4(0.0);
    float h = bt_hash(srcCell + pc.seed * 2.3);
    vec3 leaf = forestLeafColor(bt_temperature(srcCell), h);
    leaf = mix(leaf * 0.78, leaf * 1.16, bt_hash(p + srcCell));
    if (p.y < ctr.y - r * 0.30) leaf *= 1.12;
    else if (p.y > ctr.y + r * 0.30) leaf *= 0.82;
    float edge = smoothstep(r, r - 0.8, d);   // tighter rim => crisper crown
    return vec4(leaf, edge * alphaMul);
}
vec3 roadOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 pixelCoord = mapUV * pc.mapSize;
    vec2 cell = floor(pixelCoord);
    vec2 cellUV = (cell + 0.5) / pc.mapSize;
    float featureId = texture(u_featureMap, cellUV).r * 255.0;
    bool isCobble = (featureId > 0.5 && featureId < 1.5);   // FT_Road
    bool isDirt   = (featureId > 1.5 && featureId < 2.5);   // FT_DirtRoad
    if (!isCobble && !isDirt) return baseColor;

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

    float hw = isDirt ? 2.6 : 3.0;
    if (md > hw) return baseColor;

    float cs = cell.x * 127.1 + cell.y * 311.7 + pc.seed;

    // -- Dirt track: packed warm earth, grainy, frayed edge into the ground. --
    if (isDirt) {
        float ph = roadHash(cs + p.x * 17.31 + p.y * 43.77);
        vec3 dirt1 = vec3(0.45, 0.35, 0.23);
        vec3 dirt2 = vec3(0.37, 0.28, 0.18);
        vec3 roadColor = mix(dirt1, dirt2, ph);
        if (ph > 0.86) roadColor *= 1.12;               // dry clods
        else if (ph < 0.14) roadColor *= 0.84;          // damp hollows
        float edge = smoothstep(hw - 1.6, hw, md);
        roadColor = mix(roadColor, baseColor * 0.90, edge * 0.55); // frayed earthy rim
        float opacity = 0.90 - edge * 0.45;
        return mix(baseColor, roadColor, opacity);
    }

    // -- Cobblestone highway (FT_Road). --
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

// -- Tree marks: PROCEDURAL crown density from the per-cell count, with the
// hard forest-class gate. Every forest-class cell in the fragment's 3×3
// sources 1..4 organic crown blobs (the 10-commits-ago visual primitive:
// rough hashed edges, per-cell species colour, top-lit shading) — MORE and
// LARGER crowns the higher its count, so a massif core closes into solid
// canopy while its rim thins to scattered single crowns, and the canopy
// visibly thickens toward denser neighbours (the 3×3 context emerges because
// every fragment composites all nine neighbours' crowns). The boundary is
// crisp — a cell under the class line grows nothing — but its SHAPE is the
// noisy union of crown circles, локально случайная как берег, never a
// straight cell edge and never an alpha fade. --
vec3 featureDecor(vec2 worldPx, vec3 col) {
    vec2 cell = floor(worldPx);
    vec2 p = floor(fract(worldPx) * 16.0) + 0.5;
    vec3 acc = vec3(0.0);
    float alpha = 0.0;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            vec2 src = cell + vec2(float(ox), float(oy));
            float d = forestSpriteDensity(src);
            if (d <= 0.0) continue;
            // 1..4 crowns; radius grows with density too, so edge cells read
            // as отдельные кроны and cores as a closed canopy.
            int K = 1 + int(min(d, 0.999) * 3.0);
            for (int k = 0; k < 4; ++k) {
                if (k >= K) break;
                float fk = float(k);
                vec2 ctr = vec2(float(ox), float(oy)) * 16.0
                    + vec2(2.0 + 12.0 * bt_hash(src + fk * 17.31 + pc.seed),
                           2.0 + 12.0 * bt_hash(src + fk * 29.77 + pc.seed * 1.7));
                float r = (2.6 + 2.0 * bt_hash(src + fk * 41.3))
                        * (0.70 + 0.55 * d);
                vec4 st = forestBlob(src, p, ctr, r, 1.0);
                acc += st.rgb * st.a;
                alpha += st.a;
            }
        }
    }
    if (alpha > 0.01) col = mix(col, acc / alpha, clamp(alpha, 0.0, 0.94));
    return col;
}

// -- Danger-zone tint --
float zoneAt(vec2 cell) {
    return texture(u_zoneMap, fract((cell + 0.5) / pc.mapSize)).r * 255.0;
}
// Danger reads as a WISPY CRIMSON HAZE, not a colour mix into the tiles
// (owner: the old flat tint muddied the ground and stained water brown).
// The zone byte is sampled bilinearly across cell centres so the fog
// THICKENS CONTEXTUALLY toward dangerous country instead of stamping cell
// squares, and a slow-drifting procedural fbm breaks it into patches — the
// player senses the land *differs* without being told exactly how. Works
// identically over land and water (it is air, not ground). Deliberately
// thin: peak ~15% in the deepest zones where a wisp crests.
vec3 zoneTintOverlay(vec2 mapUV, vec3 baseColor) {
    vec2 wp = mapUV * pc.mapSize;
    vec2 c = wp - 0.5, b = floor(c), f = fract(c);
    f = f * f * (3.0 - 2.0 * f);
    float z00 = zoneAt(b);
    float z10 = zoneAt(b + vec2(1.0, 0.0));
    float z01 = zoneAt(b + vec2(0.0, 1.0));
    float z11 = zoneAt(b + vec2(1.0, 1.0));
    float zone = mix(mix(z00, z10, f.x), mix(z01, z11, f.x), f.y);
    // Danger reads as SPARKS — tiny, sharp, high-contrast flashes (owner:
    // «маленькие контрастные вспышечки как искры»). A world-space lattice
    // seeds candidate sparks (density grows with the zone), each spark is a
    // few SCREEN pixels (crisp at every zoom) and fires in brief bright
    // pulses on its own random phase — off most of the time, then a hot
    // crimson-white pop. Pure shader, real-time, no CPU.
    // Danger reads as RARE EMBERS — the conservative shape (after flat
    // tint, fog, sparks and fissures were all rejected): a handful of tiny
    // slow-breathing orange-red points, ONLY in genuinely deadly country
    // (zone ≥ ~6.5 — mid-difficulty wilderness carries nothing, because
    // most of the wild map IS mid-difficulty and any visible effect there
    // becomes screen-wide noise). Sparse by construction: ~one candidate
    // per 1.5-cell lattice cell at a low probability, thinned further as
    // the view pulls back. Each ember mostly smoulders dim and briefly
    // brightens on its own slow phase — quiet, not a light show.
    float t = smoothstep(6.5, 9.0, zone);
    if (t <= 0.001) return baseColor;
    const float L = 1.5;                    // lattice pitch, world cells
    float packPx = L * pc.zoom;             // lattice pitch on screen, px
    float densGate = min(1.0, (packPx / 26.0) * (packPx / 26.0));
    if (densGate <= 0.004) return baseColor;
    vec2 g0 = floor(wp / L);
    vec3 ember = vec3(0.0);
    for (int gy = -1; gy <= 1; ++gy)
    for (int gx = -1; gx <= 1; ++gx) {
        vec2 g = g0 + vec2(float(gx), float(gy));
        float h1 = bt_hash(g);
        if (h1 > t * 0.16 * densGate) continue;   // no ember in this cell
        float h2 = bt_hash(g + 101.3);
        float h3 = bt_hash(g + 202.7);
        vec2 pos = (g + vec2(0.15 + 0.7 * h2, 0.15 + 0.7 * h3)) * L;
        // Uneasy WANDER: each coal creeps along its own slow Lissajous path
        // with a periodic swell in its stride. The coal itself stays ROUND —
        // the velocity-stretched ellipses read badly (owner call).
        float wA = 0.35 + 0.50 * h2, wB = 0.23 + 0.40 * h3;
        float dash = 0.35 + 0.65 * pow(0.5 + 0.5 * sin(pc.elapsed
                                                       * (0.21 + 0.20 * h1)
                                                       + h3 * 9.0), 3.0);
        vec2 wob = vec2(sin(pc.elapsed * wA + h1 * 40.0),
                        cos(pc.elapsed * wB + h2 * 21.0));
        vec2 dpx = (wp - (pos + wob * (0.35 * L) * dash)) * pc.zoom;
        float d2 = dot(dpx, dpx);
        if (d2 > 196.0) continue;
        float rPx  = clamp(0.12 * pc.zoom, 1.0, 2.4);  // world-scaled, clamped
        float dd   = d2;
        // A SMOULDERING coal, not a magic sparkle — and not a polished
        // saucer either: per-pixel CRACKLE eats the gaussian irregularly.
        // Two granular hash scales (screen-pixel grain × a coarser clump),
        // re-rolled a few times a second, so the coal's edge is ragged and
        // its surface flickers like burning carbon instead of plastic.
        float tick = floor(pc.elapsed * 7.0);
        float nz  = bt_hash(floor(dpx * 0.9) + g * 3.7 + tick * 13.1);
        float nz2 = bt_hash(floor(dpx * 0.33) + g * 7.7 + tick * 5.3);
        float crackle = (0.30 + 0.70 * nz) * (0.45 + 0.55 * nz2);
        float body = exp(-dd / (rPx * rPx)) * crackle;
        float heart = exp(-dd / (rPx * rPx * 0.35)) * (0.35 + 0.85 * nz);
        float br = 0.45
                 + 0.30 * sin(pc.elapsed * (0.5 + 0.5 * h3) + h1 * 40.0)
                 + 0.25 * sin(pc.elapsed * (1.3 + h3) + h2 * 21.0);
        br = clamp(br, 0.15, 1.0);
        ember += (vec3(0.55, 0.10, 0.02) * body
                + vec3(1.00, 0.45, 0.10) * heart * 0.8) * br;
    }
    return baseColor + ember * (0.5 + 0.35 * t);
}

// ============================================================================
//  Mountain massif — a shaded relief reconstruction of the real heightmap.
//
//  u_master.r is a ridged multifractal (map_generator synth_master:
//  ridge = pow(1-|noise|,3)*ridgeIntensity), so the mountain cells the feature
//  layer marks (height >= threshold) ALREADY form connected ridge chains in the
//  data. The old renderer discarded that and stamped one identical centred
//  triangle per cell -> a regular grid of pyramids. We instead paint the range
//  as a continuous relief surface, lit by a fixed sun, so it reads as one
//  coherent massif with ridgelines, shaded valleys and snowy crests.
//
//    coverage    smooth mask (bilinear over the mountain feature byte) = WHERE
//    relief      R = macroHeight*amp + ridgedDetail; its gradient -> a normal
//    hillshade   Lambert vs a NW sun -> the primary 3D cue
//    colour      elevation ramp + biome tint + temperature-driven snow line
//    cast shadow short march toward the sun darkens the land SE of a range
//
//  All periodic (torus wrap); samples only already-bound data (u_master
//  linear+repeat, u_featureMap nearest+repeat). No host / no new textures.
// ----------------------------------------------------------------------------
// ── ONE celestial bearing for the whole map (mirrors sub/lighting.h) ─────────
// Sun rises +X and sets −X — the subworld sun's travel plane projected onto
// the map — and at night the slot re-points at the MOON (anti-solar, weak:
// the same kMoonDirGain=0.42 fold the 3D world uses), so the map's relief
// shadows sweep E→W through the day and flip to faint moon-shadows at night.
// xyz = direction TO the light in (mapX, mapY, up); w = strength 0..1.
// The fixed +Y tilt keeps E-W ridges lit (a pure ±X light would flatten
// them); the elevation clamp keeps relief readable at noon and at horizon.
// Raw form: x = azimuth lobe (±cos), y = RAW elevation of the active body
// (before any clamp — the water glint needs to know when the light grazes),
// z = strength 0..1, w = 1 when the moon holds the slot.
vec4 mapCelestialRaw() {
    float a  = (pc.timeOfDay - 0.25) * 6.2831853;
    float ex = cos(a), ey = sin(a);
    float sunI  = smoothstep(-0.05, 0.30, ey);
    float moonI = smoothstep(-0.05, 0.30, -ey) * 0.42;
    float m  = step(sunI, moonI);
    return vec4(mix(ex, -ex, m), mix(ey, -ey, m), max(sunI, moonI), m);
}
// Shading form: xyz = direction TO the light (elevation clamped so relief
// never flattens at noon nor sinks at the horizon), w = strength.
vec4 mapCelestial() {
    vec4 r = mapCelestialRaw();
    float el = clamp(r.y, 0.30, 0.80);
    return vec4(normalize(vec3(r.x * 0.9, 0.45, el)), r.z);
}
// Light tint: the sun warms toward the horizon (sunrise/sunset gold, neutral
// overhead) exactly like sub/lighting.h sunColor; the moon is a cool constant.
vec3 mapCelestialTint() {
    vec4 r = mapCelestialRaw();
    float warm = (1.0 - smoothstep(0.0, 0.4, r.y)) * (1.0 - r.w);
    vec3 sunT = vec3(1.0 - warm * 0.05, 1.0 - warm * 0.28, 1.0 - warm * 0.55);
    // Never a clinically white sun: a permanent light gilding on top of the
    // horizon warmth (owner ask). The moon keeps its cool tint untouched.
    sunT *= vec3(1.00, 0.965, 0.88);
    return mix(sunT, vec3(0.72, 0.82, 1.05), r.w);
}
// Mountain relief light = the shared celestial bearing (was a fixed NW sun).
#define MTN_SUN (mapCelestial().xyz)
const float MTN_AMBIENT = 0.44;   // shadow-side fill (0 => black shadows)
// The mountain elevation cutoff is MTN_LEVEL, defined with bt_biome above
// (mountains are a biome now, not a feature).

// Ridged multifractal: fold value noise about 0.5 for sharp crests, ride the
// higher octaves on the lower ones (Musgrave). Periodic so it wraps the torus.
// Returns ~0..1.3 (octave amplitudes 0.5+0.25+... modulated by `prev`).
float bt_ridge_p(vec2 p, vec2 period, int oct) {
    float sum = 0.0, amp = 0.5, prev = 1.0;
    for (int i = 0; i < 6; i++) {
        if (i >= oct) break;
        float n = bt_noise_p(p, period);
        n = 1.0 - abs(2.0 * n - 1.0);
        n *= n;
        sum += n * amp * prev;
        prev = clamp(n * 1.6, 0.0, 1.0);
        p *= 2.0; period *= 2.0; amp *= 0.5;
    }
    return sum;
}
float mtnHeightSmooth(vec2 wp) {          // bilinear climate height (linear sampler)
    return texture(u_master, fract(wp / pc.mapSize)).r;
}

// -- The massif's 3D FORM is procedural, not the climate height. --------------
// The coarse climate height is a broad, slowly-varying plateau: hillshading it
// at map scale yields a flat wash (near-zero gradient over the interior) and no
// ridgelines. So the shape we light is multi-octave ridged noise at mountain-
// -ridge frequency (base wavelength ~6 cells), which carries the ridge/valley
// drainage structure that reads as a range. The climate height only anchors the
// OVERALL swell (high where the map is high). `mtnRidges` in 0..~1.3 is reused
// for lighting, crest colouring and snow so all three agree on where spines are.
const float MTN_RIDGE_FREQ = 0.16;    // ridges per cell (base octave) -> ~6-cell spines
// Octave count is chosen by the caller from the zoom (see mtnDetailOctaves): the
// finest octaves are dropped when zoomed out so sub-pixel ridges do not shimmer
// into dark grain -- the range stays smooth wide and gets crisp up close.
float mtnRidges(vec2 wp, int oct) {
    return bt_ridge_p(wp * MTN_RIDGE_FREQ, pc.mapSize * MTN_RIDGE_FREQ, oct);
}
// Relief surface whose gradient is the shading normal. Ridged noise dominates
// (it is what you see as slopes); the climate swell adds a gentle base tilt.
float mtnField(vec2 wp, int oct) {
    return mtnRidges(wp, oct) * 1.00 + mtnHeightSmooth(wp) * 0.55;
}
// More ridge octaves the closer we are: ~3 when the map is far (each ridge only
// a few pixels), up to 5 up close. pc.zoom is pixels-per-cell.
int mtnDetailOctaves() {
    return int(clamp(2.0 + log2(max(pc.zoom, 1.0)), 3.0, 5.0));
}
float mtnMaskAt(vec2 cell) {              // 1.0 iff cell is Mountain biome (elevation)
    // Mountains are a biome now (biomes.h), classified by height >= MTN_LEVEL.
    // MTN_LEVEL sits well above sea level, so a cell this high is always land.
    float h = texture(u_master, fract((cell + 0.5) / pc.mapSize)).r;
    return (h >= MTN_LEVEL) ? 1.0 : 0.0;
}
float mtnCoverage(vec2 wp) {              // bilinear over 4 nearest cell centres
    vec2 c = wp - 0.5, b = floor(c), f = fract(c);
    f = f * f * (3.0 - 2.0 * f);
    float m00 = mtnMaskAt(b),                 m10 = mtnMaskAt(b + vec2(1.0, 0.0));
    float m01 = mtnMaskAt(b + vec2(0.0, 1.0)), m11 = mtnMaskAt(b + vec2(1.0, 1.0));
    return mix(mix(m00, m10, f.x), mix(m01, m11, f.x), f.y);
}
float mtnCastShadow(vec2 wp) {            // 1.0 => shadowed by a range toward the sun
    vec3 s   = MTN_SUN;
    vec2 dir = normalize(s.xy);
    // Sight-line rise per cell follows the light's real elevation (0.118 =
    // the old tuned 0.045 at the old fixed sun's z/|xy|), so dawn/dusk throw
    // LONG shadows and noon short ones.
    float slope = 0.118 * s.z / max(length(s.xy), 1e-3);
    float h0 = mtnHeightSmooth(wp), sh = 0.0;
    for (int i = 1; i <= 4; i++) {
        float dist = float(i) * 0.85;
        float hs   = mtnHeightSmooth(wp + dir * dist);
        float need = h0 + dist * slope;                   // sight line rising to the sun
        float occ  = smoothstep(need, need + 0.05, hs)    // occluder rises above the line
                   * smoothstep(MTN_LEVEL - 0.05, MTN_LEVEL + 0.02, hs); // and is mountain
        sh = max(sh, occ * (1.0 - float(i - 1) * 0.16));  // fade with distance
    }
    return clamp(sh, 0.0, 1.0);
}
vec3 mtnRockColor(float elev, float relief01, int biome, vec2 wp, float foot01) {
    // elevation ramp: warm stony scree low -> neutral stone -> cool grey crest.
    // `elev` blends climate height with the procedural relief so the interior
    // (where the climate height saturates) still grades from warm valleys to
    // grey summits instead of one flat brown. Kept light so the foot never
    // muddies into near-black.
    // Discrete-feeling rock ramp: warm scree low -> neutral stone -> cool grey
    // crest. `elev`/`relief01` arrive already TERRACED from the caller, so this
    // ramp is flat within a terrace -- no continuous geology/grain noise, which
    // would smear the cel-shading back into a photographic wash. The crispness is
    // the point; texture comes from the terrace steps + posterised light instead.
    vec3 low  = vec3(0.49, 0.43, 0.36);
    vec3 mid  = vec3(0.57, 0.53, 0.49);
    vec3 high = vec3(0.72, 0.73, 0.75);
    vec3 rock = (elev < 0.5) ? mix(low, mid, elev * 2.0)
                             : mix(mid, high, (elev - 0.5) * 2.0);
    // gully occlusion from the (terraced) relief -> steps cleanly, no smear
    rock *= mix(0.80, 1.04, relief01);
    // subtle biome hue bleed only at the soft foot so ranges sit in their biome
    rock = mix(rock, rock * (0.65 + 0.70 * bt_baseColor(biome)), 0.10 * (1.0 - foot01));
    return rock;
}
vec3 mountainOverlay(vec2 worldPx, vec3 col) {
    // Render the massif on the SAME 16-subcell pixel grid as the biome ground so
    // it reads as crisp pixel art, not a smooth photographic wash. Every field is
    // sampled at the quantised subcell centre `q`, so each 1/16-cell block is one
    // flat "pixel". The massif FORM (ridged relief, ridgelines, snowy crests) is
    // unchanged -- only its rendering resolution and shading are quantised.
    vec2 q = (floor(worldPx * 16.0) + 0.5) / 16.0;

    float cov = mtnCoverage(q);
    float sh  = mtnCastShadow(q);
    if (cov < 0.004 && sh < 0.004) return col;      // fast out (most of the map)

    float hm   = mtnHeightSmooth(q);
    float land = smoothstep(pc.seaLevel - 0.02, pc.seaLevel + 0.02, hm);
    if (sh > 0.0)                                   // cast shadow onto the surrounding land
        col = mix(col, col * vec3(0.74, 0.75, 0.82), sh * (1.0 - cov) * 0.55 * land);

    // Crisp massif outline: threshold the (piecewise-constant per subcell) coverage
    // into a near-hard alpha with only ~1 subcell of AA -> a stair-stepped pixel
    // edge, not the old soft ~1-cell bilinear foot.
    float covA = smoothstep(0.42, 0.58, cov);
    if (covA < 0.004) return col;

    // -- Broad faceted relief (crisp cel-shade, not a worm hillshade). ----------
    // The old shading took the gradient of the FULL-detail ridge field over half a
    // cell, so it lit every wiggle of the ridged noise -> thin glossy "worms". We
    // instead light a COARSE ridge field (few octaves) sampled over a 2-cell
    // epsilon: the finite difference then captures only ridge-SCALE slope, giving
    // each mountain one broad sunlit face and one broad shadow face -- the flat
    // planar look of drawn pixel mountains.
    const int   octN = 3;                                   // few octaves => broad shape
    const float eC   = 2.0;                                 // 2-cell epsilon => broad facets
    float dRx = mtnRidges(q + vec2(eC, 0.0), octN) - mtnRidges(q - vec2(eC, 0.0), octN);
    float dRy = mtnRidges(q + vec2(0.0, eC), octN) - mtnRidges(q - vec2(0.0, eC), octN);
    vec3  N   = normalize(vec3(-dRx, -dRy, eC * 0.55));

    float lambert = max(dot(N, MTN_SUN), 0.0);
    // Hard 3-tone cel-shade: deep shadow / mid / lit, with step() terminators so
    // the boundary between a mountain's faces is a crisp edge, not a gradient.
    float s1 = step(0.36, lambert);
    float s2 = step(0.64, lambert);
    float lightLvl = 0.55 * (s1 - s2) + 1.0 * s2;           // {0, 0.55, 1.0}
    // Handoff fade: when the sun↔moon slot swaps, the DIRECTION flips
    // instantly while both bodies are near strength 0 — the cel faces used
    // to jump their shadows at dusk/dawn (owner glitch report). Converge the
    // faces to a neutral mid tone exactly in that low-strength window; full
    // day (w=1) AND full moon (0.42 ⇒ clamp to 1) keep the crisp relief.
    float celW = clamp(mapCelestial().w / 0.30, 0.0, 1.0);
    lightLvl = mix(0.62, lightLvl, celW);
    float light = MTN_AMBIENT + (1.0 - MTN_AMBIENT) * lightLvl;

    // Massif elevation (broad, zoom-stable) for colour strata + snow placement.
    float ridgeC   = mtnRidges(q, octN);                    // 0..~1.3 coarse relief
    float relief01 = clamp(smoothstep(0.10, 0.78, ridgeC), 0.0, 1.0);
    float e01 = clamp((hm - MTN_LEVEL) / (1.0 - MTN_LEVEL), 0.0, 1.0);
    e01 = e01 * e01 * (3.0 - 2.0 * e01);

    // Terraced elevation -> flat colour steps. Few + coarse (4) so they read as
    // deliberate stylised strata, and each step foot gets one thin dark stroke
    // (a drawn contour), never the fine contour rings of a topographic print.
    const float MTN_TERRACES = 4.0;
    float terr      = relief01 * MTN_TERRACES;
    float bandF     = fract(terr);                          // 0..1 within a step
    float relief01q = floor(terr) / MTN_TERRACES;           // quantised elevation
    float riser     = smoothstep(0.10, 0.0, bandF);         // thin dark step foot

    float elev  = clamp(0.30 * e01 + 0.85 * relief01q, 0.0, 1.0);
    int   biome = bt_biome(floor(worldPx));
    vec3  rock  = mtnRockColor(elev, relief01q, biome, q, e01);
    rock *= 1.0 - riser * 0.28;                             // crisp dark contour at each step

    // -- Chunky snow caps from two CONTEXT numbers: temperature + elevation. -----
    //    Ridged noise crests are thin filaments, so thresholding them paints snow
    //    "worms". Snow is instead placed on a ROUNDED summit field (low-frequency
    //    fbm blobs, biased onto the real high relief) so caps sit as solid patches
    //    on the big summits. Colder climate lowers the line => more of the range
    //    whitens; `warmth` is the seam for future seasons / global temperature
    //    (bias it and every range's line shifts together). Hard step => crisp cap.
    float warmth    = bt_temperature(floor(worldPx));  // + seasonal / global bias here later
    float snowField = 0.55 * bt_fbm_p(q * (MTN_RIDGE_FREQ * 0.55),
                                      pc.mapSize * (MTN_RIDGE_FREQ * 0.55), 3)
                    + 0.45 * relief01;                      // rounded blobs on high ground
    float snowLine  = mix(0.46, 0.70, smoothstep(0.0, 0.6, warmth)); // warm => high line
    float snow = step(snowLine, snowField)                          // crisp blobby cap
               * smoothstep(0.28, 0.52, e01);                       // never on the foot
    vec3 snowCol = mix(vec3(0.78, 0.83, 0.92), vec3(0.99, 1.00, 1.00), s2);
    rock = mix(rock, snowCol, snow);

    float ao     = mix(0.80, 1.0, step(0.34, e01));         // hard 2-step foot darkening
    vec3  relief = rock * light * ao;                       // flat facet light, no rim sheen
    relief *= mix(1.0, 0.80, sh * celW);                    // cast shadow fades with the handoff too

    return mix(col, relief, covA);                          // covA = crisp alpha
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
    // Universal relief light: the SAME celestial bearing shades ALL land from
    // the climate heightmap — long eastern shadows at dawn, soft at noon, the
    // shadow side flipping west by evening, and a faint moon-shade at night.
    // Normalised so perfectly flat ground keeps its exact base colour (only
    // slopes change); water stays flat; the mountain massif repaints its own
    // stronger cel-shaded relief on top of this.
    vec3 waterGlint = vec3(0.0);
    {
        vec4  cel = mapCelestial();
        float e  = 1.5;
        float hL = mtnHeightSmooth(worldPx - vec2(e, 0.0));
        float hR = mtnHeightSmooth(worldPx + vec2(e, 0.0));
        float hD = mtnHeightSmooth(worldPx - vec2(0.0, e));
        float hU = mtnHeightSmooth(worldPx + vec2(0.0, e));
        vec3  N  = normalize(vec3((hL - hR) * 14.0, (hD - hU) * 14.0, 1.0));
        float lam   = clamp(dot(N, cel.xyz), 0.0, 1.0);
        float flat0 = clamp(cel.z, 0.0, 1.0);
        float hm0   = mtnHeightSmooth(worldPx);
        float land  = smoothstep(pc.seaLevel - 0.02, pc.seaLevel + 0.02, hm0);
        float shade = mix(1.0, lam / max(flat0, 1e-3), 0.55 * cel.w * land);
        col *= clamp(shade, 0.55, 1.25);

        // Water glint — the SAME bearing reflecting off open water: a field
        // of sparkles tinted by the light (gold at sunset, cool under the
        // moon) that SPREADS when the light grazes the horizon — the map
        // cousin of the subworld water's sun/moon glitter road — and
        // tightens toward noon. Two periodic noise octaves drift with the
        // game clock so the water lives. Computed here (mask + bearing in
        // scope) but ADDED after the night tint: a specular reflection of
        // the light source must survive the night darkening, exactly like
        // the 3D water's point-light glints.
        float water = 1.0 - land;
        if (water > 0.0) {
            vec4  raw = mapCelestialRaw();
            float low = 1.0 - smoothstep(0.05, 0.55, raw.y);
            float amp = raw.z * (0.30 + 0.70 * low);
            float t   = pc.elapsed * 0.8;
            float s1  = bt_noise_p(worldPx * 2.0 + vec2(t, -0.7 * t),
                                   pc.mapSize * 2.0);
            float s2  = bt_noise_p(worldPx * 6.0 - vec2(0.6 * t, t),
                                   pc.mapSize * 6.0);
            float sp  = s1 * 0.55 + s2 * 0.45;
            // Micro-sparkles fade out as the view pulls back — below ~1 px
            // per wavelet they can only alias into grain.
            float glint = smoothstep(0.80 - 0.10 * low, 0.93, sp)
                        * smoothstep(3.0, 8.0, pc.zoom);

            // THE reflection — one per frame, pure mirror geometry: treat
            // the viewer as an eye above the screen centre and the map as
            // the mirror it is. The light's image lands offset from the
            // centre ALONG ITS AZIMUTH by eyeHeight·tan(zenith) — high sun
            // ⇒ near the centre, low sun/moon ⇒ sliding toward the E/W
            // horizon. Eye height is a fraction of the VIEWPORT, so the
            // spot's screen position and size are zoom-invariant, exactly
            // like a real waterbody following you as you move. It renders
            // only where that point IS water (physically honest), as a
            // sparkle-modulated elongated pool of the light's own colour.
            float el   = max(raw.y, 0.12);              // bound tan(zenith)
            float tanZ = sqrt(max(1.0 - el * el, 0.0)) / el;
            float eyeH = pc.viewSize.y * 0.45;          // virtual eye, px
            // Soft-cap the displacement at ~1/3 of the viewport so the low
            // sun's golden road slides TOWARD the horizon side but never
            // leaves the frame (the moment worth seeing).
            float off = min(eyeH * tanZ, pc.viewSize.x * 0.34);
            vec2  refPx = (worldPx - pc.cam) * pc.zoom
                        - vec2(sign(raw.x) * off, 0.0);
            // SPHERICAL body: one round disc with a soft skirt (the owner's
            // "мир — зеркало" image is round like the light itself); size
            // follows the viewport and swells gently as the light drops.
            // Zoom is part of the size function: the disc tracks the WORLD
            // (~6.5 cells) so pulling back shrinks it toward a compact
            // 48 px gleam instead of smearing a viewport-sized blob over
            // half a sea; zooming in clamps at the viewport fraction that
            // already looked right up close.
            float R = clamp(7.5 * pc.zoom, 56.0,
                            pc.viewSize.y * (0.095 + 0.05 * low));
            vec2  q = refPx / R;
            float spot = exp(-dot(q, q));
            // Gleam with a governed core: the raw term still peaks well
            // above the water albedo, then a Reinhard-style roll-off caps
            // the centre before it nukes to a white sheet while leaving the
            // gentle perimeter (which the owner likes) untouched. The
            // interior shimmer fades when zoomed out — sub-pixel wavelets
            // could only smear the disc into grain.
            float shimmer = mix(1.0, 0.65 + 0.55 * sp,
                                smoothstep(4.0, 10.0, pc.zoom));
            // A tight brilliant kernel inside the soft halo: zoomed out the
            // wide gaussian alone read as pure blur — the kernel keeps a
            // crisp gleaming centre at every distance, still bounded by the
            // Reinhard roll-off below.
            float kernel = exp(-dot(q, q) * 6.0);
            float mirror = (spot * 0.55 + kernel * 1.05 + spot * spot * 0.3)
                         * raw.z * (0.75 + 0.55 * low) * shimmer;
            mirror = mirror / (1.0 + 0.8 * mirror);

            waterGlint = mapCelestialTint()
                       * ((glint * 0.55 + 0.05) * amp + mirror)
                       * water;
        }
    }
    // Mountains are a biome: draw the relief massif as part of the ground, BEFORE
    // the feature overlays, so rivers, roads and trees compose ON TOP of it (a
    // forested / roaded peak) instead of the massif occluding them. This layering
    // is what turns the old hard mountain border into a clean iso-height foot.
    col = mountainOverlay(worldPx, col);
    // Rivers need no overlay: carved below sea in generation, they render as
    // Biome::Water inside biomeTextureOverlay() -- crisp banks, exactly like the sea.
    col = roadOverlay(mapUV, col);
    col = featureDecor(worldPx, col);
    col = zoneTintOverlay(mapUV, col);

    // Night tint (TS renderer.ts night pass) + universal landmark/settlement
    // glow. The light field stores summed glow encoded as value/kMacroGlowCeil
    // (=1.5); decode by multiplying back, then add it in only as night falls
    // (scaled by the same nightDarken curve) so towns, active spires and any
    // future opt-in POI light their surroundings. Additive over the darkened
    // ground: dark countryside, warm-glowing settlements.
    if (pc.nightDarken > 0.0) {
        col = mix(col, vec3(0.05, 0.05, 0.15), pc.nightDarken * 0.82);
        vec3 glow = texture(u_lightField, mapUV).rgb * 1.5;   // * kMacroGlowCeil
        col += glow * pc.nightDarken * 0.85;                  // 0.85: tunable strength
    }
    // Specular water glint LAST: a reflection of the light source itself, so
    // it rides on top of the night darkening (the moon road stays visible on
    // dark water, exactly like the 3D water's glints).
    col += waterGlint;
    outColor = vec4(col, 1.0);
}
