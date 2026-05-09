// Subworld lighting — sun direction, ambient, point lights.
// TS-faithful port of src/game/subworld/lighting.ts.
//
// Pure graphics helper: computes light parameters from time-of-day.
// No game state dependencies; consumed by renderer_3d.
//
// `sunDir = (cos(sunAng), sin(sunAng), 0)` — pointing FROM origin TOWARD
// the sun (TS convention). Elevation is `sunDir.y`. Renderer_3d expects
// uSunDir pointing FROM the sun TOWARD the world, so it negates on
// upload.
#pragma once
#include "core/math.h"
#include "macro/state.h"
#include <cmath>

namespace sm::sub {

constexpr int kMaxPointLights = 8;

struct PointLight {
    float x, y, z;     // world position
    float r, g, b;     // RGB colour [0..1]
    float radius;      // attenuation falloff distance
};

struct LightParameters {
    vec3  sunDir;        // (cos, sin, 0) — toward sun (TS convention)
    vec3  sunColor;
    float sunIntensity;  // 0..1
    vec3  ambientColor;
};

// Backwards-compat alias used by the renderer.
using SunInfo = LightParameters;

namespace detail {
inline float smoothstep01(float a, float b, float x) {
    float t = (x - a) / (b - a);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}
inline float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}
}

inline LightParameters compute_light_parameters(float tod) {
    using detail::smoothstep01;
    using detail::clamp01;
    const float sunAng = (tod - 0.25f) * 6.2831853f;
    LightParameters p;
    p.sunDir   = { std::cos(sunAng), std::sin(sunAng), 0.0f };
    const float elevation = p.sunDir.y;
    p.sunIntensity = smoothstep01(-0.05f, 0.30f, elevation);
    const float warm = 1.0f - smoothstep01(0.0f, 0.4f, elevation);
    p.sunColor = {
        1.0f - warm * 0.10f,
        1.0f - warm * 0.30f,
        1.0f - warm * 0.55f,
    };
    const float dayRaw = smoothstep01(0.22f, 0.35f, tod)
                       - smoothstep01(0.65f, 0.78f, tod);
    const float dayF   = clamp01(dayRaw);
    p.ambientColor = {
        0.12f + dayF * 0.28f,
        0.12f + dayF * 0.28f,
        0.18f + dayF * 0.22f,
    };
    return p;
}

inline LightParameters compute_sun(const WorldTime& t) {
    const float tod = (float(t.hour) + float(t.minute) / 60.0f) / 24.0f;
    return compute_light_parameters(tod);
}

} // namespace sm::sub
