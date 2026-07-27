#version 450
// Creature shadow-caster vertex stage: expand the instanced creature billboard
// facing the light (world-up + a light-perpendicular right) into the light's
// clip space, so a creature casts its own procedural silhouette onto terrain.
layout(location = 0) in vec3  iPos;
layout(location = 1) in float iSize;
layout(location = 2) in float iArch;
layout(location = 3) in float iSeed;
// iTint (location 4) is present in the instance buffer but unused by the depth
// pass; the vertex input simply does not declare it.

layout(push_constant) uniform Push {
    mat4 lightMvp;
    vec4 lightRight; // xyz = horizontal axis perpendicular to the light
} pc;

layout(location = 0) out vec2  vUv;
layout(location = 1) flat out float vArch;
layout(location = 2) flat out float vSeed;

// MUST match creature.vert::archAspect so the shadow silhouette matches the lit
// one (same quad extents feed the same coverage function).
vec2 archAspect(int a) {
    if (a == 0) return vec2(1.70, 1.15);
    if (a == 1) return vec2(1.50, 1.05);
    if (a == 2) return vec2(1.15, 1.50);
    if (a == 3) return vec2(1.25, 1.80);
    if (a == 4) return vec2(1.10, 1.80);
    if (a == 5) return vec2(1.80, 2.05);
    return vec2(0.95, 0.80);
}

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];
    vUv   = vec2(c.x + 0.5, c.y); // same convention as creature.vert (no flip)
    vArch = iArch;
    vSeed = iSeed;

    vec2 asp = archAspect(int(iArch + 0.5));
    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iSize * asp.x;
    float h = iSize * asp.y;
    vec3 world = iPos + pc.lightRight.xyz * (c.x * w) + up * (c.y * h);
    gl_Position = pc.lightMvp * vec4(world, 1.0);
}
