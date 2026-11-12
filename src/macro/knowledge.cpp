#include "macro/knowledge.h"

namespace sm {

bool update_player_sight(KnowledgeLayer& k, SightRuntime& rt,
                         const OpticalWorld& world,
                         std::uint32_t playerCell, float budgetCells) {
    if (!k.has_complete_storage()) return false;
    if (playerCell == rt.lastCell) return false;
    rt.lastCell = playerCell;

    // Yesterday's sight becomes memory. Guarded per cell so a stale list from
    // another world (or an already-decayed cell) can never invent knowledge.
    for (const std::uint32_t i : rt.visibleCells) {
        if (i < k.cell_count() && k.data[i] == kKnowledgeVisible)
            k.data[i] = kKnowledgeExplored;
    }
    rt.visibleCells.clear();

    // Оптика берёт клетку парой осей — её дверь переедет на индекс вместе с
    // остальными дверями мира (ломоть 4 M-462); пара здесь не хранится и не
    // считается, она только называет ту же клетку чужой подписи.
    optical_sweep(k.width, k.height, cell_x(playerCell, k.width),
                  cell_y(playerCell, k.width), budgetCells, world, rt.scratch,
                  [&](std::size_t ci, float) {
        k.data[ci] = kKnowledgeVisible;
        rt.visibleCells.push_back(std::uint32_t(ci));
    });

    // The visible set moved with the player by definition — one bump per
    // crossing, never per frame.
    ++k.revision;
    return true;
}

} // namespace sm
