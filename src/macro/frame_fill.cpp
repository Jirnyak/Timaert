#include "macro/frame_fill.h"

#include "core/torus.h"
#include "macro/cell_facts.h"
#include "macro/chronicle.h"
#include "macro/macro_world.h"
#include "macro/resource_field.h"
#include "macro/player_entity.h"
#include "macro/squad_index.h"
#include "macro/state.h"

namespace sm {

// Знаковая кратчайшая разница клеточных КООРДИНАТ на торе: заворот — маска
// (ЗАКОН АДРЕСА: сторона — степень двойки), полмира и дальше — короткая
// дорога с другой стороны.
namespace {
inline int wrap_dc(int d, int side) {
    d &= side - 1;
    return d > side / 2 ? d - side : d;
}
} // namespace

// Пины словарей фрейма к их носителям — ЕДИНСТВЕННОЕ место, где оба числа
// видны (ЗАКОН СЛОВАРЯ п.3: пин законен только под static_assert).
static_assert(std::size_t(kFrameFieldRows)
                  == std::size_t(ResourceFieldId::Count),
              "рядов полей во фрейме обязано быть ровно столько, сколько в "
              "реестре kResourceFields — новая строка реестра правит и кап");
static_assert(kFrameNoLease == kNoWindowLease,
              "сентинел «аренды нет» у фрейма и у мира — одно число");

void fill_local_frame(const MacroWorld& w, std::uint32_t centerCell,
                      LocalContext& out) {
    out.squadCount = 0;
    out.factCount = 0;
    if (!w.gs) return;
    const GameState& gs = *w.gs;
    const int side = gs.mapW;

    // ── РОД 4 — мировые переменные ─────────────────────────────────────────
    out.worldTick = gs.worldTime.tick;
    out.worldDay = gs.worldTime.day();
    out.worldSeed = gs.worldSeed;
    out.side = side;
    out.seaLevel16 = w.terrain ? int(w.terrain->seaLevel16) : 0;
    out.leasedWindow = gs.leasedWindow;
    out.centerCell = centerCell;
    out.playerFlag = player_flag_handle(gs);
    out.playerSquad = player_squad_handle(gs);

    // ── РОДА 1 и 3 — клетки 5×5 и запас рядов полей на них ────────────────
    // Соседство — арифметика над индексом (cell_step, ЗАКОН АДРЕСА).
    const int half = kFrameSpan / 2;
    int i = 0;
    for (int dy = -half; dy <= half; ++dy) {
        for (int dx = -half; dx <= half; ++dx, ++i) {
            const std::uint32_t cell = cell_step(centerCell, dx, dy, side);
            const int x = cell_x(cell, side);
            const int y = cell_y(cell, side);
            out.cells[std::size_t(i)] = cell_facts(w, x, y);
            for (int f = 0; f < kFrameFieldRows; ++f) {
                out.stock[std::size_t(f) * kFrameCells + std::size_t(i)] =
                    resource_field_read(w, ResourceFieldId(f), x, y);
            }
        }
    }

    // ── РОД 2 — сквады клеток фрейма, АДРЕСОМ ──────────────────────────────
    // Каркас клеток отвечает «кто здесь» (ЗАКОН КЛЕТОЧНОГО КАРКАСА); без
    // собранного каркаса (голая фикстура) — честный проход живых слотов:
    // сборщик зовётся раз в мировой тик, это периодический свип, не кадр.
    if (w.store) {
        const MacroStore& st = *w.store;
        auto on_frame_cell = [&](std::uint32_t cell) -> int {
            const int cx = cell_x(cell, side);
            const int cy = cell_y(cell, side);
            const int ccx = cell_x(centerCell, side);
            const int ccy = cell_y(centerCell, side);
            const int ddx = wrap_dc(cx - ccx, side);
            const int ddy = wrap_dc(cy - ccy, side);
            if (ddx < -half || ddx > half || ddy < -half || ddy > half)
                return -1;
            return (ddy + half) * kFrameSpan + (ddx + half);
        };
        if (w.squads && !w.squads->grid.begin.empty()) {
            const CellBuckets& b = w.squads->grid;
            const int ccx = cell_x(centerCell, side);
            const int ccy = cell_y(centerCell, side);
            // Бакеты крупные (kChronicle-подобный шаг 8), поэтому берём
            // кольцо бакетов, покрывающее 5×5 клеток, и фильтруем по клетке.
            const int g0x = (ccx - half) / b.cellSize;
            const int g1x = (ccx + half) / b.cellSize;
            const int g0y = (ccy - half) / b.cellSize;
            const int g1y = (ccy + half) / b.cellSize;
            for (int gy = g0y; gy <= g1y; ++gy) {
                for (int gx = g0x; gx <= g1x; ++gx) {
                    const int wx = ((gx % b.cols) + b.cols) % b.cols;
                    const int wy = ((gy % b.rows) + b.rows) % b.rows;
                    for (const std::uint32_t* it = b.cell_begin(wx, wy),
                                            * end = b.cell_end(wx, wy);
                         it != end; ++it) {
                        const std::uint16_t slot = w.squads->order[*it].slot;
                        if (st.alive[slot] == 0 || st.dead[slot] != 0)
                            continue;
                        if (on_frame_cell(st.cell[slot].idx) < 0) continue;
                        if (out.squadCount < out.squads.size())
                            out.squads[out.squadCount++] = handle_at(st, slot);
                    }
                }
            }
        } else {
            for (std::uint32_t s32 = 0; s32 < kUnifiedCap; ++s32) {
                const std::uint16_t slot = std::uint16_t(s32);
                if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
                if (on_frame_cell(st.cell[slot].idx) < 0) continue;
                if (out.squadCount < out.squads.size())
                    out.squads[out.squadCount++] = handle_at(st, slot);
            }
        }
    }

    // ── РОД 5 — окно фактов клеток фрейма ──────────────────────────────────
    // Кольцо индексировано крупными клетками (kChronicleCellSize = 8), так
    // что радиус 1 индексной клетки покрывает 5×5 карты с запасом; точный
    // отбор — по клетке факта. Горизонт — самый длинный интерес таблицы.
    struct Sink {
        LocalContext* out;
        int ccx, ccy, side;
    } sink{&out, cell_x(centerCell, side), cell_y(centerCell, side), side};
    chronicle_near(
        gs.chronicle, sink.ccx, sink.ccy, /*radiusCells=*/1,
        out.worldDay - int(kChronicleLongestInterestDays) + 1,
        [](void* u, const WorldFact& f) {
            Sink& s = *static_cast<Sink*>(u);
            const int ddx = wrap_dc(int(f.x) - s.ccx, s.side);
            const int ddy = wrap_dc(int(f.y) - s.ccy, s.side);
            if (ddx < -(kFrameSpan / 2) || ddx > kFrameSpan / 2
                || ddy < -(kFrameSpan / 2) || ddy > kFrameSpan / 2)
                return;
            if (s.out->factCount < kFrameFactsCap)
                s.out->facts[s.out->factCount++] = f;
        },
        &sink);
}

} // namespace sm
