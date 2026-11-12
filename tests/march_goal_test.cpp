// ДВИЖЕНИЕ — НЕ СИСТЕМА, А СОГЛАШЕНИЕ (M-2 / M-307).
//
// ЗАКОН. Модуль пишет ЦЕЛЬ в свою строку и больше ничего; везут к ней ноги
// свипа — одна дверь на всякий сквад (`march_goal`, npc_ai.cpp), которая не
// читает ни рода, ни состояния, ни модуля: единственный её вопрос — в пути ли
// сквад к своей цели (`SquadGoal::underway`). Прибытие случается В КЛЕТКЕ
// цели (M-461: равенство индексов, для всякого рода цели одно), один раз, и
// делает сквад тем, что несла цель (`goal_arrive`); адрес прибытием не
// гасится. Встать — значит отказаться от адреса (`goal_drop`). Погоня есть
// цель рода `Squad`: «за кем» называет модуль, адрес каждую думку
// пересчитывают ноги (модули рода не читают).
//
// ПОЧТИ ВСЕ ХОДОКИ ЗДЕСЬ — СТРОКИ БЕЗ МОДУЛЯ (род `Spire`, в таблице думок
// `nullptr`), и это не удобство фикстуры, а предмет: ноги обязаны везти того,
// за кого никто не думает. Свидетель, у которого решал бы модуль, не отличил
// бы «ноги агностичны» от «модуль сам позаботился».
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» — в ряду M-2 реестра (числа фактические, прогон
// 2026-11-10).
#include "check.h"
#include <memory>

#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
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

struct World {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    std::unique_ptr<MacroStore> stOwn = make_macro_store();
    MacroStore& st = *stOwn;
    MacroNpcAiRuntime ai{};

    World() {
        gs.mapW = kMap;
        gs.mapH = kMap;
    }

    // Ходок: строка стора с телом, планкой и ординалом. Цели у рождённого
    // нет — её ставит сам тест.
    MacroHandle walker(NPCType type, SquadType stype, std::uint8_t state,
                       int x, int y, std::uint32_t ordinal) {
        MacroHandle h = store_birth(st);
        st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(x, y, kMap)};
        st.kind[h.slot] = ecs::NPCKind{std::uint16_t(type), std::uint16_t{0}};
        ecs::MacroNpcRuntime rt{};
        rt.squadType = std::uint8_t(stype);
        rt.state = state;
        rt.moveMult = 1.0f;
        st.runtime[h.slot] = rt;
        store_rename_ordinal(st, h, ordinal);
        h = handle_at(st, h.slot);
        st.sheet[h.slot] = make_character_sheet(
            type, 1, avatar_sheet_seed(st.spawnId[h.slot].index));
        ecs::Pools pools{};
        pools.hp = pools.maxHp = 100;
        pools.sp = pools.maxSp = 110;
        st.pools[h.slot] = pools;
        return h;
    }

    void think(int thinks) {
        for (int i = 0; i < thinks; ++i) {
            MacroWorld mw{.gs = &gs, .store = &st};
            tick_macro_npc_ai(mw, ai, kAiTicks);
        }
    }

    std::uint32_t at(int x, int y) const {
        return ecs::cell_index(x, y, kMap);
    }
    float dist_sq(MacroHandle h, int x, int y) const {
        return torus_dist_sq(float(ecs::cell_x(st.cell[h.slot], kMap)),
                             float(ecs::cell_y(st.cell[h.slot], kMap)),
                             float(x), float(y), float(kMap), float(kMap));
    }
};

