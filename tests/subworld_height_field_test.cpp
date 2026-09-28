// THE WORLD'S VERTICAL TRUTH HAS ONE SAMPLING LAW (sub/height.h).
//
// The height field refreshes three different ways — a full resample, a
// toroidal slide across a seam crossing, and a per-cell block when an async
// cell is stitched in — and the whole point of the cheap two is that they
// produce EXACTLY what the expensive one would. Not approximately: the surface
// bodies stand on and the mesh drawn under them are the same numbers, so a
// last-bit disagreement is a body a hair inside the ground on one path and a
// hair above it on the other.
//
// This used to be an env-var fprintf inside the renderer's upload path
// (TIMAERT_SEAM_SELFCHECK) that compared to a TOLERANCE of 1e-2, because the
// sampler was inlined at several call sites and -ffast-math reassociated each
// one differently. Collapsing every path onto the single `resample_block` call
// site removed the excuse, so the witness asserts what the law actually says:
// bit for bit.
#include "check.h"

#include "sub/height.h"
#include "sub/seamless_manager.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

using sm::sub::kHeightVerts;
using sm::sub::SubworldHeightField;

// A window with REAL RELIEF. A flat world would pass every comparison below
// while proving nothing — the same class of lie as a veto witness green on a
// world with no trees — so the cells differ in height and one of them is a
// mountain, and the test asserts the relief exists before trusting any parity.
sm::sub::CellContext resolve_relief_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    const int mx = ((cx % 3) + 3) % 3;
    const int my = ((cy % 3) + 3) % 3;
    const int slot = my * 3 + mx;
    c.macroHeight = 0.50f + 0.06f * float(slot);
    c.biome = (slot == 4) ? sm::Biome::Mountain : sm::Biome::Meadow;
    c.feature = (slot == 1 || slot == 7) ? sm::FT_Road : sm::FT_None;
    c.landmark.id = -1;
    c.landmark.size = 0;
    c.landmark.kind = sm::LandmarkType::None;
    c.seed = 0x4e1d0000u ^ sm::sub::cell_seed(0u, cx, cy);
    return c;
}

// What the expensive path says, built fresh from the same composite.
void build_reference(const sm::sub::SeamlessSubworldManager& mgr,
                     SubworldHeightField& out) {
    sm::sub::CompositeDirty full;
    full.any = true;
    full.fullHeight = true;
    out.refresh(mgr, full);
}

// Bit-for-bit comparison over the whole grid. Counts what it looked at, so a
// silent zero-sample pass is impossible (AGENTS §8 п.3).
std::size_t grid_mismatches(const SubworldHeightField& a,
                            const SubworldHeightField& b,
                            std::size_t& samples) {
    const float* pa = a.vertices();
    const float* pb = b.vertices();
    std::size_t mism = 0;
    int x0 = kHeightVerts, x1 = -1, y0 = kHeightVerts, y1 = -1;
    float worst = 0.0f;
    for (int y = 0; y < kHeightVerts; ++y) {
        for (int x = 0; x < kHeightVerts; ++x) {
            const std::size_t i = std::size_t(y) * kHeightVerts + std::size_t(x);
            ++samples;
            if (pa[i] == pb[i]) continue;
            ++mism;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
            const float d = pa[i] > pb[i] ? pa[i] - pb[i] : pb[i] - pa[i];
            if (d > worst) worst = d;
        }
    }
    // A failure has to say WHERE, or the next reader is left guessing which of
    // the three refresh paths drifted.
    if (mism) {
        std::fprintf(stderr,
                     "[height-field] %zu mismatches in x[%d..%d] y[%d..%d], "
                     "worst delta %.9g m\n",
                     mism, x0, x1, y0, y1, double(worst));
    }
    return mism;
}

