#version 450
// Instanced ROUND structure vertex stage: wall towers, gate jambs, the spire.
// A 12-sided prism (sides + top cap) is generated from gl_VertexIndex, exactly
// like struct.vert generates the unit cube; the instance layout is the SAME
// StructInstance (radius rides iHalf.x, iYaw is meaningless on a round body),
// and the fragment stage is the shared struct.frag — a cylinder is a shape,
// not a new material. Vertex count per instance = kSides * 9 (6 side + 3 cap).
layout(location = 0) in vec3 iPos;   // instance: centre (world)
layout(location = 1) in vec3 iHalf;  // instance: x = radius, y = half-height
layout(location = 2) in float iType; // instance: 0 = wall/stone, 1 = house
layout(location = 3) in float iSeed; // instance: per-structure random seed
// location 4 (iYaw) is deliberately not consumed: a round body has no yaw.

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
layout(location = 3) out float vLocalY;
layout(location = 4) out float vSeed;

const int kSides = 12;
const float kTwoPi = 6.28318530718;

void main() {
    int sideVerts = kSides * 6;
    vec3 local;
    vec3 n;
    if (gl_VertexIndex < sideVerts) {
        int side = gl_VertexIndex / 6;
        int vi = gl_VertexIndex % 6;
        // Quad corners: (0,-1) (1,-1) (1,1) / (0,-1) (1,1) (0,1) — u picks the
        // edge angle, v the top/bottom rim.
        vec2 uv[6] = vec2[6](vec2(0, -1), vec2(1, -1), vec2(1, 1),
                             vec2(0, -1), vec2(1, 1), vec2(0, 1));
        vec2 q = uv[vi];
        float ang = (float(side) + q.x) * kTwoPi / float(kSides);
        vec2 dir = vec2(cos(ang), sin(ang));
        local = vec3(dir.x * iHalf.x, q.y * iHalf.y, dir.y * iHalf.z);
        n = vec3(dir.x, 0.0, dir.y);
        vLocalY = q.y;
    } else {
        // Top cap: fan of kSides triangles around the axis.
        int tri = (gl_VertexIndex - sideVerts) / 3;
        int vi = (gl_VertexIndex - sideVerts) % 3;
        if (vi == 0) {
            local = vec3(0.0, iHalf.y, 0.0);
        } else {
            float ang = (float(tri) + float(vi - 1)) * kTwoPi / float(kSides);
            local = vec3(cos(ang) * iHalf.x, iHalf.y, sin(ang) * iHalf.z);
        }
        n = vec3(0.0, 1.0, 0.0);
        vLocalY = 1.0;
    }
    vec3 world = iPos + local;
    gl_Position = pc.mvp * vec4(world, 1.0);
    vNormal = n;
    vWorld = world;
    vType = iType;
    vSeed = iSeed;
}