// ── 1. НОГИ ВЕЗУТ ПО ЦЕЛИ, НЕ ЧИТАЯ НИ РОДА, НИ СОСТОЯНИЯ ────────────────
// Строка без модуля, состояние нарочно «не ходячее» (Working). Прежде ноги
// жили внутри ветки состояния своей машины: такой сквад не сдвинулся бы
// никогда.
void test_legs_carry_a_row_nobody_thinks_for() {
    World w;
    const MacroHandle h = w.walker(NPCType::Peasant, SquadType::Spire,
                                   std::uint8_t(NPCState::Working), 10, 10, 7u);
    ecs::goal_set(w.st.goal[h.slot], w.at(20, 10), 0, 0,
                  std::uint8_t(NPCState::Chasing));
    CHECK(w.st.goal[h.slot].underway == 1,
          "названный адрес значит «везти»: цель в пути той же записью");

    w.think(1);
    CHECK(w.st.cell[h.slot].idx != w.at(10, 10)
              || (w.st.runtime[h.slot].marchFlags & ecs::kMarchAdvanced) != 0,
          "ПЕРВАЯ ЖЕ думка: ноги пошли — без модуля и в «не ходячем» "
          "состоянии");
    CHECK(w.st.runtime[h.slot].state == std::uint8_t(NPCState::Working),
          "в пути ноги состояние не трогают — оно словарь модуля");

    // Десять клеток ровного мира — 13 думок базового темпа; запас взят с
    // лихвой, свидетель судит закон прибытия, а не темп.
    w.think(20);
    CHECK(w.st.cell[h.slot].idx == w.at(20, 10),
          "ДОШЁЛ: сквад В КЛЕТКЕ цели — прибытие есть равенство клеток, "
          "окрестности у него нет");
    CHECK(w.st.runtime[h.slot].state == std::uint8_t(NPCState::Chasing),
          "ПРИБЫТИЕ: стал тем, что несла цель (onArrival)");
    CHECK(w.st.goal[h.slot].underway == 0,
          "прибытие погасило «в пути»");
    CHECK(w.st.goal[h.slot].cell == w.at(20, 10),
          "а АДРЕС не погашен: он кэш предиката, работа на месте читает его");
}

// ── 2. ПРИБЫТИЕ СЛУЧАЕТСЯ У ЦЕЛИ ОДИН РАЗ ────────────────────────────────
void test_arrival_fires_once() {
    World w;
    const MacroHandle h = w.walker(NPCType::Peasant, SquadType::Spire,
                                   std::uint8_t(NPCState::Idle), 10, 10, 7u);
    ecs::goal_set(w.st.goal[h.slot], w.at(14, 10), 0, 0,
                  std::uint8_t(NPCState::Working));
    w.think(8);
    CHECK(w.st.runtime[h.slot].state == std::uint8_t(NPCState::Working),
          "фикстура: дошёл и стал Working");

    // Модуль (здесь — рука теста) сменил состояние, стоя у достигнутой цели.
    w.st.runtime[h.slot].state = std::uint8_t(NPCState::Idle);
    const std::uint32_t stoodAt = w.st.cell[h.slot].idx;
    w.think(4);
    CHECK(w.st.runtime[h.slot].state == std::uint8_t(NPCState::Idle),
          "ВТОРОГО прибытия нет: достигнутая цель не перезаписывает решение "
          "модуля своим onArrival");
    CHECK(w.st.cell[h.slot].idx == stoodAt, "и ноги стоят");
    CHECK(!ecs::march_arrived(w.st.runtime[h.slot]),
          "исход «прибыл» прожил одну думку и погас");
}

// ── 3. ВСТАТЬ — ЗНАЧИТ ОТКАЗАТЬСЯ ОТ АДРЕСА ──────────────────────────────
void test_dropped_goal_stands_and_keeps_its_predicate() {
    World w;
    const MacroHandle h = w.walker(NPCType::Peasant, SquadType::Spire,
                                   std::uint8_t(NPCState::Idle), 10, 10, 7u);
    const std::uint32_t what = ecs::goal_what(ecs::GoalKind::Squad, 4242u);
    ecs::goal_set(w.st.goal[h.slot], w.at(40, 10), what, 0,
                  std::uint8_t(NPCState::Working));
    w.think(2);
    const std::uint32_t before = w.st.cell[h.slot].idx;
    CHECK(before != w.at(10, 10), "фикстура: сквад в пути и уже сдвинулся");

    ecs::goal_drop(w.st.goal[h.slot]);
    w.think(6);
    CHECK(w.st.cell[h.slot].idx == before,
          "ВСТАЛ: без адреса везти некуда");
    CHECK(w.st.goal[h.slot].cell == ecs::kNoCell,
          "адреса нет — и это значение «стою», а не состояние");
    CHECK(w.st.goal[h.slot].what == what,
          "ПРЕДИКАТ ЦЕЛ: «что я искал» переживает остановку");
    CHECK(w.st.runtime[h.slot].state == std::uint8_t(NPCState::Idle),
          "брошенная цель не «прибывает»: onArrival не применён");
}

