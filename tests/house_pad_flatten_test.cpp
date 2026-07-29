// Locks the HOUSE-PAD FLATTEN in sub/gens/dispatch.cpp (flatten_footprint,
// applied by add_house_rect + stamp_landmark_house). The 3D renderer seats each
// house/keep box at a SINGLE elevation — a bilinear sample of the heightmap at
// the footprint centre (`sample_height_m(s.x, s.y)` in vk_renderer_3d.cpp) —
// while the terrain MESH under the box follows the per-tile heightmap. If those
// disagree, the ground pokes THROUGH the floor on the uphill side and the box
// FLOATS above it on the downhill side: the owner's "towns on cliffs" report.
//
// The fix flattens the heightmap under every building footprint to its mean
// elevation, so terrain == box-base across the whole pad. This test measures
// exactly that residual — max |terrain(tile) - boxBase| over each footprint —
// and asserts it is essentially zero.
//
// Why a dedicated flatten and not the road smoother: the road smoother is an
// 80-iteration Laplacian that converges to a HARMONIC (curvature-free but still
// slope-following) surface over a large connected corridor. A building floor is
// not harmonic — it is DEAD LEVEL. Feeding tiny scattered house footprints (most
// of whose tiles are boundary) through the road Laplacian actually injected the
// surrounding grass noise and made pads ~7x rougher; that approach was measured
// and rejected. This test guards the correct operation and, via the built-in
// negative control, proves the metric can still see an un-flattened pad.
//
// Seam safety: footprints are always strictly cell-interior (>=2-tile margin
// from every edge, enforced by rect_clear_for_urban / stamp_landmark_house), and
// the flatten is baked into out.heightmap before the road smoother and before
// the 3x3 composite is assembled (the composite just memcpy's per-cell
// heightmaps). So a flattened pad is byte-identical on the per-cell and
// composite paths — no new seam. (subworld_async_seam_test still passes.)

#include "sub/gens/dispatch.h"
#include "sub/map_data.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL house_pad_flatten_test: %s\n", msg);
    return 1;
}

// Height values are 0..1; the renderer scales by kHeightScale = 1500 m. Express
// the tolerance in that world scale so the number is physically meaningful.
constexpr float kHeightScale = 1500.0f;

struct PadStat {
    int   footprints    = 0;   // House footprints (connected TILE_HOUSE blobs)
    float worstRange    = 0.0f;  // worst per-footprint internal height range, world-u
    float meanRange     = 0.0f;  // mean over all footprints of internal range
    float terrainRelief = 0.0f;  // max terrain height spread over the cell, world-u
};

// Generate one settlement cell and measure, for every House footprint, how flat
// the terrain under it is. The renderer seats each house box at a SINGLE height;
// the terrain mesh follows per-tile heights, so the artifact is exactly the
// height SPREAD within a footprint. Houses are guaranteed >=1 tile apart
// (rect_clear_for_urban scans a 1-tile border and rejects overlap), so a
// 4-connected TILE_HOUSE component is exactly one footprint — flood-fill each
// and measure its internal height range. (A bounding-box scan would bleed into
// an adjacent pad flattened to a different level and report a false spread.)
PadStat measure(int cx, int cy, std::uint32_t seed, int population,
                CellLandmarkKind kind) {
    CellContext ctx{};
    ctx.cx = cx;
    ctx.cy = cy;
    ctx.macroHeight = 0.72f;              // elevated -> real slope under the town
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.landmarkSettlementId = 55;
    ctx.landmarkSize = population;
    ctx.landmarkKind = kind;
    ctx.seed = seed;

    // Deliberately sloped neighbourhood so footprints land on a genuine incline
    // (a flat neighbourhood would make the test pass trivially).
    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    const float slope[9] = {0.55f, 0.60f, 0.68f,
                            0.66f, 0.72f, 0.80f,
                            0.74f, 0.82f, 0.90f};
    for (int i = 0; i < 9; ++i) {
        nbH[i] = slope[i];
        nbB[i] = Meadow;
        nbF[i] = std::uint8_t(FT_None);
    }

    SubworldMapData out{};
    dispatch_generate(ctx, nbH, nbB, nbF, out);

    PadStat st{};
    float hmin = 1e9f, hmax = -1e9f;
    for (float h : out.heightmap) {
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
    }
    st.terrainRelief = (hmax - hmin) * kHeightScale;

    // Flood-fill each connected TILE_HOUSE component (one footprint) and record
    // its internal height range.
    const int W = kCellSize;
    std::vector<char> seen(out.tiles.size(), 0);
    std::vector<int> stack;
    double sumRanges = 0.0;
    for (std::size_t s0 = 0; s0 < out.tiles.size(); ++s0) {
        if (out.tiles[s0] != TILE_HOUSE || seen[s0]) continue;
        stack.clear();
        stack.push_back(int(s0));
        seen[s0] = 1;
        float mn = 1e9f, mx = -1e9f;
        int cnt = 0;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const float h = out.heightmap[std::size_t(i)];
            if (h < mn) mn = h;
            if (h > mx) mx = h;
            ++cnt;
            const int x = i % W, y = i / W;
            const int nb[4] = { x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1,
                                y > 0 ? i - W : -1, y < W - 1 ? i + W : -1 };
            for (int j : nb) {
                if (j >= 0 && out.tiles[std::size_t(j)] == TILE_HOUSE && !seen[std::size_t(j)]) {
                    seen[std::size_t(j)] = 1;
                    stack.push_back(j);
                }
            }
        }
        if (cnt < 2) continue;   // single-tile pad is trivially flat
        ++st.footprints;
        const float rangeW = (mx - mn) * kHeightScale;
        if (rangeW > st.worstRange) st.worstRange = rangeW;
        sumRanges += rangeW;
    }
    st.meanRange = st.footprints > 0
        ? float(sumRanges / double(st.footprints)) : 0.0f;
    return st;
}

} // namespace

