#version 450
// THE subworld billboard shadow-caster vertex stage — one depth-only twin of
// billboard.vert for every sprite pass (trees, creatures, NPCs). Expands the
// same gpu/bb_instance.h record facing the LIGHT (world-up + a
// light-perpendicular right), so every billboard casts its own silhouette
// from exactly the extents the lit pass drew — the aspect can no longer be a
// second guess, because it arrives in the instance.
layout(location = 0) in vec3  iPos;
layout(location = 1) in float iHalfW;
layout(location = 2) in float iHeight;
layout(location = 3) in uint  iKind;
layout(location = 4) in uint  iSeed;
// iTint (location 5) is present in the instance buffer but unused by depth.

layout(push_constant) uniform Push {
    mat4 lightMvp;
    vec4 lightRight; // xyz = horizontal axis perpendicular to the light
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out uint vKind;
layout(location = 2) flat out uint vSeed;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];
    vUv   = vec2(c.x + 0.5, c.y); // base at uv.y = 0, same as billboard.vert
    vKind = iKind;
    vSeed = iSeed;

    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iHalfW * 2.0;
    float h = iHeight;
    vec3 world = iPos + pc.lightRight.xyz * (c.x * w) + up * (c.y * h);
    gl_Position = pc.lightMvp * vec4(world, 1.0);
}
