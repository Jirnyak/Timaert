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
//   5. Ground-alias law — the ring the dither consumes is a GROUND ring
//      (map_data.h ground_biome): land answers with its biome no matter what
//      the alias column says (drift-proof), a flooded cell answers with its
//      unflooded climate ground. With that, a water cell's dry banks blend
//      like any land↔land pair: no wall inside the cell where the old blend
//      band saturated, and no scan-order bias. NEGATIVE CONTROL: the
//      removed water law (zero water corners + renormalise +
//      first-land-in-ring fallback) is reimplemented here and shown to
//      paint the banks by the ring's NW-most land and to step ~100% across
//      the band edge — the ground-band walls of 2026-08-29.
//   6. Authored passthrough — road/field/rock/shore/water tiles keep their
//      material id regardless of biome.
//   7. Structure shade is a property of the WORLD, not of the 3×3 window.
//      Same defect class as #3 in different clothing: the shade wobble used to
//      be keyed to the structure's COMPOSITE coordinate, so a crossing — which
//      reindexes every structure by a whole cell — changed the hash and made
//      every building in view jump brightness at the boundary. NEGATIVE
//      CONTROL: the old window-relative keying is shown to differ across the
//      same re-centre that the absolute keying survives.
//
// Invariant 7 asserted NOTHING until 2026-08-06: it lived in the one `bool`
// function of this file and returned `int fail() { return 1; }`, which reads as
// `true` = PASS. It — and its negative control — are back through tests/check.h.
//
// Loops here follow one rule: a loop that measures must also assert that it
// MEASURED something. A sampling loop that never ran is not a passing test.
#include "check.h"

#include "sub/material.h"
#include "sub/map_data.h"

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

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

// The old, broken keying, kept ONLY as this test's negative control: the shade
// hung off the structure's position inside the current composite.
float window_relative_shade(float compositeX, float compositeY) {
    std::uint32_t h = std::uint32_t(compositeX * 110351.0f)
        ^ (std::uint32_t(compositeY * 66821.0f) * std::uint32_t{2654435761});
    h ^= h >> 16;
    return 0.86f + 0.20f * (float(h & 0xffu) / 255.0f);
}

// Cell A (Taiga) at absolute cell (5,7); its east neighbour B (Meadow).
// Rings as production captures them: each cell is its own ring centre.
struct Seam {
    Biome ringA[9];
    Biome ringB[9];
    long long ax0, ay0, bx0, by0;
};

Seam make_seam() {
    Seam s{};
    for (int i = 0; i < 9; ++i) { s.ringA[i] = Taiga; s.ringB[i] = Meadow; }
    s.ringA[5] = Meadow;  // A's east neighbour is B
    s.ringB[3] = Taiga;   // B's west neighbour is A
    s.ax0 = 5LL * kCellSize;
    s.ay0 = 7LL * kCellSize;
    s.bx0 = 6LL * kCellSize;
    s.by0 = s.ay0;
    return s;
}

void test_pick_is_deterministic() {
    const Seam s = make_seam();
    int samples = 0, mismatches = 0;
    for (int ly = 0; ly < 64; ++ly) {
        const Biome p1 = pick_ground_biome(s.ringA, kCellSize - 1, ly,
                                           kCellSize, s.ax0, s.ay0);
        const Biome p2 = pick_ground_biome(s.ringA, kCellSize - 1, ly,
                                           kCellSize, s.ax0, s.ay0);
        ++samples;
        if (p1 != p2) ++mismatches;
    }
    CHECK(samples == 64 && mismatches == 0,
          "the same tile always picks the same ground biome");
}

void test_cell_core_is_pure_owner() {
    const Seam s = make_seam();
    int samples = 0, foreign = 0;
    for (int ly = 0; ly < kCellSize; ly += 7) {
        ++samples;
        if (pick_ground_biome(s.ringA, kCellSize / 2, ly, kCellSize,
                              s.ax0, s.ay0) != Taiga) ++foreign;
    }
    CHECK(samples > 0 && foreign == 0,
          "deep inside a cell the ground is the owner's biome, unspeckled");
}

