#include "macro/knowledge.h"

#include <cmath>

namespace sm {

void knowledge_reset(KnowledgeLayer& k, int width, int height) {
    std::size_t n = 0;
    if (!FeatureLayer::cell_count_for(width, height, n)) {
        k.width = 0;
        k.height = 0;
        k.data.clear();
        ++k.revision;
        return;
    }
    k.width = width;
    k.height = height;
    k.data.assign(n, kKnowledgeUnknown);
    ++k.revision;
}

bool update_player_sight(KnowledgeLayer& k, SightRuntime& rt,
                         const OpticalWorld& world,
                         float playerX, float playerY, float budgetCells) {
    if (!k.has_complete_storage()) return false;
    const int cx = FeatureLayer::wrap_coord(int(std::floor(playerX)), k.width);
    const int cy = FeatureLayer::wrap_coord(int(std::floor(playerY)), k.height);
    if (cx == rt.lastCellX && cy == rt.lastCellY) return false;
    rt.lastCellX = cx;
    rt.lastCellY = cy;

    // Yesterday's sight becomes memory. Guarded per cell so a stale list from
    // another world (or an already-decayed cell) can never invent knowledge.
    for (const std::uint32_t i : rt.visibleCells) {
        if (i < k.data.size() && k.data[i] == kKnowledgeVisible)
            k.data[i] = kKnowledgeExplored;
    }
    rt.visibleCells.clear();

    optical_sweep(k.width, k.height, cx, cy, budgetCells, world, rt.scratch,
                  [&](std::size_t ci, float) {
        k.data[ci] = kKnowledgeVisible;
        rt.visibleCells.push_back(std::uint32_t(ci));
    });

    // The visible set moved with the player by definition — one bump per
    // crossing, never per frame.
    ++k.revision;
    return true;
}

void reveal_area(KnowledgeLayer& k, const OpticalWorld& world,
                 OpticalScratch& scratch, float cx, float cy,
                 float budgetCells) {
    if (!k.has_complete_storage()) return;
    const int sx = int(std::floor(cx));
    const int sy = int(std::floor(cy));
    bool changed = false;
    optical_sweep(k.width, k.height, sx, sy, budgetCells, world, scratch,
                  [&](std::size_t ci, float) {
        if (k.data[ci] == kKnowledgeUnknown) {
            k.data[ci] = kKnowledgeExplored;
            changed = true;
        }
    });
    if (changed) ++k.revision;
}

} // namespace sm
