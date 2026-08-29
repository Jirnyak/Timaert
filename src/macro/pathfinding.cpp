// A* pathfinding over a torus cost grid. This file is the source of truth
// (the TS original is dead — the migration is closed).
//
// Edge cost = costGrid[dest] * stepLen (1.0 cardinal, sqrt(2) diagonal).
// Octile heuristic with torus wrap (consistent for 8-directional A*).
// Indexed binary min-heap collapses duplicates.

#include "core/torus.h"
#include "macro/pathfinding.h"
#include "macro/movement_cost.h"
#include "macro/biomes.h"
#include "macro/tree_layer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sm
{

    namespace
    {

        constexpr int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        constexpr int DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        constexpr float STEP_COST[8] = {1.0f, 1.4142136f, 1.0f, 1.4142136f,
                                        1.0f, 1.4142136f, 1.0f, 1.4142136f};

        // (was a private copy of the torus wrap — core/torus.h owns it)
        inline int pf_wrap(int v, int m) { return wrapi(v, m); }

        inline float octile_torus(int x1, int y1, int x2, int y2, int w, int h)
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

    } // namespace

    PathCostData build_cost_grid(const TerrainData &td,
                                 const FeatureLayer *features,
                                 const TreeLayer *treeLayer)
    {
        PathCostData out;
        const std::size_t total = td.cell_count();
        if (total == 0u || !td.has_rgba_storage())
            return out;

        out.width = td.width;
        out.height = td.height;
        out.costGrid.resize(total);
        out.water.assign(total, 0u);
        out.height8.assign(total, 0u);
        const std::uint8_t *featureData =
            features && features->covers(td.width, td.height) ? features->data.data() : nullptr;

        const bool haveTrees = treeLayer && treeLayer->has_complete_storage()
            && treeLayer->width == td.width && treeLayer->height == td.height;
        for (std::size_t i = 0; i < total; ++i)
        {
            // THE cell cascade (map_generator.h biome_at_cell): water rides
            // the baked mask, mountains their elevation class — the decode-
            // and-classify copy that lived here was canon-audit C5.
            const int cx = int(i % std::size_t(td.width));
            const int cy = int(i / std::size_t(td.width));
            const Biome b = biome_at_cell(td, cx, cy);
            const FeatureType f = featureData ? FeatureLayer::decode(featureData[i]) : FT_None;
            // The continuous canopy (the boolean forest-class cliff is gone):
            // density feeds the sum law exactly as it feeds the optics.
            const float density = haveTrees
                ? float(treeLayer->data[i]) / float(kMaxTreesPerCell)
                : 0.0f;
            out.costGrid[i] = cell_sp_weight(b, f, density);
            out.water[i] = (b == Water) ? 1u : 0u;
            out.height8[i] = td.rgba[i * 4u + 0u];
        }
        return out;
    }

    PathResult find_path(const PathCostData &data,
                         int startX, int startY,
                         int endX, int endY,
                         PathScratch &scratch,
                         int maxSteps,
                         float blockAtOrAbove,
                         int *stepsOut)
    {
        if (stepsOut)
            *stepsOut = 0;
        PathResult out;
        const int W = data.width, H = data.height;
        if (W <= 0 || H <= 0)
            return out;

        const int sx = pf_wrap(startX, W), sy = pf_wrap(startY, H);
        const int ex = pf_wrap(endX, W), ey = pf_wrap(endY, H);

        if (sx == ex && sy == ey)
        {
            out.path.push_back({sx, sy});
            out.found = true;
            return out;
        }

        const std::size_t cells = std::size_t(W) * H;
        // `maxSteps<=0` (default) → give A* enough budget to visit every cell
        // once. With the closed-set check this guarantees termination on any
        // grid size while letting paths be found anywhere on the torus.
        if (maxSteps <= 0)
            maxSteps = int(cells);
        scratch.init(W, H);

        PathScratch::Node start{sx, sy, 0.0f, octile_torus(sx, sy, ex, ey, W, H)};
        scratch.set_score(std::size_t(sy) * W + sx, 0.0f, -1, -1);
        scratch.open.push(start);

        int steps = 0;
        while (!scratch.open.empty() && steps < maxSteps)
        {
            ++steps;
            PathScratch::Node cur = scratch.open.pop();
            const std::size_t cidx = std::size_t(cur.y) * W + cur.x;

            if (cur.x == ex && cur.y == ey)
            {
                int cx = cur.x, cy = cur.y;
                for (;;)
                {
                    out.path.push_back({cx, cy});
                    std::size_t k = std::size_t(cy) * W + cx;
                    std::int32_t px = scratch.parentX[k], py = scratch.parentY[k];
                    if (px < 0)
                        break;
                    cx = px;
                    cy = py;
                }
                std::reverse(out.path.begin(), out.path.end());
                out.found = true;
                if (stepsOut)
                    *stepsOut = steps;
                return out;
            }

            if (scratch.closed_at(cidx))
                continue;
            scratch.close(cidx);

            for (int d = 0; d < 8; ++d)
            {
                int nx = pf_wrap(cur.x + DX[d], W);
                int ny = pf_wrap(cur.y + DY[d], H);
                std::size_t nidx = std::size_t(ny) * W + nx;
                if (scratch.closed_at(nidx))
                    continue;
                const float ncost = data.costGrid[nidx];
                if (ncost >= blockAtOrAbove)
                    continue; // gated ground is refused, never paid

                float cost = ncost * STEP_COST[d]
                           + data.climb(cidx, nidx);
                float tentG = cur.g + cost;
                if (tentG >= scratch.score(nidx))
                    continue;

                scratch.set_score(nidx, tentG, cur.x, cur.y);

                float hh = octile_torus(nx, ny, ex, ey, W, H);
                scratch.open.push({nx, ny, tentG, tentG + hh});
            }
        }
        if (stepsOut)
            *stepsOut = steps;
        return out;
    }

    PathResult find_path(const PathCostData &data,
                         int startX, int startY,
                         int endX, int endY,
                         int maxSteps)
    {
        PathScratch scratch;
        return find_path(data, startX, startY, endX, endY, scratch, maxSteps);
    }

} // namespace sm
