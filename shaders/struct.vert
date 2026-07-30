#version 450
// Instanced structure box vertex stage (Phase 5): city walls + houses. The unit
// cube (36 verts) is generated from gl_VertexIndex; the per-instance buffer
// supplies box centre, half-extents, type and seed. One draw call covers every
// wall segment and house. Extensible: a new structure kind is a new `type`
// value + one branch in the fragment stage — no new pipeline, no new geometry.
layout(location = 0) in vec3 iPos;   // instance: box centre (world)
layout(location = 1) in vec3 iHalf;  // instance: half-extents (world)
layout(location = 2) in float iType; // instance: 0 = wall, 1 = house
layout(location = 3) in float iSeed; // instance: per-structure random seed
layout(location = 4) in float iYaw;  // instance: rotation about vertical (rad)

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
layout(location = 2) out float vType;
layout(location = 3) out float vLocalY; // cube-space y in [-1,1] (roof band)
layout(location = 4) out float vSeed;

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
    vec3 corner = n + tang * q.x + bitan * q.y; // [-1,1]^3

    // Yaw about the vertical axis: oriented houses / wall segments following
    // the ring curvature. The SAME rotation the collision index inverts
    // (sub/collide.h), so the visible silhouette is exactly the solid one.
    float c = cos(iYaw), s = sin(iYaw);
    vec3 l = corner * iHalf;
    vec3 world = iPos + vec3(l.x * c - l.z * s, l.y, l.x * s + l.z * c);
    gl_Position = pc.mvp * vec4(world, 1.0);
    vNormal = vec3(n.x * c - n.z * s, n.y, n.x * s + n.z * c);
    vWorld = world;
    vType = iType;
    vLocalY = corner.y;
    vSeed = iSeed;
}