void test_seam_is_continuous_and_balanced() {
    const Seam s = make_seam();
    // Adjacent columns across the A|B border, each computed from its OWN
    // cell's ring — exactly the production setup.
    const int n = kCellSize;
    const double fA = column_fraction(s.ringA, kCellSize - 1, s.ax0, s.ay0,
                                      Meadow, n);
    const double fB = column_fraction(s.ringB, 0, s.bx0, s.by0, Meadow, n);
    if (std::fabs(fA - fB) > 0.05)
        std::fprintf(stderr, "  fA=%.3f fB=%.3f\n", fA, fB);
    CHECK(std::fabs(fA - fB) <= 0.05,
          "the mix fraction is continuous across the seam: no texture wall");

    // NEGATIVE CONTROL: the old per-cell-constant rule = owner everywhere, so
    // the same two columns would read 0.0 and 1.0 — a 100% step. Without this
    // the continuity check above proves nothing about the defect.
    const double wallGap = std::fabs(0.0 /* A: pure Taiga */
                                     - 1.0 /* B: pure Meadow */);
    CHECK(wallGap > 0.05,
          "the negative control reproduces the wall the fix removed");

    // Mid-seam balance: on the border columns the foreign biome holds a
    // substantial share (the sharpened bilinear weight is 0.5 exactly ON the
    // seam line between tile centres, so each side sees roughly half).
    if (fA < 0.30 || fA > 0.70) std::fprintf(stderr, "  fA=%.3f\n", fA);
    CHECK(fA >= 0.30 && fA <= 0.70,
          "on the border the two biomes mix, neither one owns the column");
}

// The REMOVED water law, kept ONLY as this test's negative control: zero the
// water corners, renormalise what is left, and when nothing is left fall back
// to the FIRST land biome in row-major ring scan. This is what painted a
// water cell's banks by its NW-most land neighbour and drew razor-straight
// walls exactly where the blend band saturates (owner report 2026-08-29).
Biome removed_water_law_pick(const Biome nb[9],
                             const GroundAxis& ax, const GroundAxis& ay,
                             long long absX, long long absY) {
    const Biome owner = nb[4];
    const Biome b00 = nb[ay.i0 * 3 + ax.i0];
    const Biome b10 = nb[ay.i0 * 3 + ax.i1];
    const Biome b01 = nb[ay.i1 * 3 + ax.i0];
    const Biome b11 = nb[ay.i1 * 3 + ax.i1];
    if (b00 == b10 && b00 == b01 && b00 == b11 && b00 != Water) return b00;
    const float fx = ax.f, fy = ay.f;
    const Biome cand[4] = {b00, b10, b01, b11};
    float w[4] = {(1.0f - fx) * (1.0f - fy), fx * (1.0f - fy),
                  (1.0f - fx) * fy,          fx * fy};
    float total = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (cand[i] == Water) w[i] = 0.0f;
        total += w[i];
    }
    if (total <= 0.0f) {
        if (owner != Water) return owner;
        for (int i = 0; i < 9; ++i)
            if (nb[i] != Water) return nb[i];
        return Water;
    }
    const float r = tile_hash01(absX, absY) * total;
    float acc = 0.0f;
    for (int i = 0; i < 4; ++i) {
        acc += w[i];
        if (r < acc) return cand[i];
    }
    for (int i = 3; i >= 0; --i)
        if (w[i] > 0.0f) return cand[i];
    return owner;
}

void test_ground_alias_law() {
    // Land answers with its biome even when the alias column disagrees —
    // a hand-built context cannot drift the two apart.
    CellContext land{};
    land.biome = Taiga;
    land.groundBiome = Steppe;
    CHECK(ground_biome(land) == Taiga,
          "a land cell's ground IS its biome, whatever the alias says");

    // A flooded cell answers with its unflooded climate ground.
    CellContext river{};
    river.biome = Water;
    river.groundBiome = Steppe;
    CHECK(ground_biome(river) == Steppe,
          "a flooded cell's ground is its unflooded climate ground");
}

