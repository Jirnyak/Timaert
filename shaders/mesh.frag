#version 450
#extension GL_GOOGLE_include_directive : require
// Subworld 3D terrain mesh fragment stage. Procedural per-biome ground synth
// (no atlas) lit by a 4-band quantised NdotL sun + ambient, with a PCF
// shadow-map lookup so cast shadows (terrain + trees) land on the surface.
//
// THREE FREQUENCY BANDS, and the reason the ground reads as a surface:
//   macro ~28 m — the biome patchwork. Colour only: a drier patch of grass
//                 is not a hill.
//   meso  0.3–4.5 m — the family's STRUCTURE (tussocks, ripples, plates,
//                 furrows). This is the band the eye reads as "surface", and
//                 it was the one that did not exist: the synth used to jump
//                 straight from 28 m patches to a 4.5 cm white-noise hash,
//                 which averages to a flat colour past two metres.
//   micro 2–6 cm — the grain, damped by the pixel footprint so it fades out
//                 instead of crawling when the camera moves.
// The meso and micro bands are ONE field (ground_field), read twice: its
// VALUE tints the albedo and its SLOPE tilts the normal. That is why a
// crevice here is dark AND indented — and why the sun, the relief march and
// the shadow map already in the frame shade the relief for free, with no new
// pass, no new texture and no new descriptor.
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

// The biome patchwork band: one patch per ~28 m. That is the scale at which
// ground reads as TERRAIN (a dry hollow, a mossier slope) rather than as
// surface — below it the family's meso band takes over.
const float kMacroFreq = 0.035;

// The family shapes below are authored to live in about [-1,1] with a
// standard deviation near 1/3, so ONE constant turns any of them into a
// z-score for mottle() — and the same value read as METRES needs no second
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

// THE ground field: x = the meso shape, y = the grain, both faded out by the
// pixel footprint at the range where they stop being resolvable. Read by the
// albedo (as a value) and by the normal (as a slope) — one field, so the two
// can never disagree.
vec2 ground_field(uint fam, vec2 q, float f, float gf, float px) {
    return vec2(ground_meso(fam, q, f) * resolved(px, f),
                (grain(q, gf) - 0.5) * 2.0 * resolved(px, gf));
}

// Projection. The ground is a heightfield, so the horizontal plane is the
// right one almost everywhere; on a cliff face it would stretch the pattern
// into vertical smears, so the dominant VERTICAL plane is crossfaded in by
// the geometric normal. Flat ground pays for one evaluation — the second is
// only taken where the ground actually tips over.
vec2 ground_at(uint fam, vec3 p, vec3 aN, float f, float gf, float px) {
    vec2 h = ground_field(fam, p.xz, f, gf, px);
    float wy = smoothstep(0.55, 0.88, aN.y);
    if (wy >= 0.999) return h;
    vec2 pv = (aN.x > aN.z) ? p.zy : p.xy;
    return mix(ground_field(fam, pv, f, gf, px), h, wy);
}

