#version 450
// Procedural subworld sky — the pure-shader half of the sky submodule
// (sub/sky.h is the CPU door). A true celestial dome reconstructed from the
// camera basis: day/night/twilight gradient, sun disc + glow, 1–3 procedural
// MOONS with geometric crescents, and a rich TEXTURE-FREE star field (3
// density layers + a Milky-Way band + per-star twinkle) plus drifting FBM
// clouds. Drawn fullscreen before the geometry with depth off: the backdrop.
//
// Everything celestial arrives via the push constants (SkyContext →
// SkyPush): the sun vector is the SAME one sub/lighting.h lights the world
// with, and each moon's direction/phase comes from macro/celestial.h's
// procedural orbits — this shader never invents a position.
layout(push_constant) uniform Push {
    vec4 forward;  // xyz = camera forward, w = moonCount
    vec4 right;    // xyz = camera right,   w = starSizeScale
    vec4 up;       // xyz = camera up,      w = (reserved: weather)
    vec4 p0;       // x=resX y=resY z=fov w=tod(0..1)
    vec4 p1;       // xyz=fogColor w=time(sec)
    vec4 sun;      // xyz = toward the sun (lighting.h's vector), w = (reserved)
    vec4 moonDirSize[3];  // xyz = toward the moon, w = baseSize
    vec4 moonColIllum[3]; // rgb = authored tint,   w = illuminated fraction
} pc;

// Authored constellation stars (macro/celestial.h → sub/sky.h SkyStarsUbo),
// uploaded once at init: xyz = dome direction, w = brightness. Stars only —
// the figures read through placement and brightness, no drawn edges.
layout(set = 0, binding = 0) uniform SkyStars {
    vec4 count;     // x = star count
    vec4 stars[32]; // must mirror sub/sky.h kSkyMaxConstellationStars
} cs;

layout(location = 0) out vec4 outColor;

const float TAU = 6.28318530718;
const float PI  = 3.14159265359;

float h21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1, 0)), f.x),
               mix(h21(i + vec2(0, 1)), h21(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 4; ++i) { v += a * vnoise(p); p = r * p * 2.0; a *= 0.5; }
    return v;
}

// One star layer via a PLANAR dome projection (rd.xz / domeHeight), matching
// the TS reference sky. An equirect az/el grid pinches all cells together at
// the zenith and drags the stars into the dome; the planar projection keeps
// cells roughly uniform overhead. Stars stay world-anchored as the camera
// turns because the projection is on the world-space ray direction.
vec3 starLayer(vec3 rd, float domeH, float scale, float thresh, float sizeS,
               float time, float seed) {
    vec2 suv = rd.xz / domeH * scale + seed;
    vec2 cell = floor(suv);
    vec2 f = fract(suv) - 0.5;
    float present = h21(cell + seed);
    if (present < thresh) return vec3(0.0);
    vec2 pos = (vec2(h21(cell + 11.3 + seed), h21(cell + 27.7 + seed)) - 0.5) * 0.7;
    float mag = h21(cell + 3.1 + seed);
    float d = length(f - pos);
    float b = smoothstep(sizeS * (0.5 + mag), 0.0, d);
    float tw = 0.55 + 0.45 * sin(time * 1.6 + mag * TAU);
    vec3 c = mix(vec3(0.72, 0.82, 1.0), vec3(1.0, 0.9, 0.74), h21(cell + 7.7 + seed));
    return c * b * (0.35 + mag * mag * 1.1) * tw;
}

