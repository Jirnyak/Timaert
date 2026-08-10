// Shared deterministic shadow-map receiver lookup. Keep all terrain/structure
// receivers on the same bias/filter policy so fixes apply universally.
//
// TWO cascades (Inc S of the shadow track): a NEAR map fitted tight around the
// camera (small casters — people, trees — resolve to real silhouettes) and a
// FAR map covering the whole loaded window (buildings and forests keep their
// shadows at distance; terrain does not cast by policy — see record_shadow).
// A receiver takes min(near, far): whichever cascade knows about a caster
// darkens the point, so a far building still shades a near doorstep even
// though the building itself lies outside the near volume.
#ifndef TIMAERT_SHADOW_COMMON_GLSL
#define TIMAERT_SHADOW_COMMON_GLSL

// Weighted-tent PCF, `taps` texels in each direction (2 → 5×5, 1 → 3×3).
// Out-of-volume points are lit (1.0) — the caller's other cascade decides.
float shadowPcf(sampler2D shadowMap, vec4 lightClip, float ndl, int taps) {
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
    for (int y = -taps; y <= taps; ++y) {
        for (int x = -taps; x <= taps; ++x) {
            float wx = float(taps + 1) - abs(float(x));
            float wy = float(taps + 1) - abs(float(y));
            float w = wx * wy;
            float d = texture(shadowMap, uv + vec2(x, y) * texel * 1.15).r;
            lit += ((proj.z - bias > d) ? 0.0 : 1.0) * w;
            total += w;
        }
    }
    return lit / total;
}

// Single-cascade form (the historical shadowFactor) — kept for passes that
// only ever see one map (none in the subworld today; the harness parity path
// still routes through the cascaded form with both slots on one map).
float shadowFactor(sampler2D shadowMap, vec4 lightClip, float ndl) {
    return shadowPcf(shadowMap, lightClip, ndl, 2);
}

// The subworld receiver policy: crisp 5×5 tent on the near cascade, lighter
// 3×3 on the far one (its texels are metres wide — more taps only widen the
// blur), darker cascade wins.
float shadowFactorCascaded(sampler2D nearMap, sampler2D farMap,
                           vec4 nearClip, vec4 farClip, float ndl) {
    return min(shadowPcf(nearMap, nearClip, ndl, 2),
               shadowPcf(farMap, farClip, ndl, 1));
}

#endif
