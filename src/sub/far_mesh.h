// THE FAR WORLD'S GROUND, as a FIELD — CANON S18.1.
//
// The player stands in a 3×3 window of 3072 m and the world is a torus 1049 km
// across. Beyond the window the composite simply ends, and until this exists
// what stands there is sky. This builds the ground that belongs there.
//
// WHAT MAKES IT THE SAME WORLD, and not a backdrop that resembles one:
//
//   · HEIGHT is `far_height01` — the near generator's own manifold and its own
//     ridge function, stopped at the coarse octaves (base_generator.h). Not
//     similar noise: the same function, truncated. «Гора, которую видно с
//     тридцати километров, обязана быть той горой, к которой придёшь.»
//   · COLOUR is not computed here at all. A lattice point carries its MATERIAL
//     ORDINAL and the shader takes the midpoint of that row's two authored
//     constituents — the pair the near ground's mixture converges to
//     (ground_surface.glsl
//     kGroundFresh/kGroundWorn). So the join at the composite's edge is
//     invisible BY CONSTRUCTION rather than by tuning, and no second copy of
//     the ground table is ever made. That table lives in GLSL and only there;
//     mirroring it in C++ to colour the ground would have created exactly the
//     "two answers to one question" the canon forbids.
//   · GEOMETRY IS NOT HERE, and that is the shape of the thing: this file
//     evaluates the law into two FIELDS over a lattice, and the vertex stage
//     derives a vertex from `gl_VertexIndex` and those fields (far.vert).
//     Nothing about a triangle depends on the place, so the triangles are
//     built once for the whole ladder (`build_far_lattice_indices`) and never
//     again.
//   · The air does the rest. There is no draw-distance constant here and there
//     must not be one (S18.1): the mesh covers what it covers, and what the
//     eye actually sees is decided by `aerial_perspective` — the plain
//     dissolves on its own, the summit outlives it on its own.
//
// The grid is CAMERA-CENTRED. LOD that bakes around the entry point breaks a
// couple of kilometres into a walk, which is exactly where the silhouette is
// most noticeable (the owner's ruling, S18.1).
#pragma once

#include "sub/base_generator.h"
#include "sub/height.h"
#include "sub/map_data.h"
#include "sub/material.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace sm::sub {

// ── THE LADDER OF RINGS, AND WHY IT IS ONE LATTICE ────────────────────────
// A ring is the far ground at one spacing, and the finished thing is several
// of them. They were born as six loose numbers in the renderer (32 m over
// 6144 m with a 1536 m hole; 128 m over 24576 m with a 6144 m hole), and the
// arithmetic on them says something the numbers did not: divide each ring's
// span and hole by its OWN spacing and every ring gives the SAME pair, 192
// and 48. Which is to say the rings are not three grids — they are ONE
// lattice, read at three scales.
//
// That is not a tidy coincidence to admire, it is the licence for the whole
// of this slice: if the lattice is one, its triangles are one, and the index
// buffer is built ONCE for every ring that will ever exist instead of being
// rewritten per ring per crossing. The geometry stops being data.
//
// The nesting is what makes the ladder seamless: ring r's HOLE is exactly
// ring r−1's SPAN, so each ring begins where the finer one ends and the
// innermost hole is the composite itself. All four facts below are pinned by
// the compiler rather than by prose, because prose is what let the six loose
// numbers look independent for as long as they did.
constexpr int kFarLatticeHalf  = 192;  // lattice points per side from centre
constexpr int kFarHoleQuadHalf = 48;   // the hole's half-width, in QUADS
constexpr int kFarRing0StepM   = 32;   // the innermost spacing, metres
constexpr int kFarRingRatio    = 4;    // step, span AND hole all ×4 per ring
// HOW MANY RINGS — and it is the only knob of the ladder. Two is what the
// probe drew and what the air already dissolves; the third is a measured
// decision (Ш8), not a slot left open.
constexpr int kFarRings        = 2;

