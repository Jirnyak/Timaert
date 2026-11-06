#include "macro/frame_fill.h"

#include <cmath>   // lround — кванты вектора марша (M-317)

#include "core/torus.h"
#include "macro/cell_facts.h"
#include "macro/chronicle.h"
#include "macro/macro_world.h"
#include "macro/resource_field.h"
#include "macro/player_entity.h"
#include "macro/squad_index.h"
#include "macro/npc_ai.h"     // march_cells_per_think — закон темпа (M-317)
#include "macro/nav_field.h"  // nav_step — запечённая походка (M-317)
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

// ── ВЕКТОР МАРША ДЛЯ ПАКЕТА (M-317, ЗАКОН ШВА п.3) ───────────────────────
// Вторая колонка рода 2, СЧИТАЕТСЯ при наполнении (вердикт: «вектор есть
// колонка пакета, а не новая колонка мира»). НАПРАВЛЕНИЕ — следующий шаг
// ТОЙ ЖЕ походки, что ведёт марш (запечённый `nav_step@src/macro/
// nav_field.cpp`; фолбэк — прямой торовый шаг, он же тай-брейкер жадного
// закона try_move), ТЕМП — тот же `march_cells_per_think@src/macro/
// npc_ai.h`, которым платит try_move, в ТАЙЛАХ СЦЕНЫ ЗА МИРОВОЙ ТИК
// (единица канала — разбор у колонки squadVel пакета). Третьего закона
// движения не рождается: обе половины — чтения существующих. {0,0} —
// честное «стоит»: нет цели, цель — текущая клетка (доезд в центр маршем
// не считается), заминка боя (fighting гасит ноги и тут, как в try_move).
SquadVel squad_march_vector(const MacroWorld& mw, const MacroStore& st,
                            MacroHandle h) {
    if (!mw.gs || !st.valid(h)) return SquadVel{0, 0};
    const std::uint16_t slot = h.slot;
    const auto& rt = st.runtime[slot];
    if (rt.fighting != 0) return SquadVel{0, 0};
    const ecs::SquadGoal& goal = st.goal[slot];
    if (goal.cell == ecs::kNoCell) return SquadVel{0, 0};
    const int side = mw.gs->mapW;
    const std::uint32_t here = st.cell[slot].idx;
    if (goal.cell == here) return SquadVel{0, 0};
    const int ix = cell_x(here, side), iy = cell_y(here, side);
    const int itx = cell_x(goal.cell, side), ity = cell_y(goal.cell, side);
    int dx = 0, dy = 0;
    if (rt.flying == 0 && mw.nav && mw.nav->baked()) {
        if (!nav_step(*mw.nav, ix, iy, itx, ity, dx, dy)) { dx = 0; dy = 0; }
    }
    if (dx == 0 && dy == 0) {
        // Прямой торовый шаг — фолбэк, байтово тай-брейкер жадного закона.
        const Step s = torus_step_toward(ix, iy, itx, ity, side, side);
        dx = wrap_dc(s.nx - ix, side);
        dy = wrap_dc(s.ny - iy, side);
    }
    // Вес текущей клетки — та же одна проба на думку, что в try_move; вне
    // собранного грида (голая фикстура) — честная единица (cell_weight).
    float cellW = 1.0f;
    if (const PathCostData* pcw = mw.pathCost;
        pcw && pcw->width > 0 && pcw->height > 0
        && pcw->costGrid.size()
               == std::size_t(pcw->width) * std::size_t(pcw->height)) {
        cellW = pcw->cost_at(ix, iy);
    }
    const int q = int(std::lround(
        march_cells_per_think(rt, rt.flying != 0 ? 1.0f : cellW)
        * float(kSubworldTilesPerMacroCell) / float(kAiTicks)));
    return SquadVel{std::int16_t(dx * q), std::int16_t(dy * q)};
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
            // Кольцо бакетов — ОДНОЙ дверью каркаса (squads_in_frame, ломоть
            // 6): рукописный обход здесь делил отрицательный край трункацией
            // ((−2)/8 == 0) и у начала мира терял завёрнутое кольцо — слот у
            // западного/северного шва не попадал в род 2 фрейма.
            const int ccx = cell_x(centerCell, side);
            const int ccy = cell_y(centerCell, side);
            squads_in_frame(*w.squads, st, ccx, ccy, half,
                            [&](std::uint16_t slot, int, int) {
                if (st.alive[slot] == 0 || st.dead[slot] != 0) return;
                if (out.squadCount < out.squads.size()) {
                    const MacroHandle sh = handle_at(st, slot);
                    // Вторая колонка рода 2 — вектор марша, СЧИТАЕТСЯ при
                    // наполнении (колонка пакета, не мира — M-317).
                    out.squadVel[out.squadCount] =
                        squad_march_vector(w, st, sh);
                    out.squads[out.squadCount++] = sh;
                }
            });
        } else {
            for (std::uint32_t s32 = 0; s32 < kUnifiedCap; ++s32) {
                const std::uint16_t slot = std::uint16_t(s32);
                if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
                if (on_frame_cell(st.cell[slot].idx) < 0) continue;
                if (out.squadCount < out.squads.size()) {
                    const MacroHandle sh = handle_at(st, slot);
                    out.squadVel[out.squadCount] =
                        squad_march_vector(w, st, sh);
                    out.squads[out.squadCount++] = sh;
                }
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