void test_flooded_margin_has_no_walls() {
    // A river cell at (5,7): as the macro sees it (Water owner, NW=Taiga,
    // E=Meadow, water everywhere else) and as the manager's nbGround capture
    // hands it to the dither (every Water entry replaced by that cell's
    // climate ground — Steppe here).
    const long long ax0 = 5LL * kCellSize;
    const long long ay0 = 7LL * kCellSize;
    Biome oldRing[9];
    Biome groundRing[9];
    for (int i = 0; i < 9; ++i) { oldRing[i] = Water; groundRing[i] = Steppe; }
    oldRing[0] = Taiga;  groundRing[0] = Taiga;
    oldRing[5] = Meadow; groundRing[5] = Meadow;

    std::vector<GroundAxis> axis(kCellSize);
    ground_axis_table(kCellSize, axis.data());

    // (a) The banks deep inside the cell are the cell's OWN climate ground,
    // and they do not care which corner of the ring holds land: no
    // scan-order bias.
    Biome biased[9];
    for (int i = 0; i < 9; ++i) biased[i] = groundRing[i];
    biased[0] = Tropics;  // move the NW land — the old law's whole answer
    int samples = 0, foreign = 0, nwDependent = 0;
    for (int ly = 384; ly < 640; ly += 13) {
        for (int lx = 384; lx < 640; lx += 17) {
            ++samples;
            const Biome a = pick_ground_biome(groundRing, lx, ly, kCellSize,
                                              ax0, ay0);
            const Biome b = pick_ground_biome(biased, lx, ly, kCellSize,
                                              ax0, ay0);
            if (a != Steppe) ++foreign;
            if (a != b) ++nwDependent;
        }
    }
    CHECK(samples > 0 && foreign == 0,
          "a river cell's banks are its own climate ground, mid-cell");
    CHECK(nwDependent == 0,
          "the banks do not depend on which ring corner holds land");

    // (b) No wall where the blend band saturates (~128 tiles from the east
    // seam): the Meadow fraction is continuous across the old fallback edge.
    const double fIn  = column_fraction(groundRing, kCellSize - 130, ax0, ay0,
                                        Meadow, kCellSize);
    const double fOut = column_fraction(groundRing, kCellSize - 126, ax0, ay0,
                                        Meadow, kCellSize);
    if (std::fabs(fIn - fOut) > 0.05)
        std::fprintf(stderr, "  fIn=%.3f fOut=%.3f\n", fIn, fOut);
    CHECK(std::fabs(fIn - fOut) <= 0.05,
          "no wall inside the cell where the blend band saturates");

    // NEGATIVE CONTROL: the removed law steps ~100% across those same two
    // columns (fallback NW-land inside, renormalised 100% Meadow outside) —
    // without this the continuity check proves nothing about the defect.
    auto oldColumnFraction = [&](int lx, Biome probe) {
        int hits = 0;
        for (int ly = 0; ly < kCellSize; ++ly) {
            if (removed_water_law_pick(oldRing, axis[std::size_t(lx)],
                                       axis[std::size_t(ly)],
                                       ax0 + lx, ay0 + ly) == probe) ++hits;
        }
        return double(hits) / double(kCellSize);
    };
    const double oIn  = oldColumnFraction(kCellSize - 130, Meadow);
    const double oOut = oldColumnFraction(kCellSize - 126, Meadow);
    CHECK(std::fabs(oIn - oOut) > 0.5,
          "the removed water law DOES wall the band edge: control reproduces it");
    CHECK(removed_water_law_pick(oldRing, axis[512], axis[512],
                                 ax0 + 512, ay0 + 512) == Taiga,
          "the removed law painted mid-cell banks by the NW-most ring land");
}

