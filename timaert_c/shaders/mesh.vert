#version 450
// Subworld 3D terrain mesh vertex stage (Phase 5). Transforms world-space
// positions by the camera MVP and forwards the world normal + height for the
// lit fragment pass.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

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

void main() {
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vHeight = inPos.y;
    vWorld = inPos;
}
