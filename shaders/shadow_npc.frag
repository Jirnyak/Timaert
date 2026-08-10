#version 450
// NPC shadow-caster fragment (universal shadow_bb.vert feeds it): the
// silhouette IS the composited sprite's alpha, sampled from the same
// paper-doll pool layer the main pass draws — `kind` is the layer. The pool
// stores frames feet-at-v0, so the sample is the world convention with no
// per-stage flip, and the shadow can never disagree with the body.
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vKind;

layout(set = 0, binding = 0) uniform sampler2DArray uDolls;

void main() {
    if (texture(uDolls, vec3(vUv, float(vKind))).a < 0.25) discard;
}
