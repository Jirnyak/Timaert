// THE FAR WORLD'S GROUND, as geometry — CANON S18.1.
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
//   · COLOUR is not computed here at all. A vertex carries its MATERIAL ID and
//     the shader takes the midpoint of that row's two authored constituents —
//     the very pair the near ground's mixture converges to (ground_surface.glsl
//     kGroundFresh/kGroundWorn). So the join at the composite's edge is
//     invisible BY CONSTRUCTION rather than by tuning, and no second copy of
//     the ground table is ever made. That table lives in GLSL and only there;
//     mirroring it in C++ to colour a vertex would have created exactly the
//     "two answers to one question" the canon forbids.
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

// One vertex of the far ground. Position in the same WINDOW space the
// composite uses (metres, Y absolute — sub/height.h layer 2), so the far mesh
// and the near mesh live in one coordinate system and the camera needs no
// second basis. The material id rides as a float because that is what a vertex
// attribute is; the shader reads the ground table with it.
struct FarVertex {
    float px, py, pz;
    float nx, ny, nz;
    float material;
};

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
    // 1 on a water cell. A far SEABED has to lie BELOW the water plane, not on
    // it: the skeleton's water curve reaches exactly WATER_LEVEL at the
    // shoreline, so without this the far ground and the sea surface occupy the
    // same height and the result reads as "water, then land at sea level, then
    // water again" — which is what the owner photographed.
    float        waterW      = 0.0f;
    std::uint8_t material = 0;      // biome_ground_materials()[biome]
};

struct FarMesh {
    std::vector<FarVertex>     vtx;
    std::vector<std::uint32_t> idx;
    // What the build covered, in metres from the camera — reported rather than
    // assumed, so a caller (or a test) can state the truth about coverage
    // instead of restating the argument it passed in.
    float halfSpanM = 0.0f;
    int   stepM     = 0;
};

