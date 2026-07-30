#version 450
// Instanced structure shadow caster (Phase 5): expands the per-instance box
// (cube from gl_VertexIndex) into the sun's light space so walls and houses cast
// real shadows onto the terrain and each other. Depth only.
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iHalf;
layout(location = 4) in float iYaw;

layout(push_constant) uniform Push {
    mat4 lightMvp;
} pc;

void main() {
    const vec3 FN[6] = vec3[6](vec3(0, 0, 1), vec3(0, 0, -1), vec3(1, 0, 0),
                               vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0));
    const vec2 QUAD[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1),
                                 vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    int face = gl_VertexIndex / 6;
    int vi = gl_VertexIndex % 6;
    vec3 n = FN[face];
    vec2 q = QUAD[vi];
    vec3 up = abs(n.y) > 0.5 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 tang = normalize(cross(up, n));
    vec3 bitan = cross(n, tang);
    vec3 corner = n + tang * q.x + bitan * q.y;
    // Same yaw rotation as struct.vert so the cast shadow matches the box.
    float c = cos(iYaw), s = sin(iYaw);
    vec3 l = corner * iHalf;
    gl_Position = pc.lightMvp
        * vec4(iPos + vec3(l.x * c - l.z * s, l.y, l.x * s + l.z * c), 1.0);
}
