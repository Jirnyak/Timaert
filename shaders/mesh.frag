#version 450
#extension GL_GOOGLE_include_directive : require
// Subworld 3D terrain mesh fragment stage. Procedural per-biome ground synth
// (no atlas) lit by a smooth NdotL sun + ambient, with a PCF shadow-map lookup
// so cast shadows (terrain + trees) land on the surface. The 4-band quantise
// that built objects wear is deliberately NOT here — see the light block in
// main() for the law that decides where posterisation belongs.
//
// ONE LADDER, and the reason the ground reads as a surface at every range.
// A ground's colour is a single field with a CONTINUOUS spectrum: octave i at
// the row's meso frequency times kLadderLacunarity^i carrying amplitude
// kLadderGain^i, from the terrain patchwork (~20–40 m) down to the grain
// (3–7 cm) — six or seven rungs, spelled out per material in kGroundLadder
// (ground_surface.glsl, derived from the CSV, never authored).
//
// It replaces three NAMED bands — a 28 m patchwork, the family's ~1 m
// structure, a 4 cm grain — which had nothing between them. On the meadow row
// that was content at 28.6, 7.1, 0.80, 0.30, 0.040 and 0.011 m and two holes
// almost a decade wide, and it failed in a way the owner named on sight
// (2026-09-14, the city screenshot): past the range where the pixel footprint
// had eaten the family band — about 300 m — the ONLY thing left was the 28 m
// patchwork, at full contrast, because that band alone was exempt from the
// footprint law. One spatial frequency by itself does not read as ground; it
// reads as blotches on paint. Nature shows a continuum, and an octave that
// fades must always have a coarser neighbour to fade INTO.
//
// The two halves answer two questions and carry two spreads from the CSV:
//   i <  0  the TERRAIN's patchwork (macro_sd) — how patchy this ground looks
//           from a hillside. Planar, never triplanar: directionless mottle has
//           no direction to smear down a cliff.
//   i >= 0  the SURFACE's own roughness (sd), whose rung 0 is not sampled here
//           at all — the family SHAPE stands in it. That shape is the one term
//           with a direction (ripples run, furrows run, plates tile), so it is
//           the one term projected onto a cliff, and the one term read TWICE:
//           once into the mix and once as a HEIGHT. That is why a hollow here
//           is both indented and fresher — which is what a hollow is, since it
//           is where moisture and growth collect — and why the sun, the relief
//           march and the shadow map already in the frame shade it for free,
//           with no new pass, texture or descriptor.
//
// WHAT THE LADDER DRIVES, which is the other half of the story and the half
// that was wrong for longer. The two halves ADD into ONE fraction in 0..1 —
// how WORN this spot is — and the colour is read off the segment between the
// two constituents the row authored (ground_worn below). The ladder never
// touches brightness. The model before it multiplied a single colour by a
// lognormal and drifted its hue along an authored vector, and so could land on
// any colour at all: the olives and teals nobody chose were what "dirty
// ground" meant (owner, 2026-09-14/15). A bounded fraction means a bounded
// palette, and that bound is a guarantee no amount of retuning could buy.
//
// Each rung is weighted by averaged() — what a pixel of this footprint keeps
// of a band's VALUE, which is the standard error of a mean and so never quite
// reaches zero. The relief keeps resolved(), which does reach zero, because
// the average of a stationary field's SLOPE genuinely is nothing: every rise
// inside the pixel is matched by a fall. Two laws for two quantities; both
// derivations live in shaders/surface_lib.glsl.
//
// Which band is doing what to a frame is a question for the eye, not for
// arithmetic: the console's `grounddbg` lifts any one of them out (the same
// bisect `lightdbg` gives the sun-visibility product).
//
// Every NUMBER lives in shaders/ground_surface.glsl, generated from
// data/ground_materials.csv + data/ground_cover.csv (tools/gen_ground_table.py,
// guarded by the `ground_table_test` ctest). This shader holds SHAPES only, so
// tuning the look is an edit to a CSV cell — never a shader rewrite.
//
// The material id is sampled PER-FRAGMENT from a full-resolution tile texture
// (u_material) rather than interpolated from mesh vertices. This is what keeps
// thin features — roads, field bands, shorelines — crisp and connected instead
// of dissolving into blobs between the coarse terrain vertices (mirrors the TS
// renderer's per-fragment u_tileGrid lookup).
layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;
layout(set = 0, binding = 3) uniform sampler2DShadow u_shadowFar;
layout(set = 1, binding = 0) uniform sampler2D u_material; // R8 tile material id / 255
// The stain canvas (Inc C, particles-unified-matter): persistent GPU-only
// surface marks — blood splats, death pools — mixed into the albedo BEFORE
// lit_surface() so a stain is shadowed/moonlit exactly like the ground it
// lies on. Toroidal: world tile mod 1024 at 8 px/tile; the REPEAT sampler
// does the wrap, the push's validity ring masks the far side's stale texels.
layout(set = 1, binding = 1) uniform sampler2D u_stain;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in float vHeight;
layout(location = 2) in vec3 vWorld;
layout(location = 3) in vec2 vUv;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    // xyz = camera world position — the view vector the cover layer needs to
    // look INTO the grass, and the centre of the stain canvas, which is the
    // same point and now has ONE home. w = the canvas's valid radius in
    // metres; <= 0 means there is no canvas (the smoke harness).
    vec4 camPos;
} pc;

