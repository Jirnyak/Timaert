#version 450
// NPC shadow-caster vertex stage: expand the instanced NPC billboard quad facing
// the light (world-up + a light-perpendicular right) into the light's clip
// space, so an NPC casts its own sprite silhouette onto the terrain.
// Same UV convention as npc.vert (v=0 at the head), so both stages sample the
// sprite pool identically — the old per-stage Y-flip is gone with the SSBO path.
layout(location = 0) in vec3 iPos;
layout(location = 1) in float iSize;
layout(location = 2) in uint iLayer;

layout(push_constant) uniform Push {
    mat4 lightMvp;
    vec4 lightRight; // xyz = horizontal axis perpendicular to the light
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out uint vLayer;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];
    vUv = vec2(c.x + 0.5, 1.0 - c.y);
    vLayer = iLayer;

    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iSize * 1.0;
    float h = iSize * 1.0;
    vec3 world = iPos + pc.lightRight.xyz * (c.x * w) + up * (c.y * h);
    gl_Position = pc.lightMvp * vec4(world, 1.0);
}
