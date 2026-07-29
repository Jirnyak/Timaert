#version 450
// Instanced additive particle billboard (FX Inc A). One draw call for the whole
// live particle pool: the quad corners come from gl_VertexIndex, the per-
// instance buffer supplies world position / size / colour+alpha. Unlike the
// tree billboard (cylindrical, feet-anchored), particles are CENTRED and fully
// camera-facing (spherical) — the quad spans camRight and camUp — so sparks and
// motes face the viewer from every angle. No lighting, no shadow: the additive
// fragment stage is emissive.
layout(location = 0) in vec3 iPos;    // instance: particle world position (m)
layout(location = 1) in float iSize;  // instance: current half-size (m)
layout(location = 2) in vec4 iColor;  // instance: rgb + faded alpha

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight; // xyz = camera right (world)
    vec4 camUp;    // xyz = camera up (world)
} pc;

layout(location = 0) out vec2 vUv;    // [-1,1]² across the quad (radial falloff)
layout(location = 1) out vec4 vColor;

void main() {
    // Centred quad: corners span [-0.5,0.5]² so the billboard is anchored at the
    // particle centre (not the feet like trees).
    vec2 corners[6] = vec2[6](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
        vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
    vec2 c = corners[gl_VertexIndex];

    vUv = c * 2.0;      // -> [-1,1]², so length(vUv) is the radial coord
    vColor = iColor;

    vec3 right = pc.camRight.xyz;
    vec3 up = pc.camUp.xyz;
    float s = iSize * 2.0; // iSize is a half-size; quad corner is ±0.5
    vec3 world = iPos + right * (c.x * s) + up * (c.y * s);
    gl_Position = pc.mvp * vec4(world, 1.0);
}
