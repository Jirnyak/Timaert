// ИЗЪЯТИЕ НОГ МАКРО ПО КЛЕТКАМ ОКНА — ЗАХВАТ, ломоть 2 M-419.
//
// ЗАКОН (ЗАКОН ШВА, поправка ЗАХВАТА 2026-11-07): в девяти арендованных
// клетках позицию сквада ведёт СУБМИР — fold сворачивает её на границе тика
// (`fold_captured_subs`), а макро-ноги там отказывают ЦЕЛИКОМ: ни клетки,
// ни доли. Шаг В аренду — последний шаг думки: вошедший встаёт на ПЕРВОЙ
// арендованной клетке и захватывается следующим слепком (пролёт насквозь
// был бы сквадом в окне, которого слепок не воплотил). Отказ — БЕЗ ФЛАГОВ:
// kMarchRefused значил бы «мир не пускает», а цели захваченных ЖИВУТ (думка
// идёт — M-313; читатель флага «рейс застрял» снесён M-2 как спящий).
// Ритму «тело стоит» говорит параметр captured свипа: реген — фон, жжения
// нет («жжение только при движении»), доезд доли в центр не бежит — доля
// писателя fold. Прыжок телепортёра ИЗ аренды отлагается той же формой,
// что суды автобоя; прыжок В окно законен — вошедший захватывается.
// Носители: cell_in_lease_frame (state.h), MarchGround::leaseCenter,
// два гейта в ядре try_move, captured у settle_march_rhythm, ai_teleporter.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-07, числа фактические):
//  - входной гейт ядра снят (`if (false && …)`) → КРАСНЫЙ, 10 из 20:
//    оба отказа, доля, флаги, оба думка-кейса, ведьма целиком;
//  - брейк «шаг В аренду» снят → КРАСНЫЙ, 1 из 20: «ВХОД: встал НА первой
//    арендованной клетке» — думка пролетела окно насквозь;
//  - входной гейт ставит kMarchRefused → КРАСНЫЙ, 2 из 20: «БЕЗ ФЛАГОВ» и
//    «kMarchRefused не вставал» — ровно ложь «мир не пускает»;
//  - рамки нет, только центр (гейт равенством с leaseCenter) → КРАСНЫЙ,
//    6 из 20: «УГОЛ РАМКИ» плюс оба думка-кейса — их центр НА СОСЕДЕ;
//  - captured не передан ритму (всегда false) → КРАСНЫЙ, 2 из 20: «ПЛАНКА
//    ЦЕЛА» (жжение прожгло планку на дорогой земле) и «доля не тронута»
//    (лагерь от прожога открыл доезд в центр);
//  - доезд без `!captured` (`if (resting)`) → КРАСНЫЙ, 2 из 20: обе
//    «доля не тронута» — второй писатель доли вернулся;
//  - гейт телепортёра снят → КРАСНЫЙ, 2 из 20: «ни прыжка, ни шага» и
//    «доля не тронута» — прыжок случился и стёр долю штампом телепорта;
//  - ДОЛЖНА МОЛЧАТЬ: перестановка брейков аренды и клетки игрока в цикле →
//    ЗЕЛЁНЫЙ, 20/20: закон судит СЕМАНТИКУ, не расстановку строк.
#include "check.h"
#include <memory>

#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/pathfinding.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "tables/npc.h"
#include "tables/squad_type.h"
#include "core/torus.h"
#include "bus/store.h"
#include "bus/squad_row.h"

#include <cstdint>

namespace {

using namespace sm;

constexpr int kMap = 64;

// Бюджет квантов в клетках — та же арифметика, что у звонящих ядра.
std::uint32_t cells(std::uint32_t n) {
    return n * 2u * std::uint32_t(ecs::kCellSubEdge);
}

// Ходок по образцу squad_travel_test: корован без дома (ai_nomad) с целью
// колонкой рода 2 — живой носитель марша.
sm::MacroHandle make_walker(sm::MacroStore& st, NPCType type, SquadType stype,
                            std::uint8_t state, int x, int y) {
    sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, kMap)};
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(type), std::uint16_t{0}};
    ecs::MacroNpcRuntime rt{};
    rt.squadType = std::uint8_t(stype);
    rt.state = state;
    rt.moveMult = 1.0f;
    st.runtime[h.slot] = rt;
    sm::store_rename_ordinal(st, h, 7u);
    h = sm::handle_at(st, h.slot);
    st.sheet[h.slot] = make_character_sheet(
        type, 1, avatar_sheet_seed(st.spawnId[h.slot].index));
    ecs::Pools pools{};
    pools.hp = pools.maxHp = 100;
    pools.sp = pools.maxSp = 110;
    st.pools[h.slot] = pools;
    return h;
}

