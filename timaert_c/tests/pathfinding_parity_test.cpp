#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/movement_cost.h"
#include "macro/pathfinding.h"

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace {

bool nearly(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

bool expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        return false;
    }
    return true;
}

sm::PathCostData flat_grid(int w, int h, float cost) {
    sm::PathCostData data;
    data.width = w;
    data.height = h;
    data.costGrid.assign(std::size_t(w) * std::size_t(h), cost);
    return data;
}

} // namespace

int main() {
    bool ok = true;

    ok &= expect(sm::kPathfindDefaultMaxSteps == 50000,
                 "default maxSteps must mirror TS pathfinding.ts");

    ok &= expect(nearly(sm::biome_sp_weight(sm::Water), 10.0f),
                 "water biome weight must be 10");
    ok &= expect(nearly(sm::cell_sp_weight(sm::Water, sm::FT_Road), 1.0f),
                 "road feature must override water biome cost");
    ok &= expect(sm::cell_sp_cost(sm::Meadow, sm::FT_DirtRoad) == 15,
                 "dirt road SP cost must be 15");

    sm::PathCostData grid = flat_grid(5, 5, 1.0f);
    sm::PathResult wrapped = sm::find_path(grid, 0, 0, 4, 0, 8);
    ok &= expect(wrapped.found, "torus neighbor path should be found");
    ok &= expect(wrapped.path.size() == 2, "torus neighbor path should be one edge");
    if (wrapped.path.size() == 2) {
        ok &= expect(wrapped.path[0].x == 0 && wrapped.path[0].y == 0,
                     "path should start at wrapped start");
        ok &= expect(wrapped.path[1].x == 4 && wrapped.path[1].y == 0,
                     "path should end at wrapped goal");
    }

    sm::PathResult capped = sm::find_path(grid, 0, 0, 4, 0, 1);
    ok &= expect(!capped.found && capped.path.empty(),
                 "maxSteps cap should stop search before second pop");

    sm::PathResult enoughBudget = sm::find_path(grid, 0, 0, 4, 0, 2);
    ok &= expect(enoughBudget.found,
                 "same path should succeed when cap allows target pop");

    if (!ok) return 1;
    std::printf("pathfinding_parity_test: ok\n");
    return 0;
}
