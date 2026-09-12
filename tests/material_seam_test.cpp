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
//   6b. THE GROUND-BOUNDARY LAW (material.h ground_dither01) — the field that
//      decides which of two close claims owns a tile is CORRELATED, and every
//      boundary in the micro world consults that one field. A per-tile coin
//      (what this was until 2026-09-12) makes pepper: a metre of stone, a
//      metre of grass, a metre of stone, where nature puts patches. The
//      owner photographed it on a mountainside and asked whether the defect
//      was the mountain's or everyone's — it was everyone's, because the
//      treeline and the biome seam each flipped their own copy of the same
//      coin. NEGATIVE CONTROL: the coin is reimplemented here and shown to
//      break into runs of ~2 tiles where the field holds ~20.
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

#include <array>

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
    const float r = ground_dither01(absX, absY) * total;
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


// ── 6b. The ground-boundary law ─────────────────────────────────────────────
// Three properties, and the third is the one the owner can see.
void test_ground_boundary_law_is_correlated_and_unbiased() {
    // (a) UNBIASED. The field replaces a uniform coin, so it must still be
    //     uniform in the mean — otherwise the AMOUNT of stone in a treeline
    //     band (or of one biome in a seam) would silently change with the
    //     arrangement, which is not what was being fixed.
    double sum = 0.0;
    int n = 0, lo = 0, hi = 0, outOfRange = 0;
    for (long long y = -500; y < 500; y += 7) {
        for (long long x = -500; x < 500; x += 7) {
            const float r = sub::ground_dither01(x, y);
            if (!(r >= 0.0f && r <= 1.0f)) ++outOfRange;
            sum += r;
            ++n;
            if (r < 0.25f) ++lo;
            if (r > 0.75f) ++hi;
        }
    }
    CHECK(outOfRange == 0, "the boundary field stays inside [0,1]");
    const double mean = sum / double(n);
    CHECK(mean > 0.45 && mean < 0.55, "the boundary field is unbiased");
    CHECK(lo > n / 20 && hi > n / 20,
          "...and it spans its range, not just the middle");

    // (b) CORRELATED. Neighbouring metres must agree far more often than a
    //     coin does. A coin agrees 50% of the time on which side of a
    //     threshold it lands; a field with a 24 m correlation length agrees
    //     almost always.
    int agreeField = 0, agreeCoin = 0, pairs = 0;
    for (long long y = 0; y < 400; ++y) {
        for (long long x = 0; x < 400; ++x) {
            const bool a = sub::ground_dither01(x, y) < 0.5f;
            const bool b = sub::ground_dither01(x + 1, y) < 0.5f;
            const bool ca = sub::tile_hash01(x, y) < 0.5f;
            const bool cb = sub::tile_hash01(x + 1, y) < 0.5f;
            if (a == b) ++agreeField;
            if (ca == cb) ++agreeCoin;
            ++pairs;
        }
    }
    CHECK(agreeField > pairs * 9 / 10,
          "neighbouring metres land on the same side of the field");
    CHECK(agreeCoin < pairs * 3 / 5,
          "...which the coin it replaced did not (negative control)");

    // (c) NO PEPPER, measured as the owner sees it: walk a line through the
    //     middle of a treeline band and count how long a stretch of one
    //     answer lasts. The coin gives runs of ~2 tiles — that IS the pepper.
    //     A patch is tens of metres.
    auto meanRun = [](bool useField) {
        const float t = 0.5f; // mid-band: the hardest case, 50/50 by claim
        int runs = 0, total = 0;
        bool prev = false;
        for (long long x = 0; x < 4000; ++x) {
            const bool rock = useField
                ? sub::treeline_is_rock(t, x, 12345)
                : (sub::tile_hash01(x * 7 + 3, 12345 * 7 - 5) < t);
            if (x == 0 || rock != prev) ++runs;
            prev = rock;
            ++total;
        }
        return double(total) / double(runs);
    };
    const double runField = meanRun(true);
    const double runCoin = meanRun(false);
    CHECK(runField > 8.0, "stone comes in patches, not in grains");
    CHECK(runCoin < 3.0, "...where the coin gave grains (negative control)");
    std::fprintf(stderr,
                 "  [law] mean run: field %.1f tiles, coin %.1f tiles\n",
                 runField, runCoin);

    // (d) ONE law, TWO consumers, decorrelated by an OFFSET — the treeline and
    //     the seam must not draw the same blotches on top of each other, and
    //     the offset (never a coordinate scale, which would shrink the patch)
    //     is what keeps them apart.
    int same = 0, cmp = 0;
    for (long long y = 0; y < 200; y += 3) {
        for (long long x = 0; x < 200; x += 3) {
            const bool a = sub::ground_dither01(x, y) < 0.5f;
            const bool b = sub::ground_dither01(x + 9973, y - 7919) < 0.5f;
            if (a == b) ++same;
            ++cmp;
        }
    }
    CHECK(same > cmp / 5 && same < cmp * 4 / 5,
          "the treeline's patches are independent of the seam's");
}