constexpr int kFarLatticeDim = 2 * kFarLatticeHalf + 1;   // points per row
// ONE RING OF MARGIN, which is the field's own (see FarHeightSheet): a rim
// point needs neighbours on both sides to own a real slope.
constexpr int kFarSheetDim   = kFarLatticeDim + 2;

// A ring's own three numbers, from its index alone.
constexpr int far_ring_step_m(int ring) {
    int s = kFarRing0StepM;
    for (int r = 0; r < ring; ++r) s *= kFarRingRatio;
    return s;
}
constexpr float far_ring_half_span_m(int ring) {
    return float(kFarLatticeHalf * far_ring_step_m(ring));
}
constexpr float far_ring_hole_half_m(int ring) {
    return float(kFarHoleQuadHalf * far_ring_step_m(ring));
}
// How much ground the ladder covers in total — REPORTED by the ladder, never
// authored beside it. A draw distance is forbidden outright (S18.1) and this
// is not one: it is how much sheet exists, and what is SEEN is the air's
// business. It moves only when a ring is added.
constexpr float far_ladder_half_span_m() {
    return far_ring_half_span_m(kFarRings - 1);
}

// EVERY RING IS THE SAME LATTICE, stated so the compiler can refuse a ring
// that is not. A ring whose span or hole is not its own spacing times these
// two integers would need its own triangles, and the one index buffer below
// would silently draw it wrong.
constexpr bool far_ladder_is_one_lattice() {
    for (int r = 0; r < kFarRings; ++r) {
        const int step = far_ring_step_m(r);
        if (far_ring_half_span_m(r) != float(kFarLatticeHalf * step))
            return false;
        if (far_ring_hole_half_m(r) != float(kFarHoleQuadHalf * step))
            return false;
    }
    return true;
}
// AND THE RINGS NEST: each one begins where the finer one ends.
constexpr bool far_ladder_nests() {
    for (int r = 1; r < kFarRings; ++r) {
        if (far_ring_hole_half_m(r) != far_ring_half_span_m(r - 1))
            return false;
    }
    return true;
}
static_assert(kFarRings >= 1, "a ladder with no rings is not a ladder");
static_assert(far_ladder_is_one_lattice(),
              "every far ring must be the SAME lattice at its own spacing — "
              "otherwise its triangles are not the shared ones");
static_assert(far_ladder_nests(),
              "each far ring's hole must be the previous ring's span — "
              "otherwise the ladder has a gap or an overlap in it");
// THE INNERMOST HOLE IS THE COMPOSITE, and that is why 48 is 48: the near
// ground reaches half of the 3×3 window, so the finest ring's hole is that
// same distance measured in its own steps. Change the window and this fails
// here rather than as a brown wall at the join.
static_assert(kFarHoleQuadHalf * kFarRing0StepM == kFullSize / 2,
              "the finest ring's hole must be exactly the composite's reach");

// ── WHICH QUADS A RING EMITS, AS A FORMULA ────────────────────────────────
// The hole is a square block of quads in the middle of the lattice, and
// membership in it is arithmetic — there is no computed set of emitted quads
// and there must not be one, because the whole point is that every ring's
// pattern is the same pattern and therefore never stored.
//
// This is the METRE rule of the old builder, divided through by the spacing:
// a quad was skipped when both of its X corners and both of its Z corners
// lay within `holeHalfM`, and with holeHalfM = holeQuadHalf·stepM that is
// exactly the two integer windows below. `far_mesh_test` holds the two forms
// against each other, because a boundary moved by one quad is a crack.
constexpr bool far_axis_in_hole(int i, int latticeHalf, int holeQuadHalf) {
    const int a = i - latticeHalf;
    const int b = i + 1 - latticeHalf;
    const int absA = a < 0 ? -a : a;
    const int absB = b < 0 ? -b : b;
    return absA <= holeQuadHalf && absB <= holeQuadHalf;
}
constexpr bool far_quad_emitted(int ix, int iz, int latticeHalf,
                                int holeQuadHalf) {
    if (holeQuadHalf <= 0) return true;
    return !(far_axis_in_hole(ix, latticeHalf, holeQuadHalf)
             && far_axis_in_hole(iz, latticeHalf, holeQuadHalf));
}

