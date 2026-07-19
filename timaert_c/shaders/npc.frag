#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced paper-doll NPC billboard fragment stage. NPCs cast shadows in the
// depth pass; the lit billboard receives the shadow map sampled once at the feet
// (flat across the quad) so the sprite shades as a unit instead of self-shadowing.
#include "shadow_common.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_shadow;
layout(set = 1, binding = 0) uniform sampler2DArray u_paperdolls;

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in float vLayer;
layout(location = 2) flat in vec4 vLightClip;

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
    vec3 col = texel.rgb * (pc.ambient.rgb + pc.sunColor.rgb * 0.75 * sh);
    outColor = vec4(col, texel.a);
}
