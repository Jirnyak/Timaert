// The sprite-bank slot decode and the BODY `kind` unpack — the shader half of
// the contract with assets/sprite_bank.h and gpu/bb_instance.h.
//
// A slot IS an array layer: one drawn body picture per layer, because the bank
// holds a picture per KIND (five today), not a composited frame per soul. The
// paper-doll pool that lived here packed 2×2 frames of 48×48 into each layer
// only because its working set was thousands of frames against MoltenVK's
// 2048-layer cap; with a per-kind set that pressure is gone.
//
// A body's `kind` carries BOTH halves of the sprite law, because the draw path
// must not ask what SORT of thing it is drawing — only what its row has:
//   low 16 bits  = bank slot, or BB_NO_SLOT
//   high 16 bits = procedural body plan (CreatureArchetype), used when no slot
// Its twin is gpu::bb_body_kind / gpu::kBbNoSlot.
//
// Every consumer (the lit body pass, the depth-only caster) unpacks through
// these, so a shadow silhouette can never disagree with the body it belongs to.
#ifndef DOLL_POOL_GLSL
#define DOLL_POOL_GLSL

#define BB_NO_SLOT 0xFFFFu

uint bb_slot(uint kind)      { return kind & 0xFFFFu; }
uint bb_archetype(uint kind) { return (kind >> 16u) & 0xFFFFu; }
bool bb_is_drawn(uint kind)  { return bb_slot(kind) != BB_NO_SLOT; }

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
