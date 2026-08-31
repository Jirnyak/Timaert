#include "macro/spawners.h"
#include "macro/biomes.h"
#include "macro/macro_stock.h"      // MacroWorld — the registry's context
#include "macro/settlement_score.h" // kSettlementReach — the home-field box
#include "macro/pathfinding.h"
#include "macro/resource_field.h"
#include "core/rng.h"
#include "core/torus.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace sm
{

    // ── TS-faithful tree spawner (game/tree-spawner.ts spawnTrees) ──
    // Domain-warped multi-scale FBM forest patches with smoothstep density.
    // Linear distribution looks like dandruff; this gives organic groves.
    namespace
    {

        inline float ihash01(std::int32_t x, std::int32_t y, std::int32_t sd)
        {
            std::uint32_t v = std::uint32_t(x) * 374761u + std::uint32_t(y) * 668265u + std::uint32_t(sd) * 2246822u;
            v = (v ^ (v >> 13)) * 1274126177u;
            v ^= v >> 16;
            return float(v) / 4294967296.0f;
        }

        // `period` is the number of lattice cells the WORLD spans at this
        // frequency, and the lattice is wrapped by it — otherwise the noise has
        // an edge where the world does not (CANON.md S1). This had no wrap at
        // all: the forest field ran the lattice 0 → 14 across the map and hashed
        // index 14 against index 0, which are unrelated numbers. Measured, the
        // correlation across the seam was NIL — massif membership flipped on
        // 46.8 % of the seam's rows against 18.1 % between ordinary neighbours,
        // and the shipped forest layer still flipped on 24-30 % after its 3×3
        // box filter. A player walking off the last column stepped out of a
        // forest that had no reason to end.
        inline float smoothNoise(float x, float y, std::int32_t sd, int period)
        {
            int ix = int(std::floor(x));
            int iy = int(std::floor(y));
            float fx = x - float(ix);
            float fy = y - float(iy);
            float sx = fx * fx * (3.0f - 2.0f * fx);
            float sy = fy * fy * (3.0f - 2.0f * fy);
            const int p = period > 0 ? period : 1;
            const int ix0 = wrapi(ix, p), iy0 = wrapi(iy, p);
            const int ix1 = wrapi(ix + 1, p), iy1 = wrapi(iy + 1, p);
            float n00 = ihash01(ix0, iy0, sd);
            float n10 = ihash01(ix1, iy0, sd);
            float n01 = ihash01(ix0, iy1, sd);
            float n11 = ihash01(ix1, iy1, sd);
            float a = n00 + (n10 - n00) * sx;
            float b = n01 + (n11 - n01) * sx;
            return a + (b - a) * sy;
        }

        // `period` — lattice cells per world at the BASE octave; each octave
        // doubles both the coordinate and the period, so every octave closes.
        inline float fbm(float x, float y, std::int32_t sd, int octaves,
                         int period)
        {
            float value = 0.0f, amp = 1.0f, maxAmp = 0.0f, freq = 1.0f;
            int per = period;
            for (int i = 0; i < octaves; ++i)
            {
                value += smoothNoise(x * freq, y * freq, sd + i * 100, per) * amp;
                per *= 2;
                maxAmp += amp;
                amp *= 0.5f;
                freq *= 2.0f;
            }
            return value / maxAmp;
        }

        // The bridgeable-water mask for the road planner (pathfinding.h
        // kWaterCrossEW/NS): a water cell earns an axis bit where BOTH of
        // that axis' orthogonal neighbours are land — i.e. the water is
        // exactly one cell thick in the crossing direction. Derived from THE
        // water pre-answer of the cost grid (biome_at_cell, one cascade), so
        // the mask can never disagree with the ground A* actually walks.
        std::vector<std::uint8_t> build_water_cross_axes(const PathCostData &cg)
        {
            const int W = cg.width;
            const int H = cg.height;
            std::vector<std::uint8_t> axes(std::size_t(W) * std::size_t(H), 0);
            for (int y = 0; y < H; ++y)
            {
                for (int x = 0; x < W; ++x)
                {
                    const std::size_t i = std::size_t(y) * W + x;
                    if (!cg.water[i])
                        continue;
                    const auto land = [&](int lx, int ly)
                    {
                        return cg.water[std::size_t(wrapi(ly, H)) * W
                                        + wrapi(lx, W)] == 0u;
                    };
                    std::uint8_t a = 0;
                    if (land(x - 1, y) && land(x + 1, y))
                        a |= kWaterCrossEW;
                    if (land(x, y - 1) && land(x, y + 1))
                        a |= kWaterCrossNS;
                    axes[i] = a;
                }
            }
            return axes;
        }

        // `waterCrossAxes` (optional): bridgeable water joins the banks it
        // touches — two shores of a one-cell river are ONE road component,
        // because the planner can span it. Deliberately permissive (any
        // 8-neighbour through the wet cell): the component test is a cheap
        // pre-prune, and A* with the strict axis law stays the final judge —
        // a falsely joined pair just fails its search and is stripped, while
        // a falsely SPLIT pair would lose its road with no appeal.
        std::vector<int> build_land_components(const TerrainData &td,
                                               float seaLevel,
                                               const std::vector<std::uint8_t>
                                                   *waterCrossAxes = nullptr)
        {
            const int W = td.width;
            const int H = td.height;
            const int total = W * H;
            std::vector<int> component(std::size_t(total), -1);
            std::vector<int> queue;
            queue.reserve(std::size_t(total) / 4);
            constexpr int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            constexpr int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

            const auto enterable = [&](std::size_t k)
            {
                if (float(td.rgba[k * 4 + 0]) / 255.0f >= seaLevel)
                    return true; // land walks
                return waterCrossAxes && (*waterCrossAxes)[k] != 0u; // spans join
            };

            int componentId = 0;
            for (int start = 0; start < total; ++start)
            {
                // Components SEED on land only: a bridgeable cell joins
                // shores, it is not a shore.
                if (component[std::size_t(start)] >= 0 ||
                    float(td.rgba[std::size_t(start) * 4 + 0]) / 255.0f < seaLevel)
                    continue;

                queue.clear();
                queue.push_back(start);
                component[std::size_t(start)] = componentId;
                for (std::size_t head = 0; head < queue.size(); ++head)
                {
                    const int cur = queue[head];
                    const int x = cur % W;
                    const int y = cur / W;
                    for (int dir = 0; dir < 8; ++dir)
                    {
                        const int nx = wrapi(x + dx[dir], W);
                        const int ny = wrapi(y + dy[dir], H);
                        const int ni = ny * W + nx;
                        const std::size_t k = std::size_t(ni);
                        if (component[k] >= 0 || !enterable(k))
                            continue;
                        component[k] = componentId;
                        queue.push_back(ni);
                    }
                }
                ++componentId;
            }
            return component;
        }

        // ── The road tracer's POLICY, all that survived of its private A* ──
        // (the search itself is THE find_path in pathfinding.{h,cpp} now,
        // CANON S7 — the byte-for-byte twin that lived here is dead).
        //
        // Anything at or above this is water and the road refuses it. Derived
        // from the water price below (167) with a margin under it, so the two
        // move together: the threshold is not a second number about water, it
        // is the same number read as a gate.
        constexpr float kRoadWaterBlockThreshold = 166.99f;
        // UNBRIDGEABLE water is not priced, it is REJECTED: this sentinel is
        // only the flag the block-threshold reads, never a weight a path can
        // pay. BRIDGEABLE water (build_water_cross_axes above — one cell
        // thick on a cardinal axis) keeps the honest water bed THE step law
        // already priced it at (biome_sp_weight Water = 10.0): the planner's
        // willingness to build a span is exactly what the march law says the
        // wet cell costs, so a bridge pays off where the detour is longer
        // than the crossing is dear — no new constant (CANON S26).
        constexpr float kRoadWaterReject = 167.00f;

        // The step budget of one road search. Small maps get the honest
        // whole-map budget; large maps match the last known-good native road
        // baseline (the earlier 4096-step cap pruned same-island roads before
        // A* could route around bays and rivers, leaving many cities with no
        // visible road).
        constexpr int kRoadSearchSmallMapMaxCells = 65536;
        constexpr int kRoadSearchLargeMapMaxSteps = 200000;
        inline int road_search_max_steps(std::size_t cellCount)
        {
            return cellCount <= std::size_t(kRoadSearchSmallMapMaxCells)
                ? int(cellCount)
                : kRoadSearchLargeMapMaxSteps;
        }

    } // namespace

    std::vector<TreePoint> spawn_trees(const TerrainData &td, std::uint32_t seed,
                                       float seaLevel)
    {
        const int mw = td.width;
        const int mh = td.height;
        std::vector<TreePoint> out;
        const std::size_t totalCells = td.cell_count();
        if (totalCells == 0u || totalCells > std::size_t(std::numeric_limits<int>::max())
            || !td.has_rgba_storage())
            return out;
        out.reserve(totalCells / 16u);

        std::vector<std::uint8_t> riverExclude;
        if (td.has_river_storage())
        {
            riverExclude.assign(totalCells, 0);
            constexpr int kRiverBuffer = 2;
            for (std::size_t ri = 0; ri < totalCells; ++ri)
            {
                if (td.riverData[ri] == 0)
                    continue;
                const int rx = int(ri % std::size_t(mw));
                const int ry = int(ri / std::size_t(mw));
                for (int dy = -kRiverBuffer; dy <= kRiverBuffer; ++dy)
                {
                    const int by = wrapi(ry + dy, mh);
                    for (int dx = -kRiverBuffer; dx <= kRiverBuffer; ++dx)
                    {
                        const int bx = wrapi(rx + dx, mw);
                        riverExclude[std::size_t(by) * mw + bx] = 1;
                    }
                }
            }
        }

        const std::int32_t sd = std::int32_t(seed);

        for (int y = 0; y < mh; ++y)
        {
            for (int x = 0; x < mw; ++x)
            {
                const std::size_t idx = std::size_t(y) * std::size_t(mw) + std::size_t(x);

                // Hard exclusion: water (mask channel A == 0).
                if (td.rgba[idx * 4 + 3] == 0)
                    continue;
                if (!riverExclude.empty() && riverExclude[idx] > 0)
                    continue;

                const float h = float(td.rgba[idx * 4 + 0]) / 255.0f;
                const std::uint8_t temp = td.rgba[idx * 4 + 2];
                const std::uint8_t moist = td.rgba[idx * 4 + 1];

                // Shoreline buffer + mountain cap.
                if (h < seaLevel + 0.03f || h > 0.80f)
                    continue;

                // Biome exclusion via TS 3×3 climate matrix
                // (row 0 col 0 = Tundra, row 0 col 2 = Snow, row 2 col 0 = Desert).
                const int tRow = std::min(2, int(temp) / 86);
                const int moistCol = std::min(2, int(moist) / 86);
                if ((tRow == 0 && moistCol != 1) || (tRow == 2 && moistCol == 0))
                    continue;

                // Organic noise — domain-warped multi-scale FBM.
                const float nx = float(x) / float(mw);
                const float ny = float(y) / float(mh);
                const float warpX = fbm(nx * 8.0f, ny * 8.0f, sd + 100, 3, 8);
                const float warpY = fbm(nx * 8.0f, ny * 8.0f, sd + 200, 3, 8);
                const float wnx = nx + (warpX - 0.5f) * 0.06f;
                const float wny = ny + (warpY - 0.5f) * 0.06f;

                // The frequency IS the period: the world spans exactly this
                // many lattice cells, so wrapping by it closes the ring.
                const float large = fbm(wnx * 14.0f, wny * 14.0f, sd + 500, 4, 14);
                const float med   = fbm(wnx * 35.0f, wny * 35.0f, sd + 600, 3, 35);
                const float fine  = fbm(wnx * 70.0f, wny * 70.0f, sd + 700, 2, 70);
                const float noise = large * 0.40f + med * 0.35f + fine * 0.25f;

                constexpr float t0 = 0.35f, t1 = 0.55f;
                const float clamped = std::clamp((noise - t0) / (t1 - t0), 0.0f, 1.0f);
                const float density = clamped * clamped * (3.0f - 2.0f * clamped);
                const float cellRand = ihash01(x, y, sd + 999);
                if (cellRand < density)
                    out.push_back({x, y});
            }
        }
        return out;
    }

    // Road tracing between connected city pairs over a road-aware cost grid.
    // Cross-island pairs are component-pruned (one-cell water counts as a
    // join — a span can cross it). Same-island pairs use generation-tagged
    // whole-map A* that refuses wide water and PAYS for one-cell crossings,
    // which land as FT_Bridge (build_feature_layer).
    std::vector<std::uint8_t> trace_roads(const TerrainData &td, Politik &P,
                                          RoadTraceStats *stats,
                                          float seaLevel,
                                          const TreeLayer *treeLayer)
    {
        const int W = td.width, H = td.height;
        RoadTraceStats localStats;
        localStats.cityCount = int(P.cities.size());
        const std::size_t totalCells = td.cell_count();
        if (totalCells == 0u || totalCells > std::size_t(std::numeric_limits<int>::max())
            || !td.has_rgba_storage())
        {
            if (stats)
                *stats = localStats;
            return {};
        }

        std::vector<std::uint8_t> mask(totalCells, 0);
        if (P.cities.empty())
        {
            if (stats)
                *stats = localStats;
            return mask;
        }

        // Road-specific cost grid (FeatureLayer is built *after* roads, so
        // mountain/water are derived directly from terrain height).
        // Prices for the road A*. NOTHING here may cost less than 1.0, because
        // the octile heuristic charges exactly 1.0 per cardinal
        // step: a cell cheaper than that makes h an OVER-estimate, A* stops
        // being optimal, and the discount becomes invisible to the very search
        // it was meant to steer.
        //
        // That is what happened. An existing road cell was priced 0.30 to
        // encourage reuse, and measured against a Dijkstra over the identical
        // graph the search came out 205 % above the optimum — it used ZERO of
        // the 400 road cells lying along its way. In the shipping road pass two
        // city pairs ten rows apart laid 802 cells as two parallel twins where
        // reuse would have cost about 421. The discount had never worked once.
        //
        // So the SAME economics are expressed without going under the floor:
        // road stays at 1.0 and open ground is surcharged instead. The ratios
        // are the old ones (ground 3.33× a road, mountain 16.7×), the heuristic
        // is admissible again, and reuse is now something the search can see.
        // The planner walks THE step law (movement_cost.h build_cost_grid):
        // the road is laid over the same weights the march will pay — biome
        // bed + continuous canopy (a pine thicket finally costs more than a
        // meadow, so roads route AROUND deep woods), and the edge climb rides
        // in find_path exactly as it does in every walker. This was the
        // second, private cost table (its own land 3.33 / mountain 16.7 /
        // its own raw-byte mountain test — canon-audit H1/§7.9); the mountain
        // WALL is now the biome bed plus the honest climb. Existing road
        // cells and city anchors are priced at the paved bed — the cheapest
        // step there is, so reuse stays visible to an admissible search.
        // Water wider than one cell is not priced, it is REJECTED:
        // kRoadWaterReject is only the flag the block-threshold reads, never
        // a weight a path can pay. One-cell water (waterAxes) stays at the
        // step law's own water bed — payable, and paying it lays a BRIDGE.
        const float kRoadShare = feature_bed_weight(FT_Road);
        PathCostData cg = build_cost_grid(td, nullptr, treeLayer);
        const std::vector<std::uint8_t> waterAxes = build_water_cross_axes(cg);
        for (std::size_t i = 0; i < totalCells; ++i)
        {
            if (cg.water[i] && waterAxes[i] == 0u)
                cg.costGrid[i] = kRoadWaterReject;
        }
        const std::vector<int> landComponent =
            build_land_components(td, seaLevel, &waterAxes);

        for (const City &c : P.cities)
            cg.costGrid[std::size_t(wrapi(c.y, H)) * W + wrapi(c.x, W)] = kRoadShare;

        std::vector<std::pair<int, int>> dropPairs;
        PathScratch pathScratch;
        const int maxSteps = road_search_max_steps(totalCells);
        auto path_crosses_rejected_water = [&](const PathResult &path)
        {
            if (!path.found)
                return false;
            for (const PathPoint &p : path.path)
            {
                if (cg.costGrid[std::size_t(p.y) * W + p.x]
                        >= kRoadWaterReject - 0.01f)
                    return true;
            }
            return false;
        };
        for (std::size_t i = 0; i < P.cities.size(); ++i)
        {
            for (int b : P.cities[i].connections)
            {
                if (b < 0 || std::size_t(b) <= i || std::size_t(b) >= P.cities.size())
                    continue;
                const City &a = P.cities[i];
                const City &B = P.cities[std::size_t(b)];
                ++localStats.attemptedEdges;

                const int ax = wrapi(a.x, W);
                const int ay = wrapi(a.y, H);
                const int bx = wrapi(B.x, W);
                const int by = wrapi(B.y, H);
                const int aComponent = landComponent[std::size_t(ay) * W + ax];
                const int bComponent = landComponent[std::size_t(by) * W + bx];
                if (aComponent < 0 || aComponent != bComponent)
                {
                    dropPairs.push_back({int(i), b});
                    ++localStats.prunedEdges;
                    ++localStats.componentPrunedEdges;
                    continue;
                }

                int edgeSteps = 0;
                PathResult pr = find_path(cg, ax, ay, bx, by, pathScratch,
                                          maxSteps, kRoadWaterBlockThreshold,
                                          &edgeSteps, waterAxes.data());
                localStats.expansions += edgeSteps;
                const bool crossesWater = path_crosses_rejected_water(pr);

                if (!pr.found || crossesWater)
                {
                    dropPairs.push_back({int(i), b});
                    ++localStats.prunedEdges;
                    continue;
                }

                for (const PathPoint &p : pr.path)
                {
                    std::size_t k = std::size_t(p.y) * W + p.x;
                    mask[k] = 255;
                    cg.costGrid[k] = kRoadShare;
                }
                ++localStats.keptEdges;
            }
        }

        auto strip = [&](int from, int to)
        {
            for (int &c : P.cities[std::size_t(from)].connections)
                if (c == to)
                {
                    c = -1;
                    break;
                }
            int *arr = P.cities[std::size_t(from)].connections;
            constexpr int N = sizeof(P.cities[std::size_t(from)].connections) / sizeof(P.cities[std::size_t(from)].connections[0]);
            int w = 0;
            for (int r = 0; r < N; ++r)
                if (arr[r] != -1)
                    arr[w++] = arr[r];
            for (; w < N; ++w)
                arr[w] = -1;
        };
        for (auto [x, y] : dropPairs)
        {
            strip(x, y);
            strip(y, x);
        }

        if (stats)
            *stats = localStats;
        return mask;
    }

    // Dirt lanes — the FT_DirtRoad rows of the road-class registry
    // (spawners.h kRoadClasses). Same A*, same step law, same water gate as
    // the stone pass; the cost grid is built AFTER stone landed in
    // `features`, so laid stone prices at its 1.0 bed and dirt lanes merge
    // into the highways instead of twinning beside them. The old tracer
    // (spiral scan for the nearest road cell + straight lerp that knew
    // nothing of the world but "not water") is dead; what survives of the
    // spiral is the nearest-TARGET choice for the landmark row.
    int trace_dirt_roads(FeatureLayer &features, const TerrainData &td,
                         const std::vector<VillageRoadSite> &villages,
                         const std::vector<RoadSite> &landmarks,
                         int landmarkReach,
                         float seaLevel,
                         const TreeLayer *treeLayer)
    {
        const std::size_t totalCells = td.cell_count();
        if (totalCells == 0u
            || totalCells > std::size_t(std::numeric_limits<int>::max())
            || !td.has_rgba_storage()
            || !features.covers(td.width, td.height))
        {
            return 0;
        }
        const int W = td.width, H = td.height;

        // The same planner economics as trace_roads: THE step-cost grid —
        // which now already carries the stone (and its bridges) at their
        // beds — with unbridgeable water rejected, never priced, and
        // one-cell water payable at the step law's water bed. Laid dirt is
        // priced at ITS bed as it lands, so later villages reuse earlier
        // lanes — and reuse the highways' bridges, which sit at the paved
        // bed already.
        const float kDirtShare = feature_bed_weight(FT_DirtRoad);
        const float kBridgeShare = feature_bed_weight(FT_Bridge);
        PathCostData cg = build_cost_grid(td, &features, treeLayer);
        const std::vector<std::uint8_t> waterAxes = build_water_cross_axes(cg);
        for (std::size_t i = 0; i < totalCells; ++i)
        {
            // An existing bridge is bridgeable BY CONSTRUCTION (same water
            // mask, same axis test), so this can never wall off laid stone.
            if (cg.water[i] && waterAxes[i] == 0u)
                cg.costGrid[i] = kRoadWaterReject;
        }
        const std::vector<int> landComponent =
            build_land_components(td, seaLevel, &waterAxes);
        const int maxSteps = road_search_max_steps(totalCells);
        PathScratch scratch;

        int laid = 0;
        auto stamp = [&](int x, int y)
        {
            const std::size_t idx = std::size_t(y) * W + x;
            if (FeatureLayer::decode(features.data[idx]) == FT_None)
            {
                // Every bridge is stone (owner, 2026-08-29): a dirt lane that
                // crosses water lays the same span the highway does — a dirt
                // bridge would be a second bridge kind for no world reason.
                features.data[idx] = cg.water[idx] ? FT_Bridge : FT_DirtRoad;
                ++laid;
            }
            const float share = cg.water[idx] ? kBridgeShare : kDirtShare;
            if (cg.costGrid[idx] > share)
                cg.costGrid[idx] = share;
        };
        // The settlement-on-a-road invariant (subworld road stitching is
        // feature-driven): every village cell carries its road class, and the
        // village anchors price at the dirt bed exactly as city anchors price
        // at the paved bed in the stone pass.
        for (const VillageRoadSite &v : villages)
            stamp(wrapi(v.x, W), wrapi(v.y, H));

        auto lay = [&](int ax, int ay, int bx, int by)
        {
            const int ac = landComponent[std::size_t(ay) * W + ax];
            const int bc = landComponent[std::size_t(by) * W + bx];
            if (ac < 0 || ac != bc)
                return; // different island: honestly no road, never a lerp
            PathResult pr = find_path(cg, ax, ay, bx, by, scratch, maxSteps,
                                      kRoadWaterBlockThreshold, nullptr,
                                      waterAxes.data());
            if (!pr.found)
                return;
            for (const PathPoint &p : pr.path)
                stamp(p.x, p.y);
        };

        for (const RoadClassDef &row : kRoadClasses)
        {
            if (row.surface != FT_DirtRoad)
                continue; // stone rows are trace_roads' business
            for (const VillageRoadSite &v : villages)
            {
                const int vx = wrapi(v.x, W);
                const int vy = wrapi(v.y, H);
                switch (row.link)
                {
                case RoadLink::VillageHomeCity:
                    if (v.hasCity)
                        lay(vx, vy, wrapi(v.cityX, W), wrapi(v.cityY, H));
                    break;
                case RoadLink::VillageNearestLandmark:
                {
                    // Nearest-target CHOICE (the spiral's surviving job);
                    // torus Chebyshev, ties to the earlier row.
                    int bestD = landmarkReach, bx = -1, by = -1;
                    for (const RoadSite &lm : landmarks)
                    {
                        const int lx = wrapi(lm.x, W);
                        const int ly = wrapi(lm.y, H);
                        int dx = std::abs(lx - vx);
                        dx = std::min(dx, W - dx);
                        int dy = std::abs(ly - vy);
                        dy = std::min(dy, H - dy);
                        const int d = std::max(dx, dy);
                        if (d < bestD)
                        {
                            bestD = d;
                            bx = lx;
                            by = ly;
                        }
                    }
                    if (bx >= 0)
                        lay(vx, vy, bx, by);
                    break;
                }
                case RoadLink::CityConnections:
                    break;
                }
            }
        }
        return laid;
    }

    FeatureLayer build_feature_layer(const TerrainData &td,
                                     const std::vector<std::uint8_t> &roadMask,
                                     const std::vector<std::uint8_t> *dirtMask,
                                     float seaLevel)
    {
        FeatureLayer fl;
        std::size_t total = 0;
        if (!FeatureLayer::cell_count_for(td.width, td.height, total)
            || total > std::numeric_limits<std::size_t>::max() / 4u)
            return fl;

        fl.resize(td.width, td.height);
        if (fl.data.empty())
            return fl;

        if (td.rgba.size() < total * 4u)
            return fl;

        const std::size_t roadMaskLimit = std::min(roadMask.size(), total);
        const std::size_t dirtMaskLimit = dirtMask
            ? std::min(dirtMask->size(), total)
            : 0u;
        auto is_water = [&](std::size_t idx)
        {
            return td.rgba[idx * 4u + 3] == 0
                || float(td.rgba[idx * 4u + 0]) / 255.0f < seaLevel;
        };
        // Biome water — THE baked land mask (biome_at_cell's own answer, the
        // exact cells the planner priced as water). A road path only ever
        // stands on such a cell by PAYING the bridgeable-water price, so a
        // wet masked cell IS a span: it stamps FT_Bridge. The broader
        // is_water above (mask OR raw height) keeps its old job — the
        // coast's height/mask disagreement strip still gets no feature.
        auto is_biome_water = [&](std::size_t idx)
        {
            return td.rgba[idx * 4u + 3] == 0;
        };
        // The feature layer carries only MAN-MADE structures: dirt roads,
        // then roads (last-writer-wins), bridges where either crossed water.
        // Mountains are the Mountain biome (elevation-classified) and
        // forests are the tree-count field (macro/tree_layer.h, seeded by
        // the spawn_trees massif mask).
        if (dirtMask)
        {
            for (std::size_t i = 0; i < dirtMaskLimit; ++i)
            {
                if (!(*dirtMask)[i])
                    continue;
                if (is_biome_water(i))
                    fl.data[i] = FT_Bridge; // every bridge is stone (owner)
                else if (!is_water(i))
                    fl.data[i] = FT_DirtRoad;
            }
        }
        for (std::size_t i = 0; i < roadMaskLimit; ++i)
        {
            if (!roadMask[i])
                continue;
            if (is_biome_water(i))
                fl.data[i] = FT_Bridge;
            else if (!is_water(i))
                fl.data[i] = FT_Road;
        }
        return fl;
    }

    // field_wheat_min / plough_cell_ok / plough_field_cell are declared in
    // spawners.h beside the worldgen stamp below, but DEFINED in
    // macro_stock.cpp: the daily labour rotation (npc_ai.cpp) calls them,
    // and its test targets link the land's doors, not the worldgen — the
    // same link seam that keeps world_tick's tests off this file.

    void stamp_field_features(FeatureLayer& fl, const MacroWorld& world,
                              const std::vector<FieldSite>& villages,
                              float seaLevel)
    {
        std::size_t total = 0;
        if (!FeatureLayer::cell_count_for(fl.width, fl.height, total)
            || fl.data.size() < total)
            return;
        if (!world.terrain) return;
        const TerrainData& td = *world.terrain;
        if (td.width != fl.width || td.height != fl.height
            || td.rgba.size() < total * 4u)
            return;

        const int w = fl.width;
        const int h = fl.height;
        auto cell_ok = [&](int x, int y, int& wheatOut) {
            return plough_cell_ok(fl, world, x, y, wheatOut, seaLevel);
        };

        for (const FieldSite& v : villages) {
            // Candidates: the home-field box (±kSettlementReach — the ONE
            // radius the crews harvest and the plough prospects) around the
            // village cell, in fixed scan order — the pick is deterministic
            // from the world data alone (context, not dice).
            struct Candidate { int x, y; int wheat; };
            Candidate best[kFieldsPerVillage];
            int found = 0;
            for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
                for (int dx = -kSettlementReach; dx <= kSettlementReach;
                     ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int x = FeatureLayer::wrap_coord(v.x + dx, w);
                    const int y = FeatureLayer::wrap_coord(v.y + dy, h);
                    int wheat = 0;
                    if (!cell_ok(x, y, wheat)) continue;
                    // Insertion into the fattest-first shortlist.
                    int at = found < kFieldsPerVillage ? found : -1;
                    for (int k = 0; k < found; ++k) {
                        if (wheat > best[k].wheat) { at = k; break; }
                    }
                    if (at < 0) continue;
                    const int last = found < kFieldsPerVillage
                        ? found : kFieldsPerVillage - 1;
                    for (int k = last; k > at; --k) best[k] = best[k - 1];
                    best[at] = Candidate{x, y, wheat};
                    if (found < kFieldsPerVillage) ++found;
                }
            }
            for (int k = 0; k < found; ++k) {
                fl.data[std::size_t(best[k].y) * std::size_t(w)
                        + std::size_t(best[k].x)] = FT_Field;
            }
        }
    }

} // namespace sm
