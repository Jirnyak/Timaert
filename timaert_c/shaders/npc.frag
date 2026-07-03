#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced paper-doll NPC billboard fragment stage (Phase 5). The procedural
// humanoid lives in npc_sprite.glsl and is SHARED with the shadow caster, so an
// NPC casts a real-silhouette shadow with no bespoke shadow code. Here it is lit
// by sun + ambient and darkened by the PCF shadow map (flat, at the feet).
#include "npc_sprite.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

layout(location = 0) in vec2 vUv;   // x: 0..1 across, y: 0..1 up (0 = feet)
layout(location = 1) in float vSeed;
layout(location = 2) flat in vec4 vLightClip;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

float npcShadow(vec4 lightClip) {
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;
    float bias = 0.004;
    vec2 texel = 1.0 / vec2(textureSize(u_shadow, 0));
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float d = texture(u_shadow, uv + vec2(x, y) * texel).r;
            lit += (proj.z - bias > d) ? 0.0 : 1.0;
        }
    return lit / 9.0;
}

void main() {
    vec3 col;
    float a = npcCoverage(vUv, vSeed, col);
    if (a < 0.5) discard;

    float sh = npcShadow(vLightClip);
    col *= pc.ambient.rgb + pc.sunColor.rgb * 0.75 * sh;
    outColor = vec4(col, 1.0);
}
