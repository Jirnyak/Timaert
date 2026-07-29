// Subworld lighting — the single directional light (sun by day, moon by night),
// ambient fill, and point lights. TS-faithful port of subworld/lighting.ts.
//
// Pure graphics helper: computes light parameters from time-of-day.
// No game state dependencies; consumed by the Vulkan renderer_3d.
//
// `sunDir = (cos(sunAng), sin(sunAng), 0)` points FROM the origin TOWARD the
// sun; `sunDir.y` is the sun's elevation. The renderer uploads it AS-IS and the
// shaders use it directly as L in N·L (see mesh.frag) — nothing negates it, so
// do NOT re-invert. At night the sun drops below the horizon and its radiance
// folds to zero; the same slot is then re-pointed at the MOON (≈ -sunDir) with a
// weak cool radiance, so `sunDir`/`sunColor` always describe whichever celestial
// body is currently lighting the world.
#pragma once
#include "core/math.h"
#include "macro/state.h"
#include <cmath>
#include <cstdint>

namespace sm::sub {

// ── Positional point lights ──────────────────────────────────────────────
// Budget for simultaneous subworld point lights (torches, spell glows, lit
// windows, the player's own light). Uploaded to the GPU as a storage buffer at
// set 0, binding 1 — the SAME set the shadow sampler lives on, so the one
// descriptor is already bound by every lit pass (terrain/trees/structs/NPCs/
// creatures/water) and lighting them is a single shader addition, not six.
// 32 is generous for the handful of emitters ever near the camera; raising it
// is one number here (the SSBO and its std430 mirror scale automatically).
constexpr int kSubworldMaxLights = 32;

// One point light, std430-compatible (two vec4 lanes, 32 B, 16-B aligned) so
// the CPU struct and the GLSL `Light` map byte-for-byte with no padding fixups:
//   pos.xyz   = world position      pos.w   = radius (attenuation reach, m)
//   color.rgb = linear RGB radiance color.w = intensity (scalar multiplier)
// Packing radius/intensity into the w lanes keeps the stride at a clean 32 B.
struct GpuLight {
    float pos[4];
    float color[4];
};
static_assert(sizeof(GpuLight) == 32, "GpuLight must match std430 stride");

// The whole light SSBO as the shader sees it: a count (padded to a 16-byte
// boundary so the array starts std430-aligned) followed by the fixed budget of
// lights. Created once at full size; each frame writes `count` + the first
// `count` entries. `count == 0` ⇒ every lit pass falls straight through to the
// directional-only result, i.e. byte-identical to the pre-point-light renderer.
struct GpuLightBuffer {
    std::uint32_t count;
    std::uint32_t _pad[3];
    GpuLight      lights[kSubworldMaxLights];
};
static_assert(sizeof(GpuLightBuffer) == 16 + 32 * kSubworldMaxLights,
              "GpuLightBuffer must match the std430 SSBO layout");

struct LightParameters {
    vec3  sunDir;        // toward the active body: sun by day, moon (≈ -sun) by night
    vec3  sunColor;      // its radiance, day-intensity folded in; the moon adds a
                         // weak cool term at night (the two never overlap)
    float sunIntensity;  // SUN elevation intensity 0..1 (→0 at night). Drives the
                         // night branch of compute_shadow_basis — this is the SUN's,
                         // not the moon's, so shadow logic still reads "is it night".
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

// Peak moon directional radiance as a fraction of full sun (whose sunColor peaks
// at ~1.0). The moon is a weak, cool analogue of the sun — "просто он слабее".
// This is the one knob to brighten/dim directional moonlight; ambient fill is
// tuned separately in the ambientColor block below.
//
// Prominence: the moon must read as a DIRECTIONAL light — moon-facing faces
// clearly brighter than the fill — not a flat wash. That means the directional
// gain has to dominate the ambient floor (contrast = gain·N·L : ambient). At
// 0.18 with a 0.18 blue-ambient floor the ratio was ~1:1 → the "равномерно"
// (uniform) look the owner flagged. Raised here, with the ambient floor lowered
// below, so the ratio is ~2–3:1 and the moon sculpts the terrain.
constexpr float kMoonDirGain = 0.42f;

inline LightParameters compute_light_parameters(float tod) {
    using detail::smoothstep01;
    using detail::clamp01;
    const float sunAng = (tod - 0.25f) * 6.2831853f;
    LightParameters p;
    p.sunDir   = { std::cos(sunAng), std::sin(sunAng), 0.0f };
    const float elevation = p.sunDir.y;
    p.sunIntensity = smoothstep01(-0.05f, 0.30f, elevation);
    // Warm the sun toward the horizon (orange at sunrise/set), neutral overhead.
    const float warm = 1.0f - smoothstep01(0.0f, 0.4f, elevation);
    // CRITICAL — fold the day-intensity into sunColor so the DIRECT sun term
    // fades to zero once the sun is below the horizon. Every lit consumer
    // (terrain, structures, tree/NPC/creature billboards, water) multiplies this
    // sunColor into its N·L (or flat) sun term, so scaling it *here* is the one
    // universal switch that turns the sun off at night. Without it the sun term
    // stayed warm-bright below the horizon: billboards (flat sun term, no N·L)
    // and vertical wall faces (N·L catches the sun's still-large horizontal
    // component) then "glow" at night, while flat terrain only escaped by
    // geometry (upward N·L ≤ 0 against a below-horizon sun). Ambient (moonlight)
    // is applied separately and left unscaled, so night is moonlit, never black.
    p.sunColor = {
        (1.0f - warm * 0.10f) * p.sunIntensity,
        (1.0f - warm * 0.30f) * p.sunIntensity,
        (1.0f - warm * 0.55f) * p.sunIntensity,
    };
    // ── Moonlight ───────────────────────────────────────────────────────────
    // The moon rides the SAME directional slot as the sun. It sits opposite the
    // sun in the sky plane (mAng = sunAng - π ⇒ moonDir = -sunDir), so it is UP
    // exactly when the sun is DOWN and the two are never both bright. We reuse
    // the sun's elevation (the moon's is -elevation), fade the moon in the same
    // smoothstep, then simply SUM the two radiances — one is always ≈0. When the
    // moon outshines the sun (after dusk) we flip the shared direction to the
    // moon; that flip happens while BOTH terms are ≈0, so there is no visible
    // pop. This keeps the whole lit pipeline on one sunDir/sunColor and turns the
    // old pure-black night into a faint, cool directional for every lit object at
    // once — no per-object special-casing.
    const float moonElevation = -elevation;
    const float moonIntensity =
        smoothstep01(-0.05f, 0.30f, moonElevation) * kMoonDirGain;
    if (moonIntensity > p.sunIntensity) {
        p.sunDir = { -p.sunDir.x, -p.sunDir.y, -p.sunDir.z };
    }
    p.sunColor.x += 0.60f * moonIntensity;  // cool blue-white
    p.sunColor.y += 0.70f * moonIntensity;
    p.sunColor.z += 1.00f * moonIntensity;
    const float dayRaw = smoothstep01(0.22f, 0.35f, tod)
                       - smoothstep01(0.65f, 0.78f, tod);
    const float dayF   = clamp01(dayRaw);
    // Ambient = non-directional fill. The night floor (dayF→0) is deliberately
    // LOW and cool so the directional moon (kMoonDirGain above) dominates and
    // sculpts relief instead of washing it flat; the day peak (dayF→1) is
    // unchanged at ~0.40 grey. Do NOT raise the night floor to "brighten" night
    // — that flattens the moon back out (the owner's "равномерно"). Brighten via
    // kMoonDirGain, which stays directional.
    p.ambientColor = {
        0.07f + dayF * 0.33f,
        0.08f + dayF * 0.32f,
        0.15f + dayF * 0.25f,
    };
    return p;
}

inline LightParameters compute_sun(const WorldTime& t) {
    const float tod = (float(t.hour) + float(t.minute) / 60.0f) / 24.0f;
    return compute_light_parameters(tod);
}

} // namespace sm::sub
