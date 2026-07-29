// First-person free camera for the subworld 3D view. TS-faithful port of
// src/game/subworld/camera.ts. Constants match TS:
//   - EYE_HEIGHT  = 2.0   (eye altitude above floor)
//   - FOV         = 1.309 rad ≈ 75°
//   - MAX_PITCH   = π/3   (60°)
//   - HEIGHT_SCALE= 500   (heightmap [0..1] → world units)
// Yaw is the horizontal look angle (0 = +X, π/2 = +Y in the TS plane;
// our renderer uses Y-up so we treat XZ as the ground plane and clamp
// pitch the same way).
#pragma once
#include "core/math.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sm::sub {

constexpr float kEyeHeight   = 2.0f;
constexpr float kFovRad      = 1.309f;          // ≈ 75°
constexpr float kFovDeg      = kFovRad * 57.29578f;
constexpr float kMaxPitchRad = 1.04719755f;     // π/3
// Note: TS exports HEIGHT_SCALE = 500 here, but in the C++ port the
// renderer owns its own height scale (`kHeightScale` in renderer_3d.cpp,
// currently 28 to fit the compact 192² mesh). Each consumer picks its
// own scale; the TS value is an implementation detail of the renderer
// in both ports, not a shared constant.

struct Camera {
    vec3  pos     = {0, kEyeHeight, 0};
    float yaw     = 0.0f;     // around +Y, 0 = looking +X
    float pitch   = 0.0f;     // around side axis (+ = up, clamped to ±π/3)
    float fovDeg  = kFovDeg;

    vec3 forward() const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw),   sy = std::sin(yaw);
        return {cp * cy, sp, cp * sy};
    }
    vec3 right() const {
        return normalize(cross(forward(), {0, 1, 0}));
    }
};

// Bilinear sample of a heightmap at fractional coords. Mirrors TS
// `sampleHeight`. Out-of-bounds clamps to nearest edge.
inline float sample_height(const float* hm, int W, int H, float x, float y) {
    if (W <= 0 || H <= 0) return 0.0f;
    float fx = std::clamp(x, 0.0f, float(W) - 1.001f);
    float fy = std::clamp(y, 0.0f, float(H) - 1.001f);
    int ix = int(fx), iy = int(fy);
    float dx = fx - float(ix), dy = fy - float(iy);
    int ix1 = std::min(ix + 1, W - 1);
    int iy1 = std::min(iy + 1, H - 1);
    float h00 = hm[std::size_t(iy)  * W + ix ];
    float h10 = hm[std::size_t(iy)  * W + ix1];
    float h01 = hm[std::size_t(iy1) * W + ix ];
    float h11 = hm[std::size_t(iy1) * W + ix1];
    return h00 * (1.0f - dx) * (1.0f - dy)
         + h10 * dx          * (1.0f - dy)
         + h01 * (1.0f - dx) * dy
         + h11 * dx          * dy;
}

// Mouse-look. dx/dy in screen pixels.
inline void rotate_camera(Camera& c, float dx, float dy, float sensitivity = 0.002f) {
    c.yaw   += dx * sensitivity;
    c.pitch  = std::clamp(c.pitch - dy * sensitivity, -kMaxPitchRad, kMaxPitchRad);
}

// Forward/strafe in the ground plane (yaw-only).
inline void move_vector(float yaw, float forward, float strafe,
                        float& mx, float& my) {
    float cy = std::cos(yaw), sy = std::sin(yaw);
    mx = forward * cy + strafe * sy;
    my = forward * sy + strafe * cy;
}

// Forward follows full look direction (yaw+pitch); strafe stays horizontal.
inline void move_vector_3d(float yaw, float pitch, float forward, float strafe,
                           float& mx, float& my, float& mz) {
    float cy = std::cos(yaw),   sy = std::sin(yaw);
    float cp = std::cos(pitch), sp = std::sin(pitch);
    mx = forward * cy * cp + strafe * sy;
    my = forward * sy * cp + strafe * cy;
    mz = forward * sp;
}

} // namespace sm::sub
