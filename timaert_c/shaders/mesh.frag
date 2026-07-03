#version 450
// Subworld 3D terrain mesh fragment stage (Phase 5). Procedural per-biome ground
// synth (no atlas) lit by a 4-band quantised NdotL sun + ambient, with a PCF
// shadow-map lookup so cast shadows (terrain + trees) land on the surface.
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in float vHeight;
layout(location = 2) in vec3 vWorld;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec4 outColor;

// Procedural subworld ground synth: per-biome pixel-art, no atlas. The biome is
// derived from elevation + a low-frequency moisture field, then painted on a
// quantised sub-grid so the ground reads as retro pixel-art (matches the macro
// synth philosophy). In the game the biome comes from the tile grid; feed it in
// place of the derived bands.
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

vec3 groundColor(vec2 w, float h) {
    float moist = g_noise(w * 0.35 + 17.0);

    vec3 sand   = vec3(0.76, 0.70, 0.48);
    vec3 grass  = vec3(0.34, 0.52, 0.26);
    vec3 forest = vec3(0.20, 0.38, 0.18);
    vec3 swamp  = vec3(0.29, 0.39, 0.24);
    vec3 dirt   = vec3(0.46, 0.40, 0.29);
    vec3 rock   = vec3(0.49, 0.47, 0.45);
    vec3 snow   = vec3(0.92, 0.94, 0.98);

    vec3 col;
    if (h < 0.22) {
        col = mix(sand * 0.78, sand, smoothstep(0.10, 0.22, h)); // wet -> dry sand
    } else if (h < 0.46) {
        vec3 low = mix(grass, swamp, smoothstep(0.62, 0.90, moist));
        low = mix(low, forest, smoothstep(0.45, 0.72, moist) * 0.55);
        col = mix(mix(sand, grass, 0.5), low, smoothstep(0.22, 0.28, h)); // beach fade
    } else if (h < 0.66) {
        col = mix(mix(grass, forest, 0.4), dirt, smoothstep(0.46, 0.66, h));
    } else if (h < 0.82) {
        col = mix(dirt, rock, smoothstep(0.66, 0.82, h));
    } else {
        col = mix(rock, snow, smoothstep(0.82, 0.90, h));
    }

    col *= 0.92 + 0.08 * g_noise(w * 1.7);        // low-freq patchiness
    col *= 0.86 + 0.14 * g_hash(floor(w * 22.0)); // quantised pixel speckle
    return col;
}

// PCF shadow lookup: 1.0 = lit, 0.0 = shadowed.
float shadowFactor(vec4 lightClip, float ndl) {
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;
    float bias = max(0.0015, 0.006 * (1.0 - ndl));
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
    vec3 N = normalize(vNormal);
    float ndlRaw = max(dot(N, normalize(pc.sunDir.xyz)), 0.0);
    float ndl = floor(ndlRaw * 4.0) / 4.0; // 4-band quantise
    float sh = shadowFactor(pc.lightMvp * vec4(vWorld, 1.0), ndlRaw);
    vec3 base = groundColor(vWorld.xz, vHeight);
    vec3 col = base * (pc.ambient.rgb + pc.sunColor.rgb * ndl * sh);
    outColor = vec4(col, 1.0);
}
