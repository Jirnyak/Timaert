// FOLD ЗАХВАТА — КЛЕТКА+ДОЛЯ ОДНОЙ ДВЕРЬЮ, РУМБ ЧЕСТНЫЙ, ПРЫЖКА НЕТ
// (ломоть 1 эпика M-419; ЗАКОН ШВА, ЗАХВАТ: «позиция КАЖДОГО захваченного
// сворачивается в клетку+долю на границе тика» — дверь игрока стала дверью
// всех, прежняя `squad_set_sub` писала одну ДОЛЮ одному ФЛАЖКУ и умерла).
//
// Предмет — дверь `squad_fold_pos@src/bus/store.h`. Четыре закона:
//  - ТА ЖЕ КЛЕТКА: доля пишется, румб входа и время-в-клетке НЕ трогаются
//    (не-ход никогда не перештамповывает вход);
//  - СОСЕДНЯЯ КЛЕТКА: клетка пишется, румб входа ЧЕСТНЫЙ — тело ПРОШЛО
//    границу ногами, это не прыжок: доля НЕ зануляется
//    (анти-`squad_jump_to_cell`), entryTicks обнуляются, часы думки
//    (tickAccum) не трогаются — каданс сквада фолду не принадлежит;
//  - ТОРОВЫЙ ШОВ: шаг 0 → side−1 есть румб −1, не прыжок через мир
//    (ЗАКОН АДРЕСА: заворот — маска);
//  - ПРОТУХШИЙ ХЭНДЛ: no-op — позиция не ложится поверх нового жильца.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-07, числа фактические;
// знаменатель 9 → 11 ломтём 3: добавлен контраст прыжка в хвосте, мутации
// fold его двери не трогают — числители целы):
//  - доля зануляется вместо записи (fold ведёт себя как прыжок) →
//    КРАСНЫЙ, 2 из 11: «ТА ЖЕ КЛЕТКА: доля свёрнута» и «ДОЛЯ НЕ ЗАНУЛЕНА»;
//  - румб штампуется прыжком (`kEntryDirNone` вместо pack_entry_dir) →
//    КРАСНЫЙ, 2 из 11: «РУМБ ЧЕСТНЫЙ» и «ТОРОВЫЙ ШОВ»;
//  - `entryTicks = 0` снят → КРАСНЫЙ, 1 из 11: «вход свежий»;
//  - гейт смены клетки снят (`if (true)` — перештамповка всегда) →
//    КРАСНЫЙ, 1 из 11: «ТА ЖЕ КЛЕТКА: румб не перештампован»;
//  - `rt.sub = {}` снят из squad_jump_to_cell (прыжок повёз долю) →
//    КРАСНЫЙ, 1 из 11: «ПРЫЖОК: доля — центр»;
//  - ДОЛЖНА МОЛЧАТЬ: порядок `cell.idx = newIdx` и `entryTicks = 0` внутри
//    ветки → ЗЕЛЁНЫЙ, 11/11: закон судит СЕМАНТИКУ, не расстановку строк.
#include "check.h"

#include "bus/entry_context.h"
#include "bus/store.h"

#include <cstdint>
#include <memory>

