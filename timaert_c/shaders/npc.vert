#version 450
// Instanced paper-doll NPC billboard vertex stage (Phase 5). One draw call for
// every NPC: the quad corners come from gl_VertexIndex, the per-instance buffer
// supplies world position / size / seed. Camera-facing (cylindrical) so NPCs
// always face the viewer. Same instanced pattern as the trees; the fragment
// stage draws a procedural humanoid keyed by the per-instance seed. In the game
// this is where the character paper-doll atlas is sampled instead.
layout(location = 0) in vec3 iPos;   // instance: feet world position
layout(location = 1) in float iSize; // instance: half-height scale
layout(location = 2) in float iSeed; // instance: per-NPC random seed

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;  // xyz = camera right (world)
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;  // world -> light clip (shadow map)
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vSeed;
layout(location = 2) flat out vec4 vLightClip; // feet in light space

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vUv = vec2(c.x + 0.5, c.y);
    vSeed = iSeed;

    vec3 right = pc.camRight.xyz;
    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iSize * 1.5;
    float h = iSize * 3.2;
    vec3 world = iPos + right * (c.x * w) + up * (c.y * h);
    gl_Position = pc.mvp * vec4(world, 1.0);
    // Shadow coord at the feet (flat) so the whole billboard shades as a unit.
    vLightClip = pc.lightMvp * vec4(iPos, 1.0);
}