// ── 4. НОВОРОЖДЁННЫЙ СТОИТ БЕЗ ЦЕЛИ, И «Я ПРИШЁЛ» ЕМУ НЕ ПРИХОДИТ ────────
// Пара с контролем: рождение цели не ставит (адреса нет, везти некуда), а
// `goal_set` на свою клетку есть марш нулевой длины и честно прибывает.
// (Прежде новорождённому ставили цель «стой тут» особой дверью — она снята,
// M-459/вердикт 2026-11-12: стоять значит не иметь адреса.)
void test_standing_goal_is_not_a_march() {
    World w;
    const MacroHandle stand = w.walker(NPCType::Peasant, SquadType::Spire,
                                       std::uint8_t(NPCState::Idle), 10, 10, 7u);
    const MacroHandle zero = w.walker(NPCType::Peasant, SquadType::Spire,
                                      std::uint8_t(NPCState::Idle), 30, 30, 8u);
    ecs::goal_set(w.st.goal[zero.slot], w.at(30, 30), 0, 0,
                  std::uint8_t(NPCState::Working));
    CHECK(w.st.goal[stand.slot].cell == ecs::kNoCell
              && w.st.goal[stand.slot].underway == 0
              && ecs::goal_kind(w.st.goal[stand.slot].what)
                     == ecs::GoalKind::None,
          "ФИКСТУРА: у рождённого слота адреса нет, рода нет, он не в пути "
          "(закон рождения сквада судит `rooted_module_day_test`)");

    w.think(1);
    CHECK(w.st.runtime[stand.slot].state == std::uint8_t(NPCState::Idle)
              && !ecs::march_arrived(w.st.runtime[stand.slot]),
          "СТОЯЩЕМУ «я пришёл» не приходит");
    CHECK(w.st.runtime[zero.slot].state == std::uint8_t(NPCState::Working)
              && ecs::march_arrived(w.st.runtime[zero.slot]),
          "КОНТРОЛЬ: марш нулевой длины прибывает той же думкой");
}

// ── 5. ПОГОНЯ: АДРЕС ПЕРЕСЧИТЫВАЮТ НОГИ, ВСТРЕЧА — В КЛЕТКЕ ДОБЫЧИ ───────
void test_chase_address_follows_the_prey() {
    World w;
    const MacroHandle prey = w.walker(NPCType::Peasant, SquadType::Spire,
                                      std::uint8_t(NPCState::Idle), 20, 12, 21u);
    const MacroHandle hunter = w.walker(NPCType::Peasant, SquadType::Spire,
                                        std::uint8_t(NPCState::Idle), 10, 12,
                                        22u);
    // Адрес нарочно ЛОЖНЫЙ и далёкий: модуль назвал «за кем», а «где» —
    // дело ног.
    ecs::goal_set(w.st.goal[hunter.slot], w.at(50, 50),
                  ecs::goal_what(ecs::GoalKind::Squad,
                                 w.st.spawnId[prey.slot].index),
                  0, std::uint8_t(NPCState::Chasing));
    w.think(1);
    CHECK(w.st.goal[hunter.slot].cell == w.at(20, 12),
          "НОГИ ПРОЧЛИ РОД Squad: адрес стал клеткой добычи");
    CHECK(w.dist_sq(hunter, 20, 12) < w.dist_sq(hunter, 50, 50),
          "и повезли к ней, а не по ложному адресу");

    // Добыча ушла — адрес идёт за ней, модуль цель не переписывал.
    w.st.cell[prey.slot].idx = w.at(14, 20);
    w.think(1);
    CHECK(w.st.goal[hunter.slot].cell == w.at(14, 20),
          "добыча ДВИЖЕТСЯ — адрес пересчитан той же думкой");

    // Встреча: ровно клетка добычи — тот же закон прибытия, что у всех.
    bool stoppedShort = false;
    for (int i = 0; i < 24 && w.st.goal[hunter.slot].underway != 0; ++i) {
        w.think(1);
        if (w.st.goal[hunter.slot].underway == 0
            && w.st.cell[hunter.slot].idx != w.st.cell[prey.slot].idx) {
            stoppedShort = true;
        }
    }
    CHECK(!stoppedShort,
          "погоня НЕ «прибыла» за клетку до добычи");
    CHECK(w.st.cell[hunter.slot].idx == w.st.cell[prey.slot].idx,
          "НАСТИГ: охотник в клетке добычи");
    CHECK(w.st.runtime[hunter.slot].state == std::uint8_t(NPCState::Chasing),
          "и стал тем, что несла цель");
}

