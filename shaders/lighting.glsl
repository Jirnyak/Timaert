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

// Defined below the light SSBO they read; prototyped here so lit_surface —
// this file's headline — can stay at the top.
float cloud_sun_visibility(vec2 worldXZ);
float terrain_visibility(vec3 worldPos);
uint  light_debug_bits(); // `lightdbg` bisect mask (terrainParams.y, 0 = off)

// base    — unlit surface albedo (procedural ground, sprite texel, wall colour).
// ambient — non-directional fill (day sky → night moonlight); applied unshadowed.
// sunColor— direct sun radiance with day-intensity already folded in (0 at night).
// sunTerm — surface directional response: quantised N·L for meshes/structures,
//           a flat constant for billboards (no meaningful per-pixel normal).
// shadow  — PCF shadow-map visibility of the sun (1 = fully lit, 0 = occluded).
// worldPos— fragment world position, feeding the OTHER two visibility members
//           of the one sun-visibility law:
//             terrain_visibility — the window heightfield marched toward the
//               celestial light, so mountains and hills occlude the sun (and
//               the moon at night) analytically at any range;
//             cloud_sun_visibility — the drifting cloud field overhead.
//           The full law: direct light = radiance × response
//               × object-map(shadow) × relief(march) × clouds(field) —
//           three occluder classes, each answered by the data that owns it,
//           multiplied in this ONE place so every lit object obeys at once.
vec3 lit_surface(vec3 base, vec3 ambient, vec3 sunColor, float sunTerm,
                 float shadow, vec3 worldPos) {
    // Diagnostic bisect (console `lightdbg`): terrainParams.y carries a bit
    // mask that force-lifts one member of the visibility product to 1 so the
    // eye can name which term draws a given darkening. 0 in shipping frames.
    uint dbg = light_debug_bits();
    if ((dbg & 4u) != 0u) shadow = 1.0;
    if ((dbg & 8u) != 0u) sunTerm = 1.0;
    // When the direct term is already ~nothing — night, deep dusk, a fully
    // map-shadowed point — skip the relief march and the cloud field: their
    // product could only darken a zero. This is the single gate that makes
    // night frames pay nothing for daytime occlusion.
    float peak = max(sunColor.r, max(sunColor.g, sunColor.b)) * sunTerm * shadow;
    if (peak <= 0.004) return base * ambient;
    return base * (ambient
                   + sunColor * sunTerm * shadow
                     * ((dbg & 1u) != 0u ? 1.0 : terrain_visibility(worldPos))
                     * ((dbg & 2u) != 0u ? 1.0
                                         : cloud_sun_visibility(worldPos.xz)));
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
#define TIMAERT_MAX_POINT_LIGHTS 16

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
    // Terrain-occlusion context (terrain_visibility below): xyz = the frame's
    // celestial light direction (sun by day, dominant moon by night — the ONE
    // directional slot, so mountains occlude moonlight through the same lane);
    // terrainParams.x = window world span in metres (0 disables the march —
    // how a heightfield-less scene like the smoke harness opts out).
    vec4          sunDirW;
    vec4          terrainParams;
    // The full-window object-shadow level's world→light-clip matrix (the
    // crisp near level rides the push constants as it always has).
    mat4          lightMvpFar;
    GpuPointLight lights[];
} u_pointLights;

// The wide-level light-clip position of a world-space point — feed to
// shadowFactorHandoff beside the near clip the push constant produced.
vec4 far_light_clip(vec3 worldPos) {
    return u_pointLights.lightMvpFar * vec4(worldPos, 1.0);
}

uint light_debug_bits() {
    return uint(u_pointLights.terrainParams.y);
}

// The march heightfield in metres: exact window heights in the interior,
// macro-skeleton apron beyond it (terrainParams.z = the world span this
// texture covers; the window span stays in .x for the light field). Uploaded
// by the renderer whenever the loaded window's heights change. Same set-0
// residence as everything above: one binding, every lit pass sees the same
// relief.
layout(set = 0, binding = 2) uniform sampler2D u_heightM;

// world → u_heightM UV through the texture's own span (0 span = no field).
float height_field_span() { return u_pointLights.terrainParams.z; }

// Relief member of the sun-visibility law: march from the surface point
// toward the celestial light across the window heightfield and measure how
// deeply the ray cuts into terrain. Soft penumbra by penetration depth — the
// deeper the ridge overhangs the ray, the darker — which naturally widens
// the soft edge with distance from the caster, like real mountain shadows.
//
// The march starts ~12 m out (past its own height cell — no self-speckle;
// crisp near-field shadows are the object map's member of the law, not ours)
// and grows geometrically across the extended domain — its reach (~3.2 km)
// is one window span, exactly the apron width. Analytic against the
// heightfield ⇒ no zebra, no shimmer, works identically in flight.
float terrain_visibility(vec3 worldPos) {
    float span = height_field_span();
    vec3  L    = u_pointLights.sunDirW.xyz;
    // No heightfield, or the light is at/below the horizon (its direct
    // radiance is ~0 there — see the day/night contract): skip the march.
    if (span <= 0.0 || L.y <= 0.02) return 1.0;

    float maxPen = 0.0;
    float t = 12.0;
    for (int i = 0; i < 16; ++i) {
        vec3 p = worldPos + L * t;
        vec2 uv = p.xz / span + 0.5;
        if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) break;
        float h = texture(u_heightM, uv).r;
        maxPen = max(maxPen, h - p.y);
        // Fully dark already — deeper penetration cannot darken further, so
        // points inside a mountain's shadow stop after a couple of steps.
        if (maxPen >= 6.0) break;
        t *= 1.45;
    }
    // 0..6 m penetration fades light 1→0: a grazing ridge gives a wide soft
    // penumbra, a mountain wall goes fully dark.
    return 1.0 - smoothstep(0.0, 6.0, maxPen);
}

