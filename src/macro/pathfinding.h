// A* pathfinding over a torus cost grid. This file is the source of truth
// (the TS original is dead — the migration is closed).
#pragma once

#include "macro/features.h"
#include "macro/movement_cost.h" // kClimbSpWeight — the edge half of the law
#include "macro/map_generator.h" // TerrainData

#include <algorithm> // std::fill (scratch generation wrap)
#include <cstdint>
#include <limits>
#include <utility>   // std::swap (heap)
#include <vector>

namespace sm
{

    struct TreeLayer;

    struct PathPoint
    {
        int x, y;
    };

    // Pre-computed SP-weight grid (1.0 .. 10.0 per cell). Mirrors PathCostData.
    // `water` marks the cells whose weight IS the water weight, as data rather
    // than a magic-number comparison: the macro march needs to know where a
    // body cannot make camp (Session 21 — no Resting at sea), and reading the
    // biome back out of a float would be the two-tables drift in miniature.
    struct PathCostData
    {
        int width = 0;
        int height = 0;
        std::vector<float> costGrid;       // the CELL half: bed + canopy
        std::vector<std::uint8_t> water;   // 1 = open water cell
        // The height byte per cell (terrain R), baked beside the weights so
        // every walker prices the EDGE half of the law — the uphill climb —
        // without reaching back into the terrain (movement_cost.h).
        std::vector<std::uint8_t> height8;

        // The edge-climb term between two cells of this grid, by index:
        // kClimbSpWeight × max(0, Δh01), downhill free. Zero when heights
        // are not baked (the silent legal contribution).
        float climb(std::size_t fromIdx, std::size_t toIdx) const {
            if (height8.size() != costGrid.size()) return 0.0f;
            const int dh = int(height8[toIdx]) - int(height8[fromIdx]);
            return dh > 0 ? kClimbSpWeight * (float(dh) / 255.0f) : 0.0f;
        }
    };

    struct PathResult
    {
        std::vector<PathPoint> path;
        bool found = false;
    };

    // ── THE reusable A* scratch (CANON S7 — one search, one scratch) ─────
    // Grew up in the road tracer (spawners.cpp, where it saved a fresh ~13 MB
    // of allocations per city pair on a 1024² map) and is now the working
    // memory of EVERY find_path: allocated once for a map size, reset between
    // searches by bumping a generation tag instead of refilling the arrays.
    struct PathScratch
    {
        struct Node
        {
            int x, y;
            float g, f;
        };

        // Indexed binary min-heap with by-key dedup (matches the TS MinHeap
        // contract the combat A* always had), generation-tagged so `contains`
        // never needs a clearing pass.
        struct Heap
        {
            std::vector<Node> items;
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

            void push(const Node &n)
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

            Node pop()
            {
                Node top = items.front();
                indexOf[key(top.x, top.y)] = -1;
                Node last = items.back();
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

        std::vector<float> gScores;
        std::vector<std::int32_t> parentX;
        std::vector<std::int32_t> parentY;
        Heap open;
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
            if (generation == 0u) // wrapped: honest full clear, then restart
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

        inline void close(std::size_t idx) { closedTag[idx] = generation; }

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

    // "No gate": no real cell weight reaches infinity, so the default blocks
    // nothing — the combat search prices water (10.0) instead of refusing it,
    // while the road tracer passes its water-reject sentinel here.
    inline constexpr float kPathNoBlock = std::numeric_limits<float>::infinity();

    // Build cost grid from terrain master texture + feature layer + the
    // tree-count layer (forest-class cells drag like the old FT_Tree).
    // Mirrors movement-cost.ts buildCostGrid.
    // Water rides the terrain's baked land mask (map_generator.h
    // biome_at_cell) — the old float seaLevel parameter re-derived it by
    // threshold and could disagree with the mask at the coast.
    PathCostData build_cost_grid(const TerrainData &td,
                                 const FeatureLayer *features = nullptr,
                                 const TreeLayer *treeLayer = nullptr);

    // 8-direction A* with octile heuristic + edge cost = costGrid[dest] * stepLen
    // + climb. THE search (CANON S7): the combat march, the stone roads and the
    // dirt lanes all walk this one function — the road tracer's byte-for-byte
    // private twin in spawners.cpp is dead. What made the twin different is now
    // parameters:
    //   `scratch`        — reusable working memory (see PathScratch above);
    //   `maxSteps`       — <= 0 (default) means "unlimited", capped internally
    //                      at the cell count (every cell visited at most once,
    //                      so termination is guaranteed; the TS-mirrored 50_000
    //                      default silently failed on large maps);
    //   `blockAtOrAbove` — cells priced at or above this are REFUSED, not paid
    //                      (the road water gate); default gates nothing;
    //   `stepsOut`       — heap pops spent, for the tracer's stats.
    PathResult find_path(const PathCostData &data,
                         int sx, int sy, int gx, int gy,
                         PathScratch &scratch,
                         int maxSteps = 0,
                         float blockAtOrAbove = kPathNoBlock,
                         int *stepsOut = nullptr);

    // One-shot convenience: a private scratch per call (tests, tools). Hot
    // callers hold a PathScratch and use the overload above.
    PathResult find_path(const PathCostData &data,
                         int sx, int sy, int gx, int gy,
                         int maxSteps = 0);

} // namespace sm
