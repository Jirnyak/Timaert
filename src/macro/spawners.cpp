#include "macro/spawners.h"
#include "macro/biomes.h"
#include "macro/macro_stock.h"      // MacroWorld — the registry's context
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

        inline float smoothNoise(float x, float y, std::int32_t sd)
        {
            int ix = int(std::floor(x));
            int iy = int(std::floor(y));
            float fx = x - float(ix);
            float fy = y - float(iy);
            float sx = fx * fx * (3.0f - 2.0f * fx);
            float sy = fy * fy * (3.0f - 2.0f * fy);
            float n00 = ihash01(ix, iy, sd);
            float n10 = ihash01(ix + 1, iy, sd);
            float n01 = ihash01(ix, iy + 1, sd);
            float n11 = ihash01(ix + 1, iy + 1, sd);
            float a = n00 + (n10 - n00) * sx;
            float b = n01 + (n11 - n01) * sx;
            return a + (b - a) * sy;
        }

        inline float fbm(float x, float y, std::int32_t sd, int octaves)
        {
            float value = 0.0f, amp = 1.0f, maxAmp = 0.0f, freq = 1.0f;
            for (int i = 0; i < octaves; ++i)
            {
                value += smoothNoise(x * freq, y * freq, sd + i * 100) * amp;
                maxAmp += amp;
                amp *= 0.5f;
                freq *= 2.0f;
            }
            return value / maxAmp;
        }

        std::vector<int> build_land_components(const TerrainData &td,
                                               float seaLevel)
        {
            const int W = td.width;
            const int H = td.height;
            const int total = W * H;
            std::vector<int> component(std::size_t(total), -1);
            std::vector<int> queue;
            queue.reserve(std::size_t(total) / 4);
            constexpr int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
            constexpr int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

            int componentId = 0;
            for (int start = 0; start < total; ++start)
            {
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
                        if (component[k] >= 0 ||
                            float(td.rgba[k * 4 + 0]) / 255.0f < seaLevel)
                            continue;
                        component[k] = componentId;
                        queue.push_back(ni);
                    }
                }
                ++componentId;
            }
            return component;
        }

        constexpr int kRoadDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        constexpr int kRoadDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        constexpr float kRoadStep[8] = {1.0f, 1.4142136f, 1.0f, 1.4142136f,
                                        1.0f, 1.4142136f, 1.0f, 1.4142136f};
        constexpr float kRoadWaterBlockThreshold = 49.99f;

        inline float road_octile_torus(int x1, int y1, int x2, int y2, int w, int h)
        {
            int dx = std::abs(x2 - x1);
            int dy = std::abs(y2 - y1);
            if (dx > w / 2)
                dx = w - dx;
            if (dy > h / 2)
                dy = h - dy;
            return dx > dy
                       ? float(dx) + 0.4142136f * float(dy)
                       : float(dy) + 0.4142136f * float(dx);
        }

        struct RoadNodeRec
        {
            int x, y;
            float g, f;
        };

        struct RoadIndexedHeap
        {
            std::vector<RoadNodeRec> items;
            std::vector<std::int32_t> indexOf;
            std::vector<std::uint32_t> tag;
            int width = 0;
            std::uint32_t generation = 1u;

            void init(int w, int h, std::uint32_t gen)
            {
                width = w;
                generation = gen;
                const std::size_t cells = std::size_t(w) * h;
                if (indexOf.size() != cells)
                {
                    indexOf.assign(cells, -1);
                    tag.assign(cells, 0u);
                }
                items.clear();
            }

            inline std::size_t key(int x, int y) const
            {
                return std::size_t(y) * width + x;
            }

            inline bool empty() const { return items.empty(); }

            inline bool contains(std::size_t k) const
            {
                return tag[k] == generation && indexOf[k] >= 0;
            }

            void push(const RoadNodeRec &n)
            {
                std::size_t k = key(n.x, n.y);
                if (contains(k))
                {
                    const std::int32_t at = indexOf[k];
                    if (n.f < items[std::size_t(at)].f)
                    {
                        items[std::size_t(at)] = n;
                        bubble_up(at);
                    }
                    return;
                }

                items.push_back(n);
                std::int32_t idx = std::int32_t(items.size() - 1);
                tag[k] = generation;
                indexOf[k] = idx;
                bubble_up(idx);
            }

            RoadNodeRec pop()
            {
                RoadNodeRec top = items.front();
                indexOf[key(top.x, top.y)] = -1;
                RoadNodeRec last = items.back();
                items.pop_back();
                if (!items.empty())
                {
                    items.front() = last;
                    indexOf[key(last.x, last.y)] = 0;
                    sink_down(0);
                }
                return top;
            }

            void bubble_up(std::int32_t i)
            {
                while (i > 0)
                {
                    std::int32_t p = (i - 1) >> 1;
                    if (items[std::size_t(i)].f >= items[std::size_t(p)].f)
                        return;
                    swap_at(i, p);
                    i = p;
                }
            }

            void sink_down(std::int32_t i)
            {
                std::int32_t n = std::int32_t(items.size());
                for (;;)
                {
                    std::int32_t l = 2 * i + 1, r = 2 * i + 2, s = i;
                    if (l < n && items[std::size_t(l)].f < items[std::size_t(s)].f)
                        s = l;
                    if (r < n && items[std::size_t(r)].f < items[std::size_t(s)].f)
                        s = r;
                    if (s == i)
                        return;
                    swap_at(i, s);
                    i = s;
                }
            }

            void swap_at(std::int32_t a, std::int32_t b)
            {
                std::swap(items[std::size_t(a)], items[std::size_t(b)]);
                indexOf[key(items[std::size_t(a)].x, items[std::size_t(a)].y)] = a;
                indexOf[key(items[std::size_t(b)].x, items[std::size_t(b)].y)] = b;
            }
        };

        struct RoadPathScratch
        {
            std::vector<float> gScores;
            std::vector<std::int32_t> parentX;
            std::vector<std::int32_t> parentY;
            RoadIndexedHeap open;
            std::vector<std::uint32_t> closedTag;
            std::vector<std::uint32_t> gTag;
            std::uint32_t generation = 1u;

            void init(int w, int h)
            {
                const std::size_t cells = std::size_t(w) * h;
                if (gScores.size() != cells)
                {
                    closedTag.assign(cells, 0u);
                    gScores.assign(cells, std::numeric_limits<float>::infinity());
                    parentX.assign(cells, -1);
                    parentY.assign(cells, -1);
                    gTag.assign(cells, 0u);
                }

                ++generation;
                if (generation == 0u)
                {
                    std::fill(closedTag.begin(), closedTag.end(), 0u);
                    std::fill(gTag.begin(), gTag.end(), 0u);
                    if (open.tag.size() == cells)
                        std::fill(open.tag.begin(), open.tag.end(), 0u);
                    generation = 1u;
                }

                open.init(w, h, generation);
            }

            inline bool closed_at(std::size_t idx) const
            {
                return closedTag[idx] == generation;
            }

            inline void close(std::size_t idx)
            {
                closedTag[idx] = generation;
            }

            inline float score(std::size_t idx) const
            {
                return gTag[idx] == generation
                    ? gScores[idx]
                    : std::numeric_limits<float>::infinity();
            }

            inline void set_score(std::size_t idx, float g, int px, int py)
            {
                gTag[idx] = generation;
                gScores[idx] = g;
                parentX[idx] = px;
                parentY[idx] = py;
            }
        };

        PathResult find_road_path(const PathCostData &data,
                                  int startX, int startY,
                                  int endX, int endY,
                                  RoadPathScratch &scratch,
                                  int *stepsOut)
        {
            if (stepsOut)
                *stepsOut = 0;
            PathResult out;
            const int W = data.width, H = data.height;
            if (W <= 0 || H <= 0)
                return out;

            const int sx = wrapi(startX, W);
            const int sy = wrapi(startY, H);
            const int ex = wrapi(endX, W);
            const int ey = wrapi(endY, H);

            if (sx == ex && sy == ey)
            {
                out.path.push_back({sx, sy});
                out.found = true;
                return out;
            }

            scratch.init(W, H);

            RoadNodeRec start{sx, sy, 0.0f, road_octile_torus(sx, sy, ex, ey, W, H)};
            scratch.set_score(std::size_t(sy) * W + sx, 0.0f, -1, -1);
            scratch.open.push(start);

            constexpr int kRoadSearchSmallMapMaxCells = 65536;
            // Match the last known-good native road baseline. The 4096-step
            // large-map cap pruned same-island roads before A* could route
            // around bays and rivers, leaving many cities with no visible road.
            constexpr int kRoadSearchLargeMapMaxSteps = 200000;
            const std::size_t cellCount = std::size_t(W) * std::size_t(H);
            const int maxSteps = cellCount <= std::size_t(kRoadSearchSmallMapMaxCells)
                ? int(cellCount)
                : kRoadSearchLargeMapMaxSteps;
            int steps = 0;
            while (!scratch.open.empty() && steps < maxSteps)
            {
                ++steps;
                RoadNodeRec cur = scratch.open.pop();
                const std::size_t cidx = std::size_t(cur.y) * W + cur.x;
                if (scratch.closed_at(cidx))
                    continue;
                scratch.close(cidx);

                if (cur.x == ex && cur.y == ey)
                {
                    int cx = ex, cy = ey;
                    while (cx != -1 && cy != -1)
                    {
                        out.path.push_back({cx, cy});
                        const std::size_t idx = std::size_t(cy) * W + cx;
                        int px = scratch.parentX[idx];
                        int py = scratch.parentY[idx];
                        cx = px;
                        cy = py;
                    }
                    std::reverse(out.path.begin(), out.path.end());
                    out.found = true;
                    if (stepsOut)
                        *stepsOut = steps;
                    return out;
                }

                for (int k = 0; k < 8; ++k)
                {
                    int nx = wrapi(cur.x + kRoadDx[k], W);
                    int ny = wrapi(cur.y + kRoadDy[k], H);
                    const std::size_t nidx = std::size_t(ny) * W + nx;
                    if (scratch.closed_at(nidx))
                        continue;
                    const float ncost = data.costGrid[nidx];
                    if (ncost >= kRoadWaterBlockThreshold)
                        continue;
                    float ng = cur.g + ncost * kRoadStep[k];
                    if (ng < scratch.score(nidx))
                    {
                        scratch.set_score(nidx, ng, cur.x, cur.y);
                        const float f = ng + road_octile_torus(nx, ny, ex, ey, W, H);
                        scratch.open.push({nx, ny, ng, f});
                    }
                }
            }

            if (stepsOut)
                *stepsOut = steps;
            return out;
        }

    } // namespace

    std::vector<TreePoint> spawn_trees(const TerrainData &td, std::uint32_t seed,
                                       float /*densityIgnored*/,
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
                const float warpX = fbm(nx * 8.0f, ny * 8.0f, sd + 100, 3);
                const float warpY = fbm(nx * 8.0f, ny * 8.0f, sd + 200, 3);
                const float wnx = nx + (warpX - 0.5f) * 0.06f;
                const float wny = ny + (warpY - 0.5f) * 0.06f;

                const float large = fbm(wnx * 14.0f, wny * 14.0f, sd + 500, 4);
                const float med = fbm(wnx * 35.0f, wny * 35.0f, sd + 600, 3);
                const float fine = fbm(wnx * 70.0f, wny * 70.0f, sd + 700, 2);
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
    // Cross-island pairs are component-pruned. Same-island pairs use
    // generation-tagged whole-map A* and block rejected water cells.
    std::vector<std::uint8_t> trace_roads(const TerrainData &td, Politik &P,
                                          RoadTraceStats *stats,
                                          float seaLevel)
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
        const float kRoadShare = 0.30f; // existing-road cell cost (cheap → reuse)
        const float kLand = 1.00f;
        const float kMountain = 5.00f;     // h > 0.78 → mountain peak
        const float kWaterReject = 50.00f; // water cell: blocked by road A*
        PathCostData cg;
        cg.width = W;
        cg.height = H;
        cg.costGrid.assign(totalCells, kLand);
        for (std::size_t i = 0; i < totalCells; ++i)
        {
            const float h = float(td.rgba[i * 4u + 0]) / 255.0f;
            if (h < seaLevel)
                cg.costGrid[i] = kWaterReject;
            else if (td.rgba[i * 4u + 0] > 200)
                cg.costGrid[i] = kMountain;
        }
        const std::vector<int> landComponent = build_land_components(td, seaLevel);

        for (const City &c : P.cities)
            cg.costGrid[std::size_t(wrapi(c.y, H)) * W + wrapi(c.x, W)] = kRoadShare;

        std::vector<std::pair<int, int>> dropPairs;
        RoadPathScratch pathScratch;
        auto path_crosses_rejected_water = [&](const PathResult &path)
        {
            if (!path.found)
                return false;
            for (const PathPoint &p : path.path)
            {
                if (cg.costGrid[std::size_t(p.y) * W + p.x] >= kWaterReject - 0.01f)
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
                PathResult pr = find_road_path(cg, ax, ay, bx, by,
                                               pathScratch,
                                               &edgeSteps);
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

    // TS-faithful port of generateDirtRoads (game/dirt-road-spawner.ts).
    // Spiral search up to 60 tiles → torus-aware lerp trace; skip cells already
    // on a road; skip water cells (isLand); stamp 255 elsewhere.
    std::vector<std::uint8_t> trace_dirt_roads(int mapW, int mapH,
                                               const std::vector<std::uint8_t> &roadMask,
                                               const std::vector<int> &vx, const std::vector<int> &vy,
                                               const std::uint8_t *landMaskA /* may be nullptr */,
                                               std::size_t landMaskByteCount)
    {
        std::vector<std::uint8_t> dirt;
        std::size_t totalCells = 0;
        if (!FeatureLayer::cell_count_for(mapW, mapH, totalCells)
            || totalCells > std::numeric_limits<std::size_t>::max() / 4u
            || roadMask.size() < totalCells
            || vx.size() != vy.size())
        {
            return dirt;
        }

        const std::size_t requiredLandMaskBytes = totalCells * 4u;
        if (landMaskA && landMaskByteCount > 0u
            && landMaskByteCount < requiredLandMaskBytes)
        {
            return dirt;
        }

        dirt.assign(totalCells, 0);
        auto isLand = [&](int x, int y)
        {
            if (!landMaskA)
                return true;
            return landMaskA[(std::size_t(y) * mapW + x) * 4 + 3] != 0;
        };
        for (std::size_t i = 0; i < vx.size(); ++i)
        {
            const int cx = wrapi(vx[i], mapW);
            const int cy = wrapi(vy[i], mapH);
            // Skip if village is already on a road.
            if (roadMask[std::size_t(cy) * mapW + cx])
                continue;

            // Spiral scan to nearest road cell (radius up to 60, TS parity).
            int fx = -1, fy = -1;
            bool found = false;
            for (int r = 1; r <= 60 && !found; ++r)
            {
                for (int dy = -r; dy <= r && !found; ++dy)
                {
                    for (int dx = -r; dx <= r && !found; ++dx)
                    {
                        if (std::abs(dx) != r && std::abs(dy) != r)
                            continue;
                        int nx = wrapi(cx + dx, mapW);
                        int ny = wrapi(cy + dy, mapH);
                        if (roadMask[std::size_t(ny) * mapW + nx])
                        {
                            fx = nx;
                            fy = ny;
                            found = true;
                        }
                    }
                }
            }
            if (!found)
                continue;

            // Torus shortest-path delta.
            int dx = fx - cx;
            int dy = fy - cy;
            if (std::abs(dx) > mapW / 2)
                dx += dx > 0 ? -mapW : mapW;
            if (std::abs(dy) > mapH / 2)
                dy += dy > 0 ? -mapH : mapH;
            int steps = std::max(std::abs(dx), std::abs(dy));
            if (steps == 0)
                continue;

            for (int s = 0; s <= steps; ++s)
            {
                float t = float(s) / float(steps);
                int x = wrapi(cx + int(std::lround(dx * t)), mapW);
                int y = wrapi(cy + int(std::lround(dy * t)), mapH);
                std::size_t idx = std::size_t(y) * mapW + x;
                if (roadMask[idx])
                    continue; // don't overwrite roads
                if (!isLand(x, y))
                    continue; // skip water/ice
                dirt[idx] = 255;
            }
        }
        return dirt;
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
        // The feature layer carries only MAN-MADE structures: dirt roads,
        // then roads (last-writer-wins). Mountains are the Mountain biome
        // (elevation-classified) and forests are the tree-count field
        // (macro/tree_layer.h, seeded by the spawn_trees massif mask).
        if (dirtMask)
        {
            for (std::size_t i = 0; i < dirtMaskLimit; ++i)
                if ((*dirtMask)[i] && !is_water(i))
                    fl.data[i] = FT_DirtRoad;
        }
        for (std::size_t i = 0; i < roadMaskLimit; ++i)
        {
            if (roadMask[i] && !is_water(i))
                fl.data[i] = FT_Road;
        }
        return fl;
    }

    int field_wheat_min() {
        // The ploughable bar, carried into the registry's units by the
        // wheat baseline's own scale — ONE fertility door, so raising the
        // bar or rescaling the field can never leave the two disagreeing.
        return int(kFieldMoistureMin) * kMaxWheatStandsPerCell / 255;
    }

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
        const int wheatMin = field_wheat_min();
        auto cell_ok = [&](int x, int y, int& wheatOut) {
            const std::size_t idx =
                std::size_t(y) * std::size_t(w) + std::size_t(x);
            if (fl.data[idx] != FT_None) return false;  // roads win
            const std::uint8_t alpha = td.rgba[idx * 4u + 3];
            const float height01 = float(td.rgba[idx * 4u + 0]) / 255.0f;
            if (alpha == 0 || height01 < seaLevel) return false;   // water
            if (height01 >= kMountainBiomeLevel) return false;     // no rock terraces
            // The ONE fertility door: potential minus what play has taken.
            wheatOut = resource_field_read(world, ResourceFieldId::Wheat, x, y);
            return wheatOut >= wheatMin;
        };

        for (const FieldSite& v : villages) {
            // Candidates: the Chebyshev radius 1..2 ring around the village
            // cell, in fixed scan order — the pick is deterministic from the
            // world data alone (context, not dice).
            struct Candidate { int x, y; int wheat; };
            Candidate best[kFieldsPerVillage];
            int found = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
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
