// ONE procedural cloud field for the whole subworld — the sky pass draws it
// on the dome and every lit pass (via lighting.glsl) darkens its sun term
// with it, so the cloud you SEE overhead is the shadow that crawls under
// your feet: same noise, same wind, same cloudiness — agreement by sharing
// the function, not by keeping two copies in lockstep.
//
// The field lives in abstract "field units": the sky maps its dome ray onto
// them (rd.xz / domeH * scale), the lit passes map world metres
// (worldXZ * TIMAERT_CLOUD_WORLD_SCALE). Wind is field-units/second and is
// applied HERE, so no consumer can drift the field on its own clock.
//
// Context comes from SkyContext (sub/sky.h): cloudiness01 opens the coverage
// window from clear (0) through today's scattered look (0.5) to overcast (1);
// wind is the drift vector. Both are constants today and the macro weather
// field tomorrow — this file never learns the difference.
#ifndef TIMAERT_CLOUDS_GLSL
#define TIMAERT_CLOUDS_GLSL

// World metres -> cloud field units for ground shadows. 1/700 puts a cloud
// shadow at the few-hundred-metre scale of the drawn dome clouds, and makes
// the shared wind (0.008 field-units/s) crawl shadows at a believable ~5 m/s.
#define TIMAERT_CLOUD_WORLD_SCALE (1.0 / 700.0)

float tc_h21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float tc_vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(tc_h21(i), tc_h21(i + vec2(1, 0)), f.x),
               mix(tc_h21(i + vec2(0, 1)), tc_h21(i + vec2(1, 1)), f.x), f.y);
}
float tc_fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
    for (int i = 0; i < 4; ++i) { v += a * tc_vnoise(p); p = r * p * 2.0; a *= 0.5; }
    return v;
}

// Coverage of the drifting field at a field-space point, in [0,1].
// Domain-warped fbm (fbm of a point pushed by another fbm) gives clouds
// billowed edges instead of uniform blobs — and the WARP field drifts at its
// own rate, deliberately different from the wind, so shapes continuously
// billow and dissolve WHILE they travel instead of sliding past as a frozen
// photograph of noise. Fully dynamic: no keyframes, no texture, just the two
// drift rates beating against each other. The coverage window slides with
// cloudiness: 0.5 reproduces the pre-weather look.
float cloud_cover(vec2 fieldP, float timeSec, vec2 wind, float cloudiness) {
    vec2 p = fieldP + wind * timeSec;
    vec2 churn = vec2(timeSec * 0.011, timeSec * -0.007);
    float f = tc_fbm(p + tc_fbm(p * 0.5 + churn) * 0.55);
    float lo = 0.75 - 0.60 * clamp(cloudiness, 0.0, 1.0);
    return smoothstep(lo, lo + 0.30, f);
}

#endif
