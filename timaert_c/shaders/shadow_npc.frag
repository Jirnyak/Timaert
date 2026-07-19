#version 450
// NPC shadow-caster fragment: discard by the same paper-doll atlas alpha used by
// the lit billboard, so NPCs cast their actual sprite silhouette.
layout(set = 0, binding = 0) uniform sampler2DArray u_paperdolls;

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vLayer;

void main() {
    if (texture(u_paperdolls, vec3(vec2(vUv.x, 1.0 - vUv.y), vLayer)).a < 0.25) discard;
}
