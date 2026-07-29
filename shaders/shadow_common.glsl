// Shared deterministic shadow-map receiver lookup. Keep all terrain/structure
// receivers on the same bias/filter policy so fixes apply universally.
#ifndef TIMAERT_SHADOW_COMMON_GLSL
#define TIMAERT_SHADOW_COMMON_GLSL

float shadowFactor(sampler2D shadowMap, vec4 lightClip, float ndl) {
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0
        || proj.z < 0.0 || proj.z > 1.0) {
        return 1.0;
    }

    // Receiver-side bias stays small because caster-side raster bias and a fitted
    // light volume now do the heavy lifting. No jitter: it turns undersampled
    // small-caster shadows into visible zebra flicker.
    float bias = max(0.00002, 0.00012 * (1.0 - ndl));
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));

    float lit = 0.0;
    float total = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float wx = 3.0 - abs(float(x));
            float wy = 3.0 - abs(float(y));
            float w = wx * wy;
            float d = texture(shadowMap, uv + vec2(x, y) * texel * 1.15).r;
            lit += ((proj.z - bias > d) ? 0.0 : 1.0) * w;
            total += w;
        }
    }
    return lit / total;
}

#endif