int main() {
    auto store = sm::make_macro_store();
    sm::MacroStore& st = *store;
    const std::int32_t side = 128;

    const sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = sm::ecs::MacroCell{sm::ecs::cell_index(10, 10, side)};
    sm::ecs::MacroNpcRuntime rt0{};
    rt0.entryDir = sm::pack_entry_dir(0, 1);   // вошёл с юга — румб НЕ восток
    rt0.entryTicks = 7;
    rt0.tickAccum = 55;
    rt0.sub = {5, 5};
    st.runtime[h.slot] = rt0;

    // ── ТА ЖЕ КЛЕТКА: доля свёрнута, вход не перештампован ──────────────
    sm::squad_fold_pos(st, h, 10, 10, side, sm::ecs::CellSub{17, -9});
    CHECK(st.cell[h.slot].idx == sm::ecs::cell_index(10, 10, side)
              && st.runtime[h.slot].sub.x == 17
              && st.runtime[h.slot].sub.y == -9,
          "ТА ЖЕ КЛЕТКА: доля свёрнута, клетка не тронута");
    CHECK(st.runtime[h.slot].entryDir == sm::pack_entry_dir(0, 1)
              && st.runtime[h.slot].entryTicks == 7,
          "ТА ЖЕ КЛЕТКА: румб входа и время-в-клетке не перештампованы");

    // ── СОСЕДНЯЯ КЛЕТКА: честный румб, доля цела, часы думки целы ────────
    sm::squad_fold_pos(st, h, 11, 10, side, sm::ecs::CellSub{-127, 3});
    CHECK(st.cell[h.slot].idx == sm::ecs::cell_index(11, 10, side),
          "FOLD ПИШЕТ КЛЕТКУ: тело пересекло границу — запись не врёт");
    CHECK(st.runtime[h.slot].entryDir == sm::pack_entry_dir(1, 0),
          "РУМБ ЧЕСТНЫЙ: вход с запада, тело прошло границу ногами");
    CHECK(st.runtime[h.slot].entryTicks == 0,
          "вход свежий: время-в-клетке обнулено сменой клетки");
    CHECK(st.runtime[h.slot].sub.x == -127 && st.runtime[h.slot].sub.y == 3,
          "ДОЛЯ НЕ ЗАНУЛЕНА: fold — не прыжок (анти-squad_jump_to_cell)");
    CHECK(st.runtime[h.slot].tickAccum == 55,
          "часы думки не тронуты: каданс сквада фолду не принадлежит");

    // ── ТОРОВЫЙ ШОВ: 0 → side−1 есть шаг −1 ──────────────────────────────
    st.cell[h.slot] = sm::ecs::MacroCell{sm::ecs::cell_index(0, 64, side)};
    sm::squad_fold_pos(st, h, side - 1, 64, side, sm::ecs::CellSub{0, 0});
    CHECK(st.runtime[h.slot].entryDir == sm::pack_entry_dir(-1, 0),
          "ТОРОВЫЙ ШОВ: 0 → side−1 есть шаг на запад, не прыжок через мир");

    // ── ПРОТУХШИЙ ХЭНДЛ: no-op ───────────────────────────────────────────
    const sm::ecs::CellSub subBefore = st.runtime[h.slot].sub;
    const std::uint32_t cellBefore = st.cell[h.slot].idx;
    sm::MacroHandle stale = h;
    stale.id += 999;
    sm::squad_fold_pos(st, stale, 50, 50, side, sm::ecs::CellSub{40, 40});
    CHECK(st.cell[h.slot].idx == cellBefore
              && st.runtime[h.slot].sub.x == subBefore.x
              && st.runtime[h.slot].sub.y == subBefore.y,
          "ПРОТУХШИЙ ХЭНДЛ — no-op: позиция не легла поверх жильца слота");

    // ── ПРЫЖОК — НЕ МАРШ: контраст-свидетель лосси-точки (ломоть 3) ──────
    // `squad_jump_to_cell@src/bus/store.h` ЗАКОННО стирает долю (центр),
    // румб входа (kEntryDirNone) и часы думки: прыжок границу ногами не
    // проходит. Это и есть признанная лосси-цена ре-центра флажка (память
    // ломтя 1 — «зануляет долю, свидетеля нет»); fold выше держит обратную
    // половину — ход ногами НИЧЕГО из этого не стирает.
    st.runtime[h.slot].sub = {33, -44};
    st.runtime[h.slot].tickAccum = 55;
    sm::squad_jump_to_cell(st, h, 20, 20, side);
    CHECK(st.cell[h.slot].idx == sm::ecs::cell_index(20, 20, side)
              && st.runtime[h.slot].sub.x == 0
              && st.runtime[h.slot].sub.y == 0,
          "ПРЫЖОК: клетка пишется, доля — центр (лосси-цена названа вслух)");
    CHECK(st.runtime[h.slot].entryDir == sm::kEntryDirNone
              && st.runtime[h.slot].entryTicks == 0
              && st.runtime[h.slot].tickAccum == 0,
          "ПРЫЖОК: румб входа стёрт, часы думки перезапущены — прыжок не марш");

    return sm::test::report("capture_fold_test");
}
