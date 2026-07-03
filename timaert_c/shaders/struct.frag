#version 450
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

float s_hash(vec2 p) {
    p = floor(p);
    return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453);
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
    float sh = shadowFactor(pc.lightMvp * vec4(vWorld, 1.0), ndlRaw);
    vec3 col = base * (pc.ambient.rgb + pc.sunColor.rgb * ndl * sh);
    outColor = vec4(col, 1.0);
}