// ── 6c. The row form is the same law ───────────────────────────────────────
// The renderer's million-tile fill draws the boundary field ONCE PER ROW
// (GroundDitherRow) because a field is constant over its lattice — that is
// what makes it cheaper per tile than the coin it replaced, and the seam's
// load time is the one number in this game only ever allowed to go down
// (owner, 2026-09-12). Cheaper is worth nothing if it answers differently.
//
// BIT-exactness is NOT the bar, and that is a finding, not a shrug: this TU
// ships with -ffast-math, so each inlined site may contract its own
// multiply-add and the two forms part company in the last bit (measured:
// ~1e-7 on a quarter of a sweep). The height self-check reached the same
// verdict for the same reason. What must agree is the DECISION — the byte a
// tile ends up with — so that is what is checked, over every threshold a
// decision can turn on.
void test_row_form_equals_the_one_shot() {
    // (a) the field agrees to float tolerance, and the worst delta is small
    //     enough that a decision can only differ when a threshold lands
    //     within 1e-6 of the value — which (b) then rules out by sweeping.
    int rows = 0;
    float worst = 0.0f;
    std::array<float, 256> row{};
    for (long long y : {-4097LL, -1LL, 0LL, 7LL, 1024LL, 99991LL}) {
        for (long long x0 : {-8193LL, -25LL, 0LL, 23LL, 4096LL, 123457LL}) {
            sub::GroundDitherRow walk;
            walk.begin(y);
            for (int i = 0; i < int(row.size()); ++i)
                row[std::size_t(i)] = walk.at(x0 + i);
            for (int i = 0; i < int(row.size()); ++i) {
                const float d = std::fabs(row[std::size_t(i)]
                                          - sub::ground_dither01(x0 + i, y));
                if (d > worst) worst = d;
            }
            ++rows;
        }
    }
    CHECK(rows == 36, "the sweep ran every row it meant to");
    CHECK(worst < 1e-5f, "the row form equals the one-shot to tolerance");
    std::fprintf(stderr, "  [row] worst field delta %.3g\n", double(worst));

    // (b) THE DECISIONS. Both doors the renderer calls must answer exactly
    //     like their one-shot twins — this is the byte-for-byte claim, made
    //     where bytes are actually decided.
    int treeCases = 0, treeBad = 0, pickCases = 0, pickBad = 0;
    const Biome ring[9] = {Biome::Taiga,  Biome::Taiga,  Biome::Meadow,
                           Biome::Taiga,  Biome::Meadow, Biome::Meadow,
                           Biome::Valley, Biome::Meadow, Biome::Swamp};
    sub::GroundAxis axis[64];
    sub::ground_axis_table(64, axis);
    for (long long y = -60; y < 60; ++y) {
        sub::GroundDitherRow seamWalk, treeWalk;
        seamWalk.begin(y);
        treeWalk.begin(y - 7919);
        std::array<float, 256> treeR{};
        for (int i = 0; i < int(row.size()); ++i) {
            row[std::size_t(i)] = seamWalk.at(i);
            treeR[std::size_t(i)] = treeWalk.at(i + 9973);
        }
        for (int x = 0; x < 64; ++x) {
            const sub::GroundAxis ay = axis[std::size_t((y + 64) % 64)];
            sub::GroundDitherRow pickWalk;
            pickWalk.begin(y);
            if (sub::pick_ground_biome_axis(ring, axis[std::size_t(x)], ay,
                                            pickWalk, x)
                != sub::pick_ground_biome_axis(ring, axis[std::size_t(x)], ay,
                                               x, y)) ++pickBad;
            ++pickCases;
            for (float h : {0.60f, 0.73f, 0.80f, 0.86f, 0.91f, 0.95f}) {
                if (sub::apply_mountain_treeline_at(Biome::Meadow, h,
                                                    treeR[std::size_t(x)])
                    != sub::apply_mountain_treeline(Biome::Meadow, h, x, y))
                    ++treeBad;
                ++treeCases;
            }
        }
    }
    CHECK(pickCases > 5000 && pickBad == 0,
          "the row-fed biome pick answers exactly like the one-shot");
    CHECK(treeCases > 30000 && treeBad == 0,
          "the row-fed treeline answers exactly like the one-shot");
    if (pickBad || treeBad)
        std::fprintf(stderr, "    decisions differed: pick %d/%d tree %d/%d\n",
                     pickBad, pickCases, treeBad, treeCases);
}


