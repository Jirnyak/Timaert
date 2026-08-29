// River system test — the two honest stages of the universal river system.
//
// Stage 1 — macroworld generation (map_generator.cpp::generate_river_data,
//   exercised end-to-end through generate_terrain). Rivers are least-cost A*
//   paths that hug the climate-biome (Voronoi) edges toward the nearest sea and
//   are then carved below sea level so they classify as honest Biome::Water
//   cells. Guards here:
//     * determinism            — same seed => bit-identical river mask + rgba;
//     * honest-water carve      — every river cell sits below sea and is masked
//                                 water (no river cell left above sea);
//     * drains-to-sea           — every river cell is connected to the open sea
//                                 through the 4-connected water network;
//     * DFS anomaly bound       — the regression guard for the old Dial-bucket
//                                 queue that collapsed the search depth-first
//                                 and produced dead-straight 239-494 cell
//                                 rivers; the binary-heap A* caps runs near 60;
//     * coverage sanity         — rivers are neither absent nor blanketing;
//     * fail-closed             — malformed terrain yields an empty river mask.
//
// Stage 2 — subworld realization (base_generator.cpp::generate_heightmap). A
//   single macro river cell becomes an honest submerged water cell: the bed
//   sits below the water plane, the neighbouring land cell stays dry (no
//   submerged-land / coastline defect), and the land->water descent inside the
//   cell is smooth and cliff-free. There is no width knob — the descent falls
//   out of the height remap + bilinear blend, exactly as intended.
#include "check.h"

#include "macro/biomes.h"
#include "macro/map_generator.h"
#include "sub/base_generator.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{

// The sea-level byte exactly as map_generator.cpp derives it internally
// (floor(clamp(seaLevel) * 255)); seaLevel 0.40 -> 102.
std::uint8_t sea_byte(float seaLevel)
{
    const int v = int(std::floor(std::clamp(seaLevel, 0.0f, 1.0f) * 255.0f));
    return std::uint8_t(std::clamp(v, 0, 255));
}

inline std::uint8_t height_byte(const sm::TerrainData& td, std::size_t cell)
{
    return td.rgba[cell * 4u + 0u];
}

inline std::uint8_t mask_byte(const sm::TerrainData& td, std::size_t cell)
{
    return td.rgba[cell * 4u + 3u];
}

// Longest axis-aligned run of contiguous river cells (horizontal or vertical).
// This is the DFS-anomaly detector: the old saturating Dial-bucket queue popped
// LIFO once costs exceeded its ceiling, turning the least-cost search into a
// depth-first march that laid down dead-straight rivers hundreds of cells long
// (measured 239-494 on 1024^2 maps). The binary-heap A* has no ceiling and caps
// natural runs near ~60, so a bound well below the anomaly regime catches any
// regression to the depth-first behaviour.
long max_axis_run(const sm::TerrainData& td)
{
    const int w = td.width;
    const int h = td.height;
    long best = 0;
    for (int y = 0; y < h; ++y)
    {
        long run = 0;
        for (int x = 0; x < w; ++x)
        {
            if (td.riverData[std::size_t(y) * w + x] > 0)
            {
                if (++run > best) best = run;
            }
            else
            {
                run = 0;
            }
        }
    }
    for (int x = 0; x < w; ++x)
    {
        long run = 0;
        for (int y = 0; y < h; ++y)
        {
            if (td.riverData[std::size_t(y) * w + x] > 0)
            {
                if (++run > best) best = run;
            }
            else
            {
                run = 0;
            }
        }
    }
    return best;
}

