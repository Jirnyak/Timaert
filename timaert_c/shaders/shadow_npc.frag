#version 450
#extension GL_GOOGLE_include_directive : require
// NPC shadow-caster fragment: discard where the SHARED npc sprite coverage is
// transparent, so the cast shadow is the NPC's REAL silhouette (the same code
// the lit billboard draws). Depth only. The identical contract holds for any
// arbitrary NPC / mob sprite -- swap in an atlas alpha sample and shadows follow.
#include "npc_sprite.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vSeed;

void main() {
    vec3 col;
    if (npcCoverage(vUv, vSeed, col) < 0.5) discard;
}