// Sun visibility under the drifting cloud field at a world-space point — the
// ground half of the sky's clouds (same field, same wind; see clouds.glsl).
// Peak dimming is 0.62: a cloud bank reads clearly but never fakes night.
float cloud_sun_visibility(vec2 worldXZ) {
    vec4 sp = u_pointLights.skyParams;
    // ABSOLUTE world coords (window pos + composite origin from the spare
    // sunDirW.w / terrainParams.w lanes): the cloud field must not resample
    // when the 3×3 window recentres at a seam — same anchor rule as
    // mesh.frag's ground detail.
    vec2 absXZ = worldXZ + vec2(u_pointLights.sunDirW.w,
                                u_pointLights.terrainParams.w);
    float cover = cloud_cover(absXZ * TIMAERT_CLOUD_WORLD_SCALE,
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

// THE LIGHT FIELD: a 2D additive light map over the window — every small
// light (a town's torches, windows, lanterns; thousands) splatted on the CPU,
// added here with ONE sample. Vertical honesty comes from the heightfield
// already bound at binding 2: the field stores GROUND-level light, so a
// receiver fades out of it as it rises above the ground (a tower top is not
// lit by the street's fires). terrainParams.x doubles as the enable switch —
// span 0 (the harness) reads a black 1×1 and adds nothing.
layout(set = 0, binding = 4) uniform sampler2D u_lightField;

// Byte 255 in the field == this intensity (src/sub/lighting.h
// kLightFieldScale — keep the two equal).
#define TIMAERT_LIGHT_FIELD_SCALE 4.0

vec3 light_field(vec3 worldPos) {
    float span = u_pointLights.terrainParams.x;
    if (span <= 0.0) return vec3(0.0);
    vec2 uv = worldPos.xz / span + 0.5;
    vec3 pool = texture(u_lightField, uv).rgb * TIMAERT_LIGHT_FIELD_SCALE;
    // Ground height reads u_heightM through the height field's OWN span —
    // the texture is wider than the light-field window (march apron). A
    // heightfield-less writer (harness) gets flat ground, not a divide by 0.
    float hspan = height_field_span();
    float ground = hspan > 0.0
        ? texture(u_heightM, worldPos.xz / hspan + 0.5).r : 0.0;
    float vert = clamp(1.0 - (worldPos.y - ground) / 12.0, 0.0, 1.0);
    return pool * vert;
}

vec3 point_lights(vec3 worldPos, vec3 N) {
    vec3 acc = vec3(0.0);
    uint n = min(u_pointLights.count, uint(TIMAERT_MAX_POINT_LIGHTS));
    for (uint i = 0u; i < n; ++i) {
        vec3  Lp     = u_pointLights.lights[i].posRadius.xyz;
        float radius = u_pointLights.lights[i].posRadius.w;
        vec3  toL    = Lp - worldPos;
        // Out-of-radius fragments pay one multiply-compare, not sqrt + the
        // shading math — with a full 32-light town budget this is what most
        // fragments do for most lights.
        float d2 = dot(toL, toL);
        if (d2 >= radius * radius) continue;
        vec3  Lcol   = u_pointLights.lights[i].colorRgbInt.rgb;
        float gain   = u_pointLights.lights[i].colorRgbInt.w;
        float dist   = sqrt(d2);
        float atten  = point_light_atten(dist, radius);
        float ndl = max(dot(N, toL / max(dist, 1e-3)), 0.0);
        acc += Lcol * (gain * atten * ndl);
    }
    // The thousands that are not in the loop: one field sample.
    return acc + light_field(worldPos);
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
        vec3  toL    = Lp - worldPos;
        float d2 = dot(toL, toL);
        if (d2 >= radius * radius) continue; // same skip as point_lights()
        vec3  Lcol   = u_pointLights.lights[i].colorRgbInt.rgb;
        float gain   = u_pointLights.lights[i].colorRgbInt.w;
        acc += Lcol * (gain * point_light_atten(sqrt(d2), radius));
    }
    // The thousands that are not in the loop: one field sample.
    return acc + light_field(worldPos);
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
        vec3  toL    = Lp - worldPos;
        float d2 = dot(toL, toL);
        if (d2 >= radius * radius) continue; // same skip as point_lights()
        vec3  Lcol   = u_pointLights.lights[i].colorRgbInt.rgb;
        float gain   = u_pointLights.lights[i].colorRgbInt.w;
        float dist   = sqrt(d2);
        vec3  H      = normalize(toL / max(dist, 1e-3) + V);
        float nh     = max(dot(N, H), 0.0);
        float glint  = pow(nh, 90.0) * 1.2 + pow(nh, 14.0) * 0.22;
        acc += Lcol * (gain * point_light_atten(dist, radius) * glint);
    }
    return acc;
}

#endif