layout(location = 0) out vec4 outColor;

#include "shadow_common.glsl"
#include "lighting.glsl"
#include "surface_lib.glsl"
#include "ground_surface.glsl"

// ── THE BISECT ─────────────────────────────────────────────────────────────
// Console `grounddbg` (src/app/main.cpp), the surface's answer to `lightdbg`:
// a bit mask riding the light SSBO that lifts ONE term of the ground out of
// the frame, so which band draws a given look is a question the eye answers
// instead of arithmetic. 0 in shipping frames — the whole thing compiles to
// nothing when the mask is zero because every gate is a uniform branch.
const uint kGdbgTerrain = 1u;  // the ladder below the meso frequency
const uint kGdbgShape   = 2u;  // the family's own shape — rung 0
const uint kGdbgSurface = 4u;  // the ladder above it
const uint kGdbgCover   = 8u;  // grass, snow, moss
const uint kGdbgRelief  = 16u; // the normal the shape tilts

// The amplitude under which a ladder rung is dropped outright. A rung's reach
// is its own amplitude times what the footprint leaves of it, and at 2 % of
// the family shape's the product with a material's sigma (~0.2) cannot move a
// channel by one 8-bit step. Since both factors fall monotonically with the
// rung index, the first rung under this floor is also the last — which is what
// lets the loop leave early and what makes a seven-rung ladder cost about what
// three disconnected bands did.
const float kLadderFloor = 0.02;

// The family shapes below are authored to live in about [-1,1] with a
// standard deviation near 1/3, so ONE constant turns any of them into a
// z-score for the ladder — and the same value read as METRES needs no second
// table. (A shape whose peak is 1 and whose sigma is 1/3 is the same
// convention the reference project's families use.)
const float kNormShape = 3.0;

// A blade of grass in a breeze sways with a period near two thirds of a
// second. Quoted as a frequency because that is what the sine wants.
const float kCoverSwayHz = 1.6;

// How fast the JOINT between two materials wanders: one bend every ~3 m, with
// a ragged octave four times finer on top. That is the scale a real margin
// has — a shoreline bends at the stride, and frays at the hand.
const float kJointFreq = 0.35;

// Cap on any normal tilt, ground or cover: tan(35 deg). A finite difference
// across a step discontinuity (the crack in a stone plate) has an unbounded
// slope, and an uncapped tilt flips the shading normal — one pixel then
// catches a full sun term on ground that faces away from it.
const float kMaxTilt = 0.70;

// ── THE FAMILIES ────────────────────────────────────────────────────────────
// A family is a SHAPE, not a material: several grounds share one and differ
// only by the numbers in their CSV row. `f` is the row's meso frequency in
// cycles per metre, `q` is ABSOLUTE world coordinates in metres.
float ground_meso(uint fam, vec2 q, float f) {
    if (fam == kGfTurf) {
        // Tussocks: a clump octave and its child. Grass ground is lumpy at
        // the scale of the plants that build it.
        float a = wnoise(q, f);
        float b = wnoise(q + 11.3, f * 2.7);
        return (a * 0.66 + b * 0.34) * 2.0 - 1.0;
    }
    if (fam == kGfSand) {
        // Wind ripples. NOT a sine — that was the first version and the owner
        // named it on sight, "волнистый периодический" (2026-09-12): a sine is
        // a perfectly repeating wave, and no amount of phase warping hides the
        // regular train of identical crests. A ripple field is instead:
        //   • STRETCHED — five times longer across the drift than along it, so
        //     the crests run as bands (the lattice is stretched, never rotated:
        //     a rotation would break the tiling, whose period is axis-aligned);
        //   • RIDGED — sharp crest, broad trough, the asymmetry wind builds;
        //   • WANDERING — displaced by a wave three ripples long, so the train
        //     never lines up with itself.
        // The second, finer band is the coarse sand between the crests.
        vec2 qw = q + (wnoise(q, f * 0.12) - 0.5) * (1.1 / f);
        float crest = wridge(qw, vec2(f * 0.14, f));
        float grit  = wnoise2(qw + 31.7, vec2(f * 0.30, f * 2.1));
        return (crest - 0.5) * 1.30 + (grit - 0.5) * 0.50;
    }
    if (fam == kGfFurrow) {
        // Ridge-and-furrow: one ridge pair per wavelength (the strip width a
        // team could plough in a day), with the plough lines eight to the
        // ridge — the two scales a ploughed field actually has. Rows advance
        // along q.y; the caller swaps the axes for the north-south twin.
        float ridge  = cos(q.y * wfreq(f) * 6.2831853);
        float plough = cos(q.y * wfreq(f * 8.0) * 6.2831853);
        return ridge * 0.62 + plough * 0.24 + (wnoise(q, f * 6.0) - 0.5) * 0.50;
    }
    if (fam == kGfStone) {
        // Plates split by cracks: each plate keeps its own tone, the crack is
        // a valley along its edge. One plate per wavelength — WARPED by a
        // wave two plates wide, because an unwarped lattice draws a perfect
        // square grid that runs to the horizon and reads as paving. Bare
        // massif is jointed, not paved. (The warp is itself periodic, so the
        // plates still tile: floor() and fract() shift by whole cells.)
        vec2 c = wcoord(q, f) + wnoise(q, f * 0.5) * 2.2;
        float plate = wcell(floor(c), f) - 0.5;
        vec2 e = abs(fract(c) - 0.5);
        float crack = smoothstep(0.34, 0.5, max(e.x, e.y));
        return plate * (1.0 - crack) - crack * 0.80;
    }
    if (fam == kGfTrack) {
        // Packed dirt: shallow hollows where the feet fall, gravel between.
        float hollow = smoothstep(0.55, 0.85, wnoise(q, f));
        float gravel = wnoise(q, f * 5.0) - 0.5;
        return gravel * 0.90 - hollow * 0.60;
    }
    if (fam == kGfMud) {
        // Wet flats: broad slicks with almost no relief of their own.
        float lo = wnoise(q, f);
        float hi = wnoise(q + 7.0, f * 3.1);
        return ((lo * 0.72 + hi * 0.28) * 2.0 - 1.0) * 0.80;
    }
    // kGfSoil — bare organic ground: two octaves of plain mottle.
    float a = wnoise(q, f);
    float b = wnoise(q + 5.1, f * 3.3);
    return (a * 0.70 + b * 0.30) * 2.0 - 1.0;
}

