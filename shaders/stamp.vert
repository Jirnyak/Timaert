#version 450
// Stain-canvas stamp vertex stage (particles-unified-matter, Inc C). One
// instanced quad per mark COMMAND: the CPU decided where/what/how big, this
// stage places the quad on the toroidal canvas, the fragment stage computes
// the mark's every pixel (the gigahrush surface_marks shape shaders).
//
// Canvas addressing law (must match mesh.frag's sampling law): the canvas
// spans kCanvasTiles=1024 world tiles (1 tile = 1 m), texel = world tile mod
// 1024, at 8 px/tile. A stamp near the toroidal edge is NOT wrapped: the quad
// just clips. The edge sits exactly half a canvas (512 tiles) from the camera
// — beyond the terrain shader's validity mask — so a clipped sliver there can
// never be seen.
layout(location = 0) in vec4 iPosRadSeed; // wx, wz (m), radius (m), seed
layout(location = 1) in vec4 iColor;      // rgb + intensity (max alpha), unorm
layout(location = 2) in float iType;      // MarkType as float (see stamp.frag)

layout(location = 0) out vec2 vUv;        // [-1,1]² across the mark disk
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out float vSeed;
layout(location = 3) flat out float vType;

// Keep in lockstep with mesh.frag u_stain sampling and the C++ static_asserts
// (vk_renderer_3d.cpp): world tile = world metre + kFullSize/2 = w + 1536.
const float kHalfWindowTiles = 1536.0;
const float kCanvasTiles = 1024.0;

void main() {
    vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 c = corners[gl_VertexIndex];

    vUv = c;
    vColor = iColor;
    vSeed = iPosRadSeed.w;
    vType = iType;

    // World metres → canvas UV [0,1): toroidal by fract (matches the REPEAT
    // sampler on the read side), then NDC. The radius stays un-wrapped.
    vec2 tile = iPosRadSeed.xy + vec2(kHalfWindowTiles);
    vec2 uv = fract(tile / kCanvasTiles);
    float radUv = iPosRadSeed.z / kCanvasTiles;
    vec2 ndc = (uv + c * radUv) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
