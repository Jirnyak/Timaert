// Subworld lighting — the single directional light (sun by day, moon by night),
// ambient fill, and point lights. TS-faithful port of subworld/lighting.ts.
//
// Pure graphics helper: computes light parameters from time-of-day.
// No game state dependencies; consumed by the Vulkan renderer_3d.
//
// `sunDir` points FROM the origin TOWARD the sun (macro/celestial.h sun_dir);
// `sunDir.y` is the sun's elevation. The renderer uploads it AS-IS and the
// shaders use it directly as L in N·L (see mesh.frag) — nothing negates it, so
// do NOT re-invert. At night the sun's radiance folds to zero and the same
// slot is re-pointed at whichever MOON dominates the sky (celestial.h
// night_light — illumination × horizon fade over the procedural orbits), so
// `sunDir`/`sunColor` always describe whichever celestial body is currently
// lighting the world. On an all-new-moon night the directional term honestly
// goes to zero and only the ambient floor below remains (owner-approved
// context: dark new-moon nights are a feature).
#pragma once
#include "core/math.h"
#include "macro/celestial.h"
#include "macro/state.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

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
// boundary so the array starts std430-aligned), the sky context lane, then the
// fixed budget of lights. Created once at full size; each frame writes the
// header + the first `count` entries. `count == 0` ⇒ every lit pass falls
// straight through to the directional-only result.
//
// skyParams carries the cloud-shadow context (x = time s, yz = wind in cloud
// field units/s, w = cloudiness01) INTO every lit pass through the one set-0
// descriptor they all already bind — cloud shadows cost zero new descriptors.
// The values are SkyContext's (sub/sky.h), so the drawn sky and the shadows
// under it share one wind and one cloudiness by construction.
// sunDirW/terrainParams are the terrain-occlusion context (lighting.glsl
// terrain_visibility): the frame's celestial light direction (sun by day, the
// dominant moon by night — the ONE directional slot) and the window's world
// span in metres (x lane; 0 disables the march — the harness's flat toy world
// opts out this way). They ride the light SSBO for the same reason skyParams
// does: this one set-0 buffer is already bound by every lit pass, so mountains
// occluding the sun costs receivers zero new descriptors and zero push lanes.
// lightMvpFar is the FULL-WINDOW object-shadow level's world→light-clip
// matrix (column-major mat4, same layout the push-constant lightMvp travels
// in). The object map is ONE idea at two scales — a crisp ±256 m level around
// the camera and this wide level over the whole window (the owner's "sphere
// subtracted from the base") — and the wide matrix rides the light SSBO for
// the same reason skyParams does: the one set-0 buffer every lit pass already
// binds, zero new push lanes.
struct GpuLightBuffer {
    std::uint32_t count;
    std::uint32_t _pad[3];
    float         skyParams[4];
    float         sunDirW[4];       // xyz = celestial light dir, w unused
    float         terrainParams[4]; // x = window world span (m), yzw unused
    float         lightMvpFar[16];
    GpuLight      lights[kSubworldMaxLights];
};
static_assert(sizeof(GpuLightBuffer)
                  == 16 + 16 + 32 + 64 + 32 * kSubworldMaxLights,
              "GpuLightBuffer must match the std430 SSBO layout");

// Cull a candidate light set down to the SSBO budget, keeping the ones NEAREST
// the camera. The renderer gathers one GpuLight per live LightEmitter with no
// upper bound (a dense city could exceed kSubworldMaxLights once torches / lit
// windows exist); this decides which survive when they overflow.
//
//   * count ≤ budget → no-op: `lights` is left untouched and in gather order, so
//     every scene that fits (all of them today) is byte-identical to the
//     pre-cull renderer. This is the property the unit test pins.
//   * count > budget → keep the `budget` lights with the smallest squared
//     distance from `camPos` (partitioned via nth_element, O(n); the tail order
//     is irrelevant), then shrink to `budget`. The player's own light rides the
//     camera at ≈0 distance, so it is always in the near set and never dropped
//     for a distant torch. The shader sum is order-independent ⇒ partitioning
//     the survivors changes nothing visible.
//
// Pure and Vulkan-free so it is unit-testable on its own (see
// point_light_cull_test); the renderer copies the surviving prefix into the
// mapped SSBO. `budget` is a parameter (not the constant) purely so the test can
// exercise the overflow path at a small size.
inline void cull_nearest_lights(std::vector<GpuLight>& lights,
                                const vec3& camPos,
                                std::size_t budget) {
    if (lights.size() <= budget) return;
    std::nth_element(
        lights.begin(), lights.begin() + budget, lights.end(),
        [&camPos](const GpuLight& a, const GpuLight& b) {
            const float ax = a.pos[0] - camPos.x, ay = a.pos[1] - camPos.y,
                        az = a.pos[2] - camPos.z;
            const float bx = b.pos[0] - camPos.x, by = b.pos[1] - camPos.y,
                        bz = b.pos[2] - camPos.z;
            return (ax * ax + ay * ay + az * az)
                 < (bx * bx + by * by + bz * bz);
        });
    lights.resize(budget);
}

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

inline LightParameters compute_light_parameters(int day, float tod) {
    using detail::smoothstep01;
    using detail::clamp01;
    LightParameters p;
    // THE sun arc — macro/celestial.h owns the formula; sky.frag consumes the
    // same vector via its push constants, so "one celestial direction" is a
    // mechanism, not a comment kept in lockstep by hope.
    const SkyDir s = sun_dir(tod);
    p.sunDir   = { s.x, s.y, s.z };
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
    // The dominant moon rides the SAME directional slot as the sun. WHICH moon
    // that is — and how strong — is macro/celestial.h's night_light: the
    // procedural orbits put a full moon opposite the sun (up exactly when the
    // sun is down) and a new moon beside it (down with it, and unlit anyway),
    // so sun and moon are never both bright and the handover flip below always
    // happens while BOTH terms are ≈0 — no visible pop. We SUM the two
    // radiances (one is always ≈0) and keep the whole lit pipeline on one
    // sunDir/sunColor: terrain, billboards and the water's moon-path all
    // follow the same real moon with no per-object special-casing. strength01
    // already folds illumination × horizon fade, so a crescent lights weakly
    // and an all-new-moon night contributes nothing.
    const NightLight nl = night_light(day, tod);
    const float moonIntensity = nl.strength01 * kMoonDirGain;
    if (moonIntensity > p.sunIntensity) {
        p.sunDir = { nl.dir.x, nl.dir.y, nl.dir.z };
    }
    // The moon's authored tint, cooled: night light should read blue-white
    // even under the pale moon's near-white disc (scene tone, not disc tone).
    p.sunColor.x += nl.rgb[0] * 0.65f * moonIntensity;
    p.sunColor.y += nl.rgb[1] * 0.75f * moonIntensity;
    p.sunColor.z += nl.rgb[2] * 1.00f * moonIntensity;
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
    const float tod = (float(t.hour()) + float(t.minute()) / 60.0f) / 24.0f;
    return compute_light_parameters(t.day(), tod);
}

} // namespace sm::sub
