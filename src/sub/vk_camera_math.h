// Vulkan-correct camera projection helpers for the subworld 3D renderer.
// Differs from core/math.h mat4_perspective / mat4_ortho:
//   • Depth maps to [0, 1] (not [-1, 1])
//   • Y is flipped (Vulkan clip convention: +Y = down)
// Copied from tests/gpu_smoke3d.cpp and placed here so the production
// renderer can use them without depending on the test harness.
#pragma once

#include "core/math.h"
#include <cmath>

namespace sm::sub {

// Right-handed perspective, depth 0..1, Y flipped for Vulkan clip space.
inline mat4 vk_perspective(float fovy, float aspect, float zn, float zf) {
    mat4 r{};
    float f = 1.0f / std::tan(fovy * 0.5f);
    r.m[0]  = f / aspect;
    r.m[5]  = -f;
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}

// Orthographic, depth 0..1, for the sun's shadow view.
inline mat4 vk_ortho(float l, float r, float b, float t, float zn, float zf) {
    mat4 m{};
    m.m[0]  = 2.0f / (r - l);
    m.m[5]  = 2.0f / (t - b);
    m.m[10] = -1.0f / (zf - zn);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -zn / (zf - zn);
    m.m[15] = 1.0f;
    return m;
}

} // namespace sm::sub