// THE LADDER, walked once. Octave i sits at `mesoF * kLadderLacunarity^i`
// carrying amplitude `kLadderGain^i` and is weighted by what a pixel of this
// footprint keeps of its VALUE. Rung 0 is SKIPPED — that is the row's own meso
// frequency, and the family shape stands in it (the caller adds it, because it
// is the one rung that needs the cliff projection and the one whose slope the
// relief also wants).
//
// Returns the two halves as z-scores: x = the terrain's patchwork (i < 0),
// y = the surface's roughness (i > 0). Both leave here unit-variance at full
// resolution and SMALLER at range — normalised by the row's constants, never
// by the per-fragment weighted sum, because dividing by what the footprint
// actually left would renormalise the far view straight back to full contrast
// and undo the whole thing.
//
// The loop leaves early on amplitude, not on rung count: see kLadderFloor.
vec2 ground_ladder(vec2 q, vec4 lad, float mesoF, float px) {
    vec2 z = vec2(0.0);
    float amp = pow(kLadderGain, lad.x);
    float f = mesoF * pow(kLadderLacunarity, lad.x);
    for (int i = int(lad.x); i <= int(lad.y); ++i) {
        float a = amp * averaged(px, f);
        if (a <= kLadderFloor) break;
        // The offset only separates rungs that happen to snap to the same
        // lattice count; the frequencies already differ, so this is belt and
        // braces. A CONSTANT shift keeps the field tileable (surface_lib.glsl
        // kSynthPeriod) — anything position-dependent would not.
        if (i != 0)
            z[i < 0 ? 0 : 1] += a * (wnoise(q + float(i) * 37.0, f) - 0.5)
                                    * kNormNoise;
        amp *= kLadderGain;
        f *= kLadderLacunarity;
    }
    return z * lad.zw;
}

// The family SHAPE, projected — rung 0 of the ladder, and the only term in the
// whole field that has a DIRECTION. The ground is a heightfield, so the
// horizontal plane is the right one almost everywhere; on a cliff face it
// would stretch the ripples and furrows into vertical smears, so the dominant
// VERTICAL plane is crossfaded in by the geometric normal. Flat ground pays
// for one evaluation — the second is only taken where the ground tips over.
float ground_shape(uint fam, vec3 p, vec3 aN, float f) {
    float h = ground_meso(fam, p.xz, f);
    float wy = smoothstep(0.55, 0.88, aN.y);
    if (wy >= 0.999) return h;
    vec2 pv = (aN.x > aN.z) ? p.zy : p.xy;
    return mix(ground_meso(fam, pv, f), h, wy);
}

// The height the normal is taken from: the family shape alone, in metres.
// The rest of the ladder is deliberately NOT in it. The rungs BELOW the shape
// are tens of metres across — relief at that scale is the terrain heightfield's
// job, not a per-fragment normal's — and the rungs ABOVE it are finer than the
// footprint long before their depth (a tenth of their own wavelength) could
// shade anything, so a slope measured across them is noise, not relief. They
// speak through the albedo, where their amplitude is calibrated; the shape
// speaks through both.
//
// resolved(), not averaged(), and that is the whole difference between the two
// laws: a band the screen cannot carry contributes NO slope, because inside
// one pixel every rise is matched by a fall. Reaching exactly zero is what
// keeps the early-out below live, and the relief taps are the most expensive
// thing on this path.
float ground_height_m(uint fam, vec3 p, vec3 aN, float f, float reliefM,
                      float px) {
    float d = resolved(px, f);
    if (d <= 0.0) return 0.0;
    return ground_shape(fam, p, aN, f) * d * reliefM;
}

// The ploughed field's north-south twin (id 14) is the SAME family with its
// axes swapped — the orientation the C++ material builder picked per macro
// cell (field_furrows_vertical, sub/material.h) turns the synth, not the
// table. Applied to positions and normals alike, so the gradient taps below
// stay in one space.
vec3 synth_space(vec3 p, uint mid) {
    return (mid == 14u) ? vec3(p.z, p.y, p.x) : p;
}