// ── 6. ДОБЫЧА ПРОПАЛА — ИДЁТ ТУДА, ГДЕ ВИДЕЛ ЕЁ В ПОСЛЕДНИЙ РАЗ ──────────
void test_lost_prey_leaves_the_last_known_address() {
    World w;
    const MacroHandle hunter = w.walker(NPCType::Peasant, SquadType::Spire,
                                        std::uint8_t(NPCState::Idle), 10, 12,
                                        22u);
    // Ординал, которого в мире нет: добыча умерла и убрана.
    ecs::goal_set(w.st.goal[hunter.slot], w.at(16, 12),
                  ecs::goal_what(ecs::GoalKind::Squad, 9999u),
                  0, std::uint8_t(NPCState::Idle));
    w.think(16);
    CHECK(w.st.goal[hunter.slot].cell == w.at(16, 12),
          "ординал не резолвится — адрес остался последним известным");
    CHECK(w.st.cell[hunter.slot].idx == w.at(16, 12)
              && w.st.goal[hunter.slot].underway == 0,
          "дошёл туда и прибыл: ветки «цель пропала» нет, и она не нужна");
}

// ── 7. ЛАГЕРЬ НЕ СТИРАЕТ «Я ПРИШЁЛ» ──────────────────────────────────────
// Исход прошлого марша читает ДУМКА, а в лагере думки нет: уснувший у
// крыльца обязан проснуться со своим прибытием (иначе артель не сдала бы
// груз, принесённый на последнем издыхании).
void test_camp_keeps_the_arrival_for_the_next_think() {
    World w;
    const MacroHandle h = w.walker(NPCType::Peasant, SquadType::Spire,
                                   std::uint8_t(NPCState::Idle), 10, 10, 7u);
    ecs::goal_set(w.st.goal[h.slot], w.at(13, 10), 0, 0,
                  std::uint8_t(NPCState::Idle));
    int guard = 0;
    while (!ecs::march_arrived(w.st.runtime[h.slot]) && guard++ < 16)
        w.think(1);
    CHECK(ecs::march_arrived(w.st.runtime[h.slot]),
          "фикстура: ноги назвали прибытие");

    // Лагерь ставит рука теста — тем же, чем его ставит ритм: состоянием.
    w.st.runtime[h.slot].stateAfterPause = w.st.runtime[h.slot].state;
    w.st.runtime[h.slot].state = std::uint8_t(NPCState::Resting);
    w.st.pools[h.slot].sp = 0;
    w.think(1);
    CHECK(w.st.runtime[h.slot].state == std::uint8_t(NPCState::Resting),
          "фикстура: думки не было — сквад в лагере");
    CHECK(ecs::march_arrived(w.st.runtime[h.slot]),
          "В ЛАГЕРЕ исход ЖДЁТ: «прибыл» не стёрт думкой, которой не было");
    CHECK((w.st.runtime[h.slot].marchFlags & ecs::kMarchAdvanced) == 0,
          "а флаги ХОДА погашены — в лагере тело не шло");
}

