// Paper-doll pool SLOT decode — the shader half of the packing contract with
// assets/paperdoll_atlas.h (kSubTiles = 2): four 48×48 composited frames live
// in each 96×96 array layer, and the instance's `kind` carries the SLOT
//   slot = layer·4 + quadrant   (quadrant bit0 = +u half, bit1 = +v half).
// BOTH consumers (npc.frag lit pass, shadow_npc.frag caster) sample through
// this one function, so the shadow silhouette can never disagree with the
// body, and a packing change edits exactly one file per side.
#ifndef DOLL_POOL_GLSL
#define DOLL_POOL_GLSL

// Half a texel of the 48-texel frame, in frame-uv units. NEAREST at an exact
// frame edge (uv 0.0 or 1.0) rounds into the NEIGHBOURING quadrant's texel
// row; clamping the sample half a texel inward makes that impossible while
// never moving a visible sample (the outermost half-texel repeats its own
// edge texel, exactly what CLAMP_TO_EDGE did when a frame owned the layer).
const float kDollHalfTexel = 0.5 / 48.0;

vec4 doll_sample(sampler2DArray pool, vec2 uv, uint slot) {
    vec2 quad = vec2(float(slot & 1u), float((slot >> 1u) & 1u));
    vec2 quadUv =
        (clamp(uv, kDollHalfTexel, 1.0 - kDollHalfTexel) + quad) * 0.5;
    return texture(pool, vec3(quadUv, float(slot >> 2u)));
}

#endif
