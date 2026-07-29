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

// base    — unlit surface albedo (procedural ground, sprite texel, wall colour).
// ambient — non-directional fill (day sky → night moonlight); applied unshadowed.
// sunColor— direct sun radiance with day-intensity already folded in (0 at night).
// sunTerm — surface directional response: quantised N·L for meshes/structures,
//           a flat constant for billboards (no meaningful per-pixel normal).
// shadow  — PCF shadow-map visibility of the sun (1 = fully lit, 0 = occluded).
vec3 lit_surface(vec3 base, vec3 ambient, vec3 sunColor, float sunTerm, float shadow) {
    return base * (ambient + sunColor * sunTerm * shadow);
}

#endif
