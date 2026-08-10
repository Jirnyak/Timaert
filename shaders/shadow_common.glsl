// Shared deterministic shadow-map receiver lookup. Keep all terrain/structure
// receivers on the same bias/filter policy so fixes apply universally.
//
// The map is HARDWARE-PCF sampled: the shadow sampler has compareEnable +
// LINEAR (gpu/vk_shadow.cpp), so each texture() tap is a bilinear-weighted
// 2×2 depth compare done by fixed function. The 3×3 loop below therefore
// covers what the old hand-rolled 5×5 NEAREST tent covered, at roughly a
// third of the fetch cost — this runs on every lit fragment on screen.
//
// The volume is fitted tight around the camera (its ONLY job is nearby
// movable casters — relief is the heightfield march's member of the
// sun-visibility law), so shadows DISSOLVE over the last stretch before the
// volume edge instead of clipping at a visible line.
#ifndef TIMAERT_SHADOW_COMMON_GLSL
#define TIMAERT_SHADOW_COMMON_GLSL

float shadowFactor(sampler2DShadow shadowMap, vec4 lightClip, float ndl) {
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (proj.z < 0.0 || proj.z > 1.0) return 1.0;
    // Distance to the nearest volume edge in UV; outside = lit, and the last
    // ~6% of the map (a ~30 m band at the fitted span) fades the shadow out.
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    if (edge <= 0.0) return 1.0;
    float fade = smoothstep(0.0, 0.06, edge);

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
    return mix(1.0, lit / 9.0, fade);
}

#endif
