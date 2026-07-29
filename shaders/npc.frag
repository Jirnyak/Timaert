#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced paper-doll NPC billboard fragment stage. NPCs cast shadows in the
// depth pass; the lit billboard receives the shadow map sampled once at the feet
// (flat across the quad) so the sprite shades as a unit instead of self-shadowing.
#include "shadow_common.glsl"
#include "lighting.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_shadow;
layout(set = 1, binding = 0) uniform sampler2DArray u_paperdolls;

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in float vLayer;
layout(location = 2) flat in vec4 vLightClip;
layout(location = 3) in vec3 vWorld;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(u_paperdolls, vec3(vUv, vLayer));
    if (texel.a < 0.25) discard;

    float sh = shadowFactor(u_shadow, vLightClip, 1.0);
    vec3 col = lit_surface(texel.rgb, pc.ambient.rgb, pc.sunColor.rgb, 0.75, sh);
    // Additive positional lights (flat sprite form) so an NPC lit by a torch /
    // spell / the player's lantern glows with it. Inert until count>0.
    col += texel.rgb * point_lights_flat(vWorld);
    outColor = vec4(col, texel.a);
}
