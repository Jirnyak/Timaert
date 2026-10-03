#version 450
// THE FAR WORLD'S GROUND (CANON S18.1) — fragment stage, and it is short on
// purpose. Everything that makes the far world convincing is a law that
// already exists; this shader's whole job is to not invent a second one.
//
// COLOUR IS THE MIDPOINT OF THE ROW'S TWO AUTHORED CONSTITUENTS — the very
// pair the near ground's mixture converges to as its detail falls below a
// pixel (ground_surface.glsl kGroundFresh/kGroundWorn, the same generated
// table mesh.frag reads). So the join at the composite's edge is invisible BY
// CONSTRUCTION: both sides are literally the same colour there, and nothing
// was tuned to make that true. «На дальности деталь УБИРАЕТСЯ, а не
// подменяется» — the midpoint is what removal converges to, not a stand-in.
//
// AND THE AIR IS THE LAST LINE, as it is in every lit pass. Without it the far
// world is a hard-edged cardboard cutout; with it the plain dissolves on its
// own and the ridge floats above the haze — which is the entire point of the
// track and the only thing that cannot be faked here.
#include "lighting.glsl"
#include "ground_surface.glsl"
#include "surface_lib.glsl"   // wnoise — THE noise octave, shared with the ground

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorld;
layout(location = 2) flat in float vMaterial;
layout(location = 3) in vec2 vCoverTexel;

// THE COVER ATLAS — macro FIELDS that reach the horizon, one per layer. The
// forest is layer 0 (far_mesh.h kFarCoverForest). Linear, because a cover is
// a FRACTION: unlike the material ordinal next door, its average is an honest
// amount rather than a row nobody authored.
layout(set = 1, binding = 2) uniform sampler2D uFarCover;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    vec4 camPos;
    vec4 shore;
    vec4 ring;      // the vertex stage's business; declared so the block matches
    vec4 origin;    // xz = window origin in ABSOLUTE metres, w = world seed
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    // The table's own length — clamped to it rather than to a number, so a
    // fifteenth ground row appearing tomorrow needs no edit here.
    uint m = uint(clamp(vMaterial, 0.0,
                        float(kGroundFresh.length() - 1)) + 0.5);
    // THE limit of the near ground's mixture: half of each constituent.
    vec3 base = (kGroundFresh[m] + kGroundWorn[m]) * 0.5;

    // ── THE FOREST, AS A MASS ─────────────────────────────────────────────
    // Out here nothing is a tree. What a forest IS at thirty kilometres is a
    // fraction of the ground that lies under canopy, and that fraction is a
    // macro FIELD (tree count / its ceiling, baked into the cover atlas).
    // Colour comes from the cover vocabulary's own row — the SAME two
    // constituents the near ground will dress a crown in, so the mass seen
    // from a ridge and the tree seen underfoot cannot drift apart.
    float forest = texture(uFarCover,
                           (vCoverTexel + 0.5)
                               / vec2(textureSize(uFarCover, 0))).r;
    if (forest > 0.0) {
        // WHERE the canopy stands inside a cell, as opposed to how much of it
        // there is. One PATCH PER QUARTER CELL: the field is authored per
        // macro cell (1024 m) and bilinearly spread, so it carries no
        // structure finer than that — a quarter of a cell adds shape strictly
        // BELOW the field's own resolution and therefore cannot argue with
        // it. Two octaves at the ladder's own lacunarity, because one
        // frequency at full contrast is a lava lamp rather than a forest
        // (the scar gen_ground_table.py names).
        const float kCanopyPatchM = 256.0;
        const float kPatchFreq = 1.0 / kCanopyPatchM;
        // The world's own mottle: absolute metres, so the pattern belongs to
        // the GROUND and does not swim as the window re-centres, plus a shift
        // drawn from the world seed so two worlds do not wear one blotching.
        vec2 seedShift = vec2(hash21(vec2(pc.origin.w, 1.0)),
                              hash21(vec2(2.0, pc.origin.w))) * kSynthPeriod;
        vec2 wq = pc.origin.xz + vWorld.xz + seedShift;
        float mottle = wnoise(wq, kPatchFreq) * 0.65
                     + wnoise(wq, kPatchFreq * 3.0) * 0.35;
        // A THRESHOLD, NOT A FADE, and it is what makes the edge of a wood
        // read as an edge: a fragment is under canopy when the mottle clears
        // the bar that the fraction itself sets. Because the noise is uniform,
        // the mean coverage this produces IS `forest` — the picture carries
        // the field's own number rather than a tuned version of it. The soft
        // width is half the octave's standard deviation (kNormNoise inverts
        // that sigma), so the rim is one noise step wide and nothing wider.
        const float kCanopyEdge = 0.5 / kNormNoise;
        float hard = smoothstep(1.0 - forest - kCanopyEdge,
                                1.0 - forest + kCanopyEdge, mottle);
        // AND THE DETAIL IS REMOVED, NEVER SUBSTITUTED (the law this shader's
        // header states). Once a patch is smaller than the pixel that would
        // show it, the blotching fades to the plain fraction instead of
        // being point-sampled into a crawling fizz — the mean is identical,
        // so a ridge forty kilometres out simply goes smooth.
        float px = max(fwidth(vWorld.x), fwidth(vWorld.z));
        float cov = mix(forest, hard, resolved(px, kPatchFreq));
        vec3 canopy = (kCoverFresh[kCoverCanopy]
                       + kCoverWorn[kCoverCanopy]) * 0.5;
        base = mix(base, canopy, cov);
    }

    // ONE LAW OF LIGHT, AND IT IS THE SAME CALL THE NEAR GROUND MAKES.
    // This used to be a hand-written `base * (ambient + sunColor * ndl)`, and
    // that was not a simplification — it was a SECOND law: it missed the
    // cloud field (peak dimming 0.62, so the far world came out up to 2.6×
    // brighter than the near one under the same cloud) and the relief march
    // (a massif's own shadow, which the near ground has and the far one did
    // not). Both differences land exactly on the composite's rim, which is
    // the one join this shader's header promises is invisible.
    //
    // `shadow = 1.0` is the honest argument, not a shortcut: the object
    // shadow map reaches 1024 m (kShadowFarRadiusM) and the far sheet's hole
    // is 1536 m, so the sheet begins precisely where that map ends and there
    // is nothing out there casting into it. The other two members of the
    // visibility law limit themselves: `terrain_visibility` marches only
    // inside the height field's own domain and returns 1 beyond it, so the
    // coarse ring needs no branch of its own.
    vec3 n = normalize(vNormal);
    float ndl = max(dot(n, normalize(pc.sunDir.xyz)), 0.0);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl,
                           /*shadow=*/1.0, vWorld);

    outColor = vec4(aerial_perspective(col, vWorld), 1.0);
}
