#version 450
#extension GL_GOOGLE_include_directive : require
// THE body billboard fragment stage — every living thing in the subworld, drawn
// by ONE law (sprites.md). A peasant and a wolf differ in their ROW, not in
// their pass: if the row names drawn art the finished picture is sampled out of
// the sprite bank, and if it does not, the analytic silhouette is evaluated
// from its body plan. Nothing here asks what SORT of creature it is, which is
// the entire point — the split used to be two shaders, two pipelines and two
// instance buffers keyed on `archetype == 0xFF`.
//
// Lit identically either way, because the surface is a surface once it exists:
// the shadow map is sampled per fragment (xy at the fragment, z at the body
// axis — see billboard.vert), and the positional lights come in through the
// flat form of the same universal function. One lit_surface() for every lit
// pass is the rule (src/sub/lighting.h).
#include "creature_sprite.glsl"
#include "doll_pool.glsl"
#include "shadow_common.glsl"
#include "lighting.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vKind;   // bb_body_kind(slot, archetype)
layout(location = 2) flat in uint vSeed;   // per-instance float bits
layout(location = 3) flat in vec4 vTint;
layout(location = 4) in vec4 vLightClip;
layout(location = 5) in vec3 vWorld;
layout(location = 6) in vec3 vAxisWorld;

layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;
layout(set = 0, binding = 3) uniform sampler2DShadow u_shadowFar;
// The drawn-body bank: one picture per kind, slot = array layer.
layout(set = 1, binding = 0) uniform sampler2DArray uDolls;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 surface;
    if (bb_is_drawn(vKind)) {
        surface = doll_sample(uDolls, vUv, bb_slot(vKind));
        if (surface.a < 0.01) discard;
    } else {
        vec3 col;
        // Coverage is binary here, so the alpha stays 1.0 and the blend the
        // drawn branch needs is a no-op for this one — the two branches can
        // therefore share a single pipeline without either changing.
        if (creatureCoverage(vUv, float(bb_archetype(vKind)),
                             uintBitsToFloat(vSeed), vTint.rgb, col) < 0.5)
            discard;
        surface = vec4(col, 1.0);
    }

    // Per-fragment shadow: xy at the true fragment, z at the body axis — a
    // shadow edge covers exactly the part of the sprite it mathematically
    // reaches, no self-shadow acne by construction. Crisp level where it
    // applies, wide level beyond — handoff, not union (shadow_common.glsl).
    vec4 farFull = far_light_clip(vWorld);
    vec4 farAxis = far_light_clip(vAxisWorld);
    float sh = shadowFactorHandoff(u_shadow, u_shadowFar, vLightClip,
                                   vec4(farFull.xy, farAxis.z, farFull.w), 1.0,
                                   TIMAERT_SHADOW_SPREAD_BILLBOARD);
    // `base` stays unlit so a torch ADDS its light to the body rather than
    // multiplying whatever the sun left.
    vec3 base = surface.rgb;
    surface.rgb = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, 0.7, sh,
                              vWorld);
    surface.rgb += base * point_lights_flat(vWorld);
    outColor = surface;
}
