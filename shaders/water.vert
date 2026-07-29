#version 450
// Subworld water plane (Phase 5): a flat quad at the water level, built from
// gl_VertexIndex (no vertex buffer). Drawn after the terrain with depth-test on
// but depth-write off, so it fills the valleys below the water line and the
// terrain hills poke through.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camPos;   // xyz = camera world position
    vec4 sunDir;   // xyz = direction to sun
    vec4 sunColor; // rgb
    vec4 params;   // x=time y=ambient z=waterLevel w=extent
} pc;

layout(location = 0) out vec3 vWorld;

void main() {
    vec2 c[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1),
                        vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 xz = c[gl_VertexIndex] * pc.params.w;
    vec3 world = vec3(xz.x, pc.params.z, xz.y);
    vWorld = world;
    gl_Position = pc.mvp * vec4(world, 1.0);
}
