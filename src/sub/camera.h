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
constexpr float kMaxPitchRad = kPi / 3.0f;     // π/3
// (ЗДЕСЬ СТОЯЛА `kFovDeg = kFovRad * (180/kPi)`, И ОНА БЫЛА ПОЛОВИНОЙ
//  КРУГОВОГО ПЕРЕВОДА. Угол объявлен В РАДИАНАХ, переводился в градусы ЗДЕСЬ,
//  ехал градусами колонкой камеры и переводился обратно в радианы у
//  рендерера — а единственным читателем каждого звена было СЛЕДУЮЩЕЕ ЗВЕНО.
//  Градусы не нужны ни одному настоящему читателю: Vulkan-проекция берёт
//  радианы, и камера их уже несёт. Оба перевода умерли вместе, а с ними
//  ушёл дрейф 4 ulp, который круг наживал на ровном месте — ЗАКОН КОНСТАНТ
//  («параллельные константы одной величины сводятся к одной») и ЗАКОН ТУПИКА
//  РЕНДЕРА: единицу выбирает тот, кто величину ОБЪЯВИЛ, а не тот, кто её
//  рисует. M-433, хвост FOV.)
// Vertical scale / eye height live in sub/height.h and the engine
// (`kCameraEyeM`); the camera is seated by SubworldEngine::record_shadow
// every frame, so pos here is just a pre-first-frame default.

struct Camera {
    vec3  pos     = {0, 0, 0};
    float yaw     = 0.0f;     // around +Y, 0 = looking +X
    float pitch   = 0.0f;     // around side axis (+ = up, clamped to ±π/3)
    float fovRad  = kFovRad;  // РАДИАНЫ — единица объявления и единица чтения

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
