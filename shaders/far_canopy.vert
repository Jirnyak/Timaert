#version 450
// THE FAR FOREST AS A MASS OVER THE GROUND — the vertex stage.
//
// The far sheet draws the LAND (far.vert). This draws what STANDS on it: one
// cushion of canopy, lifted off the same lattice, on the same indices, out of
// the same two images. Nothing here is a second world — it is the ground's own
// field read a second time and raised.
//
// WHY A CUSHION AND NOT A TINT. Colour mixed into the ground is a flat answer:
// from a ridge a forest is a BODY with a top, a rim and a shadow side, and the
// owner named exactly that («массивы такие зелёные типа облаков … не текстура
// плоская»). A cushion is the cheapest honest body: it costs no new buffer, no
// new atlas and no new index — only a second draw over geometry that already
// exists.
//
// AND IT DOES NOT TOUCH THE RELIEF (owner, 2026-10-03: «лес вообще не должен
// никак влиять на рельеф»). The land keeps its own height; the canopy is a
// separate surface ABOVE it, so the composite's rim has nothing new to agree
// with and the join the previous slices fought for stays exactly as it was.
#include "surface_lib.glsl"

layout(set = 1, binding = 0) uniform sampler2D uFarHeight;   // metres
layout(set = 1, binding = 2) uniform sampler2D uFarCover;    // cover fraction

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    vec4 camPos;
    vec4 shore;
    vec4 ring;      // x = spacing (m), y = lattice half, z = first atlas row
    vec4 origin;    // xz = window origin in ABSOLUTE metres, w = world seed
} pc;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out vec3  vWorld;
layout(location = 2) out float vCover;

// HOW TALL A FOREST STANDS, in metres. One number for now and deliberately so:
// the biome table already carries a per-biome span (BiomeConfig
// treeMinHeightM/treeMaxHeightM, 6–10 m on tundra to 13–20 in the tropics) and
// this is its middle. Making it per-biome is a SECOND LAYER of the cover atlas
// — the axis exists and is empty — and that is the next step, not this one.
const float kCanopyHeightM = 16.0;

// Below this fraction a place has scattered trees, not a wood, and a cushion
// drawn over them would be a green film on open ground. It is the same shape
// of statement the macro map makes with the forest class line, one step lower:
// the class line (0.5) says "this cell IS forest", this says "there is enough
// here to read as mass at all".
const float kCanopyFloor = 0.12;

// THE BLOB: how the top of a wood actually sits. One patch per quarter cell
// (the field itself carries nothing finer — it is authored per 1024 m cell and
// spread), two octaves at the ladder's lacunarity so it is cloud rather than
// corrugation, and NOT a sine anywhere: `wnoise` is the mixer the whole world
// already uses, chosen in surface_lib.glsl precisely because sin() degenerates
// into moire on part of the driver population.
//
// AND ITS SIZE FOLLOWS THE RING, by the same sampling law the rings themselves
// exist for: a lattice carries a wavelength only if it spans at least TWO of
// its steps. A 256 m blob is finer than one cell of the coarse ring (512 m),
// so asking for it there would not produce lumps — it would produce noise the
// geometry cannot express, lumpy near and glass-smooth far with a seam between
// the two. Clamped UP to twice the spacing instead: ring 0 and 1 carry the
// authored 256 m, ring 2 carries 1024 m, and what the far eye loses is the
// small stuff it could not resolve anyway. Detail is REMOVED, never
// substituted — the same sentence the far ground's own octaves live by.
float blob_patch_m(float stepM) {
    return max(256.0, 2.0 * stepM);
}

// The canopy's own top at one lattice point, in metres above sea — ground plus
// however much wood stands there. Written as a function because the NORMAL
// below needs it at four neighbours, and a normal derived from anything other
// than the surface it shades lights a shape that is not there.
float canopy_top_m(ivec2 texel, vec2 worldXZ, float patchM) {
    float ground = texelFetch(uFarHeight, texel, 0).r;
    float cover  = texelFetch(uFarCover, texel, 0).r;
    // Smooth to zero at the floor, so the rim of a wood SETTLES onto the land
    // instead of ending in a cliff of nothing — and so the fragment that
    // discards there is discarding a surface already at ground level.
    float mass = smoothstep(kCanopyFloor, 1.0, cover);
    vec2  q    = pc.origin.xz + worldXZ;
    float blob = wnoise(q, 1.0 / patchM) * 0.65
               + wnoise(q, 3.0 / patchM) * 0.35;
    // The fluctuation is MULTIPLICATIVE on the height, not additive on the
    // ground: a thin wood is a low wood, and a clearing has no canopy to
    // fluctuate. 0.45..1.35 of the mean — enough that the top reads as lumpy
    // from a ridge, not so much that a stand looks like two different woods.
    return ground + kCanopyHeightM * mass * (0.45 + 0.9 * blob);
}

void main() {
    float stepM = pc.ring.x;
    int   n     = int(pc.ring.y);
    int   row   = int(pc.ring.z);
    int   dim   = 2 * n + 1;

    // SURFACE POINTS ONLY. The canopy draw is shortened to the lattice's
    // surface run (far_mesh.h build_far_lattice_indices reports it), so no
    // skirt twin ever reaches this stage and the index is simply a point.
    int ix = gl_VertexIndex % dim;
    int iz = gl_VertexIndex / dim;

    ivec2 base   = ivec2(ix + 1, row + iz + 1);
    vec2  xz     = vec2(float(ix - n) * stepM, float(iz - n) * stepM);
    float patchM = blob_patch_m(stepM);
    float top    = canopy_top_m(base, xz, patchM);

    // THE CUSHION'S OWN SLOPE. Central differences over the same neighbours
    // the margin exists to provide — of the CANOPY's top, not the ground's, so
    // a lumpy top is lit as a lumpy top. This is the whole difference between
    // a mass and a sheet of paint: without it the sun shades the wood exactly
    // as it shades the field beside it.
    float hx0 = canopy_top_m(base + ivec2(-1, 0), xz - vec2(stepM, 0.0), patchM);
    float hx1 = canopy_top_m(base + ivec2( 1, 0), xz + vec2(stepM, 0.0), patchM);
    float hz0 = canopy_top_m(base + ivec2(0, -1), xz - vec2(0.0, stepM), patchM);
    float hz1 = canopy_top_m(base + ivec2(0,  1), xz + vec2(0.0, stepM), patchM);
    float dx  = (hx1 - hx0) / (2.0 * stepM);
    float dz  = (hz1 - hz0) / (2.0 * stepM);
    float inv = 1.0 / sqrt(dx * dx + dz * dz + 1.0);
    vNormal = vec3(-dx * inv, inv, -dz * inv);

    vec3 p  = vec3(xz.x, top, xz.y);
    vWorld  = p;
    vCover  = texelFetch(uFarCover, base, 0).r;
    gl_Position = pc.mvp * vec4(p, 1.0);
}