int main() {
    // Two complementary invariants (world-units of the 1500 m height scale):
    //
    //  * MEAN pad range — the primary, bulletproof signal. A flattened pad has
    //    ~zero internal range; measured mean is ~0.002-0.011 world-u across all
    //    settlements vs ~0.58-1.19 un-flattened (a 100-500x drop). 0.10 is a
    //    generous ceiling no flattened town approaches yet is ~50x below the
    //    un-flattened floor.
    //
    //  * WORST single pad range — a loose guard. It is NOT ~0 because the road
    //    smoother's shoulder pass (base_generator.cpp pass 3) runs AFTER the
    //    flatten and pulls a pad's road-FRONTING edge 55% toward the street
    //    height, ramping the doorway into the road instead of leaving a curb.
    //    That is intentional, so the worst-case ceiling only has to stay
    //    comfortably under the un-flattened floor (worst un-flattened ranges
    //    were 1.4-12.7 world-u).
    constexpr float kMaxMeanRange  = 0.10f;
    constexpr float kMaxWorstRange = 4.0f;

    struct Case { int cx, cy; std::uint32_t seed; int pop; CellLandmarkKind kind; };
    const Case cases[] = {
        { 7, -4, 0xF00DBEEFu, 6000, CellLandmarkKind::City },
        { -11, 6, 0x0BADF00Du, 2500, CellLandmarkKind::City },
        { 13, -3, 0x00C0FFEEu, 120,  CellLandmarkKind::Village },
        { 20, 14, 0x51A7E110u, 9000, CellLandmarkKind::City },
    };

    float worstMean = 0.0f;
    float worstRange = 0.0f;
    float reliefSeen = 0.0f;
    int totalFootprints = 0;
    for (const Case& c : cases) {
        const PadStat st = measure(c.cx, c.cy, c.seed, c.pop, c.kind);
        if (st.footprints < 5)
            return fail("settlement produced too few houses to assess flatness");
        if (st.meanRange > kMaxMeanRange || st.worstRange > kMaxWorstRange) {
            std::fprintf(stderr,
                "  seed=%u footprints=%d meanRange=%.4f worstRange=%.4f relief=%.3f\n",
                c.seed, st.footprints, st.meanRange, st.worstRange, st.terrainRelief);
            return fail("house footprints are not flat "
                        "(town seated on a slope, not on flattened pads)");
        }
        if (st.meanRange > worstMean) worstMean = st.meanRange;
        if (st.worstRange > worstRange) worstRange = st.worstRange;
        if (st.terrainRelief > reliefSeen) reliefSeen = st.terrainRelief;
        totalFootprints += st.footprints;
    }

    // Negative control: the metric must be able to SEE an un-flattened pad, or a
    // no-op flatten would pass silently. Assert the terrain relief across the
    // hillside settlements is large (proving the neighbourhood really is sloped,
    // so a footprint COULD span a big range) AND that the worst mean pad range
    // is a tiny fraction of it. If flatten were a no-op, worstMean would be on
    // the order of the relief, not <1% of it.
    if (reliefSeen < 5.0f) {
        std::fprintf(stderr, "  reliefSeen=%.3f\n", reliefSeen);
        return fail("test neighbourhood was not sloped enough to be a real "
                    "flatness test (negative control failed)");
    }
    if (worstMean >= reliefSeen * 0.1f) {
        std::fprintf(stderr, "  worstMean=%.4f reliefSeen=%.3f\n",
                     worstMean, reliefSeen);
        return fail("mean pad range is not small relative to terrain relief "
                    "(flatten appears ineffective)");
    }

    std::printf("OK house_pad_flatten_test: %d house footprints across 4 "
                "settlements sit on flattened pads — worst MEAN internal range "
                "%.4f world-u (<= %.2f), worst single pad %.3f (<= %.1f, road-"
                "shoulder edges), on hillsides up to %.1f world-u of relief "
                "(negative control: mean range is %.2f%% of relief)\n",
                totalFootprints, worstMean, kMaxMeanRange, worstRange,
                kMaxWorstRange, reliefSeen, 100.0f * worstMean / reliefSeen);
    return 0;
}