// River cells that do NOT drain to the sea. A 4-connected toroidal flood
// (kRiverDirs is 4-connected, so river paths are 4-connected) starts from the
// open sea and spreads through the whole water network; any river cell it fails
// to reach is a river that does not connect to the sea. "Water" uses the
// generator's own terminal sea test (height <= seaLevel8 — the river trace's
// `done` condition), so the boundary sea cells rivers drain into count as
// reachable water. Carved river cells (byte 94) are well under that.
long count_non_draining_rivers(const sm::TerrainData& td, std::uint8_t seaB)
{
    const int w = td.width;
    const int h = td.height;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    auto is_water = [&](std::size_t i) { return height_byte(td, i) <= seaB; };
    auto is_river = [&](std::size_t i) { return td.riverData[i] > 0; };

    std::vector<std::uint8_t> seen(n, 0u);
    std::vector<int> stack;
    stack.reserve(n / 4u);
    for (std::size_t i = 0; i < n; ++i)
    {
        if (is_water(i) && !is_river(i)) // open sea / lakes = flood sources
        {
            seen[i] = 1u;
            stack.push_back(int(i));
        }
    }
    static constexpr int dx4[4] = {1, -1, 0, 0};
    static constexpr int dy4[4] = {0, 0, 1, -1};
    while (!stack.empty())
    {
        const int idx = stack.back();
        stack.pop_back();
        const int x = idx % w;
        const int y = idx / w;
        for (int k = 0; k < 4; ++k)
        {
            const int nx = (x + dx4[k] + w) % w;
            const int ny = (y + dy4[k] + h) % h;
            const std::size_t ni = std::size_t(ny) * w + nx;
            if (seen[ni]) continue;
            if (is_water(ni) || is_river(ni))
            {
                seen[ni] = 1u;
                stack.push_back(int(ni));
            }
        }
    }
    long unreached = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        if (is_river(i) && !seen[i]) ++unreached;
    }
    return unreached;
}

long river_cell_count(const sm::TerrainData& td)
{
    long c = 0;
    for (std::uint8_t v : td.riverData)
    {
        if (v > 0) ++c;
    }
    return c;
}

// ── Stage 1: macroworld river generation ─────────────────────────────────

// Representative seeds. 42 is the historical worst case (a 494-cell dead-
// straight river under the old Dial-bucket queue); the rest cover default and
// assorted layouts. 1024^2 is the real macro map size and the size at which the
// anomaly was measured.
constexpr int kMapSize = 1024;
const std::uint32_t kSeeds[] = {1u, 2u, 3u, 7u, 42u};

void test_river_generation_is_deterministic()
{
    sm::LayerParameters params;
    params.seed = 42u;
    const sm::TerrainData a = sm::generate_terrain(kMapSize, kMapSize, params);
    const sm::TerrainData b = sm::generate_terrain(kMapSize, kMapSize, params);

    CHECK_OR_RETURN(a.riverData.size() == b.riverData.size()
                        && a.rgba.size() == b.rgba.size(),
                    "repeated generation must produce identically sized buffers");
    CHECK(a.riverData == b.riverData,
          "river mask must be bit-identical across runs (determinism)");
    CHECK(a.rgba == b.rgba,
          "carved terrain must be bit-identical across runs (determinism)");
}