// `ses` — сессия фикстуры (аренда окна живёт в ней, не в состоянии мира);
// nullptr — мир без сессии, то есть без аренды.
void drive(GameState& gs, sm::Session* ses, sm::MacroStore& st,
           MacroNpcAiRuntime& rt, int thinks,
           const PathCostData* grid = nullptr) {
    for (int i = 0; i < thinks; ++i) {
        MacroWorld mw{.gs = &gs, .session = ses, .store = &st,
                      .pathCost = grid};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
}

// ── ЯДРО, ГОЛОЕ: ОТКАЗ НА АРЕНДОВАННОЙ КЛЕТКЕ, БЕЗ ФЛАГОВ ───────────────
void test_legs_refuse_on_a_leased_cell() {
    sm::MarchGround g;
    g.side = 128;
    g.leaseCenter = sm::cell_of(10, 10, 128);

    // Центр аренды: ноги отказывают целиком — клетка, доля, дорога.
    sm::ecs::MacroNpcRuntime rt{};
    rt.sub = {5, -7};
    sm::MacroPos p{10.0f, 10.0f};
    const float walked = sm::try_move(p, rt, 20.0f, 10.0f, cells(4), g);
    CHECK(walked == 0.0f, "ЦЕНТР АРЕНДЫ: дороги нет — ноги не наши");
    CHECK(p.x == 10.0f && p.y == 10.0f, "ЦЕНТР АРЕНДЫ: клетка не тронута");
    CHECK(rt.sub.x == 5 && rt.sub.y == -7,
          "ЦЕНТР АРЕНДЫ: доля не тронута — она писателя fold");
    CHECK(rt.marchFlags == 0,
          "БЕЗ ФЛАГОВ: kMarchRefused был бы ложью «мир не пускает», "
          "а цели захваченных живут");

    // Угловая клетка РАМКИ, не центр: пин геометрии 3×3 — мутация «только
    // центр» обязана краснеть здесь (урок squad_war_test).
    sm::ecs::MacroNpcRuntime rtc{};
    rtc.sub = {5, -7};
    sm::MacroPos pc{11.0f, 11.0f};
    const float walkedC = sm::try_move(pc, rtc, 20.0f, 10.0f, cells(4), g);
    CHECK(g.leaseCenter != sm::cell_of(11, 11, 128),
          "фикстура: угловая клетка — РАМКА, не центр");
    CHECK(walkedC == 0.0f && pc.x == 11.0f && pc.y == 11.0f,
          "УГОЛ РАМКИ: отказ по всем девяти клеткам, не по одной");

    // Контроль сентинела: без аренды тот же старт идёт — гейт спит
    // значением, не веткой, и детектор видит движение, когда ноги свободны.
    sm::MarchGround freeG;
    freeG.side = 128;
    sm::ecs::MacroNpcRuntime rtf{};
    sm::MacroPos pf{10.0f, 10.0f};
    const float walkedF = sm::try_move(pf, rtf, 20.0f, 10.0f, cells(4), freeG);
    CHECK(walkedF > 0.0f && pf.x > 10.0f,
          "КОНТРОЛЬ СЕНТИНЕЛА: аренды нет — марш идёт");
    CHECK((rtf.marchFlags & sm::ecs::kMarchAdvanced) != 0,
          "КОНТРОЛЬ СЕНТИНЕЛА: кванты проехали честно");
}

// ── ЯДРО, ГОЛОЕ: ШАГ В АРЕНДУ — ПОСЛЕДНИЙ ШАГ ДУМКИ ─────────────────────
void test_step_into_lease_is_the_last_step() {
    sm::MarchGround g;
    g.side = 128;
    g.leaseCenter = sm::cell_of(10, 10, 128);
    sm::ecs::MacroNpcRuntime rt{};
    sm::MacroPos p{7.0f, 10.0f};
    // Цель ЗА окном, бюджета — с запасом на пролёт: без гейта думка прошла
    // бы окно насквозь и захват не состоялся бы.
    const float walked = sm::try_move(p, rt, 13.0f, 10.0f, cells(8), g);
    CHECK(walked > 0.0f, "ВХОД: до окна шёл честно");
    CHECK(p.x == 9.0f && p.y == 10.0f,
          "ВХОД: встал НА первой арендованной клетке — вошедший "
          "захватывается следующим слепком, пролёта насквозь нет");
}

// ── ДУМКА: У ЗАХВАЧЕННОГО ЖИВУТ ЦЕЛЬ, ДОЛЯ И ПЛАНКА ─────────────────────
void test_captured_walker_keeps_cell_share_and_goal() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    Session ses;
    gs.mapW = kMap;
    gs.mapH = kMap;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto h = make_walker(st, NPCType::Peasant, SquadType::Caravan,
                               std::uint8_t(NPCState::Traveling), 10, 10);
    sm::ecs::goal_set(st.goal[h.slot], ecs::cell_index(40, 10, kMap), 0, 0,
                      std::uint8_t(NPCState::Idle));
    st.runtime[h.slot].sub = {5, -7};

    // Свидетель рождает предусловие сам (§8 п.11): аренда — дверью мира, и
    // центр на СОСЕДЕ, чтобы пинилась РАМКА, а не совпадение с центром.
    sm::lease_window_at(ses, sm::cell_of(11, 9, gs.mapW));
    CHECK(sm::cell_is_leased(&ses, sm::cell_of(10, 10, gs.mapW), gs.mapW)
              && ses.leasedWindow != sm::cell_of(10, 10, gs.mapW),
          "фикстура: ходок в арендованной РАМКЕ, но не в центре");

    MacroNpcAiRuntime rt{};
    drive(gs, &ses, st, rt, 4);
    CHECK(st.cell[h.slot].idx == ecs::cell_index(10, 10, kMap),
          "ЗАХВАЧЕН: клетку ведёт субмир — макро не тронул");
    CHECK(st.runtime[h.slot].sub.x == 5 && st.runtime[h.slot].sub.y == -7,
          "ЗАХВАЧЕН: доля не тронута — ни маршем, ни доездом в центр");
    CHECK(st.goal[h.slot].cell == ecs::cell_index(40, 10, kMap),
          "ЦЕЛЬ ЖИВА: думка идёт, отказ ног не стал «мир не пускает»");
    CHECK((st.runtime[h.slot].marchFlags & ecs::kMarchRefused) == 0,
          "ЗАХВАЧЕН: kMarchRefused не вставал");

    // Конец аренды — ноги вернулись: тот же мир, та же цель.
    sm::release_window(ses);
    drive(gs, &ses, st, rt, 6);
    CHECK(st.cell[h.slot].idx != ecs::cell_index(10, 10, kMap),
          "КОНТРОЛЬ: аренда снята — марш к той же цели пошёл");
}

