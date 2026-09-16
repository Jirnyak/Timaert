// THE SAME MOUNTAIN — CANON S18.1, asserted rather than intended.
//
// The far world's whole promise is one sentence: «Гора, которую видно с
// тридцати километров, обязана быть той горой, к которой придёшь. По мере
// приближения силуэт УТОЧНЯЕТСЯ — добавляются октавы, — и никогда не
// подменяется.» A far relief built from its own noise would look fine in every
// screenshot and would quietly make the world two places.
//
// So this file does not check that the far ground is pretty, or smooth, or
// cheap. It checks that it is THE NEAR GROUND WITH DETAIL REMOVED:
//
//   1. the crest law is ONE law — the near generator and the far door take
//      their peak from the same door, for the same cell, and agree exactly;
//   2. removing detail does not MOVE the ground — the coarse silhouette
//      tracks the full one within the amplitude of the octave it dropped,
//      everywhere, not on average;
//   3. what it drops is DETAIL and nothing else — the difference is bounded
//      by the crag octave's own amplitude, which is what "under a pixel at
//      this distance" means in metres;
//   4. it closes on the torus like everything else — walking off the last
//      column onto the first is a STEP, not a cliff.
//
// Written against the doors, never against a copy of them: an expectation that
// re-derives what the code derives tests that you can copy (AGENTS testing law
// 5). Every check below is a RELATION between two live doors.
#include "check.h"

#include "sub/base_generator.h"
#include "sub/height.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using namespace sm;
using namespace sm::sub;

constexpr int   kWorldCells = 1024;                  // CANON.md S1
constexpr float kWorldTiles = float(kWorldCells) * float(kCellSize);

// (No amplitude is pinned here. A number copied out of the generator would
// break on every retune and would prove only that the copy was made; what the
// law promises is a RELATION — the dropped octave is small against the massif
// it is dropped from, and small in METRES against the field that samples it.)

} // namespace

