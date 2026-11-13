// ИИ-МОДУЛЬ СКВАДА БЕЗ ТИПА (строка None) — лестница
// `untyped_squad_behaviour` и её поведения. Строка `kAiModuleRows`;
// окно — `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <cstdint>

#include "bus/session.h"      // cell_is_leased — изъятие по клетке
#include "bus/store.h"        // handle_at, squad_jump_to_cell
#include "core/torus.h"
#include "macro/auto_battle.h"
#include "macro/characters.h"  // стол анкет — ступень лестницы
#include "macro/rooted_iter.h" // for_each_rooted — урна городов
#include "macro/squad.h"      // settle_auto_battle, macro_dead
#include "tables/behaviour.h"
#include "tables/faction.h"   // faction_ordinal — маги на сборке
#include "tables/npc.h"

namespace sm {

namespace {

inline float rand_f01(const TickContext& ctx) { return ctx.rng->next_f01(); }

void ai_trader(MacroHandle self, ecs::MacroNpcRuntime& rt,
               [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    const std::uint32_t home = home_cell(self, ctx);
    if (home == ecs::kNoCell) return;
    const MacroStore& st = store_ctx(ctx);
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    const int myHomeId = parent_of(st, self.slot);

    if (rt.state == std::uint8_t(NS::Idle)) {
        // Вернулся домой — постоял (прибытие назвали ноги).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((20 + rand_int(ctx, 30)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            // Pick another city (id != home).
            int candidates = 0;
            for_each_rooted(st, [&](std::uint16_t slot) {
                if (is_other_city(st, slot, myHomeId)) ++candidates;
            });
            if (candidates > 0) {
                int pick = rand_int(ctx, candidates);
                // Обход без досрочного выхода: взявший урну гасит флаг, и
                // остаток прохода молчит (у двери мест нет break — её
                // порядок есть закон приоритета клетки).
                bool taken = false;
                for_each_rooted(st, [&](std::uint16_t slot) {
                    if (taken || !is_other_city(st, slot, myHomeId))
                        return;
                    if (pick-- != 0) return;
                    ecs::goal_set(g, st.cell[slot].idx,
                                  ecs::goal_what(ecs::GoalKind::Squad,
                                                 st.spawnId[slot].index),
                                  0, std::uint8_t(NS::Working));
                    rt.state  = std::uint8_t(NS::Traveling);
                    taken = true;
                });
            } else {
                charge_squad_recovery(rt, std::uint32_t(20 * kAiTicks));
            }
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Working)) {
        // Дошёл до чужого города (ноги сделали его Working) — стоянка.
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((15 + rand_int(ctx, 20)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Returning);
        }
        return;
    }
    // Traveling/Returning: в пути решать нечего — везут ноги свипа.
}

void ai_aggressive(MacroHandle self, ecs::MacroNpcRuntime& rt,
                   [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    // No private player-channel here any more (owner, 2026-08-29: «игрок
    // ничем не особенен»). Perception and pursuit are squad_threat_step's —
    // the player's squad sits in the SAME SquadIndex at the SAME
    // kSquadSightCells as every other squad, so an aggressive row that can
    // see the player chases him through the one law it chases anyone by.
    // The old channel saw the player at 10 cells against everyone else's 6.
    // What is left below is the row's untroubled day: wander.
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    if (rt.state == std::uint8_t(NS::Chasing)) {
        // Добычи больше нет — встать: погоня отпущена, колонка пуста.
        chase_release(g);
        rt.state = std::uint8_t(NS::Idle);
        charge_squad_recovery(rt,
            std::uint32_t((5 + rand_int(ctx, 10)) * kAiTicks));
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        // Дошёл — постоял (прибытие назвали ноги).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((8 + rand_int(ctx, 15)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            ecs::goal_set(g,
                          cell_nearby(store_ctx(ctx).cell[self.slot].idx, 20,
                                      ctx),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    // Wandering: в пути решать нечего — везут ноги свипа.
}

// ── Царь-крестьянин: роамер-охотник на магов (стол анкет) ────────────────
// Слово владельца (2026-09-10): «культистов он не трогает, только магов
// Магики — но НЕ крестьян фракции магики»; поводка нет — чистый роамер по
// всей карте. Кто цель — решение ЭТОЙ функции (закон стола: данные в
// строке — решения в функции): тело мага (Witch/Sorceress) фракции
// magika. Встреча решается ТОЙ ЖЕ дверью боя, что у threat step — один
// закон битвы (CANON S13), никакого второго резолвера.

// Охотничий глаз: в полтора раза дальше сквадного (kSquadSightCells = 8) —
// охотник ИЩЕТ, а не натыкается; ±2 бакета грида (cellSize 8) покрывают
// радиус целиком.
constexpr int kMageHuntSightCells = 12;

MacroHandle nearest_magika_mage(MacroHandle self, const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return MacroHandle{};
    const MacroStore& st = store_ctx(ctx);
    // Ординал НА СБОРКЕ (M-268): здесь стоял `faction_index("magika")` —
    // линейный скан реестра по строке на КАЖДУЮ думку охотника. Имя задано
    // авторски, извне приехать не может, поэтому оно и не доживает до тика.
    constexpr int magika = faction_ordinal("magika");
    static_assert(magika >= 0, "реестр обязан нести строку магов");
    const std::uint32_t at = st.cell[self.slot].idx;
    const int cx0 = cell_x(at, ctx.mapW) / b.cellSize;
    const int cy0 = cell_y(at, ctx.mapW) / b.cellSize;
    // «Вижу» — в пределах пути в столько клеток; +1 — строгое сравнение.
    int best = kMageHuntSightCells * kStepStraight + 1;
    MacroHandle found{};
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                // Бакет несёт индекс в порядке (шаг 3): слот — ключ к
                // колонкам, хэндл — для дверей и долгой ссылки.
                const SquadWalkEntry& sw = g.order[*it];
                if (sw.slot == self.slot) continue;
                if (st.dead[sw.slot] != 0) continue;
                const auto& mcell = st.cell[sw.slot];
                const auto* oc = &mcell;
                const auto* ok = &st.kind[sw.slot];
                // Маг = род тела, не флаг: ведьма и чародейка — строки
                // реестра. Крестьянин Магики проходит мимо этого фильтра
                // ЖИВЫМ — ровно то, что владелец назвал важным.
                if (int(ok->factionIdx) != magika) continue;
                const NPCType t = NPCType(std::uint8_t(ok->type));
                if (t != NPCType::Witch && t != NPCType::Sorceress) continue;
                const int d = cell_dist(at, oc->idx, ctx.mapW);
                if (d >= best) continue;
                best = d;
                found = handle_at(st, sw.slot);
            }
        }
    }
    return found;
}

void ai_mage_hunt(MacroHandle self, ecs::MacroNpcRuntime& rt,
                  [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    if (ctx.squads && ctx.mw.store && ctx.mw.gs) {
        const MacroHandle prey = nearest_magika_mage(self, ctx);
        MacroStore& st = store_ctx(ctx);
        if (st.valid(prey)) {
            if (st.cell[self.slot].idx == st.cell[prey.slot].idx) {
                // Игрок в теле ведьмы: встреча принадлежит форс-двери
                // игрока (main.cpp detect_forced_encounter), не тихому
                // резолву — тот же гард, что у threat step.
                // ИЗЪЯТИЕ ОСТАЁТСЯ ЗА АКТИВНЫМ, И ЭТО МАРШРУТ, А НЕ
                // НЕУЯЗВИМОСТЬ: встреча активного принадлежит ЕГО двери
                // (`detect_forced_encounter@src/app/main.cpp`), которая
                // открывает предбоевой экран, — тихий авторезолв решил бы её
                // за игрока. Второй слот (ДОМ) ушёл с `.home` (ломоть 7б
                // M-421, вердикт владельца 2026-11-09 «только за активным»):
                // прочие сквады под контролем есть ОБЫЧНЫЕ сквады мира.
                if (prey.slot == ctx.playerFlagSlot) return;
                // ИЗЪЯТИЕ ПО КЛЕТКЕ, А НЕ ПО РОДУ СЦЕНЫ (M-226): клетка
                // встречи в аренде у субмира — значит её ведёт он, и решать
                // этот бой наверху значило бы завести второго писателя.
                // Встреча не пропадает: она ОТЛАГАЕТСЯ до конца аренды.
                if (cell_is_leased(ctx.mw.session, st.cell[self.slot].idx,
                                   ctx.mapW)) return;
                const AutoBattleOutcome o = resolve_auto_battle(
                    auto_battle_side_of(st, self),
                    auto_battle_side_of(st, prey),
                    Ambush::None, *ctx.rng);
                settle_auto_battle(ctx.mw, self, prey, o);
                if (!macro_dead(st, self)) {
                    // После боя — стоять: погоня отпущена, колонка пуста.
                    chase_release(st.goal[self.slot]);
                    rt.state = std::uint8_t(NS::Idle);
                    charge_squad_recovery(rt,
                        std::uint32_t((3 + rand_int(ctx, 5)) * kAiTicks));
                }
                return;
            }
            // Добыча ДВИЖЕТСЯ — род цели «сквад»: модуль называет, ЗА КЕМ,
            // а адрес каждую думку пересчитывают ноги свипа (кэш предиката,
            // случай четвёртый) — модуль рода не читает. Настигнув,
            // сквад остаётся Chasing: встречу решит следующая думка.
            ecs::goal_set(store_ctx(ctx).goal[self.slot],
                          st.cell[prey.slot].idx,
                          ecs::goal_what(ecs::GoalKind::Squad,
                                         st.spawnId[prey.slot].index),
                          0, std::uint8_t(NS::Chasing));
            rt.state = std::uint8_t(NS::Chasing);
            return;
        }
    }
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    // Некого бить — чистый роам: дальняя случайная цель по ВСЕЙ карте, не
    // прогулка по околице. Поводка нет — слово владельца.
    if (rt.state == std::uint8_t(NS::Chasing)) {
        chase_release(g);   // добыча пропала — погоня отпущена
        rt.state = std::uint8_t(NS::Idle);
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        // Дошёл — постоял (прибытие назвали ноги).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((8 + rand_int(ctx, 15)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            ecs::goal_set(g,
                          cell_of(rand_int(ctx, ctx.mapW),
                                  rand_int(ctx, ctx.mapH), ctx.mapW),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    // Wandering: в пути решать нечего — везут ноги свипа.
}

// (ЗДЕСЬ ЖИЛА ПАТРУЛЬНАЯ ВЫЛАЗКА — снесена 2026-09-21 вместе с механикой.
// Ушли: kPatrolReachHops / kPatrolDwellDays / kPatrolDwellThinks,
// PatrolRoamCells, collect_trouble_cells_, ai_patrol_errand (91 строка),
// патрульная урна run_patrol_auction и подъём вылазки из гарнизона.
//
// А 2026-09-22 ушёл и `ai_patrol` — последний легаси-круг у дома, который
// ходили генезисные одиночки-стражники. Их вырезали тем же ходом
// (npc_spawn.cpp), и машина осталась без единого носителя. Вместе с ней
// умерли значение `AIBehaviour::Patrol` и состояние `NPCState::Patrolling`:
// колонка без писателя — дефект (AGENTS §9), а не задел.
//
// СЦЕНА ЭТОГО НЕ ЗАМЕТИТ, и это проверено, а не обещано: стойку тела даёт
// `combatant_behaviour`, она отвечала `true` и на Patrol, и на Aggressive, а
// строка Guard переведена именно в Aggressive — `subworld_ai_for` вернёт тот
// же `SubworldAi::Combat`.
// Поле угрозы и страх рейса ЖИВЫ — `route_fear_` читает развилка рейса.)

void ai_teleporter(MacroHandle self, ecs::MacroNpcRuntime& rt,
                   [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    // ПРЫЖОК — АКТ, и занятость ему ставит ТОТ ЖЕ гейт, что всем актам
    // сквада. Здесь жил `teleportCooldown` — второй счётчик того же
    // субъекта со своим ручным сливом, буквально «своя таблица
    // восстановления у отдельного рода действий» (ЗАКОН СПОСОБНОСТИ п.6);
    // прежняя пара «кулдаун 50 думок + пауза Idle 10» слилась в один заряд.
    // ПРЫЖОК ИЗ АРЕНДЫ ОТЛАГАЕТСЯ (ЗАХВАТ, ломоть 2 M-419): в окне позицию
    // ведёт субмир — прыжок макро был бы вторым писателем (тело осталось бы
    // в сцене, запись уехала). Прыжок В окно законен: вошедший захватывается
    // следующим слепком. Форма гейта — та же, что у судов автобоя
    // (`cell_is_leased` по адресу сессии: нулевой адрес — аренды нет); он
    // стоит ПЕРВЫМ, чтобы отложенный прыжок не сжигал ролл.
    const std::uint32_t at = store_ctx(ctx).cell[self.slot].idx;
    const bool jumpLeased = cell_is_leased(ctx.mw.session, at, ctx.mapW);
    if (!jumpLeased && !squad_recovering(rt) && rand_f01(ctx) < 0.005f) {
        // ПРЫЖОК — ДВЕРЬЮ ПРЫЖКА (`squad_jump_to_cell@src/bus/store.h`), той
        // же, какой прыгают побег, консоль и выход из субмира: клетка,
        // стёртый румб входа и доля в центре — одним действием. Здесь модуль
        // писал черновик позиции и сам штамповал «румба нет», а центр доле
        // ставил драйвер по догадке «клетка сменилась без румба» (M-462).
        const std::uint32_t to = cell_nearby(at, 40, ctx);
        squad_jump_to_cell(store_ctx(ctx), self, cell_x(to, ctx.mapW),
                           cell_y(to, ctx.mapW), ctx.mapW);
        charge_squad_recovery(rt, std::uint32_t(50 * kAiTicks));
        // Прыгнул — стоит: прежняя прогулка осталась там, откуда прыгал.
        ecs::goal_drop(g);
        rt.state = std::uint8_t(NS::Idle);
        return;
    }
    if (rt.state == std::uint8_t(NS::Idle)) {
        // Дошёл — постоял (прибытие назвали ноги).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((12 + rand_int(ctx, 20)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            ecs::goal_set(g, cell_nearby(at, 15, ctx),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    // Wandering: в пути решать нечего — везут ноги свипа.
}

void ai_wanderer(MacroHandle self, ecs::MacroNpcRuntime& rt,
                 [[maybe_unused]] ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadGoal& g = store_ctx(ctx).goal[self.slot];
    if (rt.state == std::uint8_t(NS::Idle)) {
        // Дошёл — постоял (прибытие назвали ноги).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                std::uint32_t((10 + rand_int(ctx, 15)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            ecs::goal_set(g,
                          cell_nearby(store_ctx(ctx).cell[self.slot].idx, 25,
                                      ctx),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    // Wandering: в пути решать нечего — везут ноги свипа.
}

// Follow the waypoint route in the squad's orders (Session 15, Inc 7): walk
// to the current waypoint, arrive, take the next, loop. A squad ordered onto
// a route with no route wanders — a degraded order is a visible NPC, not a
// frozen one.
void ai_waypoints(MacroHandle e, ecs::MacroNpcRuntime& rt,
                  ecs::Pools& pools, const TickContext& ctx) {
    ecs::SquadOrders* orders = ctx.mw.store
        ? &store_ctx(ctx).orders[e.slot] : nullptr;
    if (!orders || orders->waypointCount == 0) {
        ai_wanderer(e, rt, pools, ctx);
        return;
    }
    const int i = orders->currentWaypoint % orders->waypointCount;
    ecs::SquadGoal& g = store_ctx(ctx).goal[e.slot];
    const std::uint32_t wp =
        cell_of(int(orders->waypoints[std::size_t(i * 2)]),
                int(orders->waypoints[std::size_t(i * 2 + 1)]), ctx.mapW);
    // ДОШЁЛ ДО ТОЧКИ МАРШРУТА (прибытие назвали ноги, и адрес цели — она):
    // взять следующую, постоять. Чужое прибытие — конец бегства — маршрут не
    // двигает: какое плечо кончилось, говорит адрес самой цели.
    if (ecs::march_arrived(rt) && g.cell == wp) {
        orders->currentWaypoint =
            std::uint8_t((i + 1) % orders->waypointCount);
        charge_squad_recovery(rt,
            std::uint32_t((2 + rand_int(ctx, 4)) * kAiTicks));
        return;
    }
    // Стоянка на точке: цели не пишем — названный адрес значит «везти».
    if (rt.state == std::uint8_t(NS::Idle) && squad_recovering(rt)) return;
    ecs::goal_set(g, wp, 0, 0, std::uint8_t(NS::Idle));
    rt.state = std::uint8_t(NS::Traveling);
}

}  // namespace

// The behaviour a squad ACTUALLY lives by: its type row's ai column, unless
// it CARRIES a waypoint route — the route's presence is the order (owner's
// ruling: one knob, not two). New kinds of squad AI are rows, never fields.
// ЛЕСТНИЦА ПРИОРИТЕТОВ — ЗАКОН этой двери (владелец, 2026-09-10).
// Поведение не хранится — оно ВЫВОДИТСЯ каждый think из данных на
// сущности, ступени сверху вниз:
//   1. ПРИКАЗ: маршрут в SquadOrders (его наличие И ЕСТЬ приказ);
//   2. (будущие перекрытия контекста — событие/факт пишет данные,
//      ступень читает их здесь, никогда веткой в диспетче);
//   3. АНКЕТА: строка стола дизайн-персонажей по ординалу тега;
//   4. строка типа (kNpcTypeDefs.ai) — род как он есть.
// Новая модель ИИ = функция + строка enum; новая ступень = данные на
// сущности + одна строка здесь.
// ПОВЕДЕНИЕ НЕТИПИЗИРОВАННОГО СКВАДА — и это ЕДИНСТВЕННАЯ ТОЧКА, ГДЕ
// МАКРОМИР ЕЩЁ ЧИТАЕТ КАТАЛОГ ТЕЛ (CANON S2 «Протокол двух миров»; до
// 2026-09-21 таких точек было четыре, и ни одна не была названа).
// Зовётся ТОЛЬКО из модуля None-строки kAiModuleRows — для сквадов, у которых
// нет макро-источника поведения вовсе: генезисных одиночек и квестовой цели.
// Лестница честная и по убыванию authority: маршрут в приказе (он и есть
// приказ — вердикт владельца), строка стола анкет (МАКРО-таблица, законно),
// и только потом мобная строка лидера — та самая течь.
// С их сносом (порция Б-6, «всё через универсальную систему анкет») эта
// функция умирает целиком, а не переезжает.
AIBehaviour untyped_squad_behaviour(const MacroStore& st, MacroHandle h,
                                     const ecs::NPCKind& kind) {
    if (st.orders[h.slot].waypointCount > 0) return AIBehaviour::Waypoints;
    if (const DesignCharacterDef* row =
            design_character(st.designTag[h.slot].ordinal)) {
        return row->behaviour;
    }
    return kNpcTypeDefs[kind.type].ai;
}

// ПОВЕДЕНИЕ НЕТИПИЗИРОВАННОГО СКВАДА — МОДУЛЬ None-СТРОКИ kAiModuleRows
// (ломоть 4 M-72): лестница untyped_squad_behaviour и её switch по
// AIBehaviour живут ВНУТРИ модуля; снаружи диспетч знает одну ось SquadType.
// ПОЧЕМУ У ДЕСЯТИ `ai_*` НИЖЕ СТОИТ `[[maybe_unused]] ecs::Pools& pools`, А
// НЕ СНЯТЫЙ ПАРАМЕТР (2026-10-09, вместе со сносом `-Wno-unused-parameter`).
// Это СЕМЕЙСТВО С ЕДИНОЙ ФОРМОЙ ВЫЗОВА: один `switch` по `AIBehaviour`, и
// каждая ветвь зовёт свою функцию ОДНОЙ И ТОЙ ЖЕ пятёркой аргументов.
// Снять параметр у тех десяти, кто пулы не читает, значит сделать подписи
// семейства рваными — ветвь начнёт знать, какой именно вид её callee, то
// есть ровно то знание, ради избавления от которого диспетчер и написан.
// Поэтому глушитель здесь ЛОКАЛЬНЫЙ и НАЗВАННЫЙ, а глобального одеяла
// (`-Wno-unused-parameter` на весь проект) больше нет: новый мёртвый
// параметр в любом другом месте дерева теперь красит сборку.
// Уйдёт это вместе с модулем: эпик ИИ переписывает здесь всё (M-235/M-242),
// и чинить форму семейства ДО его переписки значит работать на снос (§5 п.14).
void ai_untyped(MacroHandle e, ecs::MacroNpcRuntime& rt,
                ecs::Pools& pools, const TickContext& ctx) {
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    // Преамбула рефлексов — та же и в том же порядке (M-307, см. ai_artel).
    // Гейта мира у этого модуля нет и не было: его зовёт тот же свип, что
    // уже проверил store и gs на входе.
    scent_squad_deposit(e, kind, ctx);
    if (squad_threat_step(e, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, kind, rt, pools, ctx)) return;
    switch (untyped_squad_behaviour(store_ctx(ctx), e, kind)) {
        // ПОСЛЕДНИЕ ДВЕ МАКРО-РОЛИ, ЕЩЁ ЖИВУЩИЕ В КАТАЛОГЕ ТЕЛ (CANON S2):
        // `Gatherer` на строке Peasant и `Trader` на строке Merchant. Они
        // здесь не потому, что нетипизированный сквад должен добывать, а
        // потому что их несут СТРОКИ, и снести значение раньше строки нельзя.
        // Нетипизированный крестьянин — ДОМОСЕД, и теперь это сказано
        // прямо: прежде он проваливался в добытчика до gatherer_def_of (нет
        // поручения → home_wanderer), но поручений больше нет — «что
        // добыть» решает МОДУЛЬ Artel по своей цели (ломоть 4 M-72), а
        // думать добычей нетипизированному не с чего. Обе роли уйдут вместе
        // со своими строками (порция Б-6).
        case AIBehaviour::Gatherer:  ai_home_wanderer(e, rt, pools, ctx); break;
        // `TaxRun` ОСТАЛСЯ БЕЗ МАКРО-СМЫСЛА 2026-09-22 (сборщик стал ТИПОМ
        // сквада). Значение ещё несёт строка TaxCollector каталога тел,
        // поэтому ветка обязана существовать — но ведёт она туда же, куда
        // ведёт всякая роль без занятия. Умрёт вместе со строкой (Б-6).
        case AIBehaviour::TaxRun:       ai_wanderer  (e, rt, pools, ctx); break;
        case AIBehaviour::Trader:       ai_trader    (e, rt, pools, ctx); break;
        case AIBehaviour::Aggressive:   ai_aggressive(e, rt, pools, ctx); break;
        case AIBehaviour::Teleporter:   ai_teleporter(e, rt, pools, ctx); break;
        case AIBehaviour::Wanderer:     ai_wanderer  (e, rt, pools, ctx); break;
        // Prey. Running is not its own errand: the threat step above already
        // makes ANY row run from what it cannot beat (squad_power), so what
        // this column says about an untroubled day is "it roams" — the same
        // walk a wanderer takes. The difference between a fox and a rabbit is
        // what happens when something appears, and that is decided above.
        case AIBehaviour::Flee:         ai_wanderer  (e, rt, pools, ctx); break;
        case AIBehaviour::Waypoints:    ai_waypoints (e, rt, pools, ctx); break;
        case AIBehaviour::MageHunt:     ai_mage_hunt (e, rt, pools, ctx); break;
        // (`LairSorties` УМЕР 2026-11-02, ломоть 6 M-72: вылеты из логова —
        //  МОДУЛЬ рода Dragon (ai_dragon), лестница None их не раздаёт.)
        case AIBehaviour::Count:        break;
    }
}

}  // namespace sm
