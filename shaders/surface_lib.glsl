// surface_lib.glsl — THE procedural surface primitives. Every shader that
// synthesises a surface (mesh.frag's ground today; anything that grows one
// tomorrow) includes this file and gets the SAME noise, the same grain, the
// same mean-preserving variation, the same anti-aliasing rule. There is no
// second copy anywhere in the tree, and that is the point: three copies of a
// hash is how two surfaces that are meant to match drift apart.
//
// Ported from the reference project's shaders/surface_lib.glsl, whose header
// records what each primitive cost to learn; the tiling law below is this
// world's own addition and is explained at kSynthPeriod.
#ifndef TIMAERT_SURFACE_LIB
#define TIMAERT_SURFACE_LIB

// THE SYNTH PERIOD. Two hard constraints meet here, and one number satisfies
// both:
//   • FLOAT32. A subworld window sits anywhere in a map a million metres
//     across. At 1e6 a float32 has 6 cm of resolution left, so a 4 cm grain
//     multiplied into absolute world coordinates quantises into visible
//     rectangular blocks — which is exactly what the near ground showed.
//   • THE SEAM. The 3×3 window recentres by exactly one macro cell, 1024 m
//     (sub/map_data.h kCellSize × 1 m per tile), and the synth must be
//     invariant under that shift or the whole surface re-mottles the instant
//     the player crosses a cell boundary.
// A field that TILES at 1024 m answers both at once: every coordinate stays
// inside the window's own ±1536 m (full precision) and a 1024 m shift is the
// identity (no seam pop). So every octave snaps its frequency to a whole
// number of cycles per period and wraps its lattice there. It is also the
// LARGEST period that can do this: a bigger tile would not survive a
// one-cell shift.
const float kSynthPeriod = 1024.0;

// Deliberately NOT a sin-hash. fract(sin(dot(p,k))*43758.5) — what the ground
// synth used to run on — has known precision artefacts on part of the driver
// population: sin() at large arguments loses its low bits and the hash
// degenerates into diagonal moire. This integer-ish bit-mixer has no such
// dependency on transcendental precision.
float hash21(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// A frequency (cycles per metre) snapped to a whole number of cycles per
// synth period — the one operation that makes a field tileable. Never zero:
// a wavelength longer than the period would have no cycles to snap to.
float wfreq(float freq) {
    return max(round(freq * kSynthPeriod), 1.0) / kSynthPeriod;
}

// World metres → lattice units at a tileable frequency. The lattice count per
// period is exactly wfreq(freq)*kSynthPeriod, a whole number.
vec2 wcoord(vec2 q, float freq) { return q * wfreq(freq); }

// The hash of a lattice cell, wrapped into the period so the field repeats
// instead of running away into coordinates the mixer cannot resolve.
float wcell(vec2 cell, float freq) {
    float per = wfreq(freq) * kSynthPeriod;
    return hash21(mod(cell, vec2(per)));
}

// Value noise at a tileable frequency PER AXIS: THE noise octave, and the only
// door to one. Output is in [0,1] with mean 0.5 and standard deviation ~0.2143
// — the constant kNormNoise below inverts that, so callers can speak in
// z-scores instead of magic amplitudes.
//
// Two frequencies rather than one because a surface is often DIRECTIONAL —
// wind ripples, water combing, streaks — and stretching the lattice is how a
// field gets a grain direction without a rotation. (A rotation would break the
// tiling: the period is axis-aligned. Stretching does not.)
float wnoise2(vec2 q, vec2 freq) {
    vec2 fr = max(round(freq * kSynthPeriod), vec2(1.0)) / kSynthPeriod;
    vec2 per = fr * kSynthPeriod;
    vec2 p = q * fr;
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(mod(i, per));
    float b = hash21(mod(i + vec2(1.0, 0.0), per));
    float c = hash21(mod(i + vec2(0.0, 1.0), per));
    float d = hash21(mod(i + vec2(1.0, 1.0), per));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// The isotropic case, which is most of them.
float wnoise(vec2 q, float freq) { return wnoise2(q, vec2(freq)); }

// RIDGED noise: sharp crest, broad trough. The asymmetry wind and water build
// into a surface — a sine has neither, and a sine is also exactly periodic,
// which the eye names on sight. Same [0,1] range, mean ~0.5.
float wridge(vec2 q, vec2 freq) {
    return 1.0 - abs(2.0 * wnoise2(q, freq) - 1.0);
}

// Two-octave fine grain at `freq` cycles per metre. The 0.62/0.38 split and
// the 3.73 octave ratio are the reference's canon (its grain() is
// vnoise(uv*26)*0.62 + vnoise(uv*97)*0.38 — 97/26 = 3.73); carried over
// verbatim so a surface ported between the two projects looks the same.
float grain(vec2 q, float freq) {
    return wnoise(q, freq) * 0.62 + wnoise(q, freq * 3.73) * 0.38;
}

// Standard deviations of the two primitives above, inverted: multiplying
// (sample - 0.5) by these turns a noise sample into a unit-variance z-score.
// wnoise: 1/0.2143. grain: 1/(0.2143*sqrt(0.62^2+0.38^2)) = 1/0.1559.
const float kNormNoise = 4.665;
const float kNormGrain = 6.413;

// MEAN-PRESERVING variation: exp(sigma*z - sigma^2/2) has expectation exactly
// 1 for a unit-variance z, so texture can be added at any strength without
// shifting the surface's average brightness — the lit result stays the colour
// the material table says it is. Its coefficient of variation is
// sqrt(exp(sigma^2)-1), which is how tools/gen_ground_table.py calibrates
// sigma from a measured (or authored) CV instead of tuning it by eye.
float mottle(float sigma, float z) {
    return exp(sigma * z - 0.5 * sigma * sigma);
}

// THE anti-aliasing rule, and the reason a 4 cm grain does not shimmer at
// thirty metres. `px` is the world-space size of one pixel's footprint and
// `freq` the feature frequency in the same units: once a cycle no longer
// spans a pixel it is faded out rather than point-sampled. This replaces
// mipmaps for a synthesised surface — and the frequency it kills is exactly
// the one that would otherwise crawl when the camera moves.
float resolved(float px, float freq) {
    return clamp(1.0 - px * freq * 2.2, 0.0, 1.0);
}

#endif // TIMAERT_SURFACE_LIB
