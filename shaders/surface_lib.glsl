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

// Standard deviation of wnoise, inverted: multiplying (sample - 0.5) by this
// turns a noise sample into a unit-variance z-score. 1/0.2143.
//
// There used to be a second one here for grain(), and a mottle() beside them —
// the mean-preserving lognormal exp(sigma*z - sigma^2/2) that every surface
// multiplied its colour by. Both are gone with the model they served. A
// multiply can land a surface on ANY colour, including ones no material
// authored, and that is what the subworld ground's "dirt" turned out to be
// (owner, 2026-09-14/15). Colour now comes from mixing between authored
// constituents, which is bounded by construction; see mesh.frag ground_worn.
// Nothing here multiplies a colour any more, and nothing should.
const float kNormNoise = 4.665;

// THE anti-aliasing rule for a band's SLOPE, and the reason a 4 cm grain does
// not shimmer at thirty metres. `px` is the world-space size of one pixel's
// footprint and `freq` the feature frequency in the same units: once a cycle
// no longer spans a pixel it is faded out rather than point-sampled. This
// replaces mipmaps for a synthesised surface — and the frequency it kills is
// exactly the one that would otherwise crawl when the camera moves.
//
// Reaching exactly ZERO is the point here, and it is also CORRECT: averaging a
// stationary field's slope over a footprint wider than its own wavelength
// gives zero, because every rise is matched by a fall inside the same pixel.
// Relief that the screen cannot resolve does not shade — it is not merely
// faint, it is absent. (It is also what keeps the callers' early-outs live:
// the relief taps are the most expensive thing on the ground path and a band
// at weight 0 is skipped outright.)
float resolved(float px, float freq) {
    return clamp(1.0 - px * freq * 2.2, 0.0, 1.0);
}

// A cycle needs about this many pixels to read as a cycle rather than as
// dither. It is the screen's carrying limit, not a taste knob: below four
// samples per period the interpolated lattice stops being a shape on screen
// and starts being noise that crawls when the camera moves.
const float kResolveCycles = 4.0;

// The floor a band's weight is soft-thresholded against. A band under it
// cannot move a channel by one 8-bit step (its weight times a material's
// sigma, ~0.2, times a mid albedo lands below 1/255), so it is dropped — and
// subtracting the floor rather than comparing against it keeps the fade
// continuous all the way down to the exact zero the early-outs want.
const float kResolveFloor = 0.02;

// THE same question asked of a band's VALUE — and it has a different answer,
// which is why this is a second function and not a second caller of the one
// above. A pixel whose footprint covers N of a field's cells shows their
// AVERAGE, and the average of N independent samples keeps sigma/sqrt(N) of the
// variation, not none of it: N = (px*freq)^2, so the contrast falls as
// 1/(px*freq) and never reaches nothing. Far ground is genuinely less blotchy
// than near ground; it is not genuinely FLAT, and a band that snaps to flat is
// the "low-quality LOD" the eye names on sight.
//
// So: full weight while the screen can carry the cycle, then the standard-error
// tail. The two meet in one expression with no branch and no second constant.
float averaged(float px, float freq) {
    float n = px * freq * kResolveCycles;
    float w = inversesqrt(1.0 + n * n);
    return max(w - kResolveFloor, 0.0) / (1.0 - kResolveFloor);
}

#endif // TIMAERT_SURFACE_LIB
