#version 450
#extension GL_GOOGLE_include_directive : require
// Creature shadow-caster fragment: discard by the same coverage the lit pass
// uses, so the cast shadow is the creature's real procedural silhouette. Tint
// does not affect coverage, so a dummy colour is passed.
#include "creature_sprite.glsl"

layout(location = 0) in vec2  vUv;
layout(location = 1) flat in float vArch;
layout(location = 2) flat in float vSeed;

void main() {
    vec3 dummy;
    if (creatureCoverage(vUv, vArch, vSeed, vec3(1.0), dummy) < 0.5) discard;
}