// ── THE COVER LAYER ────────────────────────────────────────────────────────
// What lies ON the ground — grass today, and snow and moss in the same
// breath. ONE function draws every cover there will ever be: the ground row
// chooses (id, density), the cover row carries colour, strand pitch, height
// and wind response. Ash, dust, fallen leaves, a film of water = a ROW in
// data/ground_cover.csv. There is no per-cover branch in here and there must
// never be one.
//
// Returns the coverage it painted; tints the albedo and tilts the normal
// through their references.
float cover_apply(uint cid, float density, float worn, vec3 Pabs, vec3 N,
                  vec3 V, float px, float time, vec2 wind, bool withTilt,
                  inout vec3 albedo, inout vec3 nrm) {
    if (cid == 0u || density <= 0.001) return 0.0;
    vec4 cp = kCoverParams[min(cid, kCoverCount - 1u)];
    float f  = cp.x;  // strands per metre
    float hM = cp.y;  // how tall the layer stands

    // PARALLAX. The eye does not meet the blades where it meets the ground —
    // it meets them where its ray crosses the TOP of the layer. Shifting the
    // sample by that offset is what makes tall cover lean away from the
    // camera instead of reading as paint on the floor. The clamp keeps a
    // near-horizontal view from shearing the field to infinity.
    vec2 q = Pabs.xz + (V.xz / max(V.y, 0.30)) * hM;

    // WIND. The top of the layer is carried by the world's wind — the same
    // vector the clouds drift on (lighting.glsl skyParams), so the grass and
    // the cloud shadows crossing it move with one weather.
    q += wind * (cp.z * hM * 0.35)
         * sin(time * kCoverSwayHz + Pabs.x * 0.7 + Pabs.z * 0.5);

    float d = resolved(px, f);  // strands dissolve into a flat tint at range

    // COVERAGE: clumps four strand-pitches wide decide WHERE the layer is,
    // the density says how much of that survives. Density 1 covers everything
    // (snow); 0.18 leaves islands (moss in the joints of stone).
    float clump = wnoise(q, f * 0.25);
    float cov = clamp((clump - 1.0 + density * 1.6) * 3.0, 0.0, 1.0);
    if (cov <= 0.001) return 0.0;

    // The strand field, and what it is allowed to do: thin the COVERAGE, not
    // tint the blades. A sparser patch of sward is a patch where more ground
    // shows between the stems — which is what a sward actually does, and what
    // keeps the layer inside the two colours its row authored. (It used to
    // multiply the cover's colour by a lognormal, and multiplying a colour is
    // exactly how a layer invents shades nobody chose.) Mean-preserving, and
    // faded by `d` so distant cover settles to its plain mean coverage
    // instead of sparkling.
    float s0 = grain(q, f);
    cov = clamp(cov * (1.0 + (s0 - 0.5) * 2.0 * cp.w * d), 0.0, 1.0);

    // THE COVER'S OWN MIX, read with the GROUND's wornness — not a field of
    // its own. That is what makes a drier hollow carry paler soil AND paler
    // grass at once: one fact about the place, answered by every layer
    // standing on it. Under the multiplicative model this needed a whole
    // extra band applied after the cover; here it is structural.
    uint c = min(cid, kCoverCount - 1u);
    albedo = mix(albedo, mix(kCoverFresh[c], kCoverWorn[c], worn), cov);

    // The strands' own slope. Height × pitch IS the slope of a blade, so the
    // tilt needs no number of its own. Two taps, and only where the strands
    // are still resolved — past that range they are a tint, and a tint has
    // no normal. That early-out is what keeps the far half of a frame from
    // paying for grass it cannot see.
    if (!withTilt || d <= 0.001) return cov;
    float e = max(px, 0.5 / f);
    vec2 g = vec2(grain(q + vec2(e, 0.0), f) - s0,
                  grain(q + vec2(0.0, e), f) - s0) / e;
    vec3 tilt = -vec3(g.x, 0.0, g.y) * (hM * cov * d);
    tilt -= N * dot(N, tilt);
    float tl = length(tilt);
    if (tl > kMaxTilt) tilt *= kMaxTilt / tl;
    nrm = normalize(nrm + tilt);
    return cov;
}

// ── ONE GROUND, WHOLE ────────────────────────────────────────────────────────
// Everything one material id is: its colour through the mix, its cover, and
// the normal its relief tilts. Written as a function because the JOINT between
// two materials calls it twice (see the blend in main) — and because "what a
// material looks like" is one thing, whether it is drawn alone or mixed with
// its neighbour.
struct Ground {
    vec3 albedo;
    vec3 nrm;
    // The two z-scores this ground was read with. They belong to the PLACE as
    // much as to the material — the surface's own roughness and the terrain's
    // patchwork — so the neighbour at a joint borrows them instead of paying
    // for its own (see ground_colour).
    float surfZ;
    float macroZ;
};

