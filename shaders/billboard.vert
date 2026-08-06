#version 450
// Instanced tree billboard vertex stage (Phase 5). One draw call for every
// tree: the quad corners come from gl_VertexIndex, the per-instance buffer
// supplies world position / size / species / seed. Camera-facing (cylindrical:
// world-up stays vertical, right follows the camera) so trees always face the
// viewer without any per-tree CPU work.
layout(location = 0) in vec3 iPos;      // instance: tree base world position
layout(location = 1) in float iHalfW;   // instance: crown half-width, world units
layout(location = 2) in float iHeight;  // instance: full tree height, world units
layout(location = 3) in float iSpecies; // instance: 0..6 species id
layout(location = 4) in float iSeed;    // instance: per-tree random seed

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;  // xyz = camera right (world space)
    vec4 sunColor;  // rgb = sun colour
    vec4 ambient;   // rgb = ambient (sky / moon) light
    mat4 lightMvp;  // world -> light clip (shadow map projection)
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vSpecies;
layout(location = 2) out float vSeed;
layout(location = 3) flat out vec4 vLightClip; // tree base in light space
layout(location = 4) out vec3 vWorld;          // interpolated world pos (point lights)

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-0.5, 0.0), vec2(0.5, 0.0), vec2(0.5, 1.0),
        vec2(-0.5, 0.0), vec2(0.5, 1.0), vec2(-0.5, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vUv = vec2(c.x + 0.5, c.y);
    vSpecies = iSpecies;
    vSeed = iSeed;

    vec3 right = pc.camRight.xyz;
    vec3 up = vec3(0.0, 1.0, 0.0);
    // Both extents come from the instance — the aspect is decided once on the
    // CPU (sub/tree_atlas.h), never re-guessed per shader.
    float w = iHalfW * 2.0;
    float h = iHeight;
    vec3 world = iPos + right * (c.x * w) + up * (c.y * h);
    gl_Position = pc.mvp * vec4(world, 1.0);
    vWorld = world;
    // Shadow coord taken at the ground-contact base (flat across the quad) so
    // the whole tree shades as a unit -- no per-fragment self-shadow acne.
    vLightClip = pc.lightMvp * vec4(iPos, 1.0);
}
