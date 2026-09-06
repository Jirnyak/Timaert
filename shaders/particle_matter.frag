#version 450
#extension GL_GOOGLE_include_directive : require
// MATTER particle fragment (particles-unified-matter, Inc A). Blood, dust,
// smoke — stuff, not light. Two deliberate differences from the energy pass
// (particle.frag):
//   1. Blend is ALPHA-OVER (pipeline: SRC_ALPHA / ONE_MINUS_SRC_ALPHA), so a
//      dark droplet DARKENS what is behind it — additive could only add light,
//      which is why shipped blood read as a red lamp and dust saturated white.
//      The sim sorts the matter segment back-to-front (pack()), because
//      alpha-over is order-dependent.
//   2. The surface is LIT by the one universal law: lit_surface() for
//      sun/ambient (shadowed, relief-marched, cloud-covered) plus the flat
//      point-light term — blood by a torch reads warm, blood in a dark crypt
//      is dark, at night it is moonlit. No white-hot core lift: matter has no
//      emissive centre.
// One lighting sample per PARTICLE (centre, flat varyings): droplets are
// centimetres across, and sampling shadow at the centre avoids billboard acne.
#include "shadow_common.glsl"
#include "lighting.glsl"

layout(location = 0) in vec2 vUv;          // [-1,1]² across the quad
layout(location = 1) in vec4 vColor;       // rgb + faded alpha (from the sim)
layout(location = 2) flat in vec3 vWorldC;    // particle centre (world)
layout(location = 3) flat in vec4 vLightClip; // centre in near-light clip

layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;
layout(set = 0, binding = 3) uniform sampler2DShadow u_shadowFar;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 camUp;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // Same soft-round falloff as the energy pass — a droplet is a soft disc —
    // but the coverage goes to ALPHA only; the colour stays the surface's own.
    float r = length(vUv);
    float fall = clamp(1.0 - r, 0.0, 1.0);
    fall = fall * fall;
    float a = vColor.a * fall;
    if (a <= 0.01) discard;

    // One shadow answer at the particle centre: crisp near level where it
    // applies, wide far level beyond (handoff, not union).
    vec4 farC = far_light_clip(vWorldC);
    float sh = shadowFactorHandoff(u_shadow, u_shadowFar, vLightClip, farC,
                                   1.0, TIMAERT_SHADOW_SPREAD_BILLBOARD);

    // `base` stays unlit so a torch ADDS its light rather than multiplying
    // whatever the sun left — the same contract as every lit billboard.
    vec3 base = vColor.rgb;
    vec3 rgb = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, 0.7, sh,
                           vWorldC);
    rgb += base * point_lights_flat(vWorldC);

    // Straight alpha out: the pipeline's blend factors (SRC_ALPHA /
    // ONE_MINUS_SRC_ALPHA) do the compositing.
    outColor = vec4(rgb, a);
}