// HOW WORN this spot is: the whole ladder collapsed into one number in 0..1.
// Both halves ADD, because "how worn is this place" is a single fact measured
// at two scales — the terrain's patchwork and the surface's own intermixing —
// and a spot is not separately worn at 20 m and at 20 cm.
//
// THE CLAMP IS THE LAW, not a safety rail. Everything downstream reads a
// colour by mixing along this fraction, so a bounded fraction means a bounded
// palette: the shader cannot put a colour on screen that is not between the
// two the CSV authored. The model this replaced multiplied one colour by a
// lognormal and drifted its hue along a vector, and could therefore land
// anywhere at all — which is what "dirty ground" was (owner, 2026-09-14/15):
// not too much texture, but olives and teals nobody chose.
//
// At range both z-scores shrink toward zero (averaged(), surface_lib.glsl), so
// the mix settles to 0.5 — the midpoint, which IS the row's mean colour. Far
// ground converges to exactly what the table says the ground is.
float ground_worn(uint mid, float surfZ, float macroZ) {
    vec2 sd = kGroundSpread[mid];
    return clamp(0.5 + surfZ * sd.x + macroZ * sd.y, 0.0, 1.0);
}

// A ground's COLOUR, given a place that was already read. This is what the
// runner-up lends to a joint: a transition is at most a tile wide, and across
// one metre a family's PATTERN is not legible — its colour is. So the
// neighbour borrows the winner's z-scores and costs no noise samples at all,
// only arithmetic. Its cover joins as the mean tint its density describes,
// which is what a sward looks like once you can no longer resolve a blade.
vec3 ground_colour(uint mid, float surfZ, float macroZ, float height01) {
    float worn = ground_worn(mid, surfZ, macroZ);
    vec3 base = mix(kGroundFresh[mid], kGroundWorn[mid], worn);
    base *= 1.0 - kGroundDamp[mid] * 0.28
                      * (1.0 - smoothstep(0.40, 0.47, height01));
    vec2 cover = kGroundCover[mid];
    uint cid = uint(cover.x + 0.5);
    if ((ground_debug_bits() & kGdbgCover) != 0u) cover.y = 0.0;
    if (cid != 0u && cover.y > 0.001) {
        uint c = min(cid, kCoverCount - 1u);
        base = mix(base, mix(kCoverFresh[c], kCoverWorn[c], worn), cover.y);
    }
    return base;
}

// `withNormal` is false for the NEIGHBOUR at a joint: its relief costs two
// extra taps of the height field and two of the strand field, and the tilt
// they produce is then averaged into the centre material's anyway. Blending
// two albedos is the point of the joint; blending two micro-reliefs is not
// worth a third of the frame's ground cost. (Measured: it was.)
Ground ground_of(uint mid, vec2 cover, vec2 gWorld, vec3 Pabs, vec3 N, vec3 V,
                 float px, float height01, float time, vec2 wind,
                 bool withNormal) {
    uint fam = kGroundFamily[mid];
    vec2 srf = kGroundSurface[mid];          // meso frequency, relief height
    vec4 lad = kGroundLadder[mid];           // iLow, iHigh, normTerrain, normSurf
    uint dbg = ground_debug_bits();

    // Synth space: the ploughed field's twin turns here (see synth_space).
    vec3 Ps = synth_space(Pabs, mid);
    vec3 aN = abs(synth_space(N, mid));

    // ── THE FIELD ──
    // One ladder, two halves, one walk. Seeded apart per MATERIAL so two
    // biomes meeting at a border do not blotch in step — the same offset the
    // patchwork band carried before, now serving every rung at once.
    vec2 lz = ground_ladder(gWorld + float(mid) * 11.0, lad, srf.x, px);
    if ((dbg & kGdbgTerrain) != 0u) lz.x = 0.0;
    if ((dbg & kGdbgSurface) != 0u) lz.y = 0.0;
    // Rung 0: the family's own shape, at its own scale, on the same law and
    // the same normaliser. Skipped whole when the footprint has eaten it —
    // that early-out is what spares the far half of a frame the cliff
    // projection's second sample.
    float shapeW = averaged(px, srf.x);
    if (shapeW > kLadderFloor && (dbg & kGdbgShape) == 0u)
        lz.y += ground_shape(fam, Ps, aN, srf.x) * kNormShape * shapeW * lad.w;
    float surfZ = lz.y;
    float macroZ = lz.x;

    // ── COLOUR ──
    // ONE mix along ONE fraction. No multiply, no hue axis, no lognormal: the
    // field says how worn this spot is and the colour is read off the segment
    // between what the ground is made of. Everything that used to modulate
    // brightness after the fact — the surface sigma, the terrain patchwork
    // applied over the cover, the chroma drift — is now the same number
    // arriving in one place, which is why the place still reaches the grass
    // (below) without a second band to keep in step.
    float worn = ground_worn(mid, surfZ, macroZ);
    vec3 base = mix(kGroundFresh[mid], kGroundWorn[mid], worn);
    // DAMP: ground inside the shoreline height band reads wet. A film of
    // standing water is the one honest MULTIPLY left here — it darkens
    // whatever lies under it rather than choosing between constituents, and a
    // scalar cannot invent a hue. One law, one column: sand, lake bed and
    // peat differ by their number, not by a branch that names them.
    base *= 1.0 - kGroundDamp[mid] * 0.28
                      * (1.0 - smoothstep(0.40, 0.47, height01));

    // ── COVER ──
    // The row says what grows here; the CALLER says how much, because at a
    // joint the sward thins into its neighbour (see the joint in main). It
    // reads the SAME wornness, so a drier hollow gets paler soil and paler
    // grass out of one fact about the place.
    vec3 Nb = N;
    float cov = cover_apply(uint(cover.x + 0.5), cover.y, worn, Pabs, N, V, px,
                            time, wind, withNormal, base, Nb);

    // ── THE NORMAL ──
    // The relief's slope, from two extra taps of the height a HALF PIXEL
    // apart — never finer, so the difference measures a slope the frame can
    // actually show. (Screen-space dFdx/dFdy of the height would be free, and
    // was tried: the hardware's 2×2 quad quantises the slope, and with a
    // field this fine the near ground broke into hard L-shaped blocks.
    // Measured cost of honesty: 0.7 ms of scene time at 3000 bodies.)
    //
    // AFTER the cover, and scaled by what the cover left showing. You do not
    // see a meadow's tussocks — you see the grass standing on them, and the
    // grass has its own strands and its own tilt two lines above. Shading the
    // ground's relief at full strength UNDER a closed sward drew both, and at
    // a low sun the second one is what made the green crunch: at 22° the
    // four-band quantise puts the whole field in one step and the bump delta
    // then carries every bit of the visible variation. One factor, and it is
    // not a taste knob — it is the fraction of ground you can actually see.
    //
    // Tangent frame: any orthonormal pair works, because the gradient is
    // taken IN it and applied back THROUGH it, so the choice cancels.
    float relief = (1.0 - cov) * srf.y;
    if (withNormal && resolved(px, srf.x) > 0.001 && relief > 0.0) {
        vec3 T = normalize(cross(N, aN.y > 0.9 ? vec3(1.0, 0.0, 0.0)
                                               : vec3(0.0, 1.0, 0.0)));
        vec3 B = cross(N, T);
        float eps = max(px, 0.02);
        float h0 = ground_height_m(fam, Ps, aN, srf.x, relief, px);
        float hT = ground_height_m(fam, synth_space(Pabs + T * eps, mid), aN,
                                   srf.x, relief, px);
        float hB = ground_height_m(fam, synth_space(Pabs + B * eps, mid), aN,
                                   srf.x, relief, px);
        // The normal of a height field z = h(x,y) is (-dh/dx, -dh/dy, 1) in
        // its own tangent frame — that is the whole of the bump mapping.
        vec3 tilt = -((hT - h0) * T + (hB - h0) * B) / eps;
        float tl = length(tilt);
        if (tl > kMaxTilt) tilt *= kMaxTilt / tl;
        // ADDED to Nb, not replacing it: the cover already tilted for its
        // strands, and the two reliefs are both offsets from the same
        // geometric normal.
        Nb = normalize(Nb + tilt);
    }

    Ground g;
    g.albedo = base;
    g.nrm = Nb;
    g.surfZ = surfZ;
    g.macroZ = macroZ;
    return g;
}

