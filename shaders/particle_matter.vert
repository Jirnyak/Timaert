#version 450
// Instanced MATTER particle billboard (particles-unified-matter, Inc A). Same
// centred camera-facing quad as particle.vert (the energy pass), but matter is
// LIT — blood, dust and smoke are surfaces, not light sources — so this stage
// also hands the fragment everything lighting needs: the particle's world
// centre and its near-shadow clip position. Both are per-INSTANCE (flat): a
// droplet is centimetres across, so one lighting sample per particle is
// exact enough, and sampling the shadow map at the CENTRE (not the billboard
// corners) avoids acne by construction — a camera-facing quad has no honest
// light-space depth gradient to begin with.
layout(location = 0) in vec3 iPos;    // instance: particle world position (m)
layout(location = 1) in float iSize;  // instance: current half-size (m)
layout(location = 2) in vec4 iColor;  // instance: rgb + faded alpha

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight; // xyz = camera right (world)
    vec4 camUp;    // xyz = camera up (world)
    vec4 sunColor; // rgb, day-intensity folded in (lighting.glsl contract)
    vec4 ambient;  // rgb non-directional fill
    mat4 lightMvp; // crisp near shadow level (far level rides the light SSBO)
} pc;

layout(location = 0) out vec2 vUv;          // [-1,1]² across the quad
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out vec3 vWorldC;    // particle centre (world)
layout(location = 3) flat out vec4 vLightClip; // centre in near-light clip

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.5, 0.5),
        vec2(-0.5, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5));
    vec2 c = corners[gl_VertexIndex];

    vUv = c * 2.0;
    vColor = iColor;
    vWorldC = iPos;
    vLightClip = pc.lightMvp * vec4(iPos, 1.0);

    vec3 right = pc.camRight.xyz;
    vec3 up = pc.camUp.xyz;
    float s = iSize * 2.0; // iSize is a half-size; quad corner is ±0.5
    vec3 world = iPos + right * (c.x * s) + up * (c.y * s);
    gl_Position = pc.mvp * vec4(world, 1.0);
}
