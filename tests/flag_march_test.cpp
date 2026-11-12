// МАРШ ФЛАЖКА — ТО ЖЕ ЯДРО МАРША, СВОИ ЧАСЫ, ШАГ ПИШЕТ САМО ЯДРО (M-412).
//
// ПЕРЕПРЕДМЕЧЕН ШАГОМ 0 ЗАХВАТА (M-419, 2026-11-07): прежний предмет прибора
// (fight_pause_test — заминка боя M-319A, «ноги встали, воля цела») умер со
// стопером ног: в окне 3×3 тело держит сам бой сцены (ЗАХВАТ, ЗАКОН ШВА),
// вне окна у стопера не было ни одного эмитента. ЖИВОЙ закон прежней фазы В
// остаётся и живёт здесь. Свип игрока не водит НИКОГДА, поэтому:
//  - марш флажка — ядро `try_move` входом «шаг задан» (M-412в: маршрут A*
//    исполняется БУКВАЛЬНО, своя походка спит), доезд в центр — вторая
//    половина квантового хода (M-375);
//  - его время — дверь `advance_flag_clock` (M-412д): рековери гаснет
//    тиками напрямую (его единица — мировой тик), время-в-клетке — квантами
//    kAiTicks через try_spend («всё или ничего», M-218), потолок —
//    сатурация, не заворот;
//  - ШАГ пишет само ядро (M-462): клетка, румб входа и доля — одним
//    действием на пересечении края; пассивная ось долю сохраняет. Прежняя
//    дверь «единственный писатель клетки после хода» (`settle_walk_cell`,
//    M-412г) сворачивала в колонку черновик-пару и угадывала прыжок по
//    отсутствию румба — черновика больше нет, а прыжок пишет своя дверь
//    (`squad_jump_to_cell@src/bus/store.h`; её закон «прыжок ставит центр»
//    держит `tests/capture_fold_test.cpp`).
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-07, числа фактические):
//  - снято угасание рековери (`squad_recovery_elapsed`) из
//    `advance_flag_clock` → КРАСНЫЙ, 1 из 10: «РЕКОВЕРИ ФЛАЖКА ГАСНЕТ
//    ТИКАМИ НАПРЯМУЮ» — ровно та замуровка игрока, от которой M-412д;
//  - (мутация 2026-11-07 «`settle_walk_cell` зануляет долю всегда» ушла
//    вместе с дверью; её наследница — «пересечение края зануляет пассивную
//    ось» — в таблице ряда M-462);
//  - сатурация `saturate_entry_ticks` заменена заворотом (`t + 1` без
//    потолка) → КРАСНЫЙ, 1 из 10: «время-в-клетке САТУРИРУЕТСЯ»;
//  - доезд в центр в `try_move` заменён нулём квантов → КРАСНЫЙ, 1 из 10:
//    «ДОЕЗД В ЦЕНТР» — финиш снова парковал бы на углу четырёх клеток;
//  - ДОЛЖНА МОЛЧАТЬ: порядок строк часов (рековери после взноса в
//    `tickAccum`) → ЗЕЛЁНЫЙ, 10/10: закон судит СЕМАНТИКУ дверей, а не
//    расстановку строк в них.
#include "check.h"

#include "bus/entry_context.h"   // kEntryDirNone — прыжок стирает край входа
#include "bus/squad_row.h"
#include "macro/npc_ai.h"

#include <cstdint>