// ── 8. МОДУЛЬ ЧИТАЕТ ПРИБЫТИЕ И НЕ ШАГАЕТ САМ ────────────────────────────
// Живой модуль (бродяга каталога тел): решает в Idle, идёт ногами свипа,
// по прибытии — пауза, и её вешает МОДУЛЬ, прочтя исход.
void test_a_module_decides_and_the_sweep_walks() {
    World w;
    const MacroHandle h = w.walker(NPCType::Adventurer, SquadType::None,
                                   std::uint8_t(NPCState::Idle), 30, 30, 7u);
    w.think(1);
    const ecs::MacroNpcRuntime& rt = w.st.runtime[h.slot];
    CHECK(w.st.goal[h.slot].cell != ecs::kNoCell,
          "первая думка: модуль назвал цель прогулки");
    // Доходит и встаёт на паузу; ни одна думка не оставляет его «в пути»
    // навсегда.
    bool paused = false;
    for (int i = 0; i < 64 && !paused; ++i) {
        w.think(1);
        paused = rt.state == std::uint8_t(NPCState::Idle)
                 && ecs::squad_recovering(rt)
                 && w.st.goal[h.slot].underway == 0;
    }
    CHECK(paused,
          "ДОШЁЛ → Idle (ноги) → пауза (модуль прочёл «я пришёл»)");
}

// ── 9. «ДОМА» — КЛЕТКА ЛОГОВА: У НОГ И У МОДУЛЯ ОДИН ОТВЕТ (M-461) ──────
// Дракон вне логова без добычи летит домой и САДИТСЯ В КЛЕТКУ логова; модуль
// спрашивает «дома ли я» тем же равенством индексов, каким ноги судят
// прибытие. Разойдись эти два ответа — севший числился бы «не дома» каждую
// думку: вечный Returning, ни одного круга (этим платили, пока ноги сажали
// за клетку до цели, а модули повторяли их порог).
void test_the_dragon_lands_in_its_lair_cell() {
    World w;
    const MacroHandle d = w.walker(NPCType::Peasant, SquadType::Dragon,
                                   std::uint8_t(NPCState::Idle), 38, 30, 7u);
    w.st.runtime[d.slot].lairCell = w.at(30, 30);
    w.think(1);
    CHECK(w.st.runtime[d.slot].state == std::uint8_t(NPCState::Returning)
              && w.st.goal[d.slot].cell == w.at(30, 30),
          "фикстура: вне логова и без добычи — домой");
    bool circled = false;
    bool landedElsewhere = false;
    for (int i = 0; i < 96 && !circled; ++i) {
        w.think(1);
        // Прибытие домой (цель — логово, «в пути» погашено) обязано застать
        // дракона В клетке логова, а не рядом с ней.
        if (w.st.goal[d.slot].cell == w.at(30, 30)
            && w.st.goal[d.slot].underway == 0
            && w.st.cell[d.slot].idx != w.at(30, 30)) {
            landedElsewhere = true;
        }
        circled = w.st.runtime[d.slot].state
                  == std::uint8_t(NPCState::Wandering);
    }
    CHECK(!landedElsewhere,
          "СЕЛ В КЛЕТКУ ЛОГОВА: прибытие домой не случается у соседней "
          "клетки");
    CHECK(circled,
          "…И ПОШЁЛ КРУЖИТЬ: севший в логово для модуля «дома»");
}

