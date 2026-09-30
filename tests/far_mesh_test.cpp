// THE FAR GROUND IS THE NEAR GROUND, FURTHER AWAY — the geometry half.
//
// `far_terrain_test` pins the HEIGHT LAW (one crest law, coarse octaves remove
// detail without moving shape). This pins the SHEET built out of it: that its
// vertices really carry that law's answer, that its normals agree with its own
// surface, that its material ids stay ordinals, and that it covers what it
// says it covers.
//
// Why each of those is worth a test rather than a glance:
//   · a mesh that samples something OTHER than far_height01 would look like a
//     world and be a different one — the exact failure CANON S18.1 forbids,
//     and the one no screenshot reveals;
//   · a normal that disagrees with its surface lights a mountain that is not
//     there, and it is invisible until the sun moves;
//   · a material id is an ORDINAL: the average of two is a third material
//     nobody authored, so the blend must not happen here.
#include "check.h"

#include "sub/far_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using namespace sm;
using namespace sm::sub;

constexpr int kWorldCells = 1024;                       // CANON.md S1
constexpr int kCamCx = 500, kCamCy = 300;

// A grid with a MASSIF in it: mountain cells in the middle band, lowland
// around. Built through the real crest/skeleton doors, never by hand.
FarCellGrid make_grid(int radius, std::uint32_t worldSeed) {
    FarCellGrid g;
    g.radiusCells = radius;
    const int n = g.span();
    g.cells.resize(std::size_t(n) * std::size_t(n));
    const std::uint8_t* biomeMat = biome_ground_materials();
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int cx = kCamCx + x - radius;
            const int cy = kCamCy + y - radius;
            const bool mtn = std::abs(cy - kCamCy) <= 1
                          && std::abs(cx - kCamCx) <= radius / 2;
            const float macroH = mtn ? 0.88f : 0.55f;
            FarCellColumn c{};
            c.skel01 = skeleton_cell_height01(macroH, mtn);
            c.peak01 = skeleton_cell_peak01(macroH, mtn, mtn ? 2 : 0,
                                            cx, cy, worldSeed, WATER_LEVEL);
            c.ridgeW = mtn ? 1.0f : 0.0f;
            c.material = biomeMat[std::size_t(mtn ? Biome::Mountain
                                                  : Biome::Meadow)];
            g.cells[std::size_t(y) * std::size_t(n) + std::size_t(x)] = c;
        }
    }
    return g;
}

// A grid with a COASTLINE in it: water to the west of the camera's column,
// land to the east, so every row crosses the shore once. Built through the
// same skeleton door the near generator uses, because the whole question is
// whether the two banks agree about one plane.
//
// PARAMETERISED BY BOTH DEPTHS ON PURPOSE. A single staged pair proves only
// that one pair works, and the pair a tired hand picks is a DEEP sea beside a
// HIGH shore — the easy case, where the manifold alone already answers. The
// case that bites is a shallow water cell (macro height just under the plane)
// beside a gentle shore, and that case has to be reached by sweeping, not by
// being lucky.
FarCellGrid make_coast_grid(int radius, std::uint32_t worldSeed,
                            float waterH, float landH) {
    FarCellGrid g;
    g.radiusCells = radius;
    const int n = g.span();
    g.cells.resize(std::size_t(n) * std::size_t(n));
    const std::uint8_t* biomeMat = biome_ground_materials();
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int cx = kCamCx + x - radius;
            const int cy = kCamCy + y - radius;
            const bool water = cx < kCamCx;
            // DEPTH VARIES CELL TO CELL, because a real sea does and because a
            // sea of one depth cannot answer the question. A water cell
            // carries NO detail octaves by authorship (`Water.heightScale` is
            // 0 in kConfigs, and the near generator reads the same row), so a
            // uniform-depth fixture produces bit-identical neighbours out of
            // honest flatness and would accuse the world of a clamp it does
            // not have. The relief of a seabed comes from its MANIFOLD.
            const float wobble = 0.04f * float((cx * 7 + cy * 13) % 5) / 4.0f;
            const float macroH = water ? (waterH - wobble) : (landH + wobble);
            FarCellColumn c{};
            c.skel01 = skeleton_cell_height01(macroH, false);
            c.peak01 = skeleton_cell_peak01(macroH, false, 0, cx, cy,
                                            worldSeed, WATER_LEVEL);
            c.ridgeW = 0.0f;
            // The detail columns a real cell carries — without them the sheet
            // is a plane and the question cannot even be asked.
            c.gradient01  = 0.24f;
            c.heightScale = biome_config(water ? Biome::Water
                                               : Biome::Meadow).heightScale;
            c.mtnScale    = 0.1f;
            c.material = biomeMat[std::size_t(water ? Biome::Water
                                                    : Biome::Meadow)];
            g.cells[std::size_t(y) * std::size_t(n) + std::size_t(x)] = c;
        }
    }
    return g;
}

} // namespace

