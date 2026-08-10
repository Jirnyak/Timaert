#version 450
#extension GL_GOOGLE_include_directive : require
// Paper-doll NPC billboard fragment stage (universal billboard.vert feeds it).
// NPCs are the BANK branch of the sprite law: the 37-layer composite was
// resolved to texels once per unique frame (assets/paperdoll_atlas.h) and this
// stage samples the finished pool layer — `kind` IS the layer index. The pool
// stores frames feet-at-v0 (rows flipped at upload), so the sample is the
// world convention with no per-stage flip.
//
// Lit exactly like its sibling billboards: the shadow map is sampled once at
// the FEET and applied flat across the quad, because a camera-facing card has
// no normal to self-shadow with, and the positional lights come in through
// the flat form of the same universal function. One lit_surface() for every
// lit pass is the rule (src/sub/lighting.h).
#include "shadow_common.glsl"
#include "lighting.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vKind;   // paper-doll pool layer
layout(location = 4) flat in vec4 vLightClip;
layout(location = 5) in vec3 vWorld;

layout(location = 0) out vec4 fColor;

// The shared lighting set: shadow map + the frame's point-light buffer. Bound
// by the draw already (vk_renderer_3d.cpp binds litSet at set 0 for this pass).
layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;

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
    vec4 finalColor = texture(uDolls, vec3(vUv, float(vKind)));
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
