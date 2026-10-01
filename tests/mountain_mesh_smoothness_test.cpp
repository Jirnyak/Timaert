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
// mesh sampling (the world's height field, sub/height.h) — a 192-quad grid, one
// vertex every 16 tiles, each vertex BOX-AVERAGED over a +/-8-tile
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

#include "check.h"
#include "sub/base_generator.h"
#include "sub/height.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

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
            generate_heightmap(cell, kCellSize, nbH, nbB, /*nbBiome5*/nullptr, b, seed,
                               ox * kCellSize, oy * kCellSize, WATER_LEVEL);
            for (int y = 0; y < kCellSize; ++y)
                for (int x = 0; x < kCellSize; ++x)
                    full[std::size_t(oy * kCellSize + y) * kFullSize + ox * kCellSize + x]
                        = cell[std::size_t(y) * kCellSize + x];
        }
    return full;
}

// THE MESH SAMPLING IS THE WORLD'S OWN (sub/height.h). This block used to
// re-derive it — 192 quads, 16-tile step, +/-8-tile box — which is exactly the
// second copy of production logic AGENTS §8 п.5 forbids: it proves you can
// copy, and it would keep passing while the real law drifted out from under it.
// The field's full-rebuild door takes a raw composite heightmap, so the witness
// now measures the surface the game actually draws.
constexpr int kMeshVtx = sm::sub::kHeightVerts;   // vertices per side

std::vector<float> mesh_vertices(const std::vector<float>& hm) {
    // Metres out, normalised in: every number below is a RATIO or a curvature
    // compared against another of its own kind, so the scale divides out — but
    // it must divide out of ALL of them, hence one place to undo it.
    sm::sub::SubworldHeightField field;
    field.rebuild_from(hm.data());
    std::vector<float> v(std::size_t(kMeshVtx) * kMeshVtx, 0.0f);
    for (std::size_t i = 0; i < v.size(); ++i) {
        // The field is METRES now (sub/height.h height_m is a curve), and
        // curvature here is stated in FIELD units — so the way back is the
        // curve's own inverse, never a division by the vertical full scale.
        v[i] = sm::sub::height01_of_m(field.vertices()[i]);
    }
    return v;
}

struct MeshStat {
    float medianCurv = 0.0f;  // p50 |Laplacian| at interior mesh vertices, м
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
    // Thresholds in мnits of the 1500 m height scale (metres of kink over
    // one 16 m mesh quad). Measured across the seed set below:
    //   * smooth crest (shipped): median 3.2-6.5, mean 6.8-13.4
    //   * classic ridged fold   : median 11.9-21.4, mean 16.5-30.5
    // The ceiling 9.0 sits with wide margin between the two — the fix passes
    // comfortably, the aliasing fold fails clearly.
    constexpr float kMaxMedianCurvM = 9.0f * sm::sub::kShoreGainM;

    // Lower bounds — mountains must still be mountains, not pancaked to plains.
    // Re-pinned 2026-07-30 for the owner-approved slope rebalance: ridge
    // wavelengths lengthened (250/110 -> ~550/250 tiles) and amplitude eased so
    // massif slopes read p50 ~30 deg instead of 43 deg wall faces. Longer waves
    // carry the SAME relief with less mesh-vertex curvature, so the curvature
    // floor drops (measured 0.9-1.0 across the seed set); range and the
    // mountains-vs-plains parity ratios below still lock the character.
    // ── ПОРОГИ ПЕРЕВЕДЕНЫ В МЕТРЫ, И ЭТО НЕ НОВАЯ КАЛИБРОВКА ──────────────
    // Все числа ниже подбирали, когда единица поля стоила 1500 м ВЕЗДЕ. Кривая
    // переноса (M-192) это сломала: единица поля на высоте 0.9 стоит 48 000 м,
    // на равнине 4 243, — и сравнение горы с равниной В ПОЛЕ стало сравнением
    // несравнимого. Свидетель краснел на ПРАВИЛЬНОМ мире: в поле отношение
    // читалось 0.3–1.2×, в метрах — 22–25×.
    // Перевод буквальный: порог_в_метрах = порог_в_поле × kShoreGainM, то есть
    // ровно та единица, в которой их и подбирали. Мера берётся у `height_gain_m`
    // — двери «сколько метров стоит ГРАДИЕНТ здесь», заведённой ровно для этого.
    constexpr float kMinMountainRangeM  = 0.15f * sm::sub::kShoreGainM;  // 225 м
    constexpr float kMinMountainMedianM = 0.6f * sm::sub::kShoreGainM;
    constexpr float kMinRangeRatio      = 8.0f;   // mtn range >> plains range (~36x seen)
    // Curvature parity relaxed 3.0 -> 1.5 with the same rebalance: long-wave
    // ridges put mountain character into RANGE (still ~36x plains) rather than
    // mesh-scale kink; the ratio still fails a true pancake (ratio -> ~1.0).
    constexpr float kMinCurvRatio       = 1.5f;   // mtn keeps more curvature than plains

