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

constexpr float kFovRad      = 1.309f;          // ≈ 75°
constexpr float kFovDeg      = kFovRad * 57.29578f;
constexpr float kMaxPitchRad = 1.04719755f;     // π/3
// Vertical scale / eye height live in sub/height.h and the engine
// (`kCameraEyeM`); the camera is seated by SubworldEngine::record_shadow
// every frame, so pos here is just a pre-first-frame default.

struct Camera {
    vec3  pos     = {0, 0, 0};
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

// Mouse-look. dx/dy in screen pixels.
inline void rotate_camera(Camera& c, float dx, float dy, float sensitivity = 0.002f) {
    c.yaw   += dx * sensitivity;
    c.pitch  = std::clamp(c.pitch - dy * sensitivity, -kMaxPitchRad, kMaxPitchRad);
}

} // namespace sm::sub