// THE TRIANGLES OF THE LATTICE — the ring's quads and the skirts that close
// their open edges. Built ONCE for the whole ladder and never again: nothing
// in here knows a spacing, a span or a place, so there is nothing for a
// crossing to invalidate.
//
// VERTEX NUMBERING IS AN ADDRESS, NOT A TABLE. A lattice point (ix, iz) is
// vertex `iz·dim + ix`; its SKIRT TWIN — the same point lowered by the
// curtain's drop — is that number plus dim². So a skirt needs no list of
// which points it hangs from and no duplicated data: the index itself says
// "the bottom of that point". A vertex stage given nothing but its index can
// answer both halves with arithmetic, which is what lets the vertex buffer
// stop existing.
//
// ONE RULE FOR EVERY EDGE, exactly as before: any quad edge with no emitted
// quad on the other side gets a skirt — the lattice's outer rim and the
// border of the hole alike. Two seams, no special cases.
inline void build_far_lattice_indices(std::vector<std::uint32_t>& out,
                                      int latticeHalf, int holeQuadHalf) {
    out.clear();
    if (latticeHalf <= 0) return;                  // no lattice, no triangles
    const int dim = 2 * latticeHalf + 1;
    const std::uint32_t skirtBase = std::uint32_t(dim) * std::uint32_t(dim);
    const auto emitted = [&](int ix, int iz) {
        if (ix < 0 || iz < 0 || ix + 1 >= dim || iz + 1 >= dim) return false;
        return far_quad_emitted(ix, iz, latticeHalf, holeQuadHalf);
    };
    // The surface first, then the curtains — two passes so the skirt run is
    // contiguous at the tail and a later slice can drop it by shortening the
    // draw rather than by rebuilding the buffer (Ш3).
    for (int iz = 0; iz + 1 < dim; ++iz) {
        for (int ix = 0; ix + 1 < dim; ++ix) {
            if (!emitted(ix, iz)) continue;
            const std::uint32_t a = std::uint32_t(iz * dim + ix);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + std::uint32_t(dim);
            const std::uint32_t d = c + 1;
            out.push_back(a); out.push_back(c); out.push_back(b);
            out.push_back(b); out.push_back(c); out.push_back(d);
        }
    }
    const auto hang = [&](std::uint32_t t0, std::uint32_t t1) {
        const std::uint32_t b0 = t0 + skirtBase;
        const std::uint32_t b1 = t1 + skirtBase;
        out.push_back(t0); out.push_back(b0); out.push_back(t1);
        out.push_back(t1); out.push_back(b0); out.push_back(b1);
    };
    for (int iz = 0; iz + 1 < dim; ++iz) {
        for (int ix = 0; ix + 1 < dim; ++ix) {
            if (!emitted(ix, iz)) continue;
            const std::uint32_t a = std::uint32_t(iz * dim + ix);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + std::uint32_t(dim);
            const std::uint32_t d = c + 1;
            if (!emitted(ix, iz - 1)) hang(a, b);   // north edge
            if (!emitted(ix, iz + 1)) hang(c, d);   // south edge
            if (!emitted(ix - 1, iz)) hang(a, c);   // west edge
            if (!emitted(ix + 1, iz)) hang(b, d);   // east edge
        }
    }
}