int main() {
    using namespace sm::test;

    const std::uint32_t worldSeed = 0x51A2B3C4u;
    const FarCellGrid grid = make_grid(/*radius*/6, worldSeed);
    CHECK(grid.live(), "the fixture grid is a grid");

    FarMesh mesh;
    // No composite in this fixture: the sampler says so by answering negative,
    // which is how a far sheet with nothing to stitch to behaves.
    const auto noComposite = [](float, float) { return -1.0f; };
    build_far_mesh(mesh, grid, kCamCx, kCamCy, /*stepM*/64,
                   /*halfSpanM*/3072.0f, kWorldCells, /*holeHalfM*/0.0f,
                   noComposite, /*blendBandM*/0.0f);

    // ── 1. IT BUILT SOMETHING, AND SAYS WHAT ──────────────────────────────
    {
        const int n = int(mesh.halfSpanM) / mesh.stepM;
        const std::size_t dim = std::size_t(2 * n + 1);
        CHECK(mesh.stepM == 64 && mesh.halfSpanM > 0.0f,
              "the build reports the spacing and reach it actually used");
        CHECK(mesh.vtx.size() >= dim * dim,
              "one vertex per grid point, plus whatever the skirts hang");
        // Two triangles per quad, PLUS the skirts that close the sheet's
        // edges. The old form pinned the count exactly and was right only
        // while the sheet had open edges — which is the defect the skirts
        // exist to close (the owner saw it as a vertical wall at the join).
        CHECK(mesh.idx.size() >= (dim - 1) * (dim - 1) * 6u,
              "every quad has its two triangles");
        CHECK(mesh.idx.size() > (dim - 1) * (dim - 1) * 6u,
              "...and the rim carries MORE than that: its edges are closed");
        std::uint32_t worst = 0;
        for (std::uint32_t i : mesh.idx) worst = std::max(worst, i);
        CHECK(worst + 1u == std::uint32_t(mesh.vtx.size()),
              "every index addresses a vertex, and every vertex is addressed");
    }

    // ── 2. THE HEIGHTS ARE THE LAW'S OWN ANSWER ───────────────────────────
    // Not "close to" — the mesh must carry what far_height01 returns for that
    // tile, or it is a different world that happens to look similar. Re-asked
    // through the same doors the builder used, which is the specification,
    // not a copy of the builder.
    {
        const int n = int(mesh.halfSpanM) / mesh.stepM;
        const int dim = 2 * n + 1;
        const float worldTiles = float(kWorldCells) * float(kCellSize);
        int samples = 0, mismatches = 0;
        for (int iz = 0; iz < dim; iz += 5) {
            for (int ix = 0; ix < dim; ix += 5) {
                const float wx = float((ix - n) * mesh.stepM);
                const float wz = float((iz - n) * mesh.stepM);
                // The cell columns at this point, blended as the builder does.
                const float fx = wx / float(kCellSize) + float(grid.radiusCells);
                const float fy = wz / float(kCellSize) + float(grid.radiusCells);
                int x0 = 0, y0 = 0; float tx = 0.0f, ty = 0.0f;
                detail::far_cell_weights(fx, fy, x0, y0, tx, ty);
                const auto& c00 = grid.at(x0, y0);
                const auto& c10 = grid.at(x0 + 1, y0);
                const auto& c01 = grid.at(x0, y0 + 1);
                const auto& c11 = grid.at(x0 + 1, y0 + 1);
                const float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty);
                const float w01 = (1 - tx) * ty,       w11 = tx * ty;
                const float skel = c00.skel01 * w00 + c10.skel01 * w10
                                 + c01.skel01 * w01 + c11.skel01 * w11;
                const float peak = c00.peak01 * w00 + c10.peak01 * w10
                                 + c01.peak01 * w01 + c11.peak01 * w11;
                const float ridge = c00.ridgeW * w00 + c10.ridgeW * w10
                                  + c01.ridgeW * w01 + c11.ridgeW * w11;
                const int gx = wrapi(kCamCx * kCellSize + int(std::floor(wx)),
                                     int(worldTiles));
                const int gz = wrapi(kCamCy * kCellSize + int(std::floor(wz)),
                                     int(worldTiles));
                const float expect =
                    far_height01(gx, gz, skel, peak, ridge, worldTiles,
                                 grid.seaLevel)
                    * kHeightScaleM;
                const float got = mesh.vtx[std::size_t(iz) * std::size_t(dim)
                                           + std::size_t(ix)].py;
                if (std::fabs(got - expect) > 1e-3f) ++mismatches;
                ++samples;
            }
        }
        CHECK(samples > 100 && mismatches == 0,
              "every vertex stands exactly where the height law puts it");
    }

    // ── 2b. THE SKIRTS HANG, AND THEY HANG FROM THE EDGE ──────────────────
    // A curtain that is not below its own edge closes nothing. Asserted as a
    // relation to the SURFACE's own lowest point, so a retune of the terrain
    // cannot make it lie: the mesh must reach below the ground it is made of.
    {
        const int n = int(mesh.halfSpanM) / mesh.stepM;
        const std::size_t surface = std::size_t(2 * n + 1)
                                  * std::size_t(2 * n + 1);
        float surfaceLow = 1e30f, meshLow = 1e30f;
        for (std::size_t i = 0; i < mesh.vtx.size(); ++i) {
            meshLow = std::min(meshLow, mesh.vtx[i].py);
            if (i < surface) surfaceLow = std::min(surfaceLow, mesh.vtx[i].py);
        }
        CHECK(mesh.vtx.size() > surface,
              "the sheet grew vertices beyond its grid — the skirts exist");
        CHECK(meshLow < surfaceLow,
              "and they hang BELOW the ground they close the edge of");
    }

    // ── 3. THE SHEET HAS A MASSIF IN IT ───────────────────────────────────
    // Without this the file could pass on a flat plane by agreeing that
    // nothing is anywhere (AGENTS testing law 3).
    {
        const int n = int(mesh.halfSpanM) / mesh.stepM;
        const std::size_t surface = std::size_t(2 * n + 1)
                                  * std::size_t(2 * n + 1);
        float lo = 1e30f, hi = -1e30f;
        for (std::size_t i = 0; i < surface; ++i) {   // the GROUND, not its skirts
            lo = std::min(lo, mesh.vtx[i].py);
            hi = std::max(hi, mesh.vtx[i].py);
        }
        CHECK(hi - lo > 200.0f,
              "the far ground has a mountain's worth of relief in it — the "
              "probe measured something, not a plane");
        CHECK(lo > 0.0f && hi < 2.0f * kHeightScaleM,
              "and it stands inside the world's own vertical range");
    }

    // ── 4. NORMALS AGREE WITH THE SURFACE THEY STAND ON ───────────────────
    // A normal is not decoration: light reads it. One that disagrees with the
    // height field paints a slope that is not there, and the error only shows
    // when the sun moves — which is to say, never in a screenshot.
    {
        int samples = 0, bad = 0, unnormalised = 0;
        const int n = int(mesh.halfSpanM) / mesh.stepM;
        const int dim = 2 * n + 1;
        for (int iz = 1; iz + 1 < dim; iz += 7) {
            for (int ix = 1; ix + 1 < dim; ix += 7) {
                const auto at = [&](int x, int z) {
                    return mesh.vtx[std::size_t(z) * std::size_t(dim)
                                    + std::size_t(x)];
                };
                const FarVertex& v = at(ix, iz);
                const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny
                                            + v.nz * v.nz);
                if (std::fabs(len - 1.0f) > 1e-3f) ++unnormalised;
                // The surface's own slope between this vertex's neighbours,
                // measured off the MESH — so the check is "your normal matches
                // YOUR ground", not "your normal matches my formula".
                const float dx = (at(ix + 1, iz).py - at(ix - 1, iz).py)
                               / (2.0f * float(mesh.stepM));
                const float dz = (at(ix, iz + 1).py - at(ix, iz - 1).py)
                               / (2.0f * float(mesh.stepM));
                const float inv = 1.0f / std::sqrt(dx * dx + dz * dz + 1.0f);
                if (std::fabs(v.nx - (-dx * inv)) > 1e-3f
                    || std::fabs(v.ny - inv) > 1e-3f
                    || std::fabs(v.nz - (-dz * inv)) > 1e-3f) ++bad;
                ++samples;
            }
        }
        CHECK(samples > 30 && unnormalised == 0,
              "every normal is a unit vector");
        CHECK(bad == 0,
              "every normal is the slope of the ground it stands on");
    }

    // ── 5. A MATERIAL ID STAYS AN ORDINAL ─────────────────────────────────
    // The average of two ordinals is a third material nobody authored. What
    // blends is the COLOUR, in the shader, over the fragment.
    {
        const std::uint8_t* biomeMat = biome_ground_materials();
        const float mtnMat = float(biomeMat[std::size_t(Biome::Mountain)]);
        const float lowMat = float(biomeMat[std::size_t(Biome::Meadow)]);
        int foreign = 0, sawMtn = 0, sawLow = 0;
        for (const FarVertex& v : mesh.vtx) {   // skirts carry their edge's own
            if (v.material == mtnMat) ++sawMtn;
            else if (v.material == lowMat) ++sawLow;
            else ++foreign;
        }
        CHECK(foreign == 0,
              "no vertex carries a material the world never authored");
        CHECK(sawMtn > 0 && sawLow > 0,
              "and both of the fixture's materials actually reached vertices "
              "(the negative control for the line above)");
    }

    // ── 6. A DEAD GRID BUILDS NOTHING ─────────────────────────────────────
    // Fail closed: an unwired caller gets an empty sheet, never a flat plane
    // at height zero, which would draw a lake over the whole world.
    {
        FarMesh empty;
        FarCellGrid none;
        build_far_mesh(empty, none, kCamCx, kCamCy, 64, 3072.0f, kWorldCells,
                       0.0f, noComposite, 0.0f);
        CHECK(empty.vtx.empty() && empty.idx.empty(),
              "no cells, no ground — the far world is not invented");
        FarMesh zeroStep;
        build_far_mesh(zeroStep, grid, kCamCx, kCamCy, 0, 3072.0f, kWorldCells,
                       0.0f, noComposite, 0.0f);
        CHECK(zeroStep.vtx.empty(), "a spacing of nothing builds nothing");
    }

    // ── 7. A SEABED IS GROUND, NOT GLASS ──────────────────────────────────
    // WHERE THE WATER IS, IS A QUESTION ABOUT HEIGHT — and the near world
    // answers it that way and only that way: `water[i] = heightmap[i] <
    // waterLevel`, per tile (sub/gens/kit/tiles.cpp sync_water_tiles_from_
    // heightmap). The shoreline is where the ground crosses the ONE plane the
    // world has. A cell's biome flag never enters it.
    //
    // So the far sheet may not answer it differently. A ceiling imposed on
    // height because the CELL is flagged water is a second law about one
    // question (DOD 6) — and it does not merely disagree, it FLATTENS: inside
    // a water cell the water weight is a constant, so every clamped point
    // lands on the same number and the ground becomes a level shelf standing
    // on the sea. That shelf is what the owner photographed as a grey-tan
    // plateau along every far shore.
    //
    // ASSERTED AS AN IDENTITY, WHICH IS THE ONLY FORM A CLAMP CANNOT HIDE IN.
    // Section 2 already says the sheet carries `far_height01`'s own answer —
    // but it says it over a fixture with no water in it, so a ceiling keyed on
    // wetness passed under it for as long as it existed. The same statement,
    // asked over a COAST, is exact: wherever a post-hoc rule would bend the
    // height, the two sides differ by exactly the bend.
    //
    // Two weaker forms were tried here first and thrown out, and it is worth
    // saying why, because both looked reasonable:
    //   · «no two neighbours identical» — `Water.heightScale` is 0 in kConfigs
    //     (and generate_heightmap reads the same row), so a seabed carries no
    //     detail octaves BY AUTHORSHIP and a flat stretch of it is honest. The
    //     witness could not tell authored flatness from a clamp.
    //   · «the seabed answers to its own depth» — true, but the ceiling only
    //     bound where the water weight was near 1, so the probes had to guess
    //     the regime, and a fixture that guessed wrong passed its own negative
    //     control. A witness whose verdict depends on where you look is not a
    //     witness (AGENTS §8 п.6).
    {
        const float worldTiles = float(kWorldCells) * float(kCellSize);
        int samples = 0, mismatches = 0, wetSamples = 0;
        float worstM = 0.0f;
        for (int wi = 1; wi <= 8; ++wi) {
            for (int li = 1; li <= 8; ++li) {
                const float waterH = WATER_LEVEL * float(wi) / 8.0f;
                const float landH  = WATER_LEVEL
                                   + (1.0f - WATER_LEVEL) * float(li) / 8.0f;
                const FarCellGrid coast =
                    make_coast_grid(/*radius*/6, worldSeed, waterH, landH);
                for (int iz = -40; iz <= 40; iz += 7) {
                    for (int ix = -96; ix <= 96; ix += 3) {
                        const float wx = float(ix) * 32.0f;
                        const float wz = float(iz) * 32.0f;
                        // The law, re-asked through the same public doors the
                        // sheet uses — never a copy of the sheet's arithmetic.
                        const float fx = wx / float(kCellSize)
                                       + float(coast.radiusCells);
                        const float fy = wz / float(kCellSize)
                                       + float(coast.radiusCells);
                        int x0 = 0, y0 = 0; float tx = 0.0f, ty = 0.0f;
                        detail::far_cell_weights(fx, fy, x0, y0, tx, ty);
                        const FarCellColumn& a = coast.at(x0,     y0);
                        const FarCellColumn& b = coast.at(x0 + 1, y0);
                        const FarCellColumn& c = coast.at(x0,     y0 + 1);
                        const FarCellColumn& d = coast.at(x0 + 1, y0 + 1);
                        const float w00 = (1 - tx) * (1 - ty);
                        const float w10 = tx * (1 - ty);
                        const float w01 = (1 - tx) * ty;
                        const float w11 = tx * ty;
                        const auto mix = [&](float FarCellColumn::* f) {
                            return a.*f * w00 + b.*f * w10
                                 + c.*f * w01 + d.*f * w11;
                        };
                        const int gx = wrapi(kCamCx * kCellSize
                                             + int(std::floor(wx)),
                                             int(worldTiles));
                        const int gz = wrapi(kCamCy * kCellSize
                                             + int(std::floor(wz)),
                                             int(worldTiles));
                        const float expect =
                            far_height01(gx, gz, mix(&FarCellColumn::skel01),
                                         mix(&FarCellColumn::peak01),
                                         mix(&FarCellColumn::ridgeW),
                                         worldTiles, coast.seaLevel,
                                         mix(&FarCellColumn::gradient01),
                                         mix(&FarCellColumn::heightScale),
                                         mix(&FarCellColumn::mtnScale),
                                         2.0f * 32.0f) * kHeightScaleM;
                        const float got = far_point_height_m(
                            coast, kCamCx, kCamCy, wx, wz, kWorldCells,
                            /*stepM*/32);
                        ++samples;
                        if (a.material == biome_ground_materials()
                                              [std::size_t(Biome::Water)]) {
                            ++wetSamples;
                        }
                        if (std::fabs(got - expect) > 1e-3f) {
                            ++mismatches;
                            worstM = std::max(worstM, std::fabs(got - expect));
                        }
                    }
                }
            }
        }
        if (mismatches > 0) {
            std::fprintf(stderr,
                         "  [seabed] %d of %d far points do not carry the "
                         "height law's own answer — something bends them "
                         "afterwards; worst %.1f m\n",
                         mismatches, samples, double(worstM));
        }
        CHECK(samples > 1000 && wetSamples > 0,
              "the sweep covered every legal shore and actually stood on "
              "water (the control for the line below)");
        CHECK(mismatches == 0,
              "over a COAST too, the far ground is exactly far_height01 — "
              "nothing bends it for being wet");
    }

    return report("far_mesh_test");
}
