#version 450
#extension GL_GOOGLE_include_directive : require
// Subworld 3D terrain mesh fragment stage (Phase 5). Procedural per-biome ground
// synth (no atlas) lit by a 4-band quantised NdotL sun + ambient, with a PCF
// shadow-map lookup so cast shadows (terrain + trees) land on the surface.
//
// The material id is sampled PER-FRAGMENT from a full-resolution tile texture
// (u_material) rather than interpolated from mesh vertices. This is what keeps
// thin features — roads, field bands, shorelines — crisp and connected instead
// of dissolving into blobs between the coarse terrain vertices (mirrors the TS
// renderer's per-fragment u_tileGrid lookup).
layout(set = 0, binding = 0) uniform sampler2D u_shadow;
layout(set = 1, binding = 0) uniform sampler2D u_material; // R8 tile material id / 255

layout(location = 0) in vec3 vNormal;
layout(location = 1) in float vHeight;
layout(location = 2) in vec3 vWorld;
layout(location = 3) in vec2 vUv;

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

// Procedural subworld ground synth: the C++ renderer feeds a compact material
// id from the 3×3 tile/biome context. The shader adds pixel-art variation only;
// snow is deliberately not height-forced here (future world-context layer).
float g_hash(vec2 p) {
    p = floor(p);
    return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453);
}
float g_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(g_hash(i), g_hash(i + vec2(1, 0)), f.x),
               mix(g_hash(i + vec2(0, 1)), g_hash(i + vec2(1, 1)), f.x), f.y);
}

vec3 materialBase(float mat) {
    int m = int(floor(mat + 0.5));
    if (m == 0)  return vec3(0.50, 0.52, 0.45); // tundra
    if (m == 1)  return vec3(0.22, 0.38, 0.28); // taiga
    if (m == 2)  return vec3(0.78, 0.82, 0.80); // static snow-biome ground, not cover
    if (m == 3)  return vec3(0.55, 0.52, 0.32); // valley
    if (m == 5)  return vec3(0.24, 0.36, 0.20); // swamp
    if (m == 6)  return vec3(0.82, 0.72, 0.48); // desert
    if (m == 7)  return vec3(0.68, 0.60, 0.32); // steppe
    if (m == 8)  return vec3(0.12, 0.36, 0.12); // tropics
    if (m == 9)  return vec3(0.55, 0.48, 0.26); // field
    if (m == 10) return vec3(0.76, 0.68, 0.46); // shore
    if (m == 11) return vec3(0.45, 0.43, 0.39); // rock
    if (m == 12) return vec3(0.42, 0.34, 0.23); // road/square
    if (m == 13) return vec3(0.38, 0.35, 0.29); // water bed
    return vec3(0.40, 0.52, 0.28);              // meadow/grass
}

vec3 groundColor(vec2 w, float h, float mat) {
    int m = int(floor(mat + 0.5));
    vec3 col = materialBase(mat);
    float patchVal = g_noise(w * 0.035 + float(m) * 11.0);
    float fine = g_hash(floor(w * 22.0));

    if (m == 9) {
        float row = step(0.5, fract((w.x + w.y * 0.35) * 0.22));
        col = mix(col * 0.82, vec3(0.70, 0.62, 0.32), row * 0.42);
    } else if (m == 10) {
        col = mix(col * 0.72, col, smoothstep(0.40, 0.47, h));
        col = mix(col, vec3(0.55, 0.50, 0.40), patchVal * 0.18);
    } else if (m == 11) {
        float crack = smoothstep(0.72, 0.88, g_noise(w * 0.18 + 31.0));
        col = mix(col, vec3(0.30, 0.30, 0.28), crack * 0.28);
    } else if (m == 12) {
        col = mix(col, vec3(0.55, 0.49, 0.38), smoothstep(0.35, 0.75, patchVal) * 0.35);
    } else if (m == 5 || m == 13) {
        col = mix(col, vec3(0.18, 0.25, 0.17), smoothstep(0.35, 0.85, patchVal) * 0.35);
    } else {
        col = mix(col * 0.88, col * 1.10, patchVal * 0.35);
    }

    col *= 0.90 + 0.10 * g_noise(w * 1.7);
    col *= 0.86 + 0.14 * fine;
    return col;
}

void main() {
    // Per-fragment tile material id (R8 stored as id/255) → 0..13 scale.
    float mat = texture(u_material, vUv).r * 255.0;
    vec3 N = normalize(vNormal);
    float ndlRaw = max(dot(N, normalize(pc.sunDir.xyz)), 0.0);
    float ndl = floor(ndlRaw * 4.0) / 4.0; // 4-band quantise
    float sh = shadowFactor(u_shadow, pc.lightMvp * vec4(vWorld, 1.0), ndlRaw);
    // Anchor the procedural detail to ABSOLUTE world coords. vWorld is
    // window-relative (composite-centred), so at a seam recentre it reindexes by
    // ±kCellSize for a fixed physical point — resampling every noise/stripe/crack
    // term and (via lines below) re-mottling brightness, which reads as a texture
    // AND lighting "pop". The renderer packs the composite's absolute origin into
    // the otherwise-unused sunDir.w / sunColor.w lanes (0 in the gpu_smoke3d
    // harness → identity), mirroring the absolute seeding trees already use, so
    // the synth stays put across the seam. Lighting/shadow math keep window vWorld.
    vec2 gWorld = vWorld.xz + vec2(pc.sunDir.w, pc.sunColor.w);
    vec3 base = groundColor(gWorld, vHeight, mat);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl, sh);
    // Additive positional lights (torches, spells, player glow). Uses window vWorld
    // — the same space as the sun/shadow math — not the absolute gWorld synth coord.
    // Inert while the light buffer count is 0 (until an emitter is gathered, Inc 3+).
    col += base * point_lights(vWorld, N);
    outColor = vec4(col, 1.0);
}