// Every macro-generation invariant runs against ONE generated map per seed
// (generation dominates runtime, so we generate each map once and check all
// aspects on it). Each aspect keeps its own message so a failure is specific.
void check_map_invariants(std::uint32_t seed)
{
    sm::LayerParameters params;
    params.seed = seed;
    const sm::TerrainData td = sm::generate_terrain(kMapSize, kMapSize, params);
    const std::uint8_t seaB = sea_byte(params.seaLevel);

    // Honest-water carve — the user's core requirement: a river IS a water
    // cell. After the carve pass every river cell must sit below sea level and
    // be masked as water, so it classifies as Biome::Water exactly like the
    // sea. No river cell may be left standing above sea (the old "river on dry
    // land" artefact).
    long aboveSea = 0;
    long notMasked = 0;
    for (std::size_t i = 0; i < td.riverData.size(); ++i)
    {
        if (td.riverData[i] == 0) continue;
        if (height_byte(td, i) >= seaB) ++aboveSea;
        if (mask_byte(td, i) != 0) ++notMasked;
    }

    // Drains-to-sea, DFS-anomaly bound, coverage sanity.
    const long unreached = count_non_draining_rivers(td, seaB);
    const long run = max_axis_run(td);
    const long rivers = river_cell_count(td);
    const long cells = long(td.riverData.size());
    constexpr long kMaxRunBound = 120; // heap A* measures 47-62; DFS gave 239-494

    const int failsBefore = sm::test::failures();
    CHECK(aboveSea == 0, "every river cell must be carved below sea level");
    CHECK(notMasked == 0, "every river cell must be masked as water (Biome::Water)");
    CHECK(unreached == 0,
          "every river cell must drain to the sea through the water network");
    CHECK(run < kMaxRunBound,
          "longest straight river run must stay below the DFS-anomaly bound");
    CHECK(rivers > cells / 1000,
          "a normal map must grow a non-trivial river network");
    CHECK(rivers < cells / 8,
          "rivers must not blanket the map (coverage runaway)");
    if (sm::test::failures() != failsBefore)
    {
        std::fprintf(stderr,
            "  seed %u: rivers=%ld/%ld aboveSea=%ld notMasked=%ld unreached=%ld maxrun=%ld\n",
            seed, rivers, cells, aboveSea, notMasked, unreached, run);
    }
}

void test_macro_river_invariants()
{
    const int failsBefore = sm::test::failures();
    for (std::uint32_t seed : kSeeds)
    {
        check_map_invariants(seed);
        if (sm::test::failures() != failsBefore) break;
    }
}

void test_river_generation_fails_closed()
{
    sm::LayerParameters params;

    // Short RGBA storage for the declared dimensions -> must not read past the
    // buffer, must leave an all-zero river mask sized to the grid.
    sm::TerrainData shortStore;
    shortStore.width = 16;
    shortStore.height = 16;
    shortStore.rgba.assign(16u, 0u); // far short of 16*16*4
    sm::generate_river_data(shortStore, params);

    // Non-positive dimensions -> empty river mask, no work.
    sm::TerrainData zeroDims;
    zeroDims.width = 0;
    zeroDims.height = 4;
    zeroDims.rgba.assign(64u, 0u);
    sm::generate_river_data(zeroDims, params);

    CHECK(shortStore.riverData.size() == 256u,
          "malformed river gen must still size the mask to the grid");
    CHECK(std::all_of(shortStore.riverData.begin(), shortStore.riverData.end(),
                      [](std::uint8_t v) { return v == 0u; }),
          "malformed river gen must stamp no rivers (fail closed)");
    CHECK(zeroDims.riverData.empty(),
          "non-positive dimensions must yield an empty river mask");
}

// ── Stage 2: subworld realization of a river cell ────────────────────────

// Build the kCellSize heightmap for a single macro cell and return the height
// at a chosen tile. `centreBiome`/`centreMH` set the cell being rendered; one
// neighbour slot is overridden to describe the river/land relationship.
struct DescentSample
{
    float riverEdge;   // river cell, at its shared boundary with land
    float riverCentre; // river cell, at its centre (the bed)
    float landCentre;  // adjacent land cell, at its centre
    float maxStep;     // largest adjacent-tile height jump across the river scanline
    int   violations;  // monotonicity violations edge->centre (low-passed)
};

