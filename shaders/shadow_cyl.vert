#version 450
// Depth-only shadow caster for instanced round structures (towers, jambs,
// spire) — the cylinder twin of shadow_struct.vert, same prism generation as
// struct_cyl.vert, sharing shadow_struct.frag (empty, depth only).
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iHalf;

layout(push_constant) uniform Push {
    mat4 lightMvp;
} pc;

const int kSides = 12;
const float kTwoPi = 6.28318530718;

void main() {
    int sideVerts = kSides * 6;
    vec3 local;
    if (gl_VertexIndex < sideVerts) {
        int side = gl_VertexIndex / 6;
        int vi = gl_VertexIndex % 6;
        vec2 uv[6] = vec2[6](vec2(0, -1), vec2(1, -1), vec2(1, 1),
                             vec2(0, -1), vec2(1, 1), vec2(0, 1));
        vec2 q = uv[vi];
        float ang = (float(side) + q.x) * kTwoPi / float(kSides);
        local = vec3(cos(ang) * iHalf.x, q.y * iHalf.y, sin(ang) * iHalf.z);
    } else {
        int tri = (gl_VertexIndex - sideVerts) / 3;
        int vi = (gl_VertexIndex - sideVerts) % 3;
        if (vi == 0) {
            local = vec3(0.0, iHalf.y, 0.0);
        } else {
            float ang = (float(tri) + float(vi - 1)) * kTwoPi / float(kSides);
            local = vec3(cos(ang) * iHalf.x, iHalf.y, sin(ang) * iHalf.z);
        }
    }
    gl_Position = pc.lightMvp * vec4(iPos + local, 1.0);
}