int main() {
    // ── ЯДРО МАРША ВХОДОМ «ШАГ ЗАДАН» (M-412в) ──────────────────────────
    sm::ecs::MacroNpcRuntime rt{};
    const sm::PathPoint path[] = {{91, 50}, {92, 50}};
    sm::MarchRoute route;
    route.pts = path;
    route.count = 2;
    sm::MarchGround ground;
    ground.side = 128;
    // Бюджет — четыре клетки квантами: заведомо больше цены маршрута.
    const std::uint32_t budget =
        std::uint32_t(4 * 2 * sm::ecs::kCellSubEdge);
    sm::ecs::MacroCell at{sm::cell_of(90, 50, 128)};

    // Цель при маршруте не читается — её место занимает сентинел.
    sm::try_move(at, rt, sm::ecs::kNoCell, budget, ground, &route);
    CHECK((rt.marchFlags & sm::ecs::kMarchAdvanced) != 0 && route.idx == 2
              && at.idx == sm::cell_of(92, 50, 128),
          "МАРШРУТ ИСПОЛНЕН БУКВАЛЬНО: какие клетки выбрал A*, те ноги и "
          "оплатили, и клетку флажка написало само ядро");
    CHECK(rt.sub.x == 0 && rt.sub.y == 0,
          "ДОЕЗД В ЦЕНТР: остаток квантов довёл долю до нуля (M-375)");
    CHECK(rt.entryDir != sm::kEntryDirNone,
          "румб входа штампован маршем: смена клетки шла НОГАМИ, не прыжком");

    // ── ЧАСЫ ФЛАЖКА (M-412д): те же колонки, тот же каданс kAiTicks ──────
    rt.recoverySteps = 3u * sm::kAiTicks;
    rt.entryTicks = 0;
    rt.tickAccum = 0;
    sm::advance_flag_clock(rt, sm::kAiTicks - 1u);
    CHECK(rt.recoverySteps == 2u * sm::kAiTicks + 1u,
          "РЕКОВЕРИ ФЛАЖКА ГАСНЕТ ТИКАМИ НАПРЯМУЮ: его единица — мировой "
          "тик, кванта думки он не ждёт");
    CHECK(rt.entryTicks == 0,
          "квант часов — всё или ничего: kAiTicks-1 тиков думку не пробили");
    sm::advance_flag_clock(rt, 1u);
    CHECK(rt.entryTicks == 1,
          "ЧАСЫ ФЛАЖКА ИДУТ: полный квант kAiTicks дал одну думку "
          "времени-в-клетке");
    CHECK(rt.tickAccum == 0,
          "квант оплачен дверью try_spend без остатка (M-218)");
    rt.entryTicks = 0xFF;
    sm::advance_flag_clock(rt, sm::kAiTicks);
    CHECK(rt.entryTicks == 0xFF,
          "время-в-клетке САТУРИРУЕТСЯ: 255 — потолок, заворота нет");

    // ── ШАГ — ОДИН ФАКТ, ОДНА ЗАПИСЬ (M-462) ─────────────────────────────
    // Бюджета ровно на край: клетка сменилась, активная ось вошла с
    // противоположного края, пассивная ось свою долю СОХРАНИЛА, румб
    // штампован — и доезда в центр не было (квантов не осталось).
    sm::ecs::MacroNpcRuntime step{};
    step.sub = {0, -5};
    const sm::PathPoint one[] = {{93, 50}};
    sm::MarchRoute hop;
    hop.pts = one;
    hop.count = 1;
    sm::ecs::MacroCell cell{sm::cell_of(92, 50, 128)};
    sm::try_move(cell, step, sm::ecs::kNoCell,
                 std::uint32_t(sm::ecs::kCellSubEdge), ground, &hop);
    CHECK(cell.idx == sm::cell_of(93, 50, 128),
          "ШАГ ПИШЕТ КЛЕТКУ: пересечение края сменило клетку флажка");
    CHECK(step.sub.x == -sm::ecs::kCellSubEdge && step.sub.y == -5,
          "…И ДОЛЮ ТЕМ ЖЕ ДЕЙСТВИЕМ: вошёл с западного края, а ось, по "
          "которой шага не было, долю сохранила");
    CHECK(step.entryDir != sm::kEntryDirNone && step.entryTicks == 0,
          "…И РУМБ ВХОДА: смена клетки шла ногами, часы клетки заведены");

    // ── НЕ ПЕРЕПРЫГНУТЬ ВСТРЕЧУ (Inc 6) ──────────────────────────────────
    // Многоклеточная думка встаёт НА клетке игрока: встреча судится клеткой,
    // и ходок, пролетевший её насквозь за одну думку, встречи не дал бы. Обе
    // полярности: с клеткой игрока на пути марш кончается в ней, без неё —
    // тем же бюджетом уходит дальше (мутация «гейта нет» до этого свидетеля
    // молчала во всех приборах).
    {
        const std::uint32_t meet = sm::cell_of(12, 50, 128);
        const std::uint32_t goal = sm::cell_of(20, 50, 128);
        sm::MarchGround guarded;
        guarded.side = 128;
        guarded.playerCell = meet;
        sm::ecs::MacroNpcRuntime fast{};
        sm::ecs::MacroCell walker{sm::cell_of(10, 50, 128)};
        sm::try_move(walker, fast, goal, budget, guarded);
        CHECK(walker.idx == meet,
              "ВСТРЕЧА НЕ ПЕРЕПРЫГНУТА: марш кончился НА клетке игрока");

        sm::ecs::MacroNpcRuntime free{};
        sm::ecs::MacroCell lone{sm::cell_of(10, 50, 128)};
        sm::try_move(lone, free, goal, budget, ground);
        CHECK(sm::cell_delta(meet, lone.idx, 128).dx > 0,
              "КОНТРОЛЬ: без клетки игрока тот же бюджет уносит дальше неё");
    }

    // ── ДИАГОНАЛЬНЫЙ КВАНТ ОСИ СТОИТ ПО ПАРЕ ИГРЫ (M-463) ─────────────────
    // Клетка есть квадрат земли, диагональ длиннее стороны: квант оси стоит
    // `kStepStraight` на прямом шаге и `kStepDiagonal` на диагональном
    // (`src/core/torus.h`). Один и тот же бюджет — два ходока из центра
    // клетки: прямой проезжает весь бюджет квантами оси, диагональный —
    // ровно в отношении пары. Ожидание выведено из самих констант.
    {
        const std::uint32_t leg =
            std::uint32_t(2 * sm::ecs::kCellSubEdge);   // одна прямая клетка
        // Проехано квантов оси: до края и вглубь следующей клетки.
        const auto ridden = [](const sm::ecs::MacroNpcRuntime& r) {
            return sm::ecs::kCellSubEdge
                 + (int(r.sub.x) + sm::ecs::kCellSubEdge);
        };
        sm::ecs::MacroNpcRuntime east{};
        sm::ecs::MacroCell eastAt{sm::cell_of(40, 40, 128)};
        sm::try_move(eastAt, east, sm::cell_of(60, 40, 128), leg, ground);
        CHECK(eastAt.idx == sm::cell_of(41, 40, 128)
                  && ridden(east) == int(leg),
              "ПРЯМОЙ ШАГ: бюджет клетки проезжает клетку — квант оси стоит "
              "как прямой");

        sm::ecs::MacroNpcRuntime diag{};
        sm::ecs::MacroCell diagAt{sm::cell_of(40, 40, 128)};
        sm::try_move(diagAt, diag, sm::cell_of(60, 60, 128), leg, ground);
        CHECK(diagAt.idx == sm::cell_of(41, 41, 128)
                  && diag.sub.x == diag.sub.y,
              "ДИАГОНАЛЬ: оси едут вместе — сквад в диагональном соседе");
        CHECK(ridden(diag)
                  == int(leg) * sm::kStepStraight / sm::kStepDiagonal,
              "ДИАГОНАЛЬНЫЙ КВАНТ ОСИ СТОИТ ПО ПАРЕ: тем же бюджетом проехано "
              "в отношении прямого шага к диагональному");
    }

    // ── ДОЕЗД В ЦЕНТР ПЛАТИТ ТОЙ ЖЕ ПАРОЙ (M-463) ────────────────────────
    // Пока обе оси не в центре, они едут вместе по цене диагонального
    // кванта; когда меньшая доехала, большая едет одна по цене прямого.
    {
        sm::ecs::CellSub s{-90, -30};
        sm::ecs::cell_sub_walk_home(s, 30u);
        const int together = 30 * sm::kStepStraight / sm::kStepDiagonal;
        CHECK(int(s.x) == -90 + together && int(s.y) == -30 + together,
              "ДОЕЗД, ОБЕ ОСИ: едут вместе, квант оси стоит как диагональный");

        sm::ecs::CellSub full{-90, -30};
        const std::uint32_t need = std::uint32_t(
            (30 * sm::kStepDiagonal + 60 * sm::kStepStraight
             + sm::kStepStraight - 1) / sm::kStepStraight);
        sm::ecs::cell_sub_walk_home(full, need);
        CHECK(full.x == 0 && full.y == 0,
              "ДОЕЗД ЦЕЛИКОМ: путь до центра — диагональная часть и прямая, "
              "той же парой");
        sm::ecs::CellSub shy{-90, -30};
        sm::ecs::cell_sub_walk_home(shy, need - 2u);
        CHECK(shy.y == 0 && shy.x < 0,
              "…и на квант-другой меньше до центра не хватает: доезд не "
              "дешевле пути");
    }

    // ── ЖАДНЫЙ ШАГ НЕ ИДЁТ ЛЕСЕНКОЙ (M-463) ──────────────────────────────
    // Шаги разной длины сравниваются по цене НА ЕДИНИЦУ ПРИБЛИЖЕНИЯ: по
    // голой цене ребра ходок предпочёл бы короткие прямые шаги длинной
    // диагонали и шёл бы к диагональной цели лесенкой — вдвое дольше. Летун
    // (походка ему не положена, идёт жадным шагом): бюджета ровно на пять
    // диагональных клеток от центра до центра.
    {
        sm::ecs::MacroNpcRuntime bird{};
        bird.flying = 1;
        sm::ecs::MacroCell birdAt{sm::cell_of(40, 40, 128)};
        const std::uint32_t five = std::uint32_t(
            5 * 2 * sm::ecs::kCellSubEdge * sm::kStepDiagonal
            / sm::kStepStraight);
        sm::try_move(birdAt, bird, sm::cell_of(45, 45, 128), five, ground);
        CHECK(birdAt.idx == sm::cell_of(45, 45, 128),
              "ЛЕТУН ДОЛЕТЕЛ ПО ДИАГОНАЛИ: пять диагональных клеток бюджетом "
              "пяти диагональных клеток — не лесенкой");
    }

    return sm::test::report("flag_march_test");
}