// ── 6d. The fill's hoisted form is the honest form ─────────────────────────
// The renderer's million-tile fill does not ask the law per tile any more. It
// hoists the four grounds a tile blends once per axis SPAN (they are constant
// along one), skips the pick entirely where all four are the same ground, and
// reads the material out of an eleven-byte table instead of calling
// terrain_material_for's two switches. That took the fill from 19.4 ms — its
// cost before any of this work — down to 18.4, which is the only reason the
// boundary law is allowed to exist: the seam's load time moves in one
// direction (owner, 2026-09-12).
//
// Every one of those moves is a claim that two expressions are the same. This
// checks the claim, tile by tile, against the honest per-tile form.
void test_hoisted_fill_matches_the_honest_form() {
    const Biome rings[3][9] = {
        {Biome::Taiga, Biome::Taiga, Biome::Meadow,
         Biome::Taiga, Biome::Meadow, Biome::Meadow,
         Biome::Valley, Biome::Meadow, Biome::Swamp},
        {Biome::Meadow, Biome::Meadow, Biome::Meadow,
         Biome::Meadow, Biome::Meadow, Biome::Meadow,
         Biome::Meadow, Biome::Meadow, Biome::Meadow},
        {Biome::Snow, Biome::Tundra, Biome::Snow,
         Biome::Tundra, Biome::Mountain, Biome::Tundra,
         Biome::Snow, Biome::Tundra, Biome::Desert},
    };
    constexpr int kSide = 96;
    sub::GroundAxis axis[kSide];
    sub::ground_axis_table(kSide, axis);
    const std::uint8_t* biomeMat = sub::biome_ground_materials();

    // The spans, found the way the fill finds them.
    int spanEnd[4] = {kSide, kSide, kSide, kSide};
    int spans = 0;
    for (int x = 1; x <= kSide && spans < 3; ++x)
        if (x == kSide || axis[x].i0 != axis[x - 1].i0
            || axis[x].i1 != axis[x - 1].i1)
            spanEnd[spans++] = x;
    CHECK(spans >= 1 && spans <= 3, "a row crosses at most three axis spans");

    int cases = 0, bad = 0;
    for (int r = 0; r < 3; ++r) {
        const Biome* ring = rings[r];
        for (int y = 0; y < kSide; y += 5) {
            const sub::GroundAxis ay = axis[y];
            sub::GroundDitherRow seamRow, treeRow;
            seamRow.begin(y);
            treeRow.begin(y - 7919);
            int x0 = 0;
            for (int sp = 0; sp < spans; ++sp) {
                const sub::GroundCorners corners =
                    sub::ground_corners(ring, axis[x0], ay);
                for (int x = x0; x < spanEnd[sp]; ++x) {
                    const float h = 0.55f + 0.005f * float((x * 7 + y) % 90);
                    // HOISTED — what the fill now does.
                    Biome bh = corners.uniform
                        ? corners.b00
                        : sub::pick_ground_biome_corners(corners, axis[x].f,
                                                         ay.f, seamRow, x);
                    bh = sub::apply_mountain_treeline_row(bh, h, treeRow, x);
                    const std::uint8_t got = biomeMat[static_cast<int>(bh)];
                    // HONEST — what it did before, per tile, through the
                    // one-shot doors and the two switches.
                    Biome bo = sub::pick_ground_biome_axis(ring, axis[x], ay,
                                                           x, y);
                    bo = sub::apply_mountain_treeline(bo, h, x, y);
                    const std::uint8_t want = static_cast<std::uint8_t>(
                        sub::terrain_material_for(TILE_EMPTY, bo));
                    if (got != want) ++bad;
                    ++cases;
                }
                x0 = spanEnd[sp];
            }
        }
    }
    CHECK(cases > 5000, "the sweep covered every span of every ring");
    CHECK(bad == 0, "the hoisted fill writes the honest byte, tile for tile");
    if (bad) std::fprintf(stderr, "    %d of %d tiles differed\n", bad, cases);

    // And the eleven-byte table is the door's own answer for every biome.
    int tabBad = 0;
    for (int i = 0; i < 11; ++i)
        if (biomeMat[i] != static_cast<std::uint8_t>(
                sub::terrain_material_for(TILE_EMPTY, static_cast<Biome>(i))))
            ++tabBad;
    CHECK(tabBad == 0, "the biome-ground table cannot drift from its door");
}

int main() {
    test_pick_is_deterministic();
    test_cell_core_is_pure_owner();
    test_seam_is_continuous_and_balanced();
    test_ground_alias_law();
    test_flooded_margin_has_no_walls();
    test_axis_table_matches_the_one_shot_form();
    test_authored_tiles_pass_through();
    test_structure_shade_survives_a_recentre();
    test_ground_boundary_law_is_correlated_and_unbiased();
    test_row_form_equals_the_one_shot();
    test_hoisted_fill_matches_the_honest_form();
    return sm::test::report("material_seam_test");
}