// What the builder needs to know about one macro cell. All of it comes from
// doors that already exist — the caller resolves the cell once and fills this,
// because a far grid reads many tiles out of every cell and asking per tile
// would re-derive the same cell for every vertex.
struct FarCellColumn {
    float        skel01   = 0.0f;   // skeleton_cell_height01 of the cell
    float        peak01   = 0.0f;   // skeleton_cell_peak01 of the cell
    float        ridgeW   = 0.0f;   // 1 on a mountain cell, 0 elsewhere
    // The three columns the ground's own detail is scaled by — the same ones
    // the near generator blends (base_generator.cpp macroGradient /
    // heightScale / mountainScale). Without them the far lowland is a
    // billiard table, which is exactly how the first probe looked.
    float        gradient01  = 0.0f;
    float        heightScale = 0.0f;
    float        mtnScale    = 0.0f;
    // (A `waterW` column stood here and fed the seabed ceiling. Both are gone:
    // the ceiling was a second answer to «where is the water», the near world
    // answers it by HEIGHT alone, and a column with no reader is a column that
    // must not exist — DOD 9. Wetness still reaches the far sheet, through the
    // only door that carries it honestly: the cell's own remapped manifold,
    // which `skeleton_cell_height01` already put under the plane.)
    std::uint8_t material = 0;      // biome_ground_materials()[biome]
};

// The cell grid the builder reads: (2R+1)² columns, row-major, centred on the
// camera's macro cell. The caller owns the gather — it is the only part that
// needs the macro world.
struct FarCellGrid {
    int                        radiusCells = 0;
    std::vector<FarCellColumn> cells;      // (2R+1)²
    // THE WORLD'S SEA PLANE, normalised. One per GRID, not per cell: the plane
    // is a property of the world, and the far sheet has to floor its seabed
    // against the very same number the near ground remaps about — otherwise the
    // horizon's coastline sits at a different height than the one you walk to,
    // which is the one thing CANON S18.1 forbids outright.
    float                      seaLevel = WATER_LEVEL;

    int span() const { return 2 * radiusCells + 1; }
    bool live() const {
        return radiusCells > 0
            && cells.size() == std::size_t(span()) * std::size_t(span());
    }
    // Clamped fetch: the rim repeats outward rather than wrapping, because the
    // grid is a WINDOW on the torus and its own edge is not a seam of the
    // world — the world's seam is in the antipode, where the air ate it long
    // ago (S18.1).
    const FarCellColumn& at(int gx, int gy) const {
        const int n = span();
        const int x = gx < 0 ? 0 : (gx >= n ? n - 1 : gx);
        const int y = gy < 0 ? 0 : (gy >= n ? n - 1 : gy);
        return cells[std::size_t(y) * std::size_t(n) + std::size_t(x)];
    }
};

namespace detail {

// Bilinear over the four nearest cell CENTRES — the same convention the near
// generator blends its 3×3 columns by (base_generator.cpp: centres sit at the
// half-cell). `fx, fy` are in CELL units measured from the grid's origin cell.
inline void far_cell_weights(float fx, float fy, int& x0, int& y0,
                             float& tx, float& ty) {
    const float gx = fx - 0.5f;
    const float gy = fy - 0.5f;
    x0 = int(std::floor(gx));
    y0 = int(std::floor(gy));
    tx = gx - float(x0);
    ty = gy - float(y0);
}

} // namespace detail