    struct Case { std::uint32_t seed; float macroH; };
    const Case cases[] = {
        {0xABCDEF01u, 0.84f}, {0x12345678u, 0.84f}, {0xCAFEBABEu, 0.78f},
        {0xDEADBEEFu, 0.95f}, {0x00FF00FFu, 0.72f},
    };

    // The verdicts moved OUT of this loop and onto the worst values it
    // collects. They used to `return fail(...)` on the first bad massif, so a
    // regression on seed one hid every other seed — and a loop that stops
    // measuring cannot say how bad the worst case is, which is the only thing
    // a threshold on the worst case means (§8 п.3).
    float worstMedian = 0.0f, worstRange = 1e9f, worstMedianMtn = 1e9f;
    float worstRangeRatio = 1e9f, worstCurvRatio = 1e9f;
    int measured = 0;
    for (const Case& c : cases) {
        MeshStat mtn = measure(composite(Mountain, c.seed, c.macroH));
        // Plains reference at a lowland height with the SAME seed.
        MeshStat pln = measure(composite(Meadow, c.seed, 0.30f));
        // В МЕТРЫ, КАЖДЫЙ СВОЕЙ МЕРОЙ: наклон кривой на СВОЕЙ высоте. Гора и
        // равнина живут на разных участках кривой, поэтому общего множителя у
        // них нет и быть не может — это и есть причина, по которой поле их не
        // сравнивает.
        const float mtnGain = sm::sub::height_gain_m(c.macroH);
        const float plnGain = sm::sub::height_gain_m(0.30f);
        mtn.range *= mtnGain; mtn.medianCurv *= mtnGain; mtn.meanCurv *= mtnGain;
        pln.range *= plnGain; pln.medianCurv *= plnGain; pln.meanCurv *= plnGain;

        if (mtn.medianCurv > kMaxMedianCurvM) {
            std::fprintf(stderr,
                "  seed=0x%08X macroH=%.2f mountain median curvature %.2f м "
                "(> %.2f): ridge crest is aliasing on the 16-tile mesh "
                "(spiky peaks). Did the C1 smooth crest 4*s*(1-s) regress to the "
                "ridged fold (1-|2s-1|)^2?\n",
                c.seed, c.macroH, mtn.medianCurv, kMaxMedianCurvM);
        }
        if (mtn.range < kMinMountainRangeM || mtn.medianCurv < kMinMountainMedianM) {
            std::fprintf(stderr,
                "  seed=0x%08X range=%.4f median=%.2f\n",
                c.seed, mtn.range, mtn.medianCurv);
        }
        const float rangeRatio = pln.range > 1e-6f ? mtn.range / pln.range : 1e9f;
        const float curvRatio  = pln.medianCurv > 1e-6f
                                     ? mtn.medianCurv / pln.medianCurv : 1e9f;
        if (rangeRatio < kMinRangeRatio) {
            std::fprintf(stderr, "  seed=0x%08X rangeRatio=%.1f\n", c.seed, rangeRatio);
        }
        if (curvRatio < kMinCurvRatio) {
            std::fprintf(stderr, "  seed=0x%08X curvRatio=%.1f\n", c.seed, curvRatio);
        }
        ++measured;

        worstMedian = std::max(worstMedian, mtn.medianCurv);
        worstRange = std::min(worstRange, mtn.range);
        worstMedianMtn = std::min(worstMedianMtn, mtn.medianCurv);
        worstRangeRatio = std::min(worstRangeRatio, rangeRatio);
        worstCurvRatio = std::min(worstCurvRatio, curvRatio);
    }

    std::printf("OK mountain_mesh_smoothness_test: %zu mountain massifs — worst "
                "mesh-vertex median curvature %.2f м (<= %.1f, no crest "
                "aliasing), while keeping relief (range >= %.3f), curvature "
                "(median >= %.2f) and parity (range %.0fx / curvature %.1fx "
                "plains)\n",
                sizeof(cases) / sizeof(cases[0]), worstMedian, kMaxMedianCurvM,
                worstRange, worstMedianMtn, worstRangeRatio, worstCurvRatio);
    // The count is asserted first and GATES the rest: the worst-value
    // accumulators start at sentinels that would sail past every threshold
    // below if the loop had measured nothing.
    const int expected = int(sizeof(cases) / sizeof(cases[0]));
    CHECK(measured == expected,
          "every massif in the table was actually measured");
    if (measured != expected) return sm::test::report("mountain_mesh_smoothness_test");
    CHECK(worstMedian <= kMaxMedianCurvM,
          "no massif's crest is ALIASING on the 16-tile mesh — the C1 smooth "
          "crest 4*s*(1-s) has not regressed to the ridged fold (1-|2s-1|)^2");
    CHECK(worstRange >= kMinMountainRangeM,
          "no massif was pancaked into plains — relief survives the smoothing");
    CHECK(worstMedianMtn >= kMinMountainMedianM,
          "...and so does curvature: a smooth mountain is still a mountain");
    CHECK(worstRangeRatio >= kMinRangeRatio,
          "a mountain still DOMINATES the plains it is measured against");
    CHECK(worstCurvRatio >= kMinCurvRatio,
          "and still differs from them in character, not only in height");
    return sm::test::report("mountain_mesh_smoothness_test");
}