// ── 10. СОСЕДНЯЯ С ЛОГОВОМ КЛЕТКА — НЕ «ДОМА» ───────────────────────────
// Вторая половина того же закона, и без неё девятый свидетель слеп: вернись в
// модуль порог «ближе двух клеток», севший в логово остался бы «дома», и
// проверка выше прошла бы зелёной (мутация `fromLair < 2.0f` — молчала).
// Дракон, стоящий в клетке РЯДОМ с логовом, обязан назвать адресом логово.
void test_next_to_the_lair_is_not_home() {
    World w;
    const MacroHandle d = w.walker(NPCType::Peasant, SquadType::Dragon,
                                   std::uint8_t(NPCState::Idle), 31, 30, 7u);
    w.st.runtime[d.slot].lairCell = w.at(30, 30);
    w.think(1);
    CHECK(w.st.goal[d.slot].cell == w.at(30, 30),
          "В СОСЕДНЕЙ КЛЕТКЕ — НЕ ДОМА: адресом названо логово, а не круг "
          "по округе");
}

// ── 11. В МИРЕ БЕЗ ИГРОКА НЕТ «КЛЕТКИ ИГРОКА» ───────────────────────────
// Ядро марша встаёт НА клетке игрока, чтобы многоклеточная думка не
// перепрыгнула встречу. «Игрока нет» обязано быть значением, а не клеткой:
// пока клетка флажка ехала парой флоатов с умолчанием (0, 0), мир без игрока
// останавливал марш всякого сквада на клетке (0,0) — остаток квантов думки
// сгорал, тело вставало на краю. Сентинел индекса клеткой не является (M-462).
// У мира этой фикстуры сессии нет вовсе; ходок входит в (0,0) через шов.
void test_a_world_without_a_player_has_no_player_cell() {
    World w;
    const MacroHandle h = w.walker(NPCType::Peasant, SquadType::Spire,
                                   std::uint8_t(NPCState::Working),
                                   kMap - 1, 0, 7u);
    ecs::goal_set(w.st.goal[h.slot], w.at(3, 0), 0, 0,
                  std::uint8_t(NPCState::Idle));
    w.think(1);
    CHECK(w.st.cell[h.slot].idx == w.at(0, 0),
          "фикстура: первая думка внесла ходока в клетку (0,0) через шов");
    CHECK(int(w.st.runtime[h.slot].sub.x) > -ecs::kCellSubEdge,
          "НЕ ВСТАЛ НА КРАЮ: остаток квантов думки проехал вглубь клетки — "
          "клетки игрока у мира без игрока нет");
}

// ── 12. ПОВОДОК ДРАКОНА — В КЛЕТКАХ ПУТИ ОТ ЛОГОВА ──────────────────────
// Кружащий дракон, ушедший за поводок, поворачивает домой; кружащий внутри
// поводка продолжает круг. Обе полярности: иначе «повернул домой» было бы
// верно и для модуля, который возвращает всякого (а мутация «поводка нет»
// до этого свидетеля молчала во всех приборах).
void test_the_dragon_leash_is_a_path_radius() {
    World w;
    const MacroHandle far = w.walker(NPCType::Peasant, SquadType::Dragon,
                                     std::uint8_t(NPCState::Wandering),
                                     50, 30, 7u);
    w.st.runtime[far.slot].lairCell = w.at(30, 30);
    ecs::goal_set(w.st.goal[far.slot], w.at(55, 30), 0, 0,
                  std::uint8_t(NPCState::Idle));
    const MacroHandle near = w.walker(NPCType::Peasant, SquadType::Dragon,
                                      std::uint8_t(NPCState::Wandering),
                                      10, 44, 8u);
    w.st.runtime[near.slot].lairCell = w.at(10, 40);
    ecs::goal_set(w.st.goal[near.slot], w.at(10, 47), 0, 0,
                  std::uint8_t(NPCState::Idle));
    w.think(1);
    CHECK(w.st.goal[far.slot].cell == w.at(30, 30)
              && w.st.runtime[far.slot].state
                     == std::uint8_t(NPCState::Returning),
          "ЗА ПОВОДКОМ: двадцать клеток от логова — круг брошен, адресом "
          "названо логово");
    CHECK(w.st.goal[near.slot].cell == w.at(10, 47)
              && w.st.runtime[near.slot].state
                     == std::uint8_t(NPCState::Wandering),
          "ВНУТРИ ПОВОДКА: четыре клетки от логова — круг продолжается");
}

