#version 450
#extension GL_GOOGLE_include_directive : require
// THE SHADOW OF A PROFILED PROP — the same body, through the light's matrix.
// It shares `prop_body_vertex` with the lit pass on purpose: a silhouette
// written twice is a silhouette that will be tuned once, and then a tree's
// shadow stops being that tree's outline with nothing failing to build.
#include "prop_body.glsl"

layout(location = 0) in vec3  iPos;
layout(location = 1) in float iHalfW;
layout(location = 2) in float iHeight;
layout(location = 3) in uint  iKind;
layout(location = 4) in uint  iSeed;
layout(location = 5) in uint  iTint;

layout(push_constant) uniform Push { mat4 lightMvp; } pc;

// The silhouette the depth pass must cut: a leaf card is mostly holes, and a
// shadow of its full quad would be a square blot under every tree.
layout(location = 0) out vec2  vLeafUv;
layout(location = 1) out float vIsLeaf;
layout(location = 2) flat out uint vSeed;

void main() {
    uint prof = min(iKind, kProfileCount - 1u);
    vec3 lp, ln; float ly; vec2 luv; float leaf;
    prop_body_vertex(uint(gl_VertexIndex), prof, iHalfW, iHeight, lp, ln, ly,
                     luv, leaf);
    mat2 rot = prop_body_yaw(iSeed);
    vec3 world = iPos + vec3(rot * lp.xz, lp.y).xzy;
    vLeafUv  = luv;
    vIsLeaf  = leaf;
    vSeed    = iSeed;
    gl_Position = pc.lightMvp * vec4(world, 1.0);
}
