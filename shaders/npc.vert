#version 450
// Instanced paper-doll NPC billboard vertex stage. One draw call for every NPC:
// the quad corners come from gl_VertexIndex, the per-instance buffer supplies
// world position / size / paper-doll pool layer (PaperdollAtlas::layer_for —
// the frame was composited once into the sprite pool, the fragment stage just
// samples it). Camera-facing (cylindrical) so NPCs always face the viewer.
layout(location = 0) in vec3 iPos;    // instance: feet world position
layout(location = 1) in float iSize;  // instance: human-height scale
layout(location = 2) in uint iLayer;  // instance: sprite-pool array layer

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;  // xyz = camera right (world)
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;  // world -> light clip (shadow map)
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out uint vLayer;
layout(location = 2) flat out vec4 vLightClip; // feet in light space
layout(location = 3) out vec3 vWorld;          // interpolated world pos (point lights)

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vUv = vec2(c.x + 0.5, 1.0 - c.y);
    vLayer = iLayer;

    vec3 right = pc.camRight.xyz;
    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iSize * 1.0;
    float h = iSize * 1.0;
    vec3 world = iPos + right * (c.x * w) + up * (c.y * h);
    gl_Position = pc.mvp * vec4(world, 1.0);
    vWorld = world;
    // Shadow coord at the feet (flat) so the whole billboard shades as a unit.
    vLightClip = pc.lightMvp * vec4(iPos, 1.0);
}