// ── 13. ОТДЫХ ЕСТЬ ДУМКА, В КОТОРУЮ НОГИ НЕ ПРОЕХАЛИ НИ КВАНТА (M-465) ───
// Жжение включает ХОД и ничто иное: стоящему не нужна причина стоять. Сквад,
// которому модуль велел встать (адрес стёрт), прежде числился «не
// остановившимся» — жёг SP как на марше и не лечился; зонд 2026-11-12 дал
// за 20 думок SP 70 → 92 и HP 80 → 80 против 70 → 95 и 80 → 100 у стоящего с
// целью на своей клетке. Обе полярности на одной земле и одной анкете:
// вставший без адреса отдыхает РОВНО как стоящий с адресом, а идущий — жжёт
// и не лечится.
void test_rest_is_a_think_without_a_step() {
    World w;
    const MacroHandle dropped = w.walker(NPCType::Peasant, SquadType::Spire,
                                         std::uint8_t(NPCState::Idle),
                                         20, 20, 7u);
    const MacroHandle standing = w.walker(NPCType::Peasant, SquadType::Spire,
                                          std::uint8_t(NPCState::Idle),
                                          40, 40, 8u);
    const MacroHandle marcher = w.walker(NPCType::Peasant, SquadType::Spire,
                                         std::uint8_t(NPCState::Working),
                                         10, 50, 9u);
    ecs::goal_drop(w.st.goal[dropped.slot]);                  // «встать»
    // «Стоит с адресом» — прибывший: марш нулевой длины кончается первой же
    // думкой, адрес остаётся кэшем предиката.
    ecs::goal_set(w.st.goal[standing.slot], w.at(40, 40), 0, 0,
                  std::uint8_t(NPCState::Idle));
    ecs::goal_set(w.st.goal[marcher.slot], w.at(30, 50), 0, 0,
                  std::uint8_t(NPCState::Idle));
    for (const MacroHandle h : {dropped, standing, marcher}) {
        w.st.pools[h.slot].sp = w.st.pools[h.slot].maxSp - 40;
        w.st.pools[h.slot].hp = w.st.pools[h.slot].maxHp - 20;
    }
    const int hp0 = w.st.pools[dropped.slot].hp;
    w.think(10);
    const ecs::Pools& a = w.st.pools[dropped.slot];
    const ecs::Pools& b = w.st.pools[standing.slot];
    const ecs::Pools& c = w.st.pools[marcher.slot];
    CHECK(w.st.cell[marcher.slot].idx != w.at(10, 50)
              && w.st.goal[marcher.slot].underway != 0,
          "фикстура: третий сквад все десять думок ШЁЛ и ещё не дошёл");
    CHECK(a.sp == b.sp && a.hp == b.hp,
          "ВСТАВШИЙ БЕЗ АДРЕСА ОТДЫХАЕТ РОВНО КАК СТОЯЩИЙ С АДРЕСОМ: "
          "причина стоять ритму не нужна");
    CHECK(a.hp > hp0,
          "…И ЛЕЧИТСЯ: думка без хода есть отдых");
    CHECK(c.hp == hp0 && c.sp < a.sp,
          "КОНТРОЛЬ: идущий не лечится и жжёт — жжение включает ход");
}

} // namespace

int main() {
    test_legs_carry_a_row_nobody_thinks_for();
    test_arrival_fires_once();
    test_dropped_goal_stands_and_keeps_its_predicate();
    test_standing_goal_is_not_a_march();
    test_chase_address_follows_the_prey();
    test_lost_prey_leaves_the_last_known_address();
    test_camp_keeps_the_arrival_for_the_next_think();
    test_a_module_decides_and_the_sweep_walks();
    test_the_dragon_lands_in_its_lair_cell();
    test_next_to_the_lair_is_not_home();
    test_a_world_without_a_player_has_no_player_cell();
    test_the_dragon_leash_is_a_path_radius();
    test_rest_is_a_think_without_a_step();
    return sm::test::report("march_goal_test");
}
