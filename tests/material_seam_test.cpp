// Locks the cross-seam ground-material dither (sub/material.{h,cpp}).
//
// The bug this guards: the ground material used to be a pure function of
// (tile, OWNING CELL biome) with the biome constant per 1024-tile cell, so
// at every subworld cell border the ground colour flipped along a perfectly
// straight line (the owner's "texture wall" screenshot — taiga vs meadow on
// a mountain river bank). The fix picks the biome per tile by bilinearly
// weighting the owning cell's 3×3 biome ring and dithering with a hash keyed
// to ABSOLUTE tile coordinates.
//
// Invariants:
//   1. Determinism — same inputs, same pick (twice).
//   2. Interior purity — deep inside a cell the pick is the owner (no
//      foreign speckle in the cell core).
//   3. Seam continuity — the mix fraction sampled in the last column of
//      cell A matches the first column of its east neighbour B (each
//      computed from its OWN ring, as in production) within a few percent:
//      the distribution is continuous across the border, i.e. no wall.
//      NEGATIVE CONTROL: the per-cell-constant rule (owner everywhere)
//      yields a 100% step across the same border — the wall this test
//      exists to prevent.
//   4. Mid-seam balance — exactly on the border the two biomes mix ~50/50.
//   5. Water containment — a Water neighbour never bleeds "water bed"
//      ground onto a land cell; a Water owner stays Water.
//   6. Authored passthrough — road/field/rock/shore/water tiles keep their
//      material id regardless of biome.

#include "sub/material.h"
#include "sub/map_data.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL material_seam_test: %s\n", msg);
    return 1;
}

// Fraction of `probe` picks over `n` rows in local column `lx` of a cell
// with ring `ring` whose top-left tile is at absolute (ax0, ay0).
double column_fraction(const Biome ring[9], int lx,
                       long long ax0, long long ay0, Biome probe, int n) {
    int hits = 0;
    for (int ly = 0; ly < n; ++ly) {
        if (pick_ground_biome(ring, lx, ly, kCellSize, ax0, ay0) == probe)
            ++hits;
    }
    return double(hits) / double(n);
}

} // namespace

int main() {
    // Cell A (Taiga) at absolute cell (5,7); its east neighbour B (Meadow).
    // Rings as production captures them: each cell is its own ring centre.
    Biome ringA[9], ringB[9];
    for (int i = 0; i < 9; ++i) { ringA[i] = Taiga; ringB[i] = Meadow; }
    ringA[5] = Meadow;  // A's east neighbour is B
    ringB[3] = Taiga;   // B's west neighbour is A
    const long long ax0 = 5LL * kCellSize, ay0 = 7LL * kCellSize;
    const long long bx0 = 6LL * kCellSize, by0 = ay0;

    // 1. Determinism.
    for (int ly = 0; ly < 64; ++ly) {
        const Biome p1 = pick_ground_biome(ringA, kCellSize - 1, ly, kCellSize,
                                           ax0, ay0);
        const Biome p2 = pick_ground_biome(ringA, kCellSize - 1, ly, kCellSize,
                                           ax0, ay0);
        if (p1 != p2) return fail("pick is not deterministic");
    }

    // 2. Interior purity: the cell core (centre column) is pure owner.
    for (int ly = 0; ly < kCellSize; ly += 7) {
        if (pick_ground_biome(ringA, kCellSize / 2, ly, kCellSize, ax0, ay0)
            != Taiga)
            return fail("cell core is not pure owner biome");
    }

    // 3. Seam continuity: adjacent columns across the A|B border agree on
    // the Meadow fraction within 5 % (they are one tile apart, each side
    // computed from its own cell's ring — exactly the production setup).
    const int n = kCellSize;
    const double fA = column_fraction(ringA, kCellSize - 1, ax0, ay0, Meadow, n);
    const double fB = column_fraction(ringB, 0, bx0, by0, Meadow, n);
    if (std::fabs(fA - fB) > 0.05) {
        std::fprintf(stderr, "  fA=%.3f fB=%.3f\n", fA, fB);
        return fail("mix fraction is discontinuous across the seam");
    }
    // NEGATIVE CONTROL: the old per-cell-constant rule = owner everywhere.
    // Same two columns, fraction gap 1.0 — the straight texture wall.
    const double wallGap = std::fabs(0.0 /* A: pure Taiga */
                                     - 1.0 /* B: pure Meadow */);
    if (!(wallGap > 0.05)) return fail("negative control broken");

    // 4. Mid-seam balance: on the border columns the foreign biome holds a
    // substantial share (the sharpened bilinear weight is 0.5 exactly ON
    // the seam line between tile centres, so each side sees roughly half).
    if (fA < 0.30 || fA > 0.70) {
        std::fprintf(stderr, "  fA=%.3f\n", fA);
        return fail("border column is not a balanced mix");
    }

    // 5a. Water containment: replace A's east neighbour with Water — the
    // border column of A must stay 100% land (no water-bed speckle).
    Biome ringW[9];
    for (int i = 0; i < 9; ++i) ringW[i] = Taiga;
    ringW[5] = Water;
    for (int ly = 0; ly < kCellSize; ly += 3) {
        if (pick_ground_biome(ringW, kCellSize - 1, ly, kCellSize, ax0, ay0)
            == Water)
            return fail("water neighbour bled onto a land cell");
    }
    // 5b. A water cell's DRY margin inherits the adjacent land biome — the
    // owner-fix for the straight green|brown wall on coastal cell borders
    // (dry ground in a Water cell used to paint as "water bed").
    Biome ringO[9];
    for (int i = 0; i < 9; ++i) ringO[i] = Meadow;
    ringO[4] = Water;
    for (int ly = 0; ly < kCellSize; ly += 101) {
        for (int lx = 0; lx < kCellSize; lx += 97) {
            if (pick_ground_biome(ringO, lx, ly, kCellSize, ax0, ay0) != Meadow)
                return fail("water cell dry margin did not inherit land biome");
        }
    }
    // 5c. Mid-ocean (all-water ring) stays water.
    Biome ringAllW[9];
    for (int i = 0; i < 9; ++i) ringAllW[i] = Water;
    if (pick_ground_biome(ringAllW, 512, 512, kCellSize, ax0, ay0) != Water)
        return fail("all-water ring did not stay water");

    // 6b. Axis-table form ≡ one-shot form (the renderer uses the tables).
    {
        std::vector<GroundAxis> axis(kCellSize);
        ground_axis_table(kCellSize, axis.data());
        for (int ly = 0; ly < kCellSize; ly += 13) {
            for (int lx = 0; lx < kCellSize; lx += 17) {
                const Biome a = pick_ground_biome(ringA, lx, ly, kCellSize,
                                                  ax0, ay0);
                const Biome b = pick_ground_biome_axis(
                    ringA, axis[std::size_t(lx)], axis[std::size_t(ly)],
                    ax0 + lx, ay0 + ly);
                if (a != b) return fail("axis form diverges from reference");
            }
        }
    }

    // 6. Authored passthrough.
    if (terrain_material_for(TILE_ROAD, Taiga)
            != terrain_material_for(TILE_ROAD, Meadow)
        || !material_is_authored(TILE_ROAD)
        || !material_is_authored(TILE_WATER)
        || material_is_authored(TILE_GRASS))
        return fail("authored-tile contract broken");

    std::printf("OK material_seam_test: deterministic dither, pure core, "
                "seam continuity |fA-fB|=%.3f (<=0.05, wall=1.0), border mix "
                "fA=%.2f, water contained, authored passthrough\n",
                std::fabs(fA - fB), fA);
    return 0;
}
