#version 450
// Subworld 3D terrain mesh vertex stage (Phase 5). Transforms world-space
// positions by the camera MVP and forwards the world normal + normalized
// height, plus the grid UV used to sample the full-resolution tile material
// texture per-fragment (mirrors the TS renderer's v_uv = a_pos).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;   // xyz = direction to sun
    vec4 sunColor; // rgb = sun colour
    vec4 ambient;  // rgb = ambient (sky / moon) light
    mat4 lightMvp; // world -> light clip (shadow map)
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out float vHeight;
layout(location = 2) out vec3 vWorld;
layout(location = 3) out vec2 vUv;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vHeight = clamp(inPos.y / 1500.0, 0.0, 1.0);
    vWorld = inPos;
    vUv = inUv;
}
