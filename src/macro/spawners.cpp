#include "macro/spawners.h"
#include "tables/biomes.h"
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
                        return !cg.water_at(lx, ly);
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
                if (!td.is_water(std::uint32_t(k)))
                    return true; // land walks
                return waterCrossAxes && (*waterCrossAxes)[k] != 0u; // spans join
            };

            int componentId = 0;
            for (int start = 0; start < total; ++start)
            {
                // Components SEED on land only: a bridgeable cell joins
                // shores, it is not a shore.
                if (component[std::size_t(start)] >= 0 ||
                    td.is_water(std::uint32_t(start)))
                    continue;

                queue.clear();
                queue.push_back(start);
                component[std::size_t(start)] = componentId;
                for (std::size_t head = 0; head < queue.size(); ++head)
                {
                    const int cur = queue[head];
                    // Сосед фронта — шаг ИНДЕКСА (cell_step, ЗАКОН АДРЕСА).
                    for (int dir = 0; dir < 8; ++dir)
                    {
                        const int ni = int(
                            cell_step(std::uint32_t(cur), dx[dir], dy[dir], W));
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

    std::vector<TreePoint> spawn_trees(const TerrainData &td, std::uint32_t seed)
    {
        const int mw = td.width;
        const int mh = td.height;
        std::vector<TreePoint> out;
        const std::size_t totalCells = td.cell_count();
        if (totalCells == 0u || totalCells > std::size_t(std::numeric_limits<int>::max())
            || !td.has_rgba_storage())
            return out;
        out.reserve(totalCells / 16u);

        // (БУФЕР ВОКРУГ РЕК СНЕСЁН 2026-10-03, M-211. Вердикт владельца:
        // «пусть не отступа ни на сколько, просто минимальное простое бинарное
        // правило — на воде леса нет». Правило это стоит ниже одной строкой
        // (`b == Water`), а буфер был вторым механизмом поверх него и читал
        // снесённую маску русла.)

        const std::int32_t sd = std::int32_t(seed);

        for (int y = 0; y < mh; ++y)
        {
            for (int x = 0; x < mw; ++x)
            {
                const std::size_t idx = std::size_t(y) * std::size_t(mw) + std::size_t(x);

                // ГДЕ ЛЕС НЕ ВСТАЁТ — спрошено у ЕДИНСТВЕННОГО каскада клетки
                // (map_generator.h biome_at_cell, M-110). Здесь стоял ТРЕТИЙ
                // каскад: своя дверь воды, СВОЙ горный потолок `h > 0.80f` и
                // климатическая матрица отдельным вызовом. Потолок и был
                // дефектом ЗАКОНА КОНСТАНТ: мир судил гору по 0.75, а лес по
                // 0.80, и полоса между ними — «камень на карте, лес под ногами».
                // Читается ПОЛЕ (ЗАКОН ПОЛЯ): лес ставится уже над рождённым
                // миром, значит каскад для него — прошлое.
                // ВОДУ СПРАШИВАЕМ У ПОРОГА РЕЛЬЕФА, И ТОЛЬКО У НЕГО (вердикт
                // владельца 2026-10-03: «как раз через единую — вода тоже через
                // неё, потому что это порог рельефа, то есть через него всё и
                // будет»). Здесь стояло `b == Water`, то есть вопрос о воде
                // задавался ПОЛЮ БИОМА — второму спеллингу того же факта. В
                // живом мире они совпадают лишь потому, что поле печётся ПОСЛЕ
                // вреза русел; согласие по порядку вызовов и есть та хрупкость,
                // на которой проект уже горел. Свидетель поймал это числом:
                // 39 деревьев встали на воде, когда рельеф срезали, а поле нет.
                if (td.is_water(std::uint32_t(idx)))
                    continue;
                // Камень не почва; мерзлота и песок — три ряда климатической
                // матрицы, на которых лес не растёт. Тайга, холодная СЕРЕДИНА,
                // деревья держит.
                const Biome b = biome_at_cell(td, std::uint32_t(idx));
                if (b == Mountain || b == Tundra || b == Snow || b == Desert)
                    continue;

                // (ОТСТУП ОТ УРЕЗА СНЕСЁН ТЕМ ЖЕ ВЕРДИКТОМ. Здесь стоял запрет
                // «клетка суши, у которой сосед — вода», то есть отступ в одну
                // клетку от ЛЮБОЙ воды. Он честно унифицировал берег реки и
                // моря, но остался отступом: правило теперь одно и бинарное —
                // лес не встаёт НА воде, и больше нигде не запрещён. Следствие
                // названо вслух: лес выходит к самому урезу по всему миру.)

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
    std::vector<std::uint8_t> trace_roads(const TerrainData &td,
                                          std::vector<City> &cities,
                                          RoadTraceStats *stats,
                                          const TreeLayer *treeLayer)
    {
        const int W = td.width, H = td.height;
        RoadTraceStats localStats;
        localStats.cityCount = int(cities.size());
        const std::size_t totalCells = td.cell_count();
        if (totalCells == 0u || totalCells > std::size_t(std::numeric_limits<int>::max())
            || !td.has_rgba_storage())
        {
            if (stats)
                *stats = localStats;
            return {};
        }

        std::vector<std::uint8_t> mask(totalCells, 0);
        if (cities.empty())
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
            build_land_components(td, &waterAxes);

        for (const City &c : cities)
            cg.costGrid[cell_of(c.x, c.y, W)] = kRoadShare;

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
        for (std::size_t i = 0; i < cities.size(); ++i)
        {
            for (int b : cities[i].connections)
            {
                if (b < 0 || std::size_t(b) <= i || std::size_t(b) >= cities.size())
                    continue;
                const City &a = cities[i];
                const City &B = cities[std::size_t(b)];
                ++localStats.attemptedEdges;

                const int ax = wrap_axis(a.x, W);
                const int ay = wrap_axis(a.y, H);
                const int bx = wrap_axis(B.x, W);
                const int by = wrap_axis(B.y, H);
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
            for (int &c : cities[std::size_t(from)].connections)
                if (c == to)
                {
                    c = -1;
                    break;
                }
            int *arr = cities[std::size_t(from)].connections;
            constexpr int N = sizeof(cities[std::size_t(from)].connections) / sizeof(cities[std::size_t(from)].connections[0]);
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
            build_land_components(td, &waterAxes);
        const int maxSteps = road_search_max_steps(totalCells);
        PathScratch scratch;

        int laid = 0;
        auto stamp = [&](int x, int y)
        {
            const std::uint32_t idx = cell_of(x, y, W);
            if (features.at(idx) == FT_None)
            {
                // «Дорога на воде есть мост» — теперь КОЛОНКА строки фичи, а
                // не ветка здесь (M-212): одно правило мира было написано в
                // двух местах. Every bridge is stone (owner, 2026-08-29) —
                // грунтовка и шоссе дают один и тот же пролёт, и это тоже
                // сказано таблицей: у обеих строк `onWater` = `FT_Bridge`.
                stamp_feature(features, td, idx, FT_DirtRoad);
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
            stamp(wrap_axis(v.x, W), wrap_axis(v.y, H));

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
                const int vx = wrap_axis(v.x, W);
                const int vy = wrap_axis(v.y, H);
                switch (row.link)
                {
                case RoadLink::VillageHomeCity:
                    if (v.hasCity)
                        lay(vx, vy, wrap_axis(v.cityX, W), wrap_axis(v.cityY, H));
                    break;
                case RoadLink::VillageNearestLandmark:
                {
                    // Nearest-target CHOICE (the spiral's surviving job);
                    // torus Chebyshev, ties to the earlier row.
                    int bestD = landmarkReach, bx = -1, by = -1;
                    for (const RoadSite &lm : landmarks)
                    {
                        const int lx = wrap_axis(lm.x, W);
                        const int ly = wrap_axis(lm.y, H);
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
                                     const std::vector<std::uint8_t> *dirtMask)
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
        // ЗДЕСЬ БЫЛО ДВА ПРЕДИКАТА ВОДЫ — «маска ИЛИ float-высота» и
        // «только маска», — и шапка второго объясняла разницу «полосой
        // несогласия берега». Полоса была не миром, а РАЗНИЦЕЙ ДВУХ
        // СПЕЛЛИНГОВ одного порога (округление float против байта).
        // С M-212 этот вопрос звонящий не задаёт вовсе: воду спрашивает
        // ОДНА дверь штампа, и «мокрая клетка под дорогой есть пролёт»
        // стало КОЛОНКОЙ строки (`FeatureDef::onWater`), а не веткой здесь.
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
                // `i` ЕСТЬ адрес клетки (маски идут тем же плоским порядком),
                // поэтому дверь зовётся индексной формой — сворачивать нечего.
                stamp_feature(fl, td, std::uint32_t(i), FT_DirtRoad);
            }
        }
        for (std::size_t i = 0; i < roadMaskLimit; ++i)
        {
            if (!roadMask[i])
                continue;
            stamp_feature(fl, td, std::uint32_t(i), FT_Road);
        }
        return fl;
    }

    // plough_cell_ok / plough_field_cell are declared in
    // spawners.h beside the worldgen stamp below, but DEFINED in
    // macro_stock.cpp: the daily labour rotation (npc_ai.cpp) calls them,
    // and its test targets link the land's doors, not the worldgen — the
    // same link seam that keeps world_tick's tests off this file.

    void stamp_field_features(FeatureLayer& fl, const MacroWorld& world,
                              const std::vector<FieldSite>& villages)
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
        auto cell_ok = [&](int x, int y, int& wheatOut) {
            return plough_cell_ok(fl, world, x, y, wheatOut);
        };

        for (const FieldSite& v : villages) {
            // Candidates: the home-field box (±kSettlementReach — the ONE
            // radius the crews harvest and the plough prospects) around the
            // village cell, in fixed scan order — the pick is deterministic
            // from the world data alone (context, not dice).
            struct Candidate { int x, y; int wheat; };
            Candidate best[kFieldsPerVillage];
            int found = 0;
            const std::uint32_t vIdx = cell_of(v.x, v.y, w);
            for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
                for (int dx = -kSettlementReach; dx <= kSettlementReach;
                     ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const std::uint32_t n = cell_step(vIdx, dx, dy, w);
                    const int x = cell_x(n, w);
                    const int y = cell_y(n, w);
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
                // ОДНА ВСПАШКА НА ГЕНЕЗИС И НА РАНТАЙМ (ЗАКОН АГНОСТИЧНОСТИ,
                // M-112): дверь не знает, кто её зовёт, и здесь у неё
                // появился первый жилец — до этого генезис писал `FT_Field`
                // сырым индексом мимо неё, то есть на один и тот же вопрос
                // «как рождается пашня» в мире было два ответа. Гейты те же
                // самые: шортлист набран `plough_cell_ok`, и дверь спросит
                // его повторно — на клетках шортлиста он даёт то же «да».
                plough_field_cell(fl, world, best[k].x, best[k].y, FT_Field);
            }
        }
    }

} // namespace sm
