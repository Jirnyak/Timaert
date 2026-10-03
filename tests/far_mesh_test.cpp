// THE FAR GROUND IS THE NEAR GROUND, FURTHER AWAY — the FIELD half.
//
// `far_terrain_test` pins the HEIGHT LAW (one crest law, coarse octaves remove
// detail without moving shape). This pins what is BAKED out of it — the two
// fields the far world actually is (height in metres, material ordinal) and
// the lattice they are read on.
//
// Why each is worth a test rather than a glance:
//   · a field that samples something OTHER than far_height01 would look like a
//     world and be a different one — the exact failure CANON S18.1 forbids,
//     and the one no screenshot reveals;
//   · a material id is an ORDINAL: the average of two is a third material
//     nobody authored, so the blend must not happen here;
//   · the lattice's triangles are shared by every ring of the ladder, so a
//     boundary moved by one quad is a crack you can see the world through.
//
// WHAT IS NO LONGER HERE, said out loud rather than quietly dropped: the
// NORMAL. It was a vertex attribute baked beside the height and this file held
// it against its own surface; it is now derived in the vertex stage from the
// same field (far.vert, central differences over the margin), which is what
// the header of far_mesh.h always said it should be — «not a stored field». A
// C++ copy of that arithmetic kept only for a test to check would be a second
// implementation of one law, so there is none, and this witness set does not
// cover it. The height field it is derived FROM is covered exactly (§1).
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
// Agreement between the field and its law is BIT-identity up to -ffast-math
// reassociation, so the bound is relative and stated in float ULPs. An
// absolute millimetre used to do this job; on the transfer curve (sub/height.h
// height_m) a far vertex stands at 11.6 km, where one ULP is already 0.9 mm —
// the old bound had quietly dropped below what a float can express. Measured
// worst over the walk: 6 ULP.
constexpr float kFieldUlpTol = 32.0f * 1.1920929e-7f;

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
            c.skel01 = macroH;   // высота ЕСТЬ макровысота (дверь снесена)
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
            c.skel01 = macroH;   // высота ЕСТЬ макровысота (дверь снесена)
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

    // THE FIXTURE IS THE FIELD, because the field is what the far ground IS.
    // Geometry used to be baked beside it and carried the same numbers on
    // vertices; it is gone (far.vert derives a vertex from gl_VertexIndex and
    // these two sheets), so what has to be pinned is the field itself.
    //
    // No composite in this fixture: the sampler says so by answering negative,
    // which is how a far sheet with nothing to stitch to behaves.
    constexpr int   kFixStepM = 64;
    constexpr float kFixHalfM = 3072.0f;
    const auto noComposite = [](float, float) { return -1.0f; };
    FarHeightSheet sheet;
    bake_far_sheet(sheet, grid, kCamCx, kCamCy, kFixStepM, kFixHalfM,
                   kWorldCells, /*holeHalfM*/0.0f, noComposite,
                   /*blendBandM*/0.0f);
    FarMaterialSheet mat;
    bake_far_material_sheet(mat, grid, kFixStepM, kFixHalfM);
    const int kFixN   = int(kFixHalfM) / kFixStepM;
    const int kFixDim = 2 * kFixN + 1;
    CHECK(sheet.live() && sheet.dim == kFixDim && sheet.stepM == kFixStepM,
          "the height field is a field, and says the lattice it is on");
    CHECK(mat.live() && mat.dim == kFixDim,
          "and the material field is the SAME lattice — one convention, not "
          "two");

    // ── 1. THE HEIGHTS ARE THE LAW'S OWN ANSWER ───────────────────────────
    // Not "close to" — the field must carry what far_height01 returns for that
    // tile, or it is a different world that happens to look similar. Re-asked
    // through the same doors the bake used, which is the specification, not a
    // copy of the bake.
    {
        const float worldTiles = float(kWorldCells) * float(kCellSize);
        int samples = 0, mismatches = 0;
        for (int iz = 0; iz < kFixDim; iz += 5) {
            for (int ix = 0; ix < kFixDim; ix += 5) {
                const float wx = float((ix - kFixN) * kFixStepM);
                const float wz = float((iz - kFixN) * kFixStepM);
                // The cell columns at this point, blended as the bake does —
                // through the ONE door that knows where a cell stands in
                // window metres (map_data.h), never through a second spelling
                // of it here. This witness used to carry its own copy of that
                // arithmetic, which is exactly why it could not see the law
                // break: a copy agrees with the original by construction
                // (AGENTS §8 п.5). §8 below pins the convention itself.
                int x0 = 0, y0 = 0; float tx = 0.0f, ty = 0.0f;
                window_cell_weights(wx, wz, grid.radiusCells, x0, y0, tx, ty);
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
                const int gx = wrapi(window_macro_tile(kCamCx, wx),
                                     int(worldTiles));
                const int gz = wrapi(window_macro_tile(kCamCy, wz),
                                     int(worldTiles));
                const float expect = height_m(
                    far_height01(gx, gz, skel, peak, ridge, worldTiles,
                                 grid.seaLevel));
                // THE TOLERANCE IS RELATIVE, and it has to be: a 1 mm
                // absolute bound stood here while the world was 1.5 km
                // tall, and on the transfer curve the same vertex sits at
                // 11.6 km, where one float ULP is already 0.9 mm. The law
                // asserted is bit-identity up to -ffast-math reassociation;
                // 32 ULP says that and nothing looser (measured worst: 6).
                if (std::fabs(sheet.at(ix, iz) - expect)
                        > kFieldUlpTol * std::fabs(expect)) ++mismatches;
                ++samples;
            }
        }
        CHECK(samples > 100 && mismatches == 0,
              "every point of the field stands exactly where the height law "
              "puts it");
    }

    // ── 2. THE MARGIN IS PART OF THE FIELD, NOT A BORDER OF ZEROES ────────
    // One ring of margin is why the vertex stage can own a real slope at the
    // rim: it has neighbours on BOTH sides there. Filled with the law like
    // everything else — a zeroed margin would light the outermost row off a
    // cliff and draw a bright frame around the world, which is what the field
    // was split out of the geometry to make impossible.
    {
        int rim = 0, atZero = 0;
        for (int i = -1; i <= kFixDim; ++i) {
            const float edge[4] = {sheet.at(i, -1), sheet.at(i, kFixDim),
                                   sheet.at(-1, i), sheet.at(kFixDim, i)};
            for (float h : edge) {
                if (h <= 0.0f) ++atZero;
                ++rim;
            }
        }
        CHECK(rim > 0 && atZero == 0,
              "the margin ring carries real ground, so the rim has two "
              "neighbours and no phantom slope");
    }

    // ── 3. THE SHEET HAS A MASSIF IN IT ───────────────────────────────────
    // Without this the file could pass on a flat plane by agreeing that
    // nothing is anywhere (AGENTS testing law 3).
    {
        float lo = 1e30f, hi = -1e30f;
        for (int iz = 0; iz < kFixDim; ++iz) {
            for (int ix = 0; ix < kFixDim; ++ix) {
                lo = std::min(lo, sheet.at(ix, iz));
                hi = std::max(hi, sheet.at(ix, iz));
            }
        }
        CHECK(hi - lo > 200.0f,
              "the far ground has a mountain\'s worth of relief in it — the "
              "field measured something, not a plane");
        CHECK(lo > 0.0f && hi < kHeightScaleM,
              "and it stands inside the world\'s own vertical range — the "
              "curve is allowed to use the sky, not to leave it");
    }

    // ── 4. A MATERIAL ID STAYS AN ORDINAL ─────────────────────────────────
    // The average of two ordinals is a third material nobody authored. What
    // blends is the COLOUR, in the shader, over the fragment.
    //
    // THE FIELD IS BYTES NOW, and that is stronger than it looks: while the
    // ordinal rode a vertex it was a FLOAT, and "off by a fraction" was a
    // statement one could make. A byte cannot hold a material nobody wrote.
    {
        const std::uint8_t* biomeMat = biome_ground_materials();
        const std::uint8_t mtnMat = biomeMat[std::size_t(Biome::Mountain)];
        const std::uint8_t lowMat = biomeMat[std::size_t(Biome::Meadow)];
        int foreign = 0, sawMtn = 0, sawLow = 0;
        for (int iz = -1; iz <= kFixDim; ++iz) {      // margin included
            for (int ix = -1; ix <= kFixDim; ++ix) {
                const std::uint8_t m = mat.at(ix, iz);
                if (m == mtnMat) ++sawMtn;
                else if (m == lowMat) ++sawLow;
                else ++foreign;
            }
        }
        CHECK(foreign == 0,
              "no point of the field carries a material the world never "
              "authored, margin included");
        CHECK(sawMtn > 0 && sawLow > 0,
              "and both of the fixture\'s materials actually reached the field "
              "(the negative control for the line above)");
    }

    // ── 5. THE TWO FIELDS STAND ON THE SAME POINTS ────────────────────────
    // They are addressed by ONE pair of lattice coordinates in far.vert
    // (`texelFetch(uFarHeight, base)` and `texelFetch(uFarMaterial, base)`),
    // so a disagreement about what a lattice point IS would paint one cell\'s
    // material onto another cell\'s ground — visible as a smear along every
    // biome border, and visible nowhere else.
    {
        CHECK(sheet.dim == mat.dim,
              "one lattice, two fields — the height and the material are "
              "indexed by the same point");
        // The material of the CELL the point stands in, re-derived here rather
        // than copied from the bake.
        int samples = 0, wrong = 0;
        for (int iz = 0; iz < kFixDim; iz += 3) {
            for (int ix = 0; ix < kFixDim; ix += 3) {
                const float wx = float((ix - kFixN) * kFixStepM);
                const float wz = float((iz - kFixN) * kFixStepM);
                // «Клетка, в которой точка стоит» = БЛИЖАЙШИЙ центр, и
                // спрашивается он той же дверью, что высота: два листа не
                // имеют права разойтись в том, где стоит клетка.
                int x0 = 0, y0 = 0; float tx = 0.0f, ty = 0.0f;
                window_cell_weights(wx, wz, grid.radiusCells, x0, y0, tx, ty);
                const int cx = tx < 0.5f ? x0 : x0 + 1;
                const int cy = ty < 0.5f ? y0 : y0 + 1;
                if (mat.at(ix, iz) != grid.at(cx, cy).material) ++wrong;
                ++samples;
            }
        }
        CHECK(samples > 100 && wrong == 0,
              "and each point carries the material of the cell it actually "
              "stands in — NEAREST, never a blend of two");
    }

    // ── 6. A DEAD GRID BAKES NOTHING ──────────────────────────────────────
    // Fail closed: an unwired caller gets an empty field, never a flat plane
    // at height zero, which would draw a lake over the whole world.
    {
        FarHeightSheet empty;
        FarMaterialSheet emptyMat;
        FarCellGrid none;
        bake_far_sheet(empty, none, kCamCx, kCamCy, 64, 3072.0f, kWorldCells,
                       0.0f, noComposite, 0.0f);
        bake_far_material_sheet(emptyMat, none, 64, 3072.0f);
        CHECK(empty.m.empty() && !empty.live() && emptyMat.id.empty(),
              "no cells, no ground — the far world is not invented");
        FarHeightSheet zeroStep;
        bake_far_sheet(zeroStep, grid, kCamCx, kCamCy, 0, 3072.0f, kWorldCells,
                       0.0f, noComposite, 0.0f);
        CHECK(!zeroStep.live(), "a spacing of nothing bakes nothing");
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
                        int x0 = 0, y0 = 0; float tx = 0.0f, ty = 0.0f;
                        window_cell_weights(wx, wz, coast.radiusCells,
                                            x0, y0, tx, ty);
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
                        const int gx = wrapi(window_macro_tile(kCamCx, wx),
                                             int(worldTiles));
                        const int gz = wrapi(window_macro_tile(kCamCy, wz),
                                             int(worldTiles));
                        const float expect = height_m(
                            far_height01(gx, gz, mix(&FarCellColumn::skel01),
                                         mix(&FarCellColumn::peak01),
                                         mix(&FarCellColumn::ridgeW),
                                         worldTiles, coast.seaLevel,
                                         mix(&FarCellColumn::gradient01),
                                         mix(&FarCellColumn::heightScale),
                                         mix(&FarCellColumn::mtnScale),
                                         2.0f * 32.0f));
                        const float got = far_point_height_m(
                            coast, kCamCx, kCamCy, wx, wz, kWorldCells,
                            /*stepM*/32);
                        ++samples;
                        if (a.material == biome_ground_materials()
                                              [std::size_t(Biome::Water)]) {
                            ++wetSamples;
                        }
                        if (std::fabs(got - expect)
                                > kFieldUlpTol * std::fabs(expect)) {
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

    // ── 8. THE LATTICE'S TRIANGLES ARE THE SAME TRIANGLES FOR EVERY RING ──
    // The rings were six loose numbers until the arithmetic on them was done:
    // divide each ring's span and hole by its OWN spacing and every ring gives
    // the same pair. That is what licenses ONE index buffer for the whole
    // ladder — and it is also exactly the kind of claim that is true today and
    // quietly false after a ring is added, so it is pinned by the compiler in
    // far_mesh.h and its CONSEQUENCE is measured here.
    //
    // WHY THE FIRST CHECK IS AN IDENTITY BETWEEN TWO SPELLINGS. The builder
    // being replaced decided the hole in METRES, on a quad's corners; the
    // lattice form decides it in QUADS. They are the same rule divided through
    // by the spacing — and "the same rule, restated" is precisely where a
    // boundary slips by one quad, which is a crack you can see the world
    // through. So the two forms are held against each other, per ring, over
    // every quad of the real lattice, rather than argued about.
    {
        const int dim = kFarLatticeDim;
        int rings = 0, spellings = 0, disagreed = 0;
        for (int r = 0; r < kFarRings; ++r) {
            const int   step = far_ring_step_m(r);
            const float hole = far_ring_hole_half_m(r);
            const int   n    = int(far_ring_half_span_m(r)) / step;
            ++rings;
            for (int iz = 0; iz + 1 < dim; ++iz) {
                for (int ix = 0; ix + 1 < dim; ++ix) {
                    // The metre rule of build_far_mesh, verbatim.
                    const float qx0 = float((ix - n) * step);
                    const float qx1 = float((ix + 1 - n) * step);
                    const float qz0 = float((iz - n) * step);
                    const float qz1 = float((iz + 1 - n) * step);
                    const bool inHoleM =
                        std::max(std::fabs(qx0), std::fabs(qx1)) <= hole
                        && std::max(std::fabs(qz0), std::fabs(qz1)) <= hole;
                    const bool emitM = !inHoleM;
                    const bool emitL = far_quad_emitted(ix, iz,
                                                        kFarLatticeHalf,
                                                        kFarHoleQuadHalf);
                    ++spellings;
                    if (emitM != emitL) ++disagreed;
                }
            }
        }
        CHECK(rings == kFarRings && spellings > 100000,
              "the sweep covered every quad of every ring (the control for "
              "the line below)");
        CHECK(disagreed == 0,
              "the hole in QUADS is the same hole the old builder measured in "
              "METRES — the same rule, not a second one");
    }

    // ── 8b. THE INDEX BUFFER COVERS THE RING AND CLOSES ITS EDGES ─────────
    // Counts stated as the lattice's own arithmetic, never as literals: a
    // pinned number would have to be re-typed on every ring added, which is
    // how a witness stops witnessing.
    {
        std::vector<std::uint32_t> idx;
        build_far_lattice_indices(idx, kFarLatticeHalf, kFarHoleQuadHalf);
        const std::uint32_t dim  = std::uint32_t(kFarLatticeDim);
        const std::uint32_t quads = std::uint32_t((kFarLatticeDim - 1)
                                                 * (kFarLatticeDim - 1)
                                        - (2 * kFarHoleQuadHalf)
                                              * (2 * kFarHoleQuadHalf));
        // Every open edge of the emitted region: its outer rim, and the hole's
        // border. One rule for both, which is what the builder claims.
        const std::uint32_t edges =
            4u * std::uint32_t(kFarLatticeDim - 1 + 2 * kFarHoleQuadHalf);
        const std::uint32_t surface = quads * 6u;
        CHECK(idx.size() == std::size_t(surface + edges * 6u),
              "the ring emits two triangles per quad outside its hole, plus "
              "one skirt quad per open edge, and nothing else");
        const std::uint32_t top = dim * dim;
        // ── the surface half: every index a lattice point, every quad legal
        std::uint32_t worstTop = 0;
        int strayTop = 0, holeTouch = 0;
        for (std::uint32_t i = 0; i < surface; ++i) {
            const std::uint32_t v = idx[i];
            if (v >= top) { ++strayTop; continue; }
            worstTop = std::max(worstTop, v);
            const int ix = int(v % dim), iz = int(v / dim);
            // A corner of an emitted quad is a corner of the quad at or one
            // before it on each axis; if NEITHER is emitted the surface has
            // reached into the hole.
            const bool near = far_quad_emitted(ix, iz, kFarLatticeHalf,
                                               kFarHoleQuadHalf)
                           || far_quad_emitted(ix - 1, iz, kFarLatticeHalf,
                                               kFarHoleQuadHalf)
                           || far_quad_emitted(ix, iz - 1, kFarLatticeHalf,
                                               kFarHoleQuadHalf)
                           || far_quad_emitted(ix - 1, iz - 1, kFarLatticeHalf,
                                               kFarHoleQuadHalf);
            if (!near) ++holeTouch;
        }
        CHECK(strayTop == 0 && holeTouch == 0,
              "the surface stays on the lattice and never reaches inside the "
              "hole the composite fills");
        CHECK(worstTop + 1u == top,
              "and it reaches the lattice's last point — the sheet is whole");
        // ── the skirt half: a curtain hangs from ONE lattice edge
        // A skirt quad is (t0, b0, t1) + (t1, b0, b1) with b = t + dim²: two
        // adjacent TOP points and their own bottom twins. That structure is
        // the whole of "it hangs from the edge"; the drop itself is the
        // vertex stage lowering a bottom twin, and it is two lines there.
        int malformed = 0, sideways = 0;
        for (std::uint32_t q = 0; q < edges; ++q) {
            const std::uint32_t* s = idx.data() + surface + q * 6u;
            const std::uint32_t t0 = s[0], t1 = s[2];
            if (t0 >= top || t1 >= top) { ++malformed; continue; }
            if (s[1] != t0 + top || s[3] != t1 || s[4] != t0 + top
                || s[5] != t1 + top) {
                ++malformed;
                continue;
            }
            const std::uint32_t d = t1 > t0 ? t1 - t0 : t0 - t1;
            if (d != 1u && d != dim) ++sideways;
        }
        CHECK(malformed == 0,
              "every skirt quad is two top points and their own bottom twins");
        CHECK(sideways == 0,
              "and the two top points are NEIGHBOURS — a curtain hangs from an "
              "edge of the sheet, never across it");
        // Fail closed, like every other door here.
        std::vector<std::uint32_t> none{1u, 2u, 3u};
        build_far_lattice_indices(none, 0, kFarHoleQuadHalf);
        CHECK(none.empty(), "no lattice, no triangles — and the old contents "
                            "of the buffer do not survive as a ghost");
    }

    // ── 8. ДАЛЬНИЙ МИР СТОИТ ТАМ, ГДЕ СТОИТ КОМПОЗИТ ──────────────────────
    // Соглашение «где в оконных метрах стоит клетка» НЕ БЫЛО ЗАКРЫТО НИКЕМ, и
    // это куплено дорого: оно стояло ЧЕТЫРЬМЯ написаниями (высота дальнего
    // листа, его материал, тайл шума, апрон теневой/водной карты), все четыре
    // съезжали на ПОЛКЛЕТКИ в одну сторону, и ни один свидетель этого не
    // видел — §1 и §5 выше переписывали ту же арифметику своим «ожидаемым»,
    // то есть сверяли копию с оригиналом (AGENTS §8 п.5). Цена дефекта: весь
    // дальний мир на 512 м мимо композита, а полоса воды одноклеточной реки
    // при этом ±177 м — УЖЕ сдвига, то есть река композита и река дали не
    // перекрывались вовсе, и полоса сшивки усредняла мокрое дно с сухой далью
    // (доклад владельца: «рельеф чуть выше в дальномире и из-за этого реки
    // нет»).
    //
    // ЗДЕСЬ ПРОВЕРЯЕТСЯ САМ ЗАКОН, А НЕ БЛЕНД: у клетки камеры своя высота, и
    // в нуле окна дальняя дверь ОБЯЗАНА вернуть ровно её. Деталь выключена
    // колонками (`heightScale`/`mtnScale`/`ridgeW` = 0), поэтому
    // `far_height01` отдаёт ровно смешанную макровысоту — вопрос остаётся
    // ОДИН, про выравнивание.
    //
    // МУТАЦИЯ → ИСХОД, ПРОГНАНО 2026-10-03 (числа печатаются ниже):
    //   база: в нуле окна «клетка 3.00», в +512 м «клетка 3.50», 27 из 27;
    //   `window_cell_weights` со сдвигом −0.5 (дефект, как он и стоял) →
    //       в нуле окна «клетка 2.50», в +512 м «3.00»; 2 из 27 КРАСНЫХ —
    //       оба высотных вердикта, то есть полуклеточный контроль ловит сдвиг
    //       с ОБЕИХ сторон;
    //   та же мутация, а вердикт о МАТЕРИАЛЕ при ней МОЛЧИТ, и это записано
    //       нарочно: в нуле окна сдвинутый бленд даёт ровно `tx = 0.5`, то
    //       есть «ближайшая» уходит на x0+1 и снова попадает в клетку камеры.
    //       Материал ловит сдвиг ПОЛОВИННЫЙ только в других точках, поэтому
    //       он здесь не детектор выравнивания, а пин единой двери;
    //   `window_macro_tile` без `kCellSize/2` → 1 из 27 КРАСНЫЙ, вердикт о
    //       тайле; два высотных МОЛЧАТ (деталь выключена, шум не читается
    //       вовсе) — поэтому у тайла свой вердикт, а не доверие чужому.
    {
        constexpr int kR = 3;
        FarCellGrid g;
        g.radiusCells = kR;
        const int n = g.span();
        g.cells.assign(std::size_t(n) * std::size_t(n), FarCellColumn{});
        g.seaLevel = WATER_LEVEL;
        const std::uint8_t* biomeMat = biome_ground_materials();
        // Высота УНИКАЛЬНА НА КЛЕТКУ по оси X: только так ответ двери читается
        // как номер клетки, а не как «похоже на правду».
        const auto cell_h = [](int x) { return 0.40f + 0.01f * float(x); };
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                FarCellColumn c{};
                c.skel01 = cell_h(x);
                c.heightScale = 0.0f;   // деталь выключена — вопрос один
                c.mtnScale    = 0.0f;
                c.ridgeW      = 0.0f;
                c.material = biomeMat[std::size_t(x == kR ? Biome::Meadow
                                                          : Biome::Desert)];
                g.cells[std::size_t(y) * std::size_t(n) + std::size_t(x)] = c;
            }
        // Обратно из метров в поле — чтобы вердикт говорил НОМЕРОМ КЛЕТКИ, а
        // не метрами: метры здесь производное, а спор идёт про адрес.
        const auto cell_at = [&](float wx) {
            const float m = far_point_height_m(g, kCamCx, kCamCy, wx, 0.0f,
                                               /*worldCellsX*/0, kFixStepM);
            const float h01 = WATER_LEVEL
                + std::log2((m / kHeightCurveM) + kFieldFloorGain)
                      / kHeightDoublings;
            return (h01 - 0.40f) / 0.01f;
        };
        const float atZero = cell_at(0.0f);
        const float atHalf = cell_at(0.5f * float(kCellSize));
        CHECK(std::fabs(atZero - float(kR)) < 0.01f,
              "ноль оконных метров есть ЦЕНТР клетки камеры: дальняя дверь "
              "отдаёт её собственную высоту, а не бленд с соседом");
        // НЕГАТИВНЫЙ КОНТРОЛЬ, живой: в ПОЛКЛЕТКИ дверь обязана отдать ровно
        // середину между клеткой камеры и восточным соседом. Это и доказывает,
        // что проверка выше различает сдвиг на полклетки, а не «любое число».
        CHECK(std::fabs(atHalf - (float(kR) + 0.5f)) < 0.01f,
              "а в полклетки — ровно половина пути к восточному соседу: "
              "детектор ВИДИТ сдвиг на полклетки, с любой стороны");
        // Материал — тот же закон и та же дверь: лист материала и лист высот
        // не имеют права разойтись в том, где стоит клетка.
        FarMaterialSheet m8;
        bake_far_material_sheet(m8, g, kFixStepM, 1024.0f);
        const int n8 = int(1024.0f) / kFixStepM;
        CHECK(m8.live()
              && m8.at(n8, n8) == biomeMat[std::size_t(Biome::Meadow)],
              "и материал в нуле окна — материал клетки камеры, не соседней");
        // Тайл шума: ближний генератор в центре клетки стоит на
        // `globalOffsetX + kCellSize/2`, и дальняя земля обязана читать ТОТ ЖЕ
        // тайл, иначе один узор лежит в двух мирах в разных местах.
        CHECK(window_macro_tile(kCamCx, 0.0f)
                  == kCamCx * kCellSize + kCellSize / 2,
              "узор дальней земли читает ТОТ ЖЕ макро-тайл, на котором стоит "
              "ближний генератор в центре клетки");
        std::printf("  [замер] выравнивание: в нуле окна клетка %.2f "
                    "(обязана %d), в полклетки %.2f (обязана %.1f)\n",
                    double(atZero), kR, double(atHalf), double(kR) + 0.5);
    }

    return report("far_mesh_test");
}
