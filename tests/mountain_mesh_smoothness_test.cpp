// Locks the MOUNTAIN RIDGE CREST shape in sub/base_generator.cpp
// (apply_mountain_ridges). Mountains are built by a domain-warped ridged
// multifractal whose per-octave crest was historically the classic ridged fold
//   sig = (1 - |2s - 1|)^2
// which has a SLOPE DISCONTINUITY (a C0 corner) at every 0.5-crossing of the
// noise. Even with the ridge octaves kept below the terrain-mesh Nyquist, that
// corner synthesises high-frequency harmonics of the base field, and the 3rd/4th
// harmonic folds back onto the 16-tile-spaced 3D terrain mesh and ALIASES — the
// owner's "chaotic spiky peaks" that the low-passing minimap never showed. The
// fix replaces the fold with the C1 smooth crest  sig = 4 s (1 - s)  (same
// 0->1->0 hump peaking on the ridge line, but no corner), which cut mesh-scale
// curvature ~70% while preserving the massif's height/prominence.
//
// This test measures EXACTLY what the eye sees in 3D: it reproduces the 3D
// renderer's mesh sampling (vk_renderer_3d.cpp) — a 192-quad grid, one vertex
// every kFullSize/192 = 16 tiles, each vertex BOX-AVERAGED over a +/-8-tile
// footprint (the mesh already low-passes) — then measures the discrete
// Laplacian (|kink|) at the mesh vertices. Tile-scale curvature is the WRONG
// metric: the mesh never samples per-tile, so per-tile roughness is invisible;
// mesh-vertex curvature is what actually renders as "spiky".
//
// It brackets the crest from BOTH sides so it fails on a regression in either
// direction:
//   * upper bound  — median mesh curvature must be LOW (the aliasing fold blows
//     this past the ceiling; the smooth crest sits comfortably under it),
//   * lower bound  — mountains must still CARVE real curvature and real range
//     (a no-op that pancaked mountains into plains would fail this),
//   * parity       — mountain range must still dominate plains range, and
//     mountain curvature must still exceed plains curvature (mountains keep
//     their character; they are not smoothed into meadows).

#include "sub/base_generator.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL mountain_mesh_smoothness_test: %s\n", msg);
    return 1;
}

// The renderer scales normalised heights by kHeightScale = 1500 m. Express the
// curvature threshold in that world scale so the number is physically meaningful
// ("metres of kink over one 16 m mesh quad").
constexpr float kHeightScale = 1500.0f;

// One 3x3 composite of a single biome at a fixed macro height, assembled the
// same way seamless_manager does (per-cell generate_heightmap then memcpy into
// the 3072x3072 buffer at the cell offset).
std::vector<float> composite(Biome b, std::uint32_t seed, float macroH) {
    std::vector<float> full(std::size_t(kFullSize) * kFullSize, 0.0f);
    float nbH[9];
    Biome nbB[9];
    for (int i = 0; i < 9; ++i) { nbH[i] = macroH; nbB[i] = b; }
    for (int oy = 0; oy < 3; ++oy)
        for (int ox = 0; ox < 3; ++ox) {
            std::vector<float> cell;
            generate_heightmap(cell, kCellSize, nbH, nbB, b, seed,
                               ox * kCellSize, oy * kCellSize);
            for (int y = 0; y < kCellSize; ++y)
                for (int x = 0; x < kCellSize; ++x)
                    full[std::size_t(oy * kCellSize + y) * kFullSize + ox * kCellSize + x]
                        = cell[std::size_t(y) * kCellSize + x];
        }
    return full;
}

// Reproduce the 3D renderer's terrain-mesh vertex sampling: 192 quads/side, one
// vertex every step = kFullSize/192 tiles, each vertex the mean height over a
// +/-half-tile box (half = step/2). This is vk_renderer_3d.cpp's sampleVertex.
constexpr int kMeshDim = 192;               // quads per side (matches renderer)
constexpr int kMeshVtx = kMeshDim + 1;      // vertices per side

std::vector<float> mesh_vertices(const std::vector<float>& hm) {
    const int step = kFullSize / kMeshDim;      // 16 tiles/quad
    const int half = std::max(1, step / 2);     // +/-8-tile box
    std::vector<float> v(std::size_t(kMeshVtx) * kMeshVtx, 0.0f);
    for (int y = 0; y < kMeshVtx; ++y)
        for (int x = 0; x < kMeshVtx; ++x) {
            const int cy = std::min(kFullSize - 1, y * step);
            const int cx = std::min(kFullSize - 1, x * step);
            const int y0 = std::max(0, cy - half), y1 = std::min(kFullSize - 1, cy + half);
            const int x0 = std::max(0, cx - half), x1 = std::min(kFullSize - 1, cx + half);
            float sum = 0.0f;
            int cnt = 0;
            for (int sy = y0; sy <= y1; ++sy) {
                const std::size_t row = std::size_t(sy) * kFullSize;
                for (int sx = x0; sx <= x1; ++sx) { sum += hm[row + sx]; ++cnt; }
            }
            v[std::size_t(y) * kMeshVtx + x] = cnt ? sum / float(cnt) : 0.0f;
        }
    return v;
}

struct MeshStat {
    float medianCurv = 0.0f;  // p50 |Laplacian| at interior mesh vertices, world-u
    float meanCurv   = 0.0f;
    float range      = 0.0f;  // hi-lo of mesh vertices (normalised 0..1)
};