// The MESO shape alone, projected — the height the normal is taken from. The
// grain is deliberately NOT in it: its depth would be a tenth of its own 4 cm
// wavelength, which is at most a couple of pixels wide before resolved() has
// already faded it, and a slope measured across two pixels is noise, not
// relief. The grain speaks through the albedo, where its amplitude is
// calibrated; the relief speaks through the normal. Skipping it also halves
// what each gradient tap below costs.
float ground_height_m(uint fam, vec3 p, vec3 aN, float f, float reliefM,
                      float px) {
    float d = resolved(px, f);
    if (d <= 0.0) return 0.0;
    float h = ground_meso(fam, p.xz, f);
    float wy = smoothstep(0.55, 0.88, aN.y);
    if (wy < 0.999) {
        vec2 pv = (aN.x > aN.z) ? p.zy : p.xy;
        h = mix(ground_meso(fam, pv, f), h, wy);
    }
    return h * d * reliefM;
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
float cover_apply(uint cid, float density, vec3 Pabs, vec3 N, vec3 V,
                  float px, float time, vec2 wind, bool withTilt,
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

    // The strand field. Faded by `d`: where the strands are not resolved
    // there is nothing to stand up, which is what keeps distant cover from
    // sparkling — it settles into the flat tint its mean colour describes.
    float s0 = grain(q, f);
    albedo = mix(albedo,
                 kCoverColour[min(cid, kCoverCount - 1u)]
                     * mottle(cp.w, (s0 - 0.5) * kNormGrain * d),
                 cov);

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
// Everything one material id is: its albedo through the three bands, its
// cover, and the normal its relief tilts. Written as a function because the
// JOINT between two materials calls it twice (see the blend in main) — and
// because "what a material looks like" is one thing, whether it is drawn
// alone or mixed with its neighbour.
struct Ground {
    vec3 albedo;
    vec3 nrm;
    // The two z-scores this ground was shaded with. They belong to the PLACE
    // as much as to the material — the surface's own relief and the terrain's
    // patchwork — so the neighbour at a joint borrows them instead of paying
    // for its own (see ground_colour).
    float surfZ;
    float macroZ;
};

// A ground's COLOUR, given a shape and a place that were already computed.
// This is what the runner-up lends to a joint: a transition is at most a tile
// wide, and across one metre a family's PATTERN is not legible — its hue and
// its lightness are. So the neighbour borrows the winner's shape and the
// place's patchwork and costs no noise samples at all, only arithmetic. Its
// cover joins as the mean tint its density describes, which is what a sward
// looks like once you can no longer resolve a blade.
vec3 ground_colour(uint mid, float surfZ, float macroZ, float height01) {
    vec4 srf = kGroundSurface[mid];
    vec3 base = kGroundAlbedo[mid] * mottle(srf.x, surfZ);
    base *= 1.0 - kGroundDamp[mid] * 0.28
                      * (1.0 - smoothstep(0.40, 0.47, height01));
    vec2 cover = kGroundCover[mid];
    uint cid = uint(cover.x + 0.5);
    if (cid != 0u && cover.y > 0.001)
        base = mix(base, kCoverColour[min(cid, kCoverCount - 1u)], cover.y);
    base *= mottle(kGroundMacroSigma[mid], macroZ);
    if (srf.z > 0.001) {
        vec3 ax = kGroundChromaAxis[mid] * srf.z;
        base *= exp(macroZ * ax - 0.5 * ax * ax);
    }
    return base;
}

// `withNormal` is false for the NEIGHBOUR at a joint: its relief costs two
// extra taps of the height field and two of the strand field, and the tilt
// they produce is then averaged into the centre material's anyway. Blending
// two albedos is the point of the joint; blending two micro-reliefs is not
// worth a third of the frame's ground cost. (Measured: it was.)
Ground ground_of(uint mid, vec2 gWorld, vec3 Pabs, vec3 N, vec3 V,
                 float px, float height01, float time, vec2 wind,
                 bool withNormal) {
    uint fam = kGroundFamily[mid];
    vec4 srf = kGroundSurface[mid];          // sigma, meso freq, chroma, relief
    float gf = kGroundGrainFreq[mid];

    // Synth space: the ploughed field's twin turns here (see synth_space).
    vec3 Ps = synth_space(Pabs, mid);
    vec3 aN = abs(synth_space(N, mid));

    vec2 shape = ground_at(fam, Ps, aN, srf.y, gf, px);

    // ── THE NORMAL ──
    // The relief's slope, from two extra taps of the height a HALF PIXEL
    // apart — never finer, so the difference measures a slope the frame can
    // actually show. (Screen-space dFdx/dFdy of the height would be free, and
    // was tried: the hardware's 2×2 quad quantises the slope, and with a
    // field this fine the near ground broke into hard L-shaped blocks.
    // Measured cost of honesty: 0.7 ms of scene time at 3000 bodies.)
    //
    // Tangent frame: any orthonormal pair works, because the gradient is
    // taken IN it and applied back THROUGH it, so the choice cancels.
    vec3 Nb = N;
    float mesoD = resolved(px, srf.y);
    if (withNormal && mesoD > 0.001 && srf.w > 0.0) {
        vec3 T = normalize(cross(N, aN.y > 0.9 ? vec3(1.0, 0.0, 0.0)
                                               : vec3(0.0, 1.0, 0.0)));
        vec3 B = cross(N, T);
        float eps = max(px, 0.02);
        float h0 = ground_height_m(fam, Ps, aN, srf.y, srf.w, px);
        float hT = ground_height_m(fam, synth_space(Pabs + T * eps, mid), aN,
                                   srf.y, srf.w, px);
        float hB = ground_height_m(fam, synth_space(Pabs + B * eps, mid), aN,
                                   srf.y, srf.w, px);
        // The normal of a height field z = h(x,y) is (-dh/dx, -dh/dy, 1) in
        // its own tangent frame — that is the whole of the bump mapping.
        vec3 tilt = -((hT - h0) * T + (hB - h0) * B) / eps;
        float tl = length(tilt);
        if (tl > kMaxTilt) tilt *= kMaxTilt / tl;
        Nb = normalize(N + tilt);
    }

    // ── ALBEDO ──
    vec3 base = kGroundAlbedo[mid];
    // MACRO band: the terrain-scale patchwork — two octaves, ~28 m and a
    // quarter of that, seeded apart per material so two biomes meeting at a
    // border do not blotch in step. Colour only: a drier patch of grass is
    // not a hill. This is the band that reaches the HORIZON — past a couple
    // of hundred metres the pixel footprint has eaten the meso structure and
    // all of the grain, and without this the far ground is one flat colour,
    // which is what the measured "one bucket covers 46% of the frame" was.
    // The 0.75/0.66 split is the same unit-variance identity as below.
    // (`patch` is a reserved word in GLSL — tessellation.)
    float biomePatch = (wnoise(gWorld + float(mid) * 11.0, kMacroFreq) - 0.5)
                           * 0.75
                       + (wnoise(gWorld + float(mid) * 7.0, kMacroFreq * 4.0)
                          - 0.5) * 0.66;
    float macroZ = biomePatch * kNormNoise;
    // MESO + MICRO through the one mean-preserving law. The 0.8/0.6 split is
    // an identity, not a taste: 0.8^2 + 0.6^2 = 1, so the two bands together
    // still carry exactly the unit variance the row's sigma was calibrated
    // for (CV = sqrt(exp(sigma^2)-1), see tools/gen_ground_table.py).
    base *= mottle(srf.x, (shape.x * 0.80 + shape.y * 0.60) * kNormShape);
    // DAMP: ground inside the shoreline height band reads wet. One law, one
    // column — sand, lake bed and peat differ by their number, not by a
    // branch that names them.
    base *= 1.0 - kGroundDamp[mid] * 0.28
                      * (1.0 - smoothstep(0.40, 0.47, height01));

    // ── COVER ──
    vec2 cover = kGroundCover[mid];
    cover_apply(uint(cover.x + 0.5), cover.y, Pabs, N, V, px,
                time, wind, withNormal, base, Nb);

    // THE PLACE, applied LAST — over the cover, not under it. The macro
    // patchwork is a property of the GROUND, not of the material: a drier
    // hollow has paler soil AND paler grass over it. Applied before the
    // cover it was erased wherever the sward closed, and a fully covered
    // ground (a meadow, snow) went back to being one flat colour at range —
    // measured: the covered cells were the only ones whose single-bucket
    // share got WORSE. Luminance and hue ride the same sample: a patch that
    // is drier is both paler and warmer.
    base *= mottle(kGroundMacroSigma[mid], macroZ);
    if (srf.z > 0.001) {
        vec3 ax = kGroundChromaAxis[mid] * srf.z;
        base *= exp(macroZ * ax - 0.5 * ax * ax);
    }

    Ground g;
    g.albedo = base;
    g.nrm = Nb;
    g.surfZ = (shape.x * 0.80 + shape.y * 0.60) * kNormShape;
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
    // The shares are bilinear, but SHARPENED to the material's own margin:
    // raw bilinear spreads every transition across a full tile, which is
    // right for a beach and wrong for a stone one tile wide — it would be in
    // its own ramp everywhere and read as a stain rather than a stone. The
    // same `edge_m` that says how far a margin wanders says how wide it is,
    // because they are the same fact about the ground. A built thing (road
    // 0.3) keeps a hand's width of margin and stays crisp.
    float band = clamp(edgeM, 0.05, 1.0);
    vec2 fs = clamp((f - 0.5) / band + 0.5, 0.0, 1.0);
    vec4 share = vec4((1.0 - fs.x) * fs.y, fs.x * fs.y,
                      fs.x * (1.0 - fs.y), (1.0 - fs.x) * (1.0 - fs.y));

    // Coverage per DISTINCT id: a tile counts once for every corner it owns,
    // so a material holding three of the four corners holds three quarters of
    // this square metre.
    vec4 cov;
    for (int k = 0; k < 4; ++k) {
        float c = 0.0;
        for (int j = 0; j < 4; ++j)
            if (abs(ids[j] - ids[k]) < 0.5) c += share[j];
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
    // The runner-up's share of the pair. It reaches 0.5 exactly on the line
    // between two tile centres and falls to 0 at either centre — so the
    // transition is one tile wide by construction, everywhere, and needs no
    // number to set its width. The noise only ROUGHENS it: a margin is not a
    // clean ramp.
    float b = kSnd < 0 ? 0.0
                       : cov[kSnd] / max(cov[kTop] + cov[kSnd], 1e-5);
    b *= 0.7 + 0.6 * wnoise(gWorld + 77.1, kJointFreq * 4.0);
    b = clamp(b, 0.0, 0.5);

    vec3 V = normalize(pc.camPos.xyz - vWorld);
    float time = u_pointLights.skyParams.x;
    vec2 wind = u_pointLights.skyParams.yz;

    Ground g = ground_of(mid, gWorld, Pabs, N, V, px, vHeight, time, wind,
                         true);
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
    // The LAND keeps its 4-band quantise — that stylised step across a hill
    // is the look of this world. The SURFACE's own relief is added as the
    // difference the bumped normal makes, smoothly: quantising a per-pixel
    // micro-normal would step and crawl, and the relief is what we came for.
    float ndl = floor(ndlGeo * 4.0) / 4.0 + (ndlBump - ndlGeo);
    ndl = clamp(ndl, 0.0, 1.0);
    float sh = shadowFactorHandoff(u_shadow, u_shadowFar,
                                   pc.lightMvp * vec4(vWorld, 1.0),
                                   far_light_clip(vWorld), ndlGeo,
                                   TIMAERT_SHADOW_SPREAD_MESH);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl, sh, vWorld);
    // Additive positional lights (torches, spells, player glow). Uses window vWorld
    // — the same space as the sun/shadow math — not the absolute gWorld synth coord.
    // Inert while the light buffer count is 0 (until an emitter is gathered, Inc 3+).
    col += base * point_lights(vWorld, Nb);
    outColor = vec4(col, 1.0);
}
