#version 450
// NPC shadow-caster fragment stage: the silhouette IS the composited sprite's
// alpha, sampled from the same paper-doll pool layer the main pass draws —
// one fetch, one threshold, and the shadow can never disagree with the body.
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vLayer;

layout(set = 0, binding = 0) uniform sampler2DArray uDolls;

void main() {
    if (texture(uDolls, vec3(vUv, float(vLayer))).a < 0.25) discard;
}