// Build the far ground around a camera standing at macro cell (camCx, camCy).
//
// `stepM` is the vertex spacing in metres and `halfSpanM` how far the sheet
// reaches; both are the CALLER's business — this is a probe, and the rings of
// the finished thing will choose them per ring. `worldCellsX` closes the
// noise on the world, exactly as the near generator's does.
// `holeHalfM` is THE HOLE UNDER THE COMPOSITE, and it is not an optimisation.
// The far sheet is the coarse answer — no detail octaves, no settlement table —
// so under the camera it misses the near ground by tens of metres. Measured on
// the first frame that ever drew it: the sheet stood at 1501 m where the ground
// the player was standing on was 1480.9 m, which put the camera UNDERNEATH it
// and filled the whole sky with its underside. The near ground is the same
// ground with its octaves back, so where the composite exists the far sheet
// must simply not be. 0 = no hole (a bare fixture with no composite).
// THE FAR GROUND AT ONE POINT, in metres — the law a ring of a given spacing
// would answer with here. Pulled out of the builder because a ring needs to
// ask what its INNER neighbour says in order to arrive at it: the two carry
// different octaves by construction (that is what a LOD ladder IS), so they
// have to be stitched rather than assumed to agree.
inline float far_point_height_m(const FarCellGrid& grid, int camCx, int camCy,
                                float wx, float wz, int worldCellsX,
                                int stepM) {
    const float cellSpanM = float(kCellSize) * 1.0f;   // a tile is a metre
    const float worldTiles =
        float(worldCellsX > 0 ? worldCellsX : 0) * float(kCellSize);
    const float fx = wx / cellSpanM + float(grid.radiusCells);
    const float fy = wz / cellSpanM + float(grid.radiusCells);
    int x0 = 0, y0 = 0; float tx = 0.0f, ty = 0.0f;
    detail::far_cell_weights(fx, fy, x0, y0, tx, ty);
    const FarCellColumn& c00 = grid.at(x0,     y0);
    const FarCellColumn& c10 = grid.at(x0 + 1, y0);
    const FarCellColumn& c01 = grid.at(x0,     y0 + 1);
    const FarCellColumn& c11 = grid.at(x0 + 1, y0 + 1);
    const float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty);
    const float w01 = (1 - tx) * ty,       w11 = tx * ty;
    const float skel = c00.skel01 * w00 + c10.skel01 * w10
                     + c01.skel01 * w01 + c11.skel01 * w11;
    const float peak = c00.peak01 * w00 + c10.peak01 * w10
                     + c01.peak01 * w01 + c11.peak01 * w11;
    const float ridge = c00.ridgeW * w00 + c10.ridgeW * w10
                      + c01.ridgeW * w01 + c11.ridgeW * w11;
    const float grad = c00.gradient01 * w00 + c10.gradient01 * w10
                     + c01.gradient01 * w01 + c11.gradient01 * w11;
    const float hs = c00.heightScale * w00 + c10.heightScale * w10
                   + c01.heightScale * w01 + c11.heightScale * w11;
    const float ms = c00.mtnScale * w00 + c10.mtnScale * w10
                   + c01.mtnScale * w01 + c11.mtnScale * w11;
    const int rawX = camCx * kCellSize + int(std::floor(wx));
    const int rawZ = camCy * kCellSize + int(std::floor(wz));
    const int gx = worldTiles > 0.0f ? wrapi(rawX, int(worldTiles)) : rawX;
    const int gz = worldTiles > 0.0f ? wrapi(rawZ, int(worldTiles)) : rawZ;
    // WHERE THE WATER IS, IS A QUESTION ABOUT HEIGHT, and the far world does
    // not get to answer it a second way. The near generator decides it per
    // TILE and only by height — `water[i] = heightmap[i] < waterLevel`
    // (gens/kit/tiles.cpp sync_water_tiles_from_heightmap) — so a shoreline is
    // wherever the ground crosses the one plane the world has, on both banks
    // and at both scales. The water cell's own manifold already sits under
    // that plane by construction (skeleton_cell_height01 remaps a wet cell
    // through t²·seaLevel), which is the whole of what the far sheet owes.
    //
    // A CEILING KEYED ON THE CELL'S FLAG used to stand here, and it did not
    // fail by not engaging — it engaged and FLATTENED. Inside a water cell the
    // weight below is a constant, so every clamped point landed on the same
    // number and the seabed became a level shelf of dry land standing on the
    // sea; the owner photographed it as a grey-tan plateau along every far
    // shore. Measured at its removal: 62 168 of 589 760 neighbouring pairs
    // over water came out BIT-IDENTICAL, against 0 of 595 968 over land.
    // ЗАКОН КЛАМПА, exactly — the clamp was hiding the absence of a rule
    // rather than enforcing one, and the rule it hid was already written next
    // door. `far_mesh_test` section 7 holds the line now.
    return height_m(far_height01(gx, gz, skel, peak, ridge, worldTiles,
                        grid.seaLevel, grad, hs, ms,
                        2.0f * float(stepM)));
}

