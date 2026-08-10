#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced structure fragment stage (Phase 5): stone city walls + tan houses
// with red-brown roofs, lit by the same sun + ambient and PCF shadow map as the
// terrain (walls cast AND receive real shadows). Colour is keyed by the instance
// `type`; adding a structure kind is one more branch, no new pipeline.
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) in float vType;
layout(location = 3) in float vLocalY;
layout(location = 4) in float vSeed;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

#include "shadow_common.glsl"
#include "lighting.glsl"

float s_hash(vec2 p) {
    p = floor(p);
    return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453);
}

void main() {
    vec3 base;
    if (vType < 0.5) {
        // stone wall: grey with a faint per-block speckle
        base = vec3(0.54, 0.54, 0.57)
               * (0.90 + 0.10 * s_hash(vWorld.xz * 9.0 + vSeed));
    } else {
        // house: tan walls, red-brown roof on the upper band
        vec3 wall = vec3(0.72, 0.60, 0.42)
                    * (0.92 + 0.08 * s_hash(vWorld.xz * 7.0 + vSeed));
        vec3 roof = vec3(0.47, 0.23, 0.16);
        base = vLocalY > 0.25 ? roof : wall;
    }

    vec3 N = normalize(vNormal);
    float ndlRaw = max(dot(N, normalize(pc.sunDir.xyz)), 0.0);
    float ndl = floor(ndlRaw * 4.0) / 4.0; // 4-band quantise
    float sh = shadowFactor(u_shadow, pc.lightMvp * vec4(vWorld, 1.0), ndlRaw);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl, sh, vWorld);
    // Additive positional lights on walls/roofs (window vWorld space, matching the
    // sun/shadow math). Inert until the light buffer is populated (Inc 3+).
    col += base * point_lights(vWorld, N);
    outColor = vec4(col, 1.0);
}
