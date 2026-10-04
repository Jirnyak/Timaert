#version 450
#extension GL_GOOGLE_include_directive : require
// A PROP WITH A SHAPE FROM THE TABLE — the lit vertex stage.
//
// WHY THIS REPLACES A SECOND ENUM VALUE. Shape used to be `Structure::Shape`
// with exactly two members, and each cost a vertex shader, a lit pipeline, a
// shadow pipeline, an instance buffer and three draw blocks — so a tree could
// not simply BE a shape, it had to be a billboard pretending to be one. Here a
// shape is a ROW (data/prop_profiles.csv): segments around, rings up. Adding
// one is a line of CSV and a draw with a different vertex count; nothing in
// the engine learns a new name (ЗАКОН СТРОКИ КАТАЛОГА).
//
// NO VERTEX BUFFER, like the rest of this renderer, and NO NEW INSTANCE
// FORMAT: the record is the existing billboard one (gpu/bb_instance.h, 32 B),
// whose `kind` lane is now the PROFILE ORDINAL. That is not thrift — it means
// the tree pass keeps the gather it already has, and flat→solid is a change of
// pipeline rather than a migration of data.
#include "prop_body.glsl"

layout(location = 0) in vec3  iPos;      // base centre, world metres
layout(location = 1) in float iHalfW;    // half-width at radius fraction 1
layout(location = 2) in float iHeight;   // full height, metres
layout(location = 3) in uint  iKind;     // PROFILE ORDINAL
layout(location = 4) in uint  iSeed;     // per-instance variation
layout(location = 5) in uint  iTint;     // reserved; the row owns colour

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    vec4 camPos;
    vec4 shore;
} pc;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out vec3  vWorld;
layout(location = 2) out float vLocalY;
layout(location = 3) flat out uint vProfile;
layout(location = 4) flat out uint vSeed;
layout(location = 5) out vec4  vLightClip;
layout(location = 6) out vec2  vLeafUv;
layout(location = 7) out float vIsLeaf;

void main() {
    uint prof = min(iKind, kProfileCount - 1u);
    vec3 lp, ln; float ly; vec2 luv; float leaf;
    prop_body_vertex(uint(gl_VertexIndex), prof, iHalfW, iHeight, lp, ln, ly,
                     luv, leaf);
    mat2 rot = prop_body_yaw(iSeed);
    vec3 world = iPos + vec3(rot * lp.xz, lp.y).xzy;
    vec3 nrm   = normalize(vec3(rot * ln.xz, ln.y).xzy);

    vNormal  = nrm;
    vWorld   = world;
    vLocalY  = ly;
    vProfile = prof;
    vSeed    = iSeed;
    vLeafUv  = luv;
    vIsLeaf  = leaf;
    vLightClip  = pc.lightMvp * vec4(world, 1.0);
    gl_Position = pc.mvp * vec4(world, 1.0);
}
