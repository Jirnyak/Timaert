#version 450
#extension GL_GOOGLE_include_directive : require
// Tree billboard fragment stage (universal billboard.vert feeds it). Trees are
// the INLINE-procedural branch of the sprite law: cheap per-fragment math buys
// every tree its own unique look from its seed, so nothing is baked. Trees
// cast shadows in the depth pass; the lit billboard itself does not receive
// the shadow map as one flat card, because that makes the whole sprite pop to
// black near other billboards.
#include "tree_sprite.glsl"
#include "shadow_common.glsl"
#include "lighting.glsl"
layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vKind;   // sprite row (species / crop)
layout(location = 2) flat in uint vSeed;   // per-tree float bits
layout(location = 4) flat in vec4 vLightClip;
layout(location = 5) in vec3 vWorld;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 col;
    float drawn = treeCoverage(vUv, float(vKind), uintBitsToFloat(vSeed), col);
    if (drawn < 0.5) discard;

    // Shadow sampled at the ground-contact base (flat across the quad) so the
    // whole tree shades as a unit instead of self-shadowing to black.
    float sh = shadowFactor(u_shadow, vLightClip, 1.0);
    vec3 base = col;
    col = lit_surface(col, pc.ambient.rgb, pc.sunColor.rgb, 0.7, sh, vWorld);
    // Additive positional lights (flat sprite form — distance only, no N·L) so a
    // tree standing in a torch / spell pool glows with it. Inert until count>0.
    col += base * point_lights_flat(vWorld);
    outColor = vec4(col, 1.0);
}