DescentSample sample_river_descent(float landMH, sm::Biome landBiome)
{
    using namespace sm;
    using namespace sm::sub;
    const int CS = kCellSize;
    const float riverMH = 94.0f / 255.0f; // carved river height (carveH = 94)

    // River cell: centre = Water, all 8 neighbours = land.
    float rNbH[9];
    Biome rNbB[9];
    for (int i = 0; i < 9; ++i) { rNbH[i] = landMH; rNbB[i] = landBiome; }
    rNbH[4] = riverMH; rNbB[4] = Biome::Water;
    std::vector<float> river;
    generate_heightmap(river, CS, rNbH, rNbB, Biome::Water, 12345u, 0, 0);

    // Adjacent land cell: centre = land, east neighbour = the river cell. Used
    // to confirm the land itself stays dry (the no-submerged-land invariant
    // kLandMargin protects); the river cell's own shared edge is a legitimate
    // 50/50 bilinear blend and is expected to sit at the waterline.
    float lNbH[9];
    Biome lNbB[9];
    for (int i = 0; i < 9; ++i) { lNbH[i] = landMH; lNbB[i] = landBiome; }
    lNbH[5] = riverMH; lNbB[5] = Biome::Water;
    std::vector<float> land;
    generate_heightmap(land, CS, lNbH, lNbB, landBiome, 12345u, 0, 0);

    const int y = CS / 2;
    DescentSample s{};
    s.riverEdge = river[std::size_t(y) * CS + 0];
    s.riverCentre = river[std::size_t(y) * CS + CS / 2];
    s.landCentre = land[std::size_t(CS / 2) * CS + CS / 2];

    s.maxStep = 0.0f;
    for (int x = 1; x < CS; ++x)
    {
        const float d = std::fabs(river[std::size_t(y) * CS + x]
                                - river[std::size_t(y) * CS + x - 1]);
        if (d > s.maxStep) s.maxStep = d;
    }

    // Monotone descent on a 32-bucket low-pass over the edge->centre half
    // (per-tile noise makes strict monotonicity meaningless).
    constexpr int B = 32;
    float bucket[B];
    for (int b = 0; b < B; ++b)
    {
        double sum = 0.0;
        int cnt = 0;
        const int x0 = b * (CS / 2) / B;
        const int x1 = (b + 1) * (CS / 2) / B;
        for (int x = x0; x < x1; ++x) { sum += river[std::size_t(y) * CS + x]; ++cnt; }
        bucket[b] = cnt ? float(sum / cnt) : 0.0f;
    }
    s.violations = 0;
    for (int b = 1; b < B; ++b)
    {
        if (bucket[b] > bucket[b - 1] + 1e-4f) ++s.violations;
    }
    return s;
}

void test_subworld_river_is_honest_submerged_water()
{
    using namespace sm::sub;
    // Three land contexts: barely above sea, a common bank, and elevated land
    // the river cuts through. In every case the bed submerges, the land stays
    // dry, and the descent is smooth and cliff-free.
    struct Case { const char* name; float landMH; sm::Biome biome; };
    const Case cases[] = {
        {"barely-above-sea", 104.0f / 255.0f, sm::Biome::Valley},
        {"just-above-sea",   110.0f / 255.0f, sm::Biome::Meadow},
        {"elevated land",    150.0f / 255.0f, sm::Biome::Steppe},
    };

    const int failsBefore = sm::test::failures();
    for (const Case& c : cases)
    {
        const DescentSample s = sample_river_descent(c.landMH, c.biome);
        CHECK(s.riverCentre < WATER_LEVEL,
              "river bed must submerge below the water plane (honest water)");
        CHECK(s.landCentre > WATER_LEVEL,
              "adjacent land cell must stay dry (no submerged-land defect)");
        CHECK(s.maxStep < 0.03f,
              "land->water descent must be cliff-free (no coastline step)");
        CHECK(s.violations <= 2,
              "land->water descent must be smoothly monotone");
        if (sm::test::failures() != failsBefore)
        {
            std::fprintf(stderr,
                "  case %s: bed=%.3f land=%.3f maxStep=%.4f viol=%d (WATER_LEVEL=%.2f)\n",
                c.name, s.riverCentre, s.landCentre, s.maxStep, s.violations, WATER_LEVEL);
            break;
        }
    }
}

} // namespace

int main()
{
    // Stage 1 — macroworld generation.
    test_river_generation_is_deterministic();
    test_macro_river_invariants();
    test_river_generation_fails_closed();
    // Stage 2 — subworld realization.
    test_subworld_river_is_honest_submerged_water();
    return sm::test::report("river_generation_test");
}
