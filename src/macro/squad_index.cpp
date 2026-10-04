// Определения каркаса клеток по сквадам (контракт — macro/squad_index.h).
// Свой TU, а не npc_ai.cpp: носитель мира линкуют и лёгкие свидетели
// (settlement_placement_test), которым цепь поведения ни к чему.
#include "macro/squad_index.h"

#include <algorithm>

#include "core/torus.h"   // wrapi — период сетки бакетов (не свёртка мира)

namespace sm {

void bucket_reset(CellBuckets& g, int mapW, int mapH, int cellSize) {
    g.cellSize = std::max(1, cellSize);
    g.cols = std::max(1, (mapW + g.cellSize - 1) / g.cellSize);
    g.rows = std::max(1, (mapH + g.cellSize - 1) / g.cellSize);
    const std::size_t n = std::size_t(g.cols) * std::size_t(g.rows);
    // assign() over the SAME size keeps the capacity, so a grid that is not
    // resized never allocates again after its first build.
    g.begin.assign(n + 1, 0u);
    g.cursor.assign(n, 0u);
}

void bucket_count(CellBuckets& g, int gx, int gy) {
    // Counts land at begin[cell + 1] so the prefix pass can sum in place.
    ++g.begin[g.cell_of(gx, gy) + 1];
}

void bucket_prefix(CellBuckets& g, std::size_t itemCount) {
    for (std::size_t i = 1; i < g.begin.size(); ++i) g.begin[i] += g.begin[i - 1];
    g.items.resize(itemCount);
    for (std::size_t i = 0; i < g.cursor.size(); ++i) g.cursor[i] = g.begin[i];
}

void bucket_scatter(CellBuckets& g, int gx, int gy, std::uint32_t item) {
    const std::size_t c = g.cell_of(gx, gy);
    g.items[g.cursor[c]++] = item;
}

void build_squad_index(SquadIndex& g, const MacroStore& st, int mapW,
                       int mapH, int cellSize) {
    CellBuckets& b = g.grid;
    bucket_reset(b, mapW, mapH, cellSize);

    // Every live macro squad — INCLUDING the player's (owner, 2026-08-29:
    // «игрок ничем не особенен», one law of sight for all). His squad is
    // perceived through this index at the same kSquadSightCells as anyone;
    // what stays special is only the MEETING, which belongs to Inc 6's
    // forced-encounter door (squad_threat_step stops short of auto-battling
    // a player-controlled squad). The Dead are no squads at all.
    // Население — слоты store (1е): порядок закона (squad_walk.h), потом
    // count и scatter идут по собранному — содержимое бакета отсортировано
    // по ординалу, и читатели «первого подходящего» (threat step, охота)
    // не зависят от кишки хранилища. Скрэтч — член, пересборка на свип
    // по-прежнему аллокаций не делает.
    collect_squads_by_ordinal(
        st, g.order,
        [&](std::uint16_t slot) { return st.dead[slot] == 0; });
    for (const SquadWalkEntry& s : g.order) {
        const auto& c = st.cell[s.slot];
        bucket_count(b, wrapi(ecs::cell_x(c, mapW) / b.cellSize, b.cols),
                     wrapi(ecs::cell_y(c, mapW) / b.cellSize, b.rows));
    }
    bucket_prefix(b, g.order.size());
    // Бакет несёт ИНДЕКС В ПОРЯДКЕ, а не биты энтити (шаг 3): читатель по
    // нему получает СРАЗУ и слот (колонки store читаются прямо, без
    // диспетча body_state), и энтити для дверей, которые ещё на мосту.
    // Порядок внутри бакета остаётся ординальным — скаттер идёт по g.order.
    for (std::uint32_t i = 0; i < std::uint32_t(g.order.size()); ++i) {
        const auto& c = st.cell[g.order[i].slot];
        bucket_scatter(b, wrapi(ecs::cell_x(c, mapW) / b.cellSize, b.cols),
                       wrapi(ecs::cell_y(c, mapW) / b.cellSize, b.rows), i);
    }
}

} // namespace sm
