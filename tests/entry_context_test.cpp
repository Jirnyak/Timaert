// Entry-side context contract (macro/entry_context.h) — the two bytes that
// carry "which side did this walker enter its macro cell from, and how long
// ago" from the macro world into subworld spawn placement.
//
// What is pinned:
//   1. pack/unpack round-trips all 8 signed steps; (0,0) and the sentinel are
//      "no entry" and unpack to a zero step (= uniform placement downstream).
//   2. The reachable band: a fresh south entrant lands in a shallow band at
//      the south edge; the mirrored direction mirrors the band; time in cell
//      DEEPENS the band monotonically; a saturated counter covers (almost) the
//      whole cell — i.e. the formula degrades to the old uniform scatter with
//      no special case, which is the design's whole point.
//   3. The deterministic mid-band placement (u = 0.5) the player uses starts
//      near the entry edge and converges on the exact cell centre as time
//      saturates — the pre-context spawn point, so "no context" and "old
//      context" are the same place.
//
// Pure — header only. No ECS, no Vulkan.
#include "macro/entry_context.h"

#include <cmath>
#include <cstdio>

using namespace sm;

namespace {

int fails = 0;
void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL entry_context_test: %s\n", msg);
        ++fails;
    }
}

constexpr float kCell = 1024.0f;

void test_pack_unpack() {
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            const std::uint8_t d = pack_entry_dir(dx, dy);
            int ux = 9, uy = 9;
            const bool has = unpack_entry_dir(d, ux, uy);
            if (dx == 0 && dy == 0) {
                check(d == kEntryDirNone && !has && ux == 0 && uy == 0,
                      "(0,0) packs to the sentinel and unpacks to a zero step");
            } else {
                check(has && ux == dx && uy == dy,
                      "signed step round-trips through the byte");
            }
        }
    }
    // Larger deltas (a multi-cell frame skip) clamp to their sign.
    int ux = 0, uy = 0;
    check(unpack_entry_dir(pack_entry_dir(3, -7), ux, uy)
              && ux == 1 && uy == -1,
          "multi-cell deltas clamp to the unit step");
    check(!unpack_entry_dir(kEntryDirNone, ux, uy) && ux == 0 && uy == 0,
          "the sentinel unpacks to no step");
    check(!unpack_entry_dir(200, ux, uy), "garbage bytes are refused");
}

void test_band_geometry() {
    // Fresh entry moving +axis (crossed the LOW edge): the whole band sits in
    // a shallow strip past the margin.
    const float lo0 = entry_axis_pos(+1, 0, kCell, 0.0f);
    const float hi0 = entry_axis_pos(+1, 0, kCell, 1.0f);
    check(lo0 == kEntryEdgeMargin, "fresh band starts at the edge margin");
    check(hi0 <= kEntryEdgeMargin + kEntryBandDepth + 0.001f,
          "fresh band is shallow");

    // The mirrored direction mirrors the band exactly.
    check(std::fabs((kCell - entry_axis_pos(-1, 0, kCell, 0.0f)) - lo0) < 0.001f,
          "-axis entry mirrors the band to the far edge");

    // Depth grows monotonically with ticks…
    float prev = entry_axis_pos(+1, 0, kCell, 1.0f);
    for (int t = 8; t <= 255; t += 8) {
        const float reach = entry_axis_pos(+1, std::uint8_t(t), kCell, 1.0f);
        check(reach >= prev - 0.001f, "band depth never shrinks with time");
        prev = reach;
    }
    // …and saturates at (cell - margin): the band has become the whole cell
    // minus the edge margins — the old uniform scatter, no special case.
    check(std::fabs(entry_axis_pos(+1, 255, kCell, 1.0f)
                    - (kCell - kEntryEdgeMargin)) < 0.001f,
          "saturated band reaches the far margin");

    // No direction on an axis = uniform over the cell, and even a u of
    // exactly 1.0 (the known rng.next_f01 contract hole) stays inside.
    check(entry_axis_pos(0, 0, kCell, 0.0f) == 0.0f
              && entry_axis_pos(0, 0, kCell, 1.0f) <= kCell - 1.0f,
          "axis without info is uniform and in-bounds");
}

void test_player_mid_band() {
    // u = 0.5 (the player's deterministic placement): starts near the entry
    // edge, deepens with time, and converges on the EXACT centre — which is
    // also where the sentinel places it, so a fresh world, a teleport and a
    // long stay all agree on the old spawn point.
    const float fresh = entry_axis_pos(+1, 0, kCell, 0.5f);
    check(fresh < kCell * 0.25f, "fresh player spawns near the entry edge");
    const float later = entry_axis_pos(+1, 40, kCell, 0.5f);
    check(later > fresh, "time in cell moves the player spawn deeper");
    check(std::fabs(entry_axis_pos(+1, 255, kCell, 0.5f) - kCell * 0.5f)
              < 0.001f,
          "saturated mid-band IS the cell centre");
    check(std::fabs(entry_axis_pos(0, 0, kCell, 0.5f) - kCell * 0.5f) < 0.001f,
          "no-context mid-band IS the cell centre");
}

void test_saturating_counter() {
    std::uint8_t t = 0;
    for (int i = 0; i < 300; ++i) t = saturate_entry_ticks(t);
    check(t == 255, "the tick counter saturates instead of wrapping");
}

} // namespace

int main() {
    test_pack_unpack();
    test_band_geometry();
    test_player_mid_band();
    test_saturating_counter();
    if (fails == 0) std::fprintf(stderr, "entry_context_test: OK\n");
    return fails == 0 ? 0 : 1;
}