// ── THE FAR GROUND AS A FIELD, WHICH IS WHAT IT ACTUALLY IS ───────────────
// A sheet of heights over a regular lattice, in METRES, centred on the
// window's macro cell. Geometry is a CONSUMER of this, never its author: the
// same field feeds a CPU mesh today and a GPU height texture tomorrow, and
// there is exactly one place the law is evaluated either way.
//
// ONE RING OF MARGIN is part of the field, not of its user. A rim sample needs
// neighbours on BOTH sides to own a real slope; without it the outermost row
// is lit by a one-sided guess and draws a bright frame around the world.
struct FarHeightSheet {
    int                dim       = 0;      // lattice points per side (2n+1)
    int                stepM     = 0;      // spacing in metres
    float              halfSpanM = 0.0f;   // reach from the centre, metres
    std::vector<float> m;                  // (dim+2)², row-major, WITH margin

    bool live() const {
        return dim > 0 && stepM > 0
            && m.size() == std::size_t(dim + 2) * std::size_t(dim + 2);
    }
    // Lattice coordinates run 0..dim-1; −1 and dim address the margin.
    float at(int ix, int iz) const {
        return m[std::size_t(iz + 1) * std::size_t(dim + 2)
                 + std::size_t(ix + 1)];
    }
};

// `innerHeightM(wx, wz)` — the height of whatever ground lies INSIDE this
// ring: the composite for the first ring, the previous ring for every one
// after it. Negative where there is none. It is
// what stitches the two grounds together, and without it the join is a CLIFF:
// ring 0 at 32 m carries wavelengths down to 64 m, the composite's 16 m mesh
// carries them down to 32 m, so the two disagree by metres at the rim however
// honestly both are derived. Caught by the owner's eyes: «3×3 норм, а дальше
// разрыв и потом норм лод уже».
//
// `blendBandM` is how far out that disagreement is dissolved — the far ground
// leaves the composite's exact height and arrives at its own over this
// distance. The march apron feathers into its skeleton the same way and for
// the same reason (vk_renderer_3d.cpp): a raw step at a boundary reads as a
// phantom cliff, and half a macro cell is the generator's own blend scale.
template <class HeightSampler>
inline void bake_far_sheet(FarHeightSheet& out, const FarCellGrid& grid,
                           int camCx, int camCy, int stepM, float halfSpanM,
                           int worldCellsX, float holeHalfM,
                           const HeightSampler& innerHeightM,
                           float blendBandM) {
    out.m.clear();
    out.dim = 0;
    out.stepM = 0;
    out.halfSpanM = 0.0f;
    if (!grid.live() || stepM <= 0 || halfSpanM <= 0.0f) return;

    const int n   = int(halfSpanM) / stepM;                // per side
    const int dim = 2 * n + 1;                             // points per row
    out.dim = dim;
    out.stepM = stepM;
    out.halfSpanM = float(n * stepM);

    // Height of one point, in METRES, from the world's own generator — and
    // then STITCHED to whatever ground lies inside this ring.
    const auto height_m = [&](float wx, float wz) {
        const float farM = far_point_height_m(grid, camCx, camCy, wx, wz,
                                              worldCellsX, stepM);
        if (blendBandM <= 0.0f || holeHalfM <= 0.0f) return farM;
        // How far this point lies OUTSIDE the ground it must agree with,
        // Chebyshev — that ground is a square and so is the band around it.
        const float outX = std::max(0.0f, std::fabs(wx) - holeHalfM);
        const float outZ = std::max(0.0f, std::fabs(wz) - holeHalfM);
        const float out = std::max(outX, outZ);
        if (out >= blendBandM) return farM;
        const float nearM = innerHeightM(wx, wz);
        if (nearM < 0.0f) return farM;      // nothing inside to agree with
        const float t = out / blendBandM;
        // Smoothstep, not a straight lerp: a C1 arrival means the band has no
        // crease of its own at either end, which is the whole point of it.
        const float w = t * t * (3.0f - 2.0f * t);
        return nearM * (1.0f - w) + farM * w;
    };

    // HEIGHTS ONCE, NOT FIVE TIMES. A point needs its own height and its four
    // neighbours' to own a normal, and asking the generator for each of them
    // per point costs five evaluations where one will do: the neighbour a
    // point wants is the point next door. Measured before this: 52 ms to build
    // the sheet, on the crossing, against a seam of 2.2 ms — the kind of
    // number that decides whether a probe is even allowed to exist.
    const int mDim = dim + 2;
    out.m.assign(std::size_t(mDim) * std::size_t(mDim), 0.0f);
    for (int iz = 0; iz < mDim; ++iz) {
        const float wz = float((iz - 1 - n) * stepM);
        for (int ix = 0; ix < mDim; ++ix) {
            const float wx = float((ix - 1 - n) * stepM);
            out.m[std::size_t(iz) * std::size_t(mDim) + std::size_t(ix)] =
                height_m(wx, wz);
        }
    }
}