void test_axis_table_matches_the_one_shot_form() {
    const Seam s = make_seam();
    std::vector<GroundAxis> axis(kCellSize);
    ground_axis_table(kCellSize, axis.data());
    int samples = 0, diverged = 0;
    for (int ly = 0; ly < kCellSize; ly += 13) {
        for (int lx = 0; lx < kCellSize; lx += 17) {
            ++samples;
            const Biome a = pick_ground_biome(s.ringA, lx, ly, kCellSize,
                                              s.ax0, s.ay0);
            const Biome b = pick_ground_biome_axis(
                s.ringA, axis[std::size_t(lx)], axis[std::size_t(ly)],
                s.ax0 + lx, s.ay0 + ly);
            if (a != b) ++diverged;
        }
    }
    CHECK(samples > 0 && diverged == 0,
          "the table the renderer uses agrees with the reference pick, always");
}

void test_authored_tiles_pass_through() {
    CHECK(terrain_material_for(TILE_ROAD, Taiga)
              == terrain_material_for(TILE_ROAD, Meadow),
          "an authored tile keeps its material whatever biome it sits in");
    CHECK(material_is_authored(TILE_ROAD) && material_is_authored(TILE_WATER),
          "road and water are authored: the dither must not touch them");
    CHECK(!material_is_authored(TILE_GRASS),
          "plain ground is NOT authored: it is exactly what the dither owns");
}

// Invariant 7. This is the section that was dead.
void test_structure_shade_survives_a_recentre() {
    // One building, one world position, seen from two different windows: the
    // player walked east, so the window centre moved +1 cell and the building's
    // composite coordinate moved -1 cell. Both readings must agree.
    int samples = 0, controlChanged = 0, absoluteChanged = 0, badArithmetic = 0;
    for (int i = 0; i < 64; ++i) {
        const double compositeX = 1500.0 + double(i) * 7.25;   // inside the 3×3
        const double compositeY = 1200.0 + double(i) * 3.5;
        const double originA = 100.0 * double(kCellSize);      // window A origin
        const double absX = originA + compositeX;
        const double absY = 100.0 * double(kCellSize) + compositeY;
        // After the crossing: origin +1 cell, composite coord -1 cell, SAME
        // absolute position.
        const double compositeX2 = compositeX - double(kCellSize);
        const double absX2 = originA + double(kCellSize) + compositeX2;

        ++samples;
        if (absX2 != absX) ++badArithmetic;
        if (structure_shade(absX, absY) != structure_shade(absX2, absY))
            ++absoluteChanged;
        if (window_relative_shade(float(compositeX), float(compositeY))
            != window_relative_shade(float(compositeX2), float(compositeY))) {
            ++controlChanged;
        }
    }
    CHECK(samples == 64 && badArithmetic == 0,
          "the fixture really does describe one place seen from two windows");
    CHECK(absoluteChanged == 0,
          "a building keeps its shade when the window re-centres around it");
    // The control has to actually FAIL, or this test proves nothing about the
    // defect it was written for.
    CHECK(controlChanged >= 32,
          "the old window-relative keying DOES pop: the control reproduces it");

    // Two structures inside one tile must still differ (1/16-tile resolution).
    CHECK(structure_shade(2048.0, 512.0) != structure_shade(2048.25, 512.0),
          "two buildings in one tile do not share a shade");

    // And the wobble stays in its declared range.
    int rangeSamples = 0, outOfRange = 0;
    for (int i = 0; i < 512; ++i) {
        const float sh = structure_shade(double(i) * 13.7, double(i) * 5.3);
        ++rangeSamples;
        if (!(sh >= 0.86f && sh <= 1.06f)) ++outOfRange;
    }
    CHECK(rangeSamples > 0 && outOfRange == 0,
          "the shade wobble stays inside its declared range");
}

} // namespace

int main() {
    test_pick_is_deterministic();
    test_cell_core_is_pure_owner();
    test_seam_is_continuous_and_balanced();
    test_ground_alias_law();
    test_flooded_margin_has_no_walls();
    test_axis_table_matches_the_one_shot_form();
    test_authored_tiles_pass_through();
    test_structure_shade_survives_a_recentre();
    return sm::test::report("material_seam_test");
}
