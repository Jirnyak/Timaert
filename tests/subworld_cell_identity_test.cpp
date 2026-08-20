// A CELL IS A PLACE, SO ITS CONTENT IS A FUNCTION OF THE PLACE.
//
// The subworld is a 3×3 frame of macro cells (CANON.md S2), and the frame's
// centre used to be a running counter that never wrapped: walk east off the
// last column and the cell called itself 1024 while reading macro cell 0's
// biome, height and landmark. Everything keyed off that number — the detail
// noise offset, the road anchor seed, the tile hash — therefore built a
// DIFFERENT subworld for the same place depending on how the player arrived.
// Measured before the fix: 100 % of tiles differed and heights by up to 63 m,
// so leaving and re-entering the same cell across the seam rebuilt the world
// under your feet — and the session cache made it worse, restoring heights but
// not tiles, so shore and water were drawn from one relief while you stood on
// another.
//
// Two invariants, and the second is the reason the first is safe to have:
//   1. the same macro cell generates the same subworld whichever coordinate the
//      window arrived by;
//   2. neighbouring cells still meet, INCLUDING across the world's own seam —
//      because the noise now closes on the world's tile span rather than
//      running off to infinity.
#include "check.h"

#include "sub/base_generator.h"
#include "sub/map_data.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kWorldCells = 1024;          // CANON.md S1
constexpr int kCS = 64;                    // a small cell keeps the test quick

// One cell's heightmap, generated exactly as the dispatcher does: the cell's
// own index decides the global offset, the world's width closes the noise.
std::vector<float> cell_height(int cx, int cy, std::uint32_t seed) {
    float nbH[9];
    sm::Biome nbB[9];
    for (int i = 0; i < 9; ++i) { nbH[i] = 0.62f; nbB[i] = sm::Biome::Meadow; }
    std::vector<float> out;
    sm::sub::generate_heightmap(out, kCS, nbH, nbB, sm::Biome::Meadow, seed,
                                cx * kCS, cy * kCS, nullptr, kWorldCells);
    return out;
}

float column_gap(const std::vector<float>& left, const std::vector<float>& right) {
    float worst = 0.0f;
    for (int y = 0; y < kCS; ++y) {
        const float a = left[std::size_t(y) * kCS + (kCS - 1)];
        const float b = right[std::size_t(y) * kCS + 0];
        worst = std::max(worst, std::fabs(a - b));
    }
    return worst;
}

float interior_step(const std::vector<float>& c) {
    float worst = 0.0f;
    for (int y = 0; y < kCS; ++y)
        for (int x = 1; x < kCS; ++x) {
            worst = std::max(worst, std::fabs(c[std::size_t(y) * kCS + x]
                                            - c[std::size_t(y) * kCS + x - 1]));
        }
    return worst;
}

} // namespace

int main() {
    using namespace sm;

    // ── 1. One place, one world ──────────────────────────────────────────
    // Cell 0 reached by walking east off the last column used to be generated
    // as "cell 1024". The index is wrapped now, so there is only one cell 0 —
    // and this test asserts the property the WRAP is for, by generating what
    // the two arrivals would produce and demanding they agree exactly.
    {
        const std::uint32_t seed = 0x593F45BAu;
        const std::vector<float> arrivedByMap = cell_height(0, 300, seed);
        const std::vector<float> arrivedOnFoot =
            cell_height(kWorldCells, 300, seed);   // the old running counter
        int differing = 0;
        float worst = 0.0f;
        for (std::size_t i = 0; i < arrivedByMap.size(); ++i) {
            const float d = std::fabs(arrivedByMap[i] - arrivedOnFoot[i]);
            if (d > 0.0f) ++differing;
            worst = std::max(worst, d);
        }
        CHECK(differing == 0,
              "the same macro cell is the same subworld however you got there");
        CHECK(worst == 0.0f, "not one tile of it moved");
    }

    // ── 2. And the neighbours still meet, seam included ──────────────────
    // Wrapping the name would be a bad trade if it bought a cliff at the
    // world's edge, so the noise closes on the world: the last cell's right
    // column must meet the first cell's left column as tightly as any two
    // ordinary neighbours meet.
    {
        const std::uint32_t seed = 0x51A2B3C4u;
        const std::vector<float> mid0 = cell_height(500, 300, seed);
        const std::vector<float> mid1 = cell_height(501, 300, seed);
        const std::vector<float> last = cell_height(kWorldCells - 1, 300, seed);
        const std::vector<float> first = cell_height(0, 300, seed);

        const float ordinary = column_gap(mid0, mid1);
        const float atSeam   = column_gap(last, first);
        const float inside   = interior_step(mid0);

        CHECK(inside > 0.0f, "the ground has relief at all (the probe measured)");
        // The seam may not be worse than an ordinary cell border. Stated as a
        // RELATION, not a pinned number, so a retune of the terrain cannot make
        // this test lie either way.
        CHECK(atSeam <= ordinary * 1.5f + inside,
              "the world's edge is no worse a join than any other cell border");
    }

    return sm::test::report("subworld_cell_identity_test");
}
