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
            c.skel01 = skeleton_cell_height01(macroH, false, mtn);
            c.peak01 = skeleton_cell_peak01(macroH, false, mtn, mtn ? 2 : 0,
                                            cx, cy, worldSeed);
            c.ridgeW = mtn ? 1.0f : 0.0f;
            c.material = biomeMat[std::size_t(mtn ? Biome::Mountain
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
                    far_height01(gx, gz, skel, peak, ridge, worldTiles)
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

    return report("far_mesh_test");
}
