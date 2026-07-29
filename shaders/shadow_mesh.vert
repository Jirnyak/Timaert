#version 450
// Terrain shadow-caster: depth-only transform into the light's clip space.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal; // unused (binding matches terrain vtx)

layout(push_constant) uniform Push {
    mat4 lightMvp;
} pc;

void main() {
    gl_Position = pc.lightMvp * vec4(inPos, 1.0);
}
