// Universal subworld surface lighting — the ONE definition of how every object
// (terrain, structures, tree / NPC / creature billboards, and any future lit
// sprite or mesh) responds to the sun and ambient. Every lit fragment stage
// includes this file and calls lit_surface(), so the day/night response lives
// in exactly one place and cannot drift or be forgotten per-shader.
//
// DAY/NIGHT CONTRACT — read before touching lighting:
//   sunColor already carries the day-intensity. It is scaled by the sun's
//   elevation smoothstep in compute_light_parameters (src/sub/lighting.h) so it
//   reaches (0,0,0) once the sun drops below the horizon. Therefore the DIRECT
//   sun term (sunColor * sunTerm * shadow) vanishes at night on its own, leaving
//   only `ambient` — the cool moonlight fill, applied UNSHADOWED. That is what
//   keeps night moonlit-but-dark for EVERY object uniformly.
//
//   The bug this system prevents: a billboard shader hand-rolled the combine
//   with a flat sun term and no N·L, and a wall shader used N·L that a vertical
//   face still catches from the below-horizon sun's horizontal component — so
//   both "glowed" at night while flat terrain escaped only because its upward
//   N·L is <= 0 against a sun that is down. Folding intensity into sunColor at
//   the source + this single combine fixes all object classes at once. Do NOT
//   re-scale sunColor here, and do NOT add a per-shader ambient floor: that
//   would re-introduce the glow.
#ifndef TIMAERT_LIGHTING_GLSL
#define TIMAERT_LIGHTING_GLSL

#include "clouds.glsl"

// Defined below the light SSBO it reads; prototyped here so lit_surface —
// this file's headline — can stay at the top.
float cloud_sun_visibility(vec2 worldXZ);

// base    — unlit surface albedo (procedural ground, sprite texel, wall colour).
// ambient — non-directional fill (day sky → night moonlight); applied unshadowed.
// sunColor— direct sun radiance with day-intensity already folded in (0 at night).
// sunTerm — surface directional response: quantised N·L for meshes/structures,
//           a flat constant for billboards (no meaningful per-pixel normal).
// shadow  — PCF shadow-map visibility of the sun (1 = fully lit, 0 = occluded).
// worldPos— fragment world position: the direct term is additionally dimmed by
//           the drifting CLOUD field overhead (cloud_sun_visibility below) —
//           inside lit_surface so every lit object darkens under the same
//           cloud at once, exactly like the day/night contract above.
vec3 lit_surface(vec3 base, vec3 ambient, vec3 sunColor, float sunTerm,
                 float shadow, vec3 worldPos) {
    return base * (ambient
                   + sunColor * sunTerm * shadow
                     * cloud_sun_visibility(worldPos.xz));
}

// ---------------------------------------------------------------------------
// POSITIONAL (point) lights — the universal second half of subworld lighting.
//
// A single fragment-visible storage buffer at set 0 / binding 1 holds every
// active point light for the frame (torches, lit windows, spell / projectile
// glows, the player's own light). It lives on the SAME shared descriptor set as
// the shadow map, so ONE descriptor lights all five lit passes — mirroring how
// the directional sun/moon was unified into one lit_surface(). The C++ side
// (src/sub/lighting.h GpuLightBuffer) owns the matching std430 layout:
//   count : u32, then 12 bytes pad → 16-byte header,
//   lights[]: vec4 posRadius (xyz = world pos, w = radius metres),
//             vec4 colorRgbInt (rgb = linear colour, w = intensity gain).
// The array is runtime-sized here and fixed (kSubworldMaxLights) on the C++
// side; the loop is clamped so a corrupt count can never run away.
//
// Contract: this term is ADDITIVE on top of lit_surface() and is unshadowed by
// the sun map (a torch is not occluded by the sun's shadow). When the buffer's
// count is 0 — the state until an emitter is gathered (Inc 3+) — point_lights()
// returns (0,0,0), so wiring it in is provably inert until real lights exist.
#define TIMAERT_MAX_POINT_LIGHTS 32

struct GpuPointLight {
    vec4 posRadius;   // xyz world position, w radius (metres)
    vec4 colorRgbInt; // rgb colour, w intensity gain
};

layout(std430, set = 0, binding = 1) readonly buffer TimaertLights {
    uint          count;
    uint          _pad0;
    uint          _pad1;
    uint          _pad2;
    // Sky context for the cloud-shadow term (clouds.glsl): x = time (s),
    // yz = wind (field units/s), w = cloudiness01. Rides the light buffer
    // because this ONE set is already bound by every lit pass — cloud shadows
    // cost zero new descriptors. Written per frame beside the lights
    // (src/sub/lighting.h GpuLightBuffer.skyParams).
    vec4          skyParams;
    // FAR shadow cascade world→light-clip matrix. Rides here for the same
    // reason as skyParams: the one set-0 descriptor every lit pass binds —
    // the second cascade costs receivers zero new push lanes.
    mat4          lightMvpFar;
    GpuPointLight lights[];
} u_pointLights;

// The far-cascade light-clip position of a world-space point — feed to
// shadowFactorCascaded beside the near clip the push constant produced.
vec4 far_light_clip(vec3 worldPos) {
    return u_pointLights.lightMvpFar * vec4(worldPos, 1.0);
}

