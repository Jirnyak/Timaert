#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced paper-doll NPC billboard fragment stage.
//
// One texture fetch. The 37-layer atlas composite (palettes, hidden masks,
// render orders) ran HERE per fragment for a while (7cd71e2) — which priced a
// sprite by its screen coverage, so walking up to a person melted the frame
// rate while the same person ten cells away cost nothing. The composite now
// runs once per unique frame into the paper-doll sprite pool
// (assets/paperdoll_atlas.h) and this stage samples the finished layer.
//
// Lit exactly like its sibling billboard (creature.frag): the shadow map is
// sampled once at the FEET and applied flat across the quad, because a
// camera-facing card has no normal to self-shadow with, and the positional
// lights come in through the flat form of the same universal function. One
// lit_surface() for every lit pass is the rule (src/sub/lighting.h).
#include "shadow_common.glsl"
#include "lighting.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vLayer;
layout(location = 2) flat in vec4 vLightClip;   // feet in light space
layout(location = 3) in vec3 vWorld;            // world pos (point lights)

layout(location = 0) out vec4 fColor;

// The shared lighting set: shadow map + the frame's point-light buffer. Bound
// by the draw already (vk_renderer_3d.cpp binds litSet at set 0 for this pass).
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

// Must match npc.vert byte for byte — one range, both stages.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

// The paper-doll sprite pool: every composited 48x48 frame is one array layer.
layout(set = 1, binding = 0) uniform sampler2DArray uDolls;

void main() {
    vec4 finalColor = texture(uDolls, vec3(vUv, float(vLayer)));
    if (finalColor.a < 0.01) discard;

    // Into the one lighting path, at the very end: the paper-doll is composed
    // first (palettes, layers, alpha), and the finished surface is lit like any
    // other. `base` is kept unlit so a torch adds ITS light to the body rather
    // than multiplying whatever the sun left — the same shape creature.frag uses.
    float sh = shadowFactor(u_shadow, vLightClip, 1.0);
    vec3 base = finalColor.rgb;
    finalColor.rgb = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, 0.7, sh, vWorld);
    finalColor.rgb += base * point_lights_flat(vWorld);
    fColor = finalColor;
}