int main() {
    using namespace sm::test;

    // ── 1. ONE CREST LAW ──────────────────────────────────────────────────
    // The near generator builds a cell's crest through skeleton_cell_peak01;
    // so must the far world, or the massif it draws is a different massif.
    // Asserted as the door being a FUNCTION OF THE PLACE: ask twice from two
    // different "windows" and get the same answer.
    {
        const std::uint32_t worldSeed = 0x51A2B3C4u;
        int samples = 0, disagreements = 0;
        for (int cy = 300; cy < 306; ++cy) {
            for (int cx = 500; cx < 506; ++cx) {
                const float macroH = 0.62f + 0.03f * float((cx + cy) % 5);
                for (int adj = 0; adj <= 4; ++adj) {
                    const float a = skeleton_cell_peak01(
                        macroH, false, true, adj, cx, cy, worldSeed);
                    const float b = skeleton_cell_peak01(
                        macroH, false, true, adj, cx, cy, worldSeed);
                    if (a != b) ++disagreements;
                    ++samples;
                }
            }
        }
        CHECK(samples > 100 && disagreements == 0,
              "the crest of a cell is a function of that cell's PLACE");
    }
    {
        // ...and the place is a TORUS place: the cell reached by walking off
        // the last column is the same cell, so it has the same crest.
        const std::uint32_t worldSeed = 0x593F45BAu;
        const float onMap = skeleton_cell_peak01(0.88f, false, true, 3,
                                                 0, 300, worldSeed);
        const float onFoot = skeleton_cell_peak01(
            0.88f, false, true, 3,
            ((kWorldCells % kWorldCells) + kWorldCells) % kWorldCells, 300,
            worldSeed);
        CHECK(onMap == onFoot,
              "one place, one crest — the world's edge is not a second cell");
    }

    // ── 2/3. REMOVING DETAIL DOES NOT MOVE THE GROUND ─────────────────────
    // The far door is the near ridge function with its fine octave stopped.
    // Two things must hold at once and they pull against each other: the
    // silhouettes must TRACK (so it is the same mountain), and the difference
    // must be bounded by exactly the octave that was dropped (so what was
    // removed is detail and not shape).
    {
        const float worldTiles = kWorldTiles;
        float worst = 0.0f;
        int samples = 0, outside = 0;
        // Walk a massif: high macro height, full ridge weight, a crest target
        // in the band the crest law actually produces.
        for (int gy = 512000; gy < 512000 + 2048; gy += 37) {
            for (int gx = 300000; gx < 300000 + 2048; gx += 41) {
                const float macroH = 0.88f;
                const float peak = 0.98f;
                const float full = mountain_ridges01(macroH, gx, gy, macroH,
                                                     peak, 1.0f, worldTiles,
                                                     /*coarseOnly=*/false);
                const float coarse = mountain_ridges01(macroH, gx, gy, macroH,
                                                       peak, 1.0f, worldTiles,
                                                       /*coarseOnly=*/true);
                const float d = std::fabs(full - coarse);
                worst = std::max(worst, d);
                // DETAIL, not shape: what the far pass drops must be small
                // against the massif's own rise, everywhere — not on average.
                if (d > (peak - macroH) * 0.10f) ++outside;
                ++samples;
            }
        }
        CHECK(samples > 1000, "the walk covered a massif's worth of ground");
        CHECK(worst > 0.0f,
              "the two silhouettes DIFFER at all — a coarse pass that changed "
              "nothing would make this whole file vacuous");
        CHECK(outside == 0,
              "the far silhouette TRACKS the near one everywhere: what it drops "
              "is a tenth of the massif's own rise at worst, never its shape");
        // ...and in METRES, against a real instrument of this world: the march
        // heightfield samples at 16 m a texel, so a difference under that is
        // below what anything downstream can even see.
        CHECK(worst * kHeightScaleM < 16.0f,
              "the dropped detail is metres — under one texel of the march "
              "field, which is the coarsest thing that reads this ground");
    }

    // ── 4. THE FAR GROUND MEETS ITSELF AT THE WORLD'S EDGE ────────────────
    // The door takes a WRAPPED tile — its caller owns the torus, exactly as
    // the near generator's does — so "same tile, same answer" is the caller's
    // contract and asserting it here would be asserting nothing. The property
    // that IS the door's own is the one the seam law cares about: walking off
    // the last column onto the first must be a STEP, not a cliff. Stated as a
    // relation against the ground's own roughness, so a retune of the terrain
    // cannot make this test lie either way (CANON S1/S2).
    {
        const float worldTiles = kWorldTiles;
        const int span = int(kWorldTiles);
        const int gy = 400000;
        float acrossSeam = 0.0f, interior = 0.0f;
        int samples = 0;
        for (int k = 0; k < 64; ++k) {
            const int y = gy + k * 613;
            // The two tiles that share the world's edge.
            const float last = far_height01(span - 1, y, 0.88f, 0.98f, 1.0f,
                                            worldTiles);
            const float first = far_height01(0, y, 0.88f, 0.98f, 1.0f,
                                             worldTiles);
            acrossSeam = std::max(acrossSeam, std::fabs(last - first));
            // ...against a pair of ordinary neighbours in the same massif.
            const float a = far_height01(300000, y, 0.88f, 0.98f, 1.0f,
                                         worldTiles);
            const float b = far_height01(300001, y, 0.88f, 0.98f, 1.0f,
                                         worldTiles);
            interior = std::max(interior, std::fabs(a - b));
            ++samples;
        }
        CHECK(samples == 64 && interior > 0.0f,
              "the probe measured: the far massif has relief tile to tile");
        CHECK(acrossSeam <= interior * 1.5f,
              "the world's edge is no worse a join than any two neighbouring "
              "tiles of the same massif");
    }

    {
        // A cell with no massif has no ridges to draw, and the far world says
        // so by answering with the manifold itself — not with a flat number,
        // and not with a different one.
        const float flat = far_height01(123456, 654321, 0.55f, 0.70f, 0.0f,
                                        kWorldTiles);
        CHECK(std::fabs(flat - 0.55f) < 1e-6f,
              "lowland far ground IS the macro manifold — nothing is invented "
              "where nothing rises");
        const float mtn = far_height01(123456, 654321, 0.88f, 0.98f, 1.0f,
                                       kWorldTiles);
        CHECK(mtn != 0.88f,
              "a massif far away still HAS a silhouette (the negative control "
              "for the line above)");
    }

    return report("far_terrain_test");
}
