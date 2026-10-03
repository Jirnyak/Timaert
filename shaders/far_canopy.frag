#version 450
// THE FAR FOREST AS A MASS — fragment stage, and like the ground's it invents
// no law of its own.
//
// COLOUR IS THE COVER VOCABULARY'S OWN ROW (data/ground_cover.csv `canopy`,
// generated into ground_surface.glsl). The same pair of constituents will
// dress the near crown when it becomes geometry, so the wood seen from a ridge
// and the tree seen underfoot cannot drift apart — the same argument the
// ground makes with kGroundFresh/kGroundWorn, one layer up.
//
// LIGHT AND AIR ARE THE SAME CALLS the far ground makes. A hand-written
// `base * ndl` here would be a second law of light, and the far sheet already
// paid for that mistake once.
#include "lighting.glsl"
#include "ground_surface.glsl"
#include "surface_lib.glsl"

layout(location = 0) in vec3  vNormal;
layout(location = 1) in vec3  vWorld;
layout(location = 2) in float vCover;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    vec4 camPos;
    vec4 shore;
    vec4 ring;
    vec4 origin;    // xz = window origin in ABSOLUTE metres, w = world seed
} pc;

layout(location = 0) out vec4 outColor;

// The same floor the vertex stage lifts from: under it there is no wood to
// draw, and the cushion is sitting on the ground anyway.
const float kCanopyFloor = 0.12;
// Where the wood's EDGE is, within the fraction. Below this the cushion is
// torn open so the land shows through — which is what makes a rim read as a
// rim rather than as a hem. Mean coverage still follows the field: the noise
// is uniform, so the share that survives the threshold IS the fraction.
const float kEdgeSoft = 0.5 / kNormNoise;
// THE SAME PATCH THE VERTEX STAGE LUMPS WITH, ring by ring: a rim torn at one
// scale over a cushion shaped at another would read as two different woods
// standing in one place. The rule is the lattice's own (a wavelength needs two
// steps to exist), so it is written identically here and there.
float patch_m(float stepM) { return max(256.0, 2.0 * stepM); }

void main() {
    if (vCover < kCanopyFloor) discard;   // no wood here, no cushion

    // THE TORN RIM. Same mottle the ground's own canopy tint uses, so the two
    // agree about where a stand stops; keyed on ABSOLUTE metres so the pattern
    // belongs to the land and does not swim when the window re-centres.
    vec2  q = pc.origin.xz + vWorld.xz
            + vec2(hash21(vec2(pc.origin.w, 1.0)),
                   hash21(vec2(2.0, pc.origin.w))) * kSynthPeriod;
    float patchM = patch_m(pc.ring.x);
    float mottle = wnoise(q, 1.0 / patchM) * 0.65
                 + wnoise(q, 3.0 / patchM) * 0.35;
    float cov = smoothstep(1.0 - vCover - kEdgeSoft,
                           1.0 - vCover + kEdgeSoft, mottle);
    // AND THE DETAIL IS REMOVED, NEVER SUBSTITUTED. Once a patch is finer than
    // the pixel showing it, the tear fades to the plain fraction instead of
    // fizzing — the mean is unchanged, the far rim simply goes smooth.
    float px = max(fwidth(vWorld.x), fwidth(vWorld.z));
    cov = mix(vCover, cov, resolved(px, 1.0 / patchM));
    if (cov < 0.5) discard;

    // THE MIDPOINT of the row's two constituents — what the mix settles to at
    // range, exactly as the ground takes it.
    vec3 base = (kCoverFresh[kCoverCanopy] + kCoverWorn[kCoverCanopy]) * 0.5;

    vec3  n   = normalize(vNormal);
    float ndl = max(dot(n, normalize(pc.sunDir.xyz)), 0.0);
    // `shadow = 1.0` for the same reason the ground gives: the object shadow
    // map reaches 1024 m and this surface begins at 1536 m, so there is
    // nothing out here casting into it.
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl,
                           /*shadow=*/1.0, vWorld);

    outColor = vec4(aerial_perspective(col, vWorld), 1.0);
}
