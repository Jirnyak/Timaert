#version 450
// Procedural subworld sky (Phase 5). A true celestial dome reconstructed from
// the camera basis: day/night/twilight gradient, sun disc + glow, a moon, and
// a rich TEXTURE-FREE star field (3 density layers + a Milky-Way band + per-
// star twinkle) plus drifting FBM clouds. Ported/expanded from the GL sky.ts.
// Drawn fullscreen before the geometry with depth off, so it is the backdrop.
layout(push_constant) uniform Push {
    vec4 forward;  // xyz = camera forward
    vec4 right;    // xyz = camera right
    vec4 up;       // xyz = camera up
    vec4 p0;       // x=resX y=resY z=fov w=tod(0..1)
    vec4 p1;       // xyz=fogColor w=time(sec)
} pc;

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

// One equirect star layer: a jittered star per grid cell with magnitude-driven
// size/brightness, colour temperature, and twinkle.
vec3 starLayer(float az, float el, vec2 grid, float thresh, float sizeS,
               float time, float seed) {
    vec2 g = vec2(az / TAU + 0.5, el / PI + 0.5) * grid;
    vec2 cell = floor(g);
    vec2 f = fract(g) - 0.5;
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
        float az = atan(rd.z, rd.x);
        float el = asin(clamp(rd.y, -1.0, 1.0));
        vec3 mwN = normalize(vec3(0.3, 0.35, 0.9));
        float band = 1.0 - smoothstep(0.0, 0.30, abs(dot(rd, mwN)));
        vec3 s = vec3(0.0);
        s += starLayer(az, el, vec2(220.0, 110.0), 0.86, 0.09, time, 0.0);
        s += starLayer(az, el, vec2(90.0, 46.0), 0.90, 0.16, time, 40.0) * 1.6;
        s += starLayer(az, el, vec2(300.0, 150.0), 1.0 - 0.22 * band, 0.07,
                       time, 77.0) * (0.6 + band);
        float haze = band * fbm(vec2(az * 3.0, el * 3.0)) * 0.5;
        s += vec3(0.45, 0.52, 0.72) * haze * 0.10;
        float fade = smoothstep(-0.02, 0.12, elev);
        col += s * nightF * fade;
    }

    // 3. Sun.
    float sunAng = (tod - 0.25) * TAU;
    vec3 sunDir = normalize(vec3(cos(sunAng), sin(sunAng), 0.0));
    float sunVis = smoothstep(0.22, 0.30, tod) * smoothstep(0.78, 0.70, tod);
    float sdot = dot(rd, sunDir);
    float disc = smoothstep(0.9992, 0.9996, sdot);
    float glow = pow(max(sdot, 0.0), 256.0) * 0.6;
    float scatter = pow(max(sdot, 0.0), 8.0) * 0.12;
    vec3 sunCol = mix(vec3(1.0, 0.45, 0.10), vec3(1.0, 0.92, 0.7), dayF);
    col += sunCol * (disc + glow + scatter) * sunVis;

    // 4. Moon (night).
    float moonVis = clamp(nightF * 1.4, 0.0, 1.0);
    float mAng = (tod - 0.75) * TAU;
    vec3 moonDir = normalize(vec3(cos(mAng), abs(sin(mAng)) * 0.7 + 0.2,
                                  sin(mAng) * 0.4));
    float mD = acos(clamp(dot(rd, moonDir), -1.0, 1.0));
    float mRad = 0.028;
    float moonDisc = smoothstep(mRad, mRad * 0.82, mD);
    float surf = vnoise(rd.xz / mRad * 4.0) * 0.14;
    vec3 mCol = vec3(0.80, 0.82, 0.88) - surf;
    float mGlow = exp(-mD * mD / (mRad * mRad) * 3.0) * 0.08;
    col = mix(col, mCol, moonDisc * moonVis);
    col += vec3(0.6, 0.65, 0.8) * mGlow * moonVis;

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
