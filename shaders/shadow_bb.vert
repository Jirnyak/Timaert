#version 450
// Tree shadow-caster vertex stage: expand the instanced billboard quad facing
// the light (world-up + a light-perpendicular right) into the light's clip
// space so trees cast a correctly-placed silhouette onto the terrain.
layout(location = 0) in vec3 iPos;
layout(location = 1) in float iSize;
layout(location = 2) in float iSpecies;
layout(location = 3) in float iSeed;

layout(push_constant) uniform Push {
    mat4 lightMvp;
    vec4 lightRight; // xyz = horizontal axis perpendicular to the light
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vSpecies;
layout(location = 2) out float vSeed;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];
    vUv = vec2(c.x + 0.5, c.y);
    vSpecies = iSpecies;
    vSeed = iSeed;

    vec3 up = vec3(0.0, 1.0, 0.0);
    float w = iSize * 2.0;
    float h = iSize * 3.2;
    vec3 world = iPos + pc.lightRight.xyz * (c.x * w) + up * (c.y * h);
    gl_Position = pc.lightMvp * vec4(world, 1.0);
}