// Sun visibility under the drifting cloud field at a world-space point — the
// ground half of the sky's clouds (same field, same wind; see clouds.glsl).
// Peak dimming is 0.62: a cloud bank reads clearly but never fakes night.
float cloud_sun_visibility(vec2 worldXZ) {
    vec4 sp = u_pointLights.skyParams;
    float cover = cloud_cover(worldXZ * TIMAERT_CLOUD_WORLD_SCALE,
                              sp.x, sp.yz, sp.w);
    return 1.0 - 0.62 * cover;
}

// Sum the diffuse contribution of every active point light at a world-space
// surface point `worldPos` with (already-normalised) normal `N`. Smooth
// radius-bounded falloff so a light fades cleanly to nothing at its radius edge
// — no hard cutoff seam, no negative light. Pixel-art friendly: a mild
// quadratic edge softening keeps pools of light readable rather than physically
// exact. Callers without a meaningful per-pixel normal (sprite billboards) can
// pass a constant facing normal to get a flat, unit-lit glow instead.
// Shared radius-bounded falloff so the surface form (point_lights) and the flat
// billboard form (point_lights_flat) fade IDENTICALLY — a torch pool reaches the
// same distance whether it lands on the ground or on a creature standing in it.
float point_light_atten(float dist, float radius) {
    float a = clamp(1.0 - dist / max(radius, 1e-3), 0.0, 1.0);
    return a * a;                                         // soft quadratic edge
}

vec3 point_lights(vec3 worldPos, vec3 N) {
    vec3 acc = vec3(0.0);
    uint n = min(u_pointLights.count, uint(TIMAERT_MAX_POINT_LIGHTS));
    for (uint i = 0u; i < n; ++i) {
        vec3  Lp     = u_pointLights.lights[i].posRadius.xyz;
        float radius = u_pointLights.lights[i].posRadius.w;
        vec3  Lcol   = u_pointLights.lights[i].colorRgbInt.rgb;
        float gain   = u_pointLights.lights[i].colorRgbInt.w;
        vec3  toL    = Lp - worldPos;
        float dist   = length(toL);
        float atten  = point_light_atten(dist, radius);
        float ndl = max(dot(N, toL / max(dist, 1e-3)), 0.0);
        acc += Lcol * (gain * atten * ndl);
    }
    return acc;
}

// Billboard (sprite) form of the point-light sum. A camera-facing card has no
// meaningful per-pixel normal, so applying the surface N·L term above would be
// wrong: a torch at chest height gives N·L≈0 and would leave the sprite dark
// while lighting the ground beneath it — visually incoherent. Instead we drop
// N·L and use distance attenuation ALONE (the sprite analogue of the flat
// sunTerm lit_surface() already uses for billboards), so an actor standing in a
// pool of light glows with it as its ground does. Same attenuation curve, same
// buffer, same inert-when-count==0 guarantee as point_lights().
vec3 point_lights_flat(vec3 worldPos) {
    vec3 acc = vec3(0.0);
    uint n = min(u_pointLights.count, uint(TIMAERT_MAX_POINT_LIGHTS));
    for (uint i = 0u; i < n; ++i) {
        vec3  Lp     = u_pointLights.lights[i].posRadius.xyz;
        float radius = u_pointLights.lights[i].posRadius.w;
        vec3  Lcol   = u_pointLights.lights[i].colorRgbInt.rgb;
        float gain   = u_pointLights.lights[i].colorRgbInt.w;
        float dist   = length(Lp - worldPos);
        acc += Lcol * (gain * point_light_atten(dist, radius));
    }
    return acc;
}

// Specular (mirror-glint) form of the point-light sum, for shiny surfaces — the
// water waves today, and any future wet / polished / metal surface. Where
// point_lights() answers with a diffuse wash and point_lights_flat() with a flat
// sprite glow, a specular surface answers with a half-vector highlight: a small
// coloured reflection of the light SOURCE that rides the surface normal, so on
// animated water it shimmers as a moving reflection of a torch or spell rather
// than a static tint. Tight bright core + a soft wider halo so the reflection
// reads as a shimmering pool, not a single hard pixel. Same buffer, same
// radius-bounded attenuation, and the same additive inert-when-count==0 contract
// as the other two forms — a light reaches exactly as far on the water as its
// diffuse pool does on the ground beside it. V is the (normalised) surface→eye
// direction; N the (per-fragment, wave-perturbed) surface normal.
vec3 point_lights_spec(vec3 worldPos, vec3 N, vec3 V) {
    vec3 acc = vec3(0.0);
    uint n = min(u_pointLights.count, uint(TIMAERT_MAX_POINT_LIGHTS));
    for (uint i = 0u; i < n; ++i) {
        vec3  Lp     = u_pointLights.lights[i].posRadius.xyz;
        float radius = u_pointLights.lights[i].posRadius.w;
        vec3  Lcol   = u_pointLights.lights[i].colorRgbInt.rgb;
        float gain   = u_pointLights.lights[i].colorRgbInt.w;
        vec3  toL    = Lp - worldPos;
        float dist   = length(toL);
        vec3  H      = normalize(toL / max(dist, 1e-3) + V);
        float nh     = max(dot(N, H), 0.0);
        float glint  = pow(nh, 90.0) * 1.2 + pow(nh, 14.0) * 0.22;
        acc += Lcol * (gain * point_light_atten(dist, radius) * glint);
    }
    return acc;
}

#endif
