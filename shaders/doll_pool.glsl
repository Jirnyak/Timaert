// Sprite-bank SLOT decode — the shader half of the packing contract with
// assets/sprite_bank.h. A slot IS an array layer: one drawn body picture per
// layer, because the bank holds a picture per KIND (five today), not a
// composited frame per soul. The paper-doll pool that lived here packed 2×2
// frames of 48×48 into each layer only because its working set was thousands of
// frames against MoltenVK's 2048-layer cap; with a per-kind set that pressure
// is gone and the decode is a plain lookup.
//
// BOTH consumers (npc.frag lit pass, shadow_npc.frag caster) sample through
// this one function, so the shadow silhouette can never disagree with the body,
// and a packing change edits exactly one file per side.
#ifndef DOLL_POOL_GLSL
#define DOLL_POOL_GLSL

// Half a texel of the bank tile (sm::kSpriteBankTile), in uv units. NEAREST at
// an exact edge (uv 0.0 or 1.0) can round outside the tile; clamping half a
// texel inward makes that impossible while never moving a visible sample — the
// outermost half-texel repeats its own edge texel, exactly what CLAMP_TO_EDGE
// does when a frame owns its layer.
const float kDollHalfTexel = 0.5 / 256.0;

vec4 doll_sample(sampler2DArray pool, vec2 uv, uint slot) {
    return texture(pool, vec3(clamp(uv, kDollHalfTexel, 1.0 - kDollHalfTexel),
                              float(slot)));
}

#endif
