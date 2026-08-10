#version 450
#extension GL_GOOGLE_include_directive : require
// Procedural-creature billboard fragment stage (universal billboard.vert feeds
// it). Creatures are the INLINE-procedural branch of the sprite law: the
// analytic silhouette is unique per seed and free per instance. The quad's
// aspect came from the instance (creature_arch_aspect in macro/fauna.h) — the
// shader no longer owns a body-plan table. Creatures cast shadows in the
// depth pass; the lit billboard receives the shadow map sampled once at the
// feet (flat across the quad) so the sprite shades as a unit instead of
// self-shadowing to black. Shares creatureCoverage with the depth caster so
// the shadow is the creature's real silhouette.
#include "creature_sprite.glsl"
#include "shadow_common.glsl"
#include "lighting.glsl"
layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vKind;   // CreatureArchetype
layout(location = 2) flat in uint vSeed;   // per-instance float bits
layout(location = 3) flat in vec4 vTint;
layout(location = 4) flat in vec4 vLightClip;
layout(location = 5) in vec3 vWorld;
layout(location = 6) flat in vec4 vLightClipTop;

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
    float drawn = creatureCoverage(vUv, float(vKind), uintBitsToFloat(vSeed),
                                   vTint.rgb, col);
    if (drawn < 0.5) discard;

    // Base and crown lookups blended by height (billboard.vert emits both
    // on the sprite axis): partial shading instead of all-or-nothing.
    float sh = mix(shadowFactor(u_shadow, vLightClip, 1.0),
                   shadowFactor(u_shadow, vLightClipTop, 1.0), vUv.y);
    vec3 base = col;
    col = lit_surface(col, pc.ambient.rgb, pc.sunColor.rgb, 0.7, sh, vWorld);
    // Additive positional lights (flat sprite form) so a creature caught in a
    // torch / spell pool glows with it. Inert until count>0.
    col += base * point_lights_flat(vWorld);
    outColor = vec4(col, 1.0);
}