// ── ДУМКА: ТЕЛЕПОРТЁР НЕ ПРЫГАЕТ ИЗ АРЕНДЫ, ПЛАНКА НЕ ГОРИТ ─────────────
// Один длинный прогон держит разом четыре двери: прыжок отложен (без гейта
// ролл 0.005/думку почти наверняка выстрелил бы за 2000 думок), марш
// отказан, доезд доли не бежит, жжения нет (земля нарочно дорогая — на ней
// мутация «captured не передан ритму» прожигает планку видимо).
void test_captured_teleporter_neither_jumps_nor_burns() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    Session ses;
    gs.mapW = kMap;
    gs.mapH = kMap;
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto h = make_walker(st, NPCType::Witch, SquadType::None,
                               std::uint8_t(NPCState::Idle), 10, 10);
    st.runtime[h.slot].sub = {50, -30};

    PathCostData grid;
    grid.width = kMap;
    grid.height = kMap;
    grid.costGrid.assign(std::size_t(kMap) * std::size_t(kMap), 8.0f);
    grid.water.assign(std::size_t(kMap) * std::size_t(kMap), 0u);

    sm::lease_window_at(ses, sm::cell_of(9, 11, gs.mapW));
    CHECK(sm::cell_is_leased(&ses, sm::cell_of(10, 10, gs.mapW), gs.mapW)
              && ses.leasedWindow != sm::cell_of(10, 10, gs.mapW),
          "фикстура: ведьма в арендованной РАМКЕ, но не в центре");

    const int sp0 = int(st.pools[h.slot].sp);
    MacroNpcAiRuntime rt{};
    drive(gs, &ses, st, rt, 2000, &grid);
    CHECK(st.cell[h.slot].idx == ecs::cell_index(10, 10, kMap),
          "ТЕЛЕПОРТЁР: 2000 думок в аренде — ни прыжка, ни шага");
    CHECK(st.runtime[h.slot].sub.x == 50 && st.runtime[h.slot].sub.y == -30,
          "ТЕЛЕПОРТЁР: доля не тронута — доезд в центр не бежал");
    CHECK(int(st.pools[h.slot].sp) >= sp0,
          "ПЛАНКА ЦЕЛА: захваченный для ритма ВСТАЛ — жжения нет, реген фон");
}

}  // namespace

int main() {
    test_legs_refuse_on_a_leased_cell();
    test_step_into_lease_is_the_last_step();
    test_captured_walker_keeps_cell_share_and_goal();
    test_captured_teleporter_neither_jumps_nor_burns();
    return sm::test::report("capture_legs_test");
}
