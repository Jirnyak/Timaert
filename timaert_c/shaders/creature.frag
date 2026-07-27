#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced procedural-creature billboard fragment stage. Creatures cast
// shadows in the depth pass; the lit billboard receives the shadow map sampled
// once at the feet (flat across the quad) so the sprite shades as a unit
// instead of self-shadowing to black. Shares creatureCoverage with the depth
// caster so the shadow is the creature's real silhouette.
#include "creature_sprite.glsl"
#include "shadow_common.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

layout(location = 0) in vec2  vUv;
layout(location = 1) flat in float vArch;
layout(location = 2) flat in float vSeed;
layout(location = 3) flat in vec3  vTint;
layout(location = 4) flat in vec4  vLightClip;

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
    float drawn = creatureCoverage(vUv, vArch, vSeed, vTint, col);
    if (drawn < 0.5) discard;

    float sh = shadowFactor(u_shadow, vLightClip, 1.0);
    col *= pc.ambient.rgb + pc.sunColor.rgb * 0.7 * sh;
    outColor = vec4(col, 1.0);
}
