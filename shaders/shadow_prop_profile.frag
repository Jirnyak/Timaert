#version 450
#extension GL_GOOGLE_include_directive : require
// THE DEPTH PASS CUTS THE SAME SILHOUETTE. A leaf card is mostly holes, so a
// depth-only pass that wrote the whole quad would lay a square blot of shadow
// under every tree — the trunk's shadow honest, the crown's a stamp.
//
// THIS IS THE ONE THING FOLIAGE-AS-CARDS COSTS: the profile's shadow fragment
// used to be empty (`void main() {}`), and now it evaluates the clump twice
// per frame, once per cascade. Named here rather than discovered later.
#include "leaf_sprite.glsl"

layout(location = 0) in vec2  vLeafUv;
layout(location = 1) in float vIsLeaf;
layout(location = 2) flat in uint vSeed;

void main() {
    if (vIsLeaf > 0.5 && leaf_patch(vLeafUv, vSeed) < 0.5) discard;
}