void case_full_build_and_door() {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_relief_cell);
    mgr.consume_composite_dirty();

    const SubworldHeightField& live = mgr.height_field();
    CHECK_OR_RETURN(live.built(), "the window composited but stated no height");

    // The witness's own premise: this world HAS relief. Parity on a flat field
    // would be a tautology.
    CHECK(live.max_m() - live.min_m() > 50.0f,
          "test world is too flat to prove anything about sampling");

    SubworldHeightField ref;
    build_reference(mgr, ref);
    std::size_t samples = 0;
    const std::size_t mism = grid_mismatches(live, ref, samples);
    CHECK(samples == std::size_t(kHeightVerts) * kHeightVerts && mism == 0,
          "first build disagrees with a fresh full resample");
    CHECK(live.max_m() == ref.max_m() && live.min_m() == ref.min_m(),
          "the window extent disagrees with a fresh full resample");

    // THE DOOR AND THE GRID ARE THE SAME ANSWER. A composite tile that lands
    // exactly on a vertex must sample to that vertex — otherwise bodies stand
    // on one surface and the mesh is built from another.
    std::size_t vertexSamples = 0, vertexMism = 0;
    for (int y = 0; y < kHeightVerts; y += 17) {
        for (int x = 0; x < kHeightVerts; x += 17) {
            const float tileX = float(x * sm::sub::kHeightQuadTiles);
            const float tileY = float(y * sm::sub::kHeightQuadTiles);
            ++vertexSamples;
            if (live.sample(tileX, tileY)
                != live.vertices()[std::size_t(y) * kHeightVerts + x]) {
                ++vertexMism;
            }
        }
    }
    CHECK(vertexSamples > 0 && vertexMism == 0,
          "sample() at a vertex does not return that vertex");

    // THE DETECTOR ITSELF IS UNDER TEST (AGENTS §8 п.6). One poked vertex has
    // to turn the comparison red; a comparison that cannot fail proves nothing
    // about the ones above.
    SubworldHeightField poked;
    build_reference(mgr, poked);
    poked.debug_poke_vertex(kHeightVerts / 2, kHeightVerts / 2, -1234.5f);
    std::size_t negSamples = 0;
    const std::size_t negMism = grid_mismatches(live, poked, negSamples);
    CHECK(negSamples > 0 && negMism == 1,
          "negative control: a corrupted vertex was not detected");
}

void case_seam_crossing_and_stitch() {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, resolve_relief_cell);
    mgr.consume_composite_dirty();

    float playerX = float(sm::sub::kCellSize * 2 + 8);
    float playerY = float(sm::sub::kCellSize + 128);
    mgr.check_boundary(playerX, playerY);
    CHECK_OR_RETURN(mgr.center_cx() == 1 && mgr.center_cy() == 0,
                    "window did not cross the seam east");
    CHECK_OR_RETURN(mgr.consume_composite_dirty(),
                    "the crossing marked nothing dirty");

    // THE SLIDE. Straight after a crossing the grid is 6/9 memmoved overlap,
    // a resampled border ring and three freshly exposed placeholder cells.
    {
        SubworldHeightField ref;
        build_reference(mgr, ref);
        std::size_t samples = 0;
        const std::size_t mism = grid_mismatches(mgr.height_field(), ref, samples);
        CHECK(samples == std::size_t(kHeightVerts) * kHeightVerts && mism == 0,
              "the seam slide disagrees with a full resample");
    }

    // THE STITCH. The worker finishes the exposed cells one at a time; each
    // publish refreshes only that cell's vertex block, and each must still
    // agree with the whole window resampled from scratch.
    int drains = 0;
    std::size_t roundsChecked = 0, roundsBad = 0;
    for (int i = 0; i < 500 && drains < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mgr.check_boundary(playerX, playerY);
        if (!mgr.consume_composite_dirty()) continue;
        ++drains;
        SubworldHeightField ref;
        build_reference(mgr, ref);
        std::size_t samples = 0;
        const std::size_t mism = grid_mismatches(mgr.height_field(), ref, samples);
        ++roundsChecked;
        if (samples != std::size_t(kHeightVerts) * kHeightVerts || mism != 0) {
            ++roundsBad;
            std::fprintf(stderr,
                         "[height-field] stitch round %d: %zu mismatching vertices\n",
                         drains, mism);
        }
    }
    CHECK(roundsChecked > 0 && roundsBad == 0,
          "a stitched cell disagrees with a full resample");
}

} // namespace

int main() {
    // The size was NAMED in the design (145.5 KiB, DOD 10) — the header pins it
    // under the compiler; this only states it once out loud in the log.
    std::fprintf(stderr, "[height-field] sizeof=%zu bytes (%zu verts per side)\n",
                 sizeof(SubworldHeightField), std::size_t(kHeightVerts));
    case_full_build_and_door();
    case_seam_crossing_and_stitch();
    return sm::test::report("subworld_height_field_test");
}
