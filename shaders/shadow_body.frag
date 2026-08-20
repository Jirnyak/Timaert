#version 450
#extension GL_GOOGLE_include_directive : require
// THE body shadow-caster fragment (universal shadow_bb.vert feeds it). It asks
// the row exactly the same question the lit pass asks, through the same two
// helpers, so a cast shadow is always the real silhouette of the body that
// casts it — a drawn kind throws its picture's alpha, a procedural kind throws
// its analytic coverage, and neither can drift from what the eye sees.
#include "creature_sprite.glsl"
#include "doll_pool.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vKind;
layout(location = 2) flat in uint vSeed;

layout(set = 0, binding = 0) uniform sampler2DArray uDolls;

void main() {
    if (bb_is_drawn(vKind)) {
        // A higher cut than the lit pass on purpose: a shadow made of the
        // sprite's faintest fringe reads as dirt on the ground.
        if (doll_sample(uDolls, vUv, bb_slot(vKind)).a < 0.25) discard;
    } else {
        vec3 dummy;   // tint does not affect coverage
        if (creatureCoverage(vUv, float(bb_archetype(vKind)),
                             uintBitsToFloat(vSeed), vec3(1.0), dummy) < 0.5)
            discard;
    }
}
