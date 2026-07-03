#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced tree billboard fragment stage (Phase 5). The 7-species tree pixel-
// art lives in tree_sprite.glsl and is SHARED with the shadow caster, so the
// cast shadow is the tree's real silhouette (never a hand-authored blob). Here
// it is lit by sun + ambient and darkened by the PCF shadow map.
#include "tree_sprite.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vSpecies;
layout(location = 2) in float vSeed;
layout(location = 3) flat in vec4 vLightClip;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

// PCF shadow lookup at the tree base: 1.0 = lit, 0.0 = shadowed.
float treeShadow(vec4 lightClip) {
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;
    float bias = 0.004; // generous: base card bottom self-compares near equal
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
    float drawn = treeCoverage(vUv, vSpecies, vSeed, col);
    if (drawn < 0.5) discard;

    float sh = treeShadow(vLightClip);
    col *= pc.ambient.rgb + pc.sunColor.rgb * 0.7 * sh;
    outColor = vec4(col, 1.0);
}