MeshStat measure(const std::vector<float>& hm) {
    const std::vector<float> v = mesh_vertices(hm);
    std::vector<float> curv;
    curv.reserve(std::size_t(kMeshVtx) * kMeshVtx);
    float lo = 1e9f, hi = -1e9f;
    for (float h : v) { lo = std::min(lo, h); hi = std::max(hi, h); }
    for (int y = 1; y < kMeshVtx - 1; ++y)
        for (int x = 1; x < kMeshVtx - 1; ++x) {
            const std::size_t i = std::size_t(y) * kMeshVtx + x;
            const float lap = v[i] * 4.0f - v[i - 1] - v[i + 1]
                            - v[i - kMeshVtx] - v[i + kMeshVtx];
            curv.push_back(std::fabs(lap) * kHeightScale);
        }
    std::sort(curv.begin(), curv.end());
    MeshStat st{};
    st.range = hi - lo;
    if (!curv.empty()) {
        st.medianCurv = curv[curv.size() / 2];
        double sum = 0.0;
        for (float c : curv) sum += c;
        st.meanCurv = float(sum / double(curv.size()));
    }
    return st;
}

} // namespace

int main() {
    // Thresholds in world-units of the 1500 m height scale (metres of kink over
    // one 16 m mesh quad). Measured across the seed set below:
    //   * smooth crest (shipped): median 3.2-6.5, mean 6.8-13.4
    //   * classic ridged fold   : median 11.9-21.4, mean 16.5-30.5
    // The ceiling 9.0 sits with wide margin between the two — the fix passes
    // comfortably, the aliasing fold fails clearly.
    constexpr float kMaxMedianCurv = 9.0f;

    // Lower bounds — mountains must still be mountains, not pancaked to plains:
    constexpr float kMinMountainRange   = 0.15f;  // real vertical relief remains
    constexpr float kMinMountainMedian  = 1.0f;   // real curvature remains (not flat)
    constexpr float kMinRangeRatio      = 8.0f;   // mtn range >> plains range (~36x seen)
    constexpr float kMinCurvRatio       = 3.0f;   // mtn keeps more curvature than plains

    struct Case { std::uint32_t seed; float macroH; };
    const Case cases[] = {
        {0xABCDEF01u, 0.84f}, {0x12345678u, 0.84f}, {0xCAFEBABEu, 0.78f},
        {0xDEADBEEFu, 0.95f}, {0x00FF00FFu, 0.72f},
    };

    float worstMedian = 0.0f, worstRange = 1e9f, worstMedianMtn = 1e9f;
    float worstRangeRatio = 1e9f, worstCurvRatio = 1e9f;
    for (const Case& c : cases) {
        const MeshStat mtn = measure(composite(Mountain, c.seed, c.macroH));
        // Plains reference at a lowland height with the SAME seed.
        const MeshStat pln = measure(composite(Meadow, c.seed, 0.30f));

        if (mtn.medianCurv > kMaxMedianCurv) {
            std::fprintf(stderr,
                "  seed=0x%08X macroH=%.2f mountain median curvature %.2f world-u "
                "(> %.2f): ridge crest is aliasing on the 16-tile mesh "
                "(spiky peaks). Did the C1 smooth crest 4*s*(1-s) regress to the "
                "ridged fold (1-|2s-1|)^2?\n",
                c.seed, c.macroH, mtn.medianCurv, kMaxMedianCurv);
            return fail("mountain mesh is too spiky (crest fold aliasing)");
        }
        if (mtn.range < kMinMountainRange || mtn.medianCurv < kMinMountainMedian) {
            std::fprintf(stderr,
                "  seed=0x%08X range=%.4f median=%.2f\n",
                c.seed, mtn.range, mtn.medianCurv);
            return fail("mountain has too little relief/curvature "
                        "(pancaked into plains — over-smoothed)");
        }
        const float rangeRatio = pln.range > 1e-6f ? mtn.range / pln.range : 1e9f;
        const float curvRatio  = pln.medianCurv > 1e-6f
                                     ? mtn.medianCurv / pln.medianCurv : 1e9f;
        if (rangeRatio < kMinRangeRatio) {
            std::fprintf(stderr, "  seed=0x%08X rangeRatio=%.1f\n", c.seed, rangeRatio);
            return fail("mountain range no longer dominates plains (parity)");
        }
        if (curvRatio < kMinCurvRatio) {
            std::fprintf(stderr, "  seed=0x%08X curvRatio=%.1f\n", c.seed, curvRatio);
            return fail("mountain lost its curvature character vs plains");
        }

        worstMedian = std::max(worstMedian, mtn.medianCurv);
        worstRange = std::min(worstRange, mtn.range);
        worstMedianMtn = std::min(worstMedianMtn, mtn.medianCurv);
        worstRangeRatio = std::min(worstRangeRatio, rangeRatio);
        worstCurvRatio = std::min(worstCurvRatio, curvRatio);
    }

    std::printf("OK mountain_mesh_smoothness_test: %zu mountain massifs — worst "
                "mesh-vertex median curvature %.2f world-u (<= %.1f, no crest "
                "aliasing), while keeping relief (range >= %.3f), curvature "
                "(median >= %.2f) and parity (range %.0fx / curvature %.1fx "
                "plains)\n",
                sizeof(cases) / sizeof(cases[0]), worstMedian, kMaxMedianCurv,
                worstRange, worstMedianMtn, worstRangeRatio, worstCurvRatio);
    return 0;
}
