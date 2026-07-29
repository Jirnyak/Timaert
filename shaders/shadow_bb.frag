#version 450
#extension GL_GOOGLE_include_directive : require
// Tree shadow-caster fragment: discard where the SHARED tree sprite coverage is
// transparent, so the cast shadow is the tree's REAL silhouette (the same code
// the lit billboard draws), not a hand-authored blob. Depth only.
#include "tree_sprite.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vSpecies;
layout(location = 2) in float vSeed;

void main() {
    vec3 col;
    if (treeCoverage(vUv, vSpecies, vSeed, col) < 0.5) discard;
}