// The cell grid the builder reads: (2R+1)² columns, row-major, centred on the
// camera's macro cell. The caller owns the gather — it is the only part that
// needs the macro world.
struct FarCellGrid {
    int                        radiusCells = 0;
    std::vector<FarCellColumn> cells;      // (2R+1)²

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
// `compositeHeightM(wx, wz)` — the NEAR ground's own height at a window-space
// point, or a sampler that returns a negative number where it has none. It is
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
inline void build_far_mesh(FarMesh& out, const FarCellGrid& grid,
                           int camCx, int camCy, int stepM, float halfSpanM,
                           int worldCellsX, float holeHalfM,
                           const HeightSampler& compositeHeightM,
                           float blendBandM) {
    out.vtx.clear();
    out.idx.clear();
    out.halfSpanM = 0.0f;
    out.stepM = 0;
    if (!grid.live() || stepM <= 0 || halfSpanM <= 0.0f) return;

    const int   n         = int(halfSpanM) / stepM;        // per side
    const int   dim       = 2 * n + 1;                     // vertices per row
    const float cellSpanM = float(kCellSize) * 1.0f;       // a tile is a metre
    const float worldTiles =
        float(worldCellsX > 0 ? worldCellsX : 0) * float(kCellSize);
    out.halfSpanM = float(n * stepM);
    out.stepM = stepM;
    out.vtx.reserve(std::size_t(dim) * std::size_t(dim));

    // Height of one point, in METRES, from the world's own generator.
    const auto height_m = [&](float wx, float wz) {
        // Where this point stands, in cells from the grid's origin.
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
        const float wet = c00.waterW * w00 + c10.waterW * w10
                        + c01.waterW * w01 + c11.waterW * w11;
        // The tile this point stands on, in WORLD tile coordinates, wrapped —
        // the generator's noise closes on the world and a tile is its place.
        const int rawX = camCx * kCellSize + int(std::floor(wx));
        const int rawZ = camCy * kCellSize + int(std::floor(wz));
        const int gx = worldTiles > 0.0f ? wrapi(rawX, int(worldTiles)) : rawX;
        const int gz = worldTiles > 0.0f ? wrapi(rawZ, int(worldTiles)) : rawZ;
        // NYQUIST: this mesh may carry any octave whose wavelength is at
        // least twice its own spacing, and no shorter one — see
        // base_generator.h terrain_detail01. Halving the step doubles what the
        // ground may show, which is how detail comes BACK as you approach.
        float far01 = far_height01(gx, gz, skel, peak, ridge, worldTiles,
                                   grad, hs, ms, 2.0f * float(stepM));
        // A SEABED IS UNDER THE SEA. The ceiling comes down as the ground
        // becomes water, and it stops one kLandMargin below the plane — the
        // very margin the LAND is lifted by on the other side of the same
        // line (base_generator.h), so the two rules are one rule read from
        // both banks.
        const float wetCeil = WATER_LEVEL - kLandMargin;
        if (wet > 0.0f) {
            const float ceil01 = 2.0f * (1.0f - wet) + wetCeil * wet;
            far01 = std::min(far01, ceil01);
        }
        const float farM = far01 * kHeightScaleM;
        if (blendBandM <= 0.0f || holeHalfM <= 0.0f) return farM;
        // How far this point lies OUTSIDE the composite, Chebyshev — the
        // composite is a square and so is the band around it.
        const float outX = std::max(0.0f, std::fabs(wx) - holeHalfM);
        const float outZ = std::max(0.0f, std::fabs(wz) - holeHalfM);
        const float out = std::max(outX, outZ);
        if (out >= blendBandM) return farM;
        const float nearM = compositeHeightM(wx, wz);
        if (nearM < 0.0f) return farM;      // no composite here to agree with
        const float t = out / blendBandM;
        // Smoothstep, not a straight lerp: a C1 arrival means the band has no
        // crease of its own at either end, which is the whole point of it.
        const float w = t * t * (3.0f - 2.0f * t);
        return nearM * (1.0f - w) + farM * w;
    };

    // HEIGHTS ONCE, NOT FIVE TIMES. A vertex needs its own height and its four
    // neighbours' to get a normal, and asking the generator for each of them
    // per vertex costs five evaluations where one will do: the neighbour a
    // vertex wants is the vertex next door. Measured before this: 52 ms to
    // build the sheet, on the crossing, against a seam of 2.2 ms — the kind of
    // number that decides whether a probe is even allowed to exist.
    //
    // One ring of MARGIN so the rim's normals are real slopes rather than
    // one-sided guesses (a rim lit differently from its neighbour draws a
    // bright frame around the world).
    const int mDim = dim + 2;
    std::vector<float> h(std::size_t(mDim) * std::size_t(mDim), 0.0f);
    for (int iz = 0; iz < mDim; ++iz) {
        const float wz = float((iz - 1 - n) * stepM);
        for (int ix = 0; ix < mDim; ++ix) {
            const float wx = float((ix - 1 - n) * stepM);
            h[std::size_t(iz) * std::size_t(mDim) + std::size_t(ix)] =
                height_m(wx, wz);
        }
    }
    const auto hAt = [&](int ix, int iz) {
        return h[std::size_t(iz + 1) * std::size_t(mDim)
                 + std::size_t(ix + 1)];
    };

    for (int iz = 0; iz < dim; ++iz) {
        const float wz = float((iz - n) * stepM);
        for (int ix = 0; ix < dim; ++ix) {
            const float wx = float((ix - n) * stepM);
            FarVertex v{};
            v.px = wx;
            v.pz = wz;
            v.py = hAt(ix, iz);
            // The NORMAL from the surface itself — central differences over
            // the neighbours already computed. Not a stored field: a normal
            // that disagreed with the height it stands on would light a
            // mountain that is not there, and only a moving sun would show it.
            const float dx = (hAt(ix + 1, iz) - hAt(ix - 1, iz))
                           / (2.0f * float(stepM));
            const float dz = (hAt(ix, iz + 1) - hAt(ix, iz - 1))
                           / (2.0f * float(stepM));
            const float inv = 1.0f / std::sqrt(dx * dx + dz * dz + 1.0f);
            v.nx = -dx * inv;
            v.ny = inv;
            v.nz = -dz * inv;
            // The material of the cell this vertex stands in — NEAREST, not
            // blended: a material id is an ordinal into a table, and the
            // average of two ordinals is a third material nobody authored.
            // What blends is the COLOUR, in the shader, over the fragment,
            // where blending is legal.
            const float fx = wx / cellSpanM + float(grid.radiusCells);
            const float fy = wz / cellSpanM + float(grid.radiusCells);
            v.material = float(grid.at(int(std::floor(fx)),
                                       int(std::floor(fy))).material);
            out.vtx.push_back(v);
        }
    }

    // ── THE QUADS, AND THE SKIRTS THAT CLOSE THEIR EDGES ──────────────────
    // A ring's edge is a CLIFF unless something closes it: two grounds that
    // carry different octaves meet at different heights however honestly both
    // are derived, and the gap between them is a hole you can see the world
    // through. The owner saw exactly that — a vertical brown wall standing at
    // the join with water on both sides of it.
    //
    // The canon's own remedy (S18.1): «трещины между кольцами лечатся ЮБКАМИ —
    // вертикальная занавеска на пару метров вниз по краю каждого кольца». A
    // skirt hangs straight down from the edge and fills the crack with itself;
    // at the distances a far ring is drawn it is already under a pixel.
    //
    // ONE RULE FOR ALL THREE EDGES, and that is the point of doing it this
    // way: any quad edge with no emitted quad on the other side gets a skirt —
    // the sheet's outer rim, the hole around the composite, and the border
    // between this ring and the next one. Three seams, no special cases.
    std::vector<bool> emitted(std::size_t(dim - 1) * std::size_t(dim - 1),
                              false);
    out.idx.reserve(std::size_t(dim - 1) * std::size_t(dim - 1) * 6u);
    for (int iz = 0; iz + 1 < dim; ++iz) {
        for (int ix = 0; ix + 1 < dim; ++ix) {
            // Inside the composite the near ground answers, so no quad is
            // emitted there. Tested on the quad's FAR corner: a quad that
            // straddles the edge stays, so the sheet always reaches under the
            // composite's rim rather than leaving a gap at it.
            if (holeHalfM > 0.0f) {
                const float qx = float((ix + 1 - n) * stepM);
                const float qz = float((iz + 1 - n) * stepM);
                const float qx0 = float((ix - n) * stepM);
                const float qz0 = float((iz - n) * stepM);
                const float maxAbsX = std::max(std::fabs(qx0), std::fabs(qx));
                const float maxAbsZ = std::max(std::fabs(qz0), std::fabs(qz));
                if (maxAbsX <= holeHalfM && maxAbsZ <= holeHalfM) continue;
            }
            emitted[std::size_t(iz) * std::size_t(dim - 1)
                    + std::size_t(ix)] = true;
            const std::uint32_t a = std::uint32_t(iz * dim + ix);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + std::uint32_t(dim);
            const std::uint32_t d = c + 1;
            out.idx.push_back(a); out.idx.push_back(c); out.idx.push_back(b);
            out.idx.push_back(b); out.idx.push_back(c); out.idx.push_back(d);
        }
    }

    // HOW FAR THE CURTAIN HANGS. Not a taste number: it must outreach the
    // worst height two neighbouring grounds can disagree by, and that is
    // bounded by how steeply the ground can fall across the spacing that
    // separates them — four steps of it is past generous, and at the ranges
    // these rings are drawn a curtain of any depth is a line of pixels.
    const float skirtM = 4.0f * float(stepM);
    const auto emittedAt = [&](int ix, int iz) {
        if (ix < 0 || iz < 0 || ix + 1 >= dim || iz + 1 >= dim) return false;
        return bool(emitted[std::size_t(iz) * std::size_t(dim - 1)
                            + std::size_t(ix)]);
    };
    // A skirt quad hangs from two neighbouring TOP vertices; its two bottom
    // vertices are new, and they carry the same normal and material so the
    // curtain is lit as the ground it hangs from rather than as a wall.
    const auto hang = [&](std::uint32_t t0, std::uint32_t t1) {
        const FarVertex& v0 = out.vtx[t0];
        const FarVertex& v1 = out.vtx[t1];
        FarVertex b0 = v0; b0.py -= skirtM;
        FarVertex b1 = v1; b1.py -= skirtM;
        const std::uint32_t i0 = std::uint32_t(out.vtx.size());
        out.vtx.push_back(b0);
        out.vtx.push_back(b1);
        const std::uint32_t i1 = i0 + 1;
        out.idx.push_back(t0); out.idx.push_back(i0); out.idx.push_back(t1);
        out.idx.push_back(t1); out.idx.push_back(i0); out.idx.push_back(i1);
    };
    for (int iz = 0; iz + 1 < dim; ++iz) {
        for (int ix = 0; ix + 1 < dim; ++ix) {
            if (!emittedAt(ix, iz)) continue;
            const std::uint32_t a = std::uint32_t(iz * dim + ix);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + std::uint32_t(dim);
            const std::uint32_t d = c + 1;
            if (!emittedAt(ix, iz - 1)) hang(a, b);   // north edge
            if (!emittedAt(ix, iz + 1)) hang(c, d);   // south edge
            if (!emittedAt(ix - 1, iz)) hang(a, c);   // west edge
            if (!emittedAt(ix + 1, iz)) hang(b, d);   // east edge
        }
    }
}

} // namespace sm::sub