void main() {
    vec3 N = normalize(vNormal);
    // Anchor the procedural detail to ABSOLUTE world coords. vWorld is
    // window-relative (composite-centred), so at a seam recentre it reindexes by
    // ±kCellSize for a fixed physical point — resampling every noise/stripe/crack
    // term and re-mottling brightness, which reads as a texture AND lighting
    // "pop". The renderer packs the composite's absolute origin into the
    // otherwise-unused sunDir.w / sunColor.w lanes (0 in the gpu_smoke3d
    // harness → identity), mirroring the absolute seeding trees already use.
    //
    // The origin is folded in MODULO the synth period, and that is not a
    // detail: it is a multiple of the recentre step (the origin is whole
    // macro cells), so the wrap is exact and the anchor is unchanged — while
    // the coordinate handed to the synth stays inside one period instead of
    // running to a million metres, where a float32 has 6 cm of resolution
    // left and the grain quantises into visible blocks. See kSynthPeriod.
    // Lighting/shadow math keep window vWorld.
    vec2 gWorld = vWorld.xz
                  + mod(vec2(pc.sunDir.w, pc.sunColor.w), kSynthPeriod);
    vec3 Pabs = vec3(gWorld.x, vWorld.y, gWorld.y);

    // The world size of one pixel, taken from the position itself so it is
    // honest on every slope and at every range. It is the argument to every
    // resolved() below — THE defence against a high frequency crawling under
    // a moving camera, and the reason this synth needs no mipmaps.
    //
    // GEOMETRIC MEAN of the two screen axes, not their max. Ground is almost
    // always seen at a grazing angle: one pixel then covers centimetres ACROSS
    // the view and half a metre ALONG it. The max is the footprint of the
    // longer axis, and taking it erased every surface frequency past a few
    // metres — the whole middle distance went back to being flat plastic. The
    // geometric mean is the footprint of an AREA-equivalent square, the same
    // quantity a mip level is chosen by, and it keeps the detail the short
    // axis can still resolve.
    float px = sqrt(max(length(dFdx(vWorld)), 1e-5)
                    * max(length(dFdy(vWorld)), 1e-5));

    // ── THE JOINT ─────────────────────────────────────────────────────────
    // The material texture is one texel per world TILE, sampled NEAREST —
    // deliberately, because that is what keeps a one-tile road connected
    // instead of dissolving between terrain vertices 16 m apart. Its price is
    // that a point sample of it is a step function, and every joint between
    // two materials is a 1 m axis-aligned staircase (owner, 2026-09-12:
    // «стыки тайловые, очень резкие»).
    //
    // THE ANSWER IS TO STOP POINT-SAMPLING IT. A fragment does not sit on one
    // tile; it sits inside a square metre that two (or three, or four) tiles
    // share. One textureGather hands back all four ids at once, and the
    // bilinear fractions are each tile's SHARE of this fragment. Two grounds
    // covering 0.6 and 0.4 of it do not meet at a line — the surface here IS
    // 60 % one and 40 % the other, and mixing them by exactly those shares is
    // a continuous function of position with no edge anywhere in it.
    //
    // This replaced a first attempt that sampled the id twice — once here,
    // once a metre away — and blended wherever the two disagreed. It looked
    // better than the staircase and was still wrong in kind: "the two samples
    // disagree" is a BINARY region, and the silhouette of that region was
    // simply the old hard edge in a new place (the owner saw the fingers and
    // tongues it made along a beach).
    vec2 texSize = vec2(textureSize(u_material, 0));
    vec2 texelUv = 1.0 / texSize;

    // The jitter survives, doing the one job it is actually good at: moving
    // the boundary, not softening it. A margin in nature wanders — two
    // octaves, one bending every ~3 m and one fraying four times finer — and
    // how far is the ground's own `edge_m`: a road keeps 0.3 m and stays a
    // road, sand creeps into grass with 1.6. Metres are texels here (a tile
    // is a metre, kTileMeters), so the texture's own size converts them.
    // This one amplitude is read from the point sample on purpose: it scales
    // a DISPLACEMENT, which stays continuous however abruptly it changes.
    float edgeM = kGroundEdge[min(uint(texture(u_material, vUv).r * 255.0
                                       + 0.5),
                                  kGroundCount - 1u)];
    vec2 uvJ = vUv;
    if (edgeM > 0.0) {
        vec2 wander = vec2(wnoise(gWorld + 3.1, kJointFreq),
                           wnoise(gWorld + 91.7, kJointFreq)) - 0.5;
        vec2 ragged = vec2(wnoise(gWorld + 57.3, kJointFreq * 4.0),
                           wnoise(gWorld + 13.9, kJointFreq * 4.0)) - 0.5;
        uvJ += (wander * 2.0 + ragged * 0.7) * edgeM * texelUv;
    }

    // The four ids that share this fragment, and their shares of it.
    // textureGather returns the 2×2 in the order (0,1) (1,1) (1,0) (0,0).
    vec4 ids = textureGather(u_material, uvJ) * 255.0;
    vec2 f = fract(uvJ * texSize - 0.5);

    // Coverage per DISTINCT id: a tile counts once for every corner it owns,
    // so a material holding three of the four corners holds three quarters of
    // this square metre. Taken on the RAW bilinear shares — the sharpening
    // below needs to know WHICH two grounds meet before it can ask them how
    // wide their margin is.
    vec4 raw = vec4((1.0 - f.x) * f.y, f.x * f.y,
                    f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
    vec4 cov;
    for (int k = 0; k < 4; ++k) {
        float c = 0.0;
        for (int j = 0; j < 4; ++j)
            if (abs(ids[j] - ids[k]) < 0.5) c += raw[j];
        cov[k] = c;
    }
    int kTop = 0;
    for (int k = 1; k < 4; ++k) if (cov[k] > cov[kTop]) kTop = k;
    int kSnd = -1;
    for (int k = 0; k < 4; ++k) {
        if (abs(ids[k] - ids[kTop]) < 0.5) continue;
        if (kSnd < 0 || cov[k] > cov[kSnd]) kSnd = k;
    }

    uint mid = min(uint(ids[kTop] + 0.5), kGroundCount - 1u);
    uint midB = kSnd < 0 ? mid : min(uint(ids[kSnd] + 0.5), kGroundCount - 1u);

    // Now sharpen, with the margin of the PAIR — the wider of the two, because
    // a margin belongs to the meeting, not to one side of it. (Read from the
    // point-sampled centre instead, as it was at first, the width itself
    // jumped at the tile line: a discontinuity in how the discontinuity is
    // hidden, which is the same mistake one level down.)
    float band = clamp(max(kGroundEdge[mid], kGroundEdge[midB]), 0.05, 1.0);
    vec2 fs = clamp((f - 0.5) / band + 0.5, 0.0, 1.0);
    vec4 share = vec4((1.0 - fs.x) * fs.y, fs.x * fs.y,
                      fs.x * (1.0 - fs.y), (1.0 - fs.x) * (1.0 - fs.y));
    float covTop = 0.0, covSnd = 0.0;
    for (int j = 0; j < 4; ++j) {
        if (abs(ids[j] - ids[kTop]) < 0.5) covTop += share[j];
        else if (kSnd >= 0 && abs(ids[j] - ids[kSnd]) < 0.5) covSnd += share[j];
    }

    // The runner-up's share of the pair. It reaches 0.5 exactly on the line
    // between two tile centres and falls to 0 at either centre — so the
    // transition is a continuous function of position, with no region and no
    // number setting its width. The noise only ROUGHENS it: a margin is not a
    // clean ramp.
    float b = kSnd < 0 ? 0.0 : covSnd / max(covTop + covSnd, 1e-5);
    b *= 0.7 + 0.6 * wnoise(gWorld + 77.1, kJointFreq * 4.0);
    b = clamp(b, 0.0, 0.5);

    vec3 V = normalize(pc.camPos.xyz - vWorld);
    float time = u_pointLights.skyParams.x;
    vec2 wind = u_pointLights.skyParams.yz;

    // THE SWARD THINS TOO. Blending only the albedo left one hard thing at a
    // joint: the winner's cover was drawn at full strength right up to the
    // line and then switched — strands, tilt and all. So the cover's DENSITY
    // is blended by the same shares. Where the neighbour grows the same thing
    // the density simply crosses over; where it grows something else (or
    // nothing) this sward thins to nothing by the halfway line, and the
    // neighbour's own cover arrives as the mean tint inside ground_colour.
    // One expression, no branch, and it costs nothing: cover_apply already
    // took the density as an argument.
    vec2 covA = kGroundCover[mid];
    vec2 covB = kGroundCover[midB];
    bool sameCover = abs(covA.x - covB.x) < 0.5;
    vec2 cover = vec2(covA.x, mix(covA.y, sameCover ? covB.y : 0.0, b));
    // The two bisect gates that belong to the CALLER: strip the sward, or
    // strip the relief the family shape tilts. Taking the cover out here
    // rather than inside ground_of is what makes the neighbour at a joint
    // lose it too — ground_colour reads the same density.
    uint gdbg = ground_debug_bits();
    if ((gdbg & kGdbgCover) != 0u) cover.y = 0.0;
    bool withRelief = (gdbg & kGdbgRelief) == 0u;

    Ground g = ground_of(mid, cover, gWorld, Pabs, N, V, px, vHeight, time,
                         wind, withRelief);
    if (b > 0.02) {
        // The two grounds, mixed by their shares of this square metre. The
        // runner-up lends its COLOUR on the winner's shape (ground_colour) —
        // a second full synth was measured at twice the price of this one for
        // a difference nobody can see across a one-metre band.
        g.albedo = mix(g.albedo,
                       ground_colour(midB, g.surfZ, g.macroZ, vHeight), b);
    }
    vec3 base = g.albedo;
    vec3 Nb = g.nrm;

    // Surface marks: alpha-over onto the albedo, gated by the validity ring
    // (Chebyshev — the sliding canvas is square). Window vWorld is the right
    // space: the canvas mapping is mod-1024, the exact seam recentre step, so
    // marks stay put across a crossing. Keep in lockstep with stamp.vert.
    vec4 mark = texture(u_stain, (vWorld.xz + vec2(1536.0)) / 1024.0);
    vec2 dCam = abs(vWorld.xz - pc.camPos.xz);
    float markOk = step(0.001, pc.camPos.w)
                   * step(max(dCam.x, dCam.y), pc.camPos.w);
    base = mix(base, mark.rgb, mark.a * markOk);

    // ── LIGHT ──
    vec3 L = normalize(pc.sunDir.xyz);
    float ndlGeo = max(dot(N, L), 0.0);
    float ndlBump = max(dot(Nb, L), 0.0);
    // HOW THE LAND TAKES THE LIGHT. The SURFACE's own relief is always added
    // as the difference the bumped normal makes, smoothly — quantising a
    // per-pixel micro-normal would step and crawl, and the relief is what we
    // came for. What is in question is the LAND under it.
    //
    // A 4-band quantise used to run here unconditionally, and it is why the
    // terrain wore hard-edged blotches (owner, 2026-09-15). The law it breaks
    // is worth writing down, because it decides the whole question:
    //
    //     A VISIBLE EDGE MUST HAVE A CAUSE IN THE WORLD.
    //
    // Posterising N·L draws its steps along the level sets of dot(N, L). On a
    // structure that is harmless: a wall is one flat facet, so the whole facet
    // lands in one band and the only edges are the wall's own corners. On a
    // billboard it never happens at all — sprites take a flat sun term. But
    // the LAND is the one smooth-shaded body in this world: its normal varies
    // continuously, so the bands cut it along curves that match no ridge and
    // no hollow, and that move with the sun rather than with the ground. How
    // many of them you see is set by the sun's elevation (a low sun spreads
    // N·L across two or three band edges, a high sun across one) — a pattern
    // whose density is a property of the hour and not of the land is exactly
    // what the eye names as dirt.
    //
    // So the land is shaded SMOOTH and the stylisation stays where it reads as
    // stylisation: struct.frag keeps its quantise unchanged, and billboards
    // keep the flat sun term they always had. Nothing is lost that was ever
    // legible — only the one surface the law was never right for.
    //
    // Two other readings were built and looked at side by side before this one
    // was kept (owner, 2026-09-15): quantising the triangle's OWN plane, which
    // does give every band edge a real cause but turns the land faceted at the
    // 16 m mesh, and the old interpolated quantise. Recorded so nobody spends
    // the evening rediscovering them.
    float ndl = clamp(ndlBump, 0.0, 1.0);
    float sh = shadowFactorHandoff(u_shadow, u_shadowFar,
                                   pc.lightMvp * vec4(vWorld, 1.0),
                                   far_light_clip(vWorld), ndlGeo,
                                   TIMAERT_SHADOW_SPREAD_MESH);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl, sh, vWorld);
    // Additive positional lights (torches, spells, player glow). Uses window vWorld
    // — the same space as the sun/shadow math — not the absolute gWorld synth coord.
    // Inert while the light buffer count is 0 (until an emitter is gathered, Inc 3+).
    col += base * point_lights(vWorld, Nb);
    // THE AIR, last — after the additive lights, because a torch's glow
    // travels the same air the surface under it does.
    outColor = vec4(aerial_perspective(col, vWorld), 1.0);
}