// ── THE MATERIAL IS A FIELD TOO, AND THAT IS THE WHOLE OF THIS DOOR ───────
// It rode as a vertex attribute for as long as there were vertices. It is not
// an attribute of a vertex though — it is a property of the PLACE, sampled at
// the lattice's points exactly as the height is, and giving it the same shape
// as the height sheet means one addressing convention for both instead of two.
//
// NEAREST, NOT BLENDED, and that has not changed: a material id is an ORDINAL
// into a table, so the average of two is a third material nobody authored —
// the yellow thread the owner photographed along every far biome border. What
// blends is the COLOUR, over the fragment, where blending is legal.
//
// The margin ring exists only so the two sheets are addressed alike; nothing
// reads a material's neighbours today. It costs 2·(dim+1) bytes and buys the
// absence of a second convention.
struct FarMaterialSheet {
    int                       dim = 0;   // lattice points per side (2n+1)
    std::vector<std::uint8_t> id;        // (dim+2)², row-major, WITH margin

    bool live() const {
        return dim > 0
            && id.size() == std::size_t(dim + 2) * std::size_t(dim + 2);
    }
    std::uint8_t at(int ix, int iz) const {
        return id[std::size_t(iz + 1) * std::size_t(dim + 2)
                  + std::size_t(ix + 1)];
    }
};

inline void bake_far_material_sheet(FarMaterialSheet& out,
                                    const FarCellGrid& grid, int stepM,
                                    float halfSpanM) {
    out.id.clear();
    out.dim = 0;
    if (!grid.live() || stepM <= 0 || halfSpanM <= 0.0f) return;

    const int n   = int(halfSpanM) / stepM;
    const int dim = 2 * n + 1;
    out.dim = dim;
    const float cellSpanM = float(kCellSize) * 1.0f;       // a tile is a metre
    const int mDim = dim + 2;
    out.id.assign(std::size_t(mDim) * std::size_t(mDim), std::uint8_t(0));
    for (int iz = 0; iz < mDim; ++iz) {
        const float wz = float((iz - 1 - n) * stepM);
        const float fy = wz / cellSpanM + float(grid.radiusCells);
        for (int ix = 0; ix < mDim; ++ix) {
            const float wx = float((ix - 1 - n) * stepM);
            const float fx = wx / cellSpanM + float(grid.radiusCells);
            out.id[std::size_t(iz) * std::size_t(mDim) + std::size_t(ix)] =
                grid.at(int(std::floor(fx)), int(std::floor(fy))).material;
        }
    }
}

} // namespace sm::sub
