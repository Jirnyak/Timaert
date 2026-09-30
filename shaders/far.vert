#version 450
// THE FAR WORLD'S GROUND (CANON S18.1) — vertex stage, and it has NO INPUTS.
//
// The ground out here is a FIELD: a sheet of heights in metres over a regular
// lattice, plus the material ordinal of the cell each point stands in. The
// triangles are a function of that lattice alone — every ring of the ladder is
// the SAME lattice at its own spacing (sub/far_mesh.h) — so geometry stopped
// being data: one index buffer serves every ring that will ever exist, and a
// vertex is derived from `gl_VertexIndex` and two images.
//
// WHAT THIS REPLACED: 296 450 vertices of 28 bytes and an index buffer rebuilt
// beside them — 15.2 MB memcpy'd into one host-mapped allocation on every
// macro cell the player crossed, while frame N−1 could still be reading it as
// vertex input (M-122's first half). The field is 1.43 MB and crosses through
// the staging arena, behind a barrier that orders it after that frame's reads.
//
// Still the same world-space the composite lives in (metres, Y absolute), so
// the far sheet and the near mesh need no second basis and the camera is one
// camera. And still no UV: there is no tile material grid out here.
layout(set = 1, binding = 0) uniform sampler2D uFarHeight;   // metres
layout(set = 1, binding = 1) uniform sampler2D uFarMaterial; // ordinal / 255

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    vec4 camPos;
    vec4 shore;
    // THE RING, in three numbers: x = its spacing in metres, y = the lattice's
    // half-width in points, z = its first row in the atlas. Nothing else
    // distinguishes one ring from another — which is exactly why they share
    // everything else, down to the triangles.
    vec4 ring;
} pc;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorld;
// FLAT, and it is the whole fix. A material id is an ORDINAL into a table, so
// interpolating it manufactures rows nobody authored: between meadow(4) and
// rock(11) lie desert(6) and steppe(7), both sandy-yellow, and between
// meadow(4) and waterbed(13) lies shore(10) as well. That is the yellow thread
// the owner photographed along every far biome border and every far coastline
// — confirmed 2026-09-30 by painting fractional ordinals magenta in a live
// run: the threads went magenta, all of them.
//
// The fetch below is NEAREST for the same reason and by the same mechanism
// (`texelFetch`, which has no filter to get wrong): the average of two
// ordinals is a third material. What blends is the COLOUR, over the fragment,
// where blending is legal. Prose is not a mechanism; `flat` and `texelFetch`
// are.
layout(location = 2) flat out float vMaterial;

void main() {
    float stepM = pc.ring.x;
    int   n     = int(pc.ring.y);
    int   row   = int(pc.ring.z);
    int   dim   = 2 * n + 1;

    // THE VERTEX INDEX IS AN ADDRESS. Below dim² it is a lattice point; at or
    // above it, the SKIRT TWIN of that same point — the curtain that closes an
    // open edge of the ring, hanging straight down from it. No list and no
    // duplicated data: the index itself says which of the two, and the drop is
    // one subtraction.
    int  v      = gl_VertexIndex;
    bool bottom = v >= dim * dim;
    int  li     = bottom ? v - dim * dim : v;
    int  ix     = li % dim;
    int  iz     = li / dim;

    // The sheet carries ONE RING OF MARGIN so that a rim point has neighbours
    // on both sides and owns a real slope; lattice point (ix, iz) therefore
    // sits at texel (ix+1, iz+1) of its ring's row band.
    ivec2 base = ivec2(ix + 1, row + iz + 1);
    float h    = texelFetch(uFarHeight, base, 0).r;

    // THE NORMAL IS THE SLOPE OF THE FIELD, not a stored attribute — central
    // differences over the very neighbours the margin exists to provide. A
    // stored normal that disagreed with the height it stands on would light a
    // mountain that is not there, and only a moving sun would ever show it.
    // A skirt carries the normal of the point it hangs from, so the curtain is
    // lit as the GROUND it closes rather than as a wall.
    float hx0 = texelFetch(uFarHeight, base + ivec2(-1, 0), 0).r;
    float hx1 = texelFetch(uFarHeight, base + ivec2( 1, 0), 0).r;
    float hz0 = texelFetch(uFarHeight, base + ivec2(0, -1), 0).r;
    float hz1 = texelFetch(uFarHeight, base + ivec2(0,  1), 0).r;
    float dx  = (hx1 - hx0) / (2.0 * stepM);
    float dz  = (hz1 - hz0) / (2.0 * stepM);
    float inv = 1.0 / sqrt(dx * dx + dz * dz + 1.0);
    vNormal = vec3(-dx * inv, inv, -dz * inv);

    // A QUARTER OF A STEP of drop, and it is metres because every edge the
    // curtain closes is STITCHED first (the bake arrives at the inner ground's
    // exact height over a band). A skirt is not a way to hide a disagreement;
    // it hides the numerical slop left after one has been resolved. Sized as a
    // disagreement it becomes visible ITSELF — at four steps it hung 128 m on
    // the fine ring and 512 m on the coarse, and the owner photographed it as
    // yellow bands along every boundary.
    vec3 p = vec3(float(ix - n) * stepM,
                  h - (bottom ? 0.25 * stepM : 0.0),
                  float(iz - n) * stepM);

    vWorld = p;
    vMaterial = texelFetch(uFarMaterial, base, 0).r * 255.0;
    gl_Position = pc.mvp * vec4(p, 1.0);
}