void main() {
    vec2 res = pc.p0.xy;
    float fov = pc.p0.z;
    float tod = pc.p0.w;
    float time = pc.p1.w;
    vec3 fog = pc.p1.xyz;

    vec2 uv = gl_FragCoord.xy / res;
    float aspect = res.x / res.y;
    float tanHF = tan(fov * 0.5);
    float nx = (uv.x * 2.0 - 1.0) * aspect * tanHF;
    float ny = (1.0 - uv.y * 2.0) * tanHF; // Vulkan y-down -> flip so top = up
    vec3 rd = normalize(pc.forward.xyz + nx * pc.right.xyz + ny * pc.up.xyz);
    float elev = rd.y;

    float dayF = clamp(smoothstep(0.22, 0.35, tod) - smoothstep(0.65, 0.78, tod),
                       0.0, 1.0);
    float nightF = 1.0 - dayF;
    float dawn = smoothstep(0.20, 0.26, tod) * smoothstep(0.35, 0.28, tod);
    float dusk = smoothstep(0.65, 0.72, tod) * smoothstep(0.80, 0.74, tod);
    float twilight = dawn + dusk;

    // 1. Gradient.
    vec3 zenithDay = vec3(0.18, 0.30, 0.62), horizDay = vec3(0.58, 0.68, 0.82);
    vec3 zenithNight = vec3(0.01, 0.01, 0.05), horizNight = vec3(0.04, 0.04, 0.09);
    vec3 twiCol = vec3(0.60, 0.25, 0.08);
    float he = clamp(elev, 0.0, 1.0);
    vec3 skyDay = mix(horizDay, zenithDay, he);
    vec3 skyNight = mix(horizNight, zenithNight, he);
    vec3 col = mix(skyNight, skyDay, dayF);
    col = mix(col, twiCol, twilight * smoothstep(0.25, 0.0, he) * 0.7);
    if (elev < 0.0) col = mix(col, fog, smoothstep(0.0, -0.12, elev));

    // 2. Stars + Milky Way (night).
    if (nightF > 0.03 && elev > -0.02) {
        // Planar dome projection for the star cells (see starLayer): the dome
        // height clamps just above the horizon so low stars do not smear.
        float domeH = max(elev + 0.12, 0.06);
        float az = atan(rd.z, rd.x);
        float el = asin(clamp(rd.y, -1.0, 1.0));
        vec3 mwN = normalize(vec3(0.3, 0.35, 0.9));
        float band = 1.0 - smoothstep(0.0, 0.30, abs(dot(rd, mwN)));
        // Star disc radii scale by the CPU-side knob (macro/celestial.h
        // kSkyStarSizeScale via SkyPush) — the data layer owns the value.
        float starS = pc.right.w;
        vec3 s = vec3(0.0);
        s += starLayer(rd, domeH, 26.0, 0.86, 0.09 * starS, time, 0.0);
        s += starLayer(rd, domeH, 12.0, 0.90, 0.16 * starS, time, 40.0) * 1.6;
        s += starLayer(rd, domeH, 40.0, 1.0 - 0.22 * band, 0.07 * starS,
                       time, 77.0) * (0.6 + band);
        // Milky-Way haze stays an az/el gradient — a smooth band has no cells
        // to pinch, so the pole distortion never shows.
        float haze = band * fbm(vec2(az * 3.0, el * 3.0)) * 0.5;
        s += vec3(0.45, 0.52, 0.72) * haze * 0.10;

        // Authored constellation stars — the sky's ANCHOR figures, drawn over
        // the procedural field: brighter, slightly warmer, with a soft glow so
        // the Wain / Hunter / Serpent read as figures at a glance. World-fixed
        // directions (no dome projection), so they never smear near the
        // horizon; twinkle is gentler than the field's — anchors hold still.
        int csN = int(cs.count.x + 0.5);
        for (int i = 0; i < csN; ++i) {
            vec3 sdir = cs.stars[i].xyz;
            float br  = cs.stars[i].w;
            float d = acos(clamp(dot(rd, sdir), -1.0, 1.0));
            float rad = 0.0075 * (0.55 + 0.45 * br) * starS;
            float core = smoothstep(rad, rad * 0.35, d);
            float glowc = exp(-d * d / (rad * rad) * 1.4) * 0.30;
            float tw = 0.85 + 0.15 * sin(time * 1.1 + float(i) * 2.3);
            s += vec3(0.92, 0.94, 1.0) * (core + glowc)
                 * (0.45 + 0.75 * br) * tw;
        }

        float fade = smoothstep(-0.02, 0.12, elev);
        col += s * nightF * fade;
    }

    // 3. Sun — the disc sits on the EXACT vector the world is lit from
    // (pc.sun = lighting.h's sunDir); visibility fades on its elevation, not
    // on a second copy of the arc formula.
    vec3 sunDir = pc.sun.xyz;
    float sunVis = smoothstep(-0.18, 0.0, sunDir.y);
    float sdot = dot(rd, sunDir);
    float disc = smoothstep(0.9992, 0.9996, sdot);
    float glow = pow(max(sdot, 0.0), 256.0) * 0.6;
    float scatter = pow(max(sdot, 0.0), 8.0) * 0.12;
    vec3 sunCol = mix(vec3(1.0, 0.45, 0.10), vec3(1.0, 0.92, 0.7), dayF);
    col += sunCol * (disc + glow + scatter) * sunVis;

    // 4. Moons — 1..3 procedural bodies from the context. Position, size,
    // tint and illuminated fraction ride the push constants (macro/celestial.h
    // orbits); nothing here is pinned to -sunDir. The crescent SHAPE is
    // geometry: the terminator is where the disc turns away from the sun, and
    // because a moon LAGS the sun by its phase, the lit fraction this draws
    // automatically equals the CPU's moon_illumination01 — same law, no sync.
    // Discs are stylised large so the dominant moon READS as the night's
    // light source; its bloom carries the moon's own tint (a crimson moon
    // gets a crimson halo) and scales with illumination, so a crescent glows
    // faintly and a new moon vanishes.
    int moonCount = int(pc.forward.w + 0.5);
    float moonVis = clamp(nightF * 1.4, 0.0, 1.0);
    for (int i = 0; i < moonCount; ++i) {
        vec3  mdir  = pc.moonDirSize[i].xyz;
        float mRad  = 0.040 * pc.moonDirSize[i].w;
        vec3  mCol  = pc.moonColIllum[i].rgb;
        float illum = pc.moonColIllum[i].w;
        float mD = acos(clamp(dot(rd, mdir), -1.0, 1.0));
        // Terminator: project the on-disc offset onto the sunward tangent.
        // u ∈ [-1,1] across the disc toward the sun; the lit side starts at
        // u = cos(sun–moon separation) — full moon (cos = -1) lights all,
        // new moon (cos = +1) lights none, quarters split the disc.
        vec3 toSunT = sunDir - mdir * dot(sunDir, mdir);
        float tlen = length(toSunT);
        vec3 offs = rd - mdir * dot(rd, mdir);
        float u = tlen > 1e-4 ? dot(offs, toSunT / tlen) / mRad : 1.0;
        float lit = smoothstep(-0.12, 0.12, u - dot(sunDir, mdir));
        float moonDisc = smoothstep(mRad, mRad * 0.85, mD);
        float surf = vnoise(rd.xz / mRad * 4.0) * 0.13;
        // The unlit part stays faintly visible (earthshine) so a crescent
        // reads as a BODY in the sky, not a floating sliver.
        vec3 face = mCol * (0.10 + 0.90 * lit) - surf * lit;
        // A lit moon is faintly visible in the day sky too — it is UP half
        // the day at quarter phases now that orbits are honest.
        float vis = clamp(moonVis + dayF * 0.20 * illum, 0.0, 1.0);
        col = mix(col, face, moonDisc * vis);
        // Two-lobe bloom: a tight core + a wide halo in the moon's own tint.
        float mCore = exp(-mD * mD / (mRad * mRad) * 2.0) * 0.16;
        float mHalo = exp(-mD * mD / (mRad * mRad) * 0.25) * 0.06;
        col += mCol * (mCore + mHalo) * illum * moonVis;
    }

    // 5. Drifting clouds.
    if (elev > -0.05) {
        float domeH = max(elev + 0.05, 0.01);
        vec2 cuv = rd.xz / domeH;
        float drift = time * 0.008;
        float clouds = smoothstep(0.45, 0.75,
                                  fbm(cuv * 0.6 + vec2(drift, drift * 0.4)));
        vec3 cCol = mix(vec3(0.06, 0.06, 0.10), vec3(0.92, 0.92, 0.96), dayF);
        cCol = mix(cCol, vec3(0.95, 0.55, 0.20), twilight * 0.6);
        float fade = smoothstep(-0.05, 0.08, elev);
        col = mix(col, cCol, clouds * mix(0.5, 0.3, dayF) * fade);
    }

    outColor = vec4(col, 1.0);
}
