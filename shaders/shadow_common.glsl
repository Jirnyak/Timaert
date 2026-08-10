// Shared deterministic shadow-map receiver lookup. Keep all terrain/structure
// receivers on the same bias/filter policy so fixes apply universally.
//
// The object map is ONE idea at TWO scales (the owner's "sphere subtracted
// from the base"): a crisp level fitted ±256 m around the camera (every
// caster, ~15 cm texels — people cast silhouettes) and a wide level over the
// whole window (trees and masonry only, the pre-existing density that suits
// casters that size). A receiver reads the crisp level wherever it applies
// and HANDS OFF to the wide level across the crisp volume's edge band — a
// takeover, never a union, so the two levels cannot double-shadow (the
// melted-blob lesson of 2026-08-10).
//
// Both maps are HARDWARE-PCF sampled: the shadow sampler has compareEnable +
// LINEAR (gpu/vk_shadow.cpp), so each texture() tap is a bilinear-weighted
// 2×2 depth compare in fixed function; the 3×3 loop covers what a hand-rolled
// 5×5 NEAREST tent covered at roughly a third of the fetch cost.
#ifndef TIMAERT_SHADOW_COMMON_GLSL
#define TIMAERT_SHADOW_COMMON_GLSL

// Core lookup: x = 3×3 hw-PCF shadow value, y = the volume's validity weight
// (1 deep inside, fading over the last ~6% of the map, 0 outside).
vec2 shadowPcfV(sampler2DShadow shadowMap, vec4 lightClip, float ndl) {
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (proj.z < 0.0 || proj.z > 1.0) return vec2(1.0, 0.0);
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    if (edge <= 0.0) return vec2(1.0, 0.0);
    float valid = smoothstep(0.0, 0.06, edge);

    // Receiver-side bias stays small because caster-side raster bias and a fitted
    // light volume now do the heavy lifting. No jitter: it turns undersampled
    // small-caster shadows into visible zebra flicker.
    float bias = max(0.00002, 0.00012 * (1.0 - ndl));
    float ref = proj.z - bias;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));

    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(shadowMap, vec3(uv + vec2(x, y) * texel * 1.5, ref));
        }
    }
    return vec2(lit / 9.0, valid);
}

// Single-map form: shadows dissolve to lit at the volume edge. (The harness's
// one-map scene routes through the handoff form with the same map in both
// slots, which reduces to exactly this.)
float shadowFactor(sampler2DShadow shadowMap, vec4 lightClip, float ndl) {
    vec2 v = shadowPcfV(shadowMap, lightClip, ndl);
    return mix(1.0, v.x, v.y);
}

// The two-level receiver policy: crisp level where it applies, wide level
// everywhere else, blended across the crisp volume's edge band. Deep inside
// the crisp volume the wide map is not even sampled.
float shadowFactorHandoff(sampler2DShadow nearMap, sampler2DShadow farMap,
                          vec4 nearClip, vec4 farClip, float ndl) {
    vec2 n = shadowPcfV(nearMap, nearClip, ndl);
    if (n.y >= 1.0) return n.x;
    vec2 f = shadowPcfV(farMap, farClip, ndl);
    float farSh = mix(1.0, f.x, f.y); // the wide level fades at the WINDOW edge
    return mix(farSh, n.x, n.y);
}

#endif
