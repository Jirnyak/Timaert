// ИИ-МОДУЛЬ СКВАДА РОДА Dragon — вылеты из логова за добычей слабее
// себя. Строка `kAiModuleRows`; окно — `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <cstdint>

#include "bus/session.h"      // cell_is_leased — изъятие по клетке
#include "bus/store.h"        // handle_at
#include "core/torus.h"
#include "macro/auto_battle.h"
#include "macro/squad.h"      // settle_auto_battle, macro_dead

namespace sm {

namespace {

// ── МОДУЛЬ РОДА Dragon — вылеты из логова (ломоть 6 M-72) ────────────────
// Дом — КЛЕТКА (lair, гора — не ландмарк). В вылете бьёт любой сквад
// СЛАБЕЕ себя (слово владельца) — той же дверью встречи, что threat step;
// наевшись или улетев за поводок — домой. Игрокова встреча — форс-двери
// игрока, как у всех. До 2026-11-02 это была модель AIBehaviour::LairSorties
// под родом None (лестница стола анкет) — второй ответ на «как он действует»
// рядом с осью рода; теперь думку раздаёт ОДНА таблица kAiModuleRows.
MacroHandle nearest_weaker_squad(MacroHandle self, int sightCells,
                                 const TickContext& ctx) {
    const SquadIndex& g = *ctx.squads;
    const CellBuckets& b = g.grid;
    if (b.cols <= 0 || b.rows <= 0) return MacroHandle{};
    const MacroStore& st = store_ctx(ctx);
    const float myPower = squad_power(auto_battle_side_of(st, self));
    const std::uint32_t at = st.cell[self.slot].idx;
    const int cx0 = cell_x(at, ctx.mapW) / b.cellSize;
    const int cy0 = cell_y(at, ctx.mapW) / b.cellSize;
    int best = sightCells * kStepStraight + 1;
    MacroHandle found{};
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            const int gx = wrapi(cx0 + ox, b.cols);
            const int gy = wrapi(cy0 + oy, b.rows);
            for (const std::uint32_t* it = b.cell_begin(gx, gy),
                                    * end = b.cell_end(gx, gy);
                 it != end; ++it) {
                const SquadWalkEntry& sw = g.order[*it];
                if (sw.slot == self.slot) continue;
                if (st.dead[sw.slot] != 0) continue;
                const auto& oc = st.cell[sw.slot];
                const int d = cell_dist(at, oc.idx, ctx.mapW);
                if (d >= best) continue;
                if (squad_power(auto_battle_side_of(
                        st, handle_at(st, sw.slot))) >= myPower) {
                    continue;   // добыча — только слабее
                }
                best = d;
                found = handle_at(st, sw.slot);
            }
        }
    }
    return found;
}

// Поводок — ВНУТРИ модуля (вердикт владельца 2026-10-10, дословно
// «5) внутри модуля»; список НЕ-ЕДИНОГО называет его прямо): не колонка
// сквада и не агенда строки — у агенды радиуса был второй носитель того же
// числа. Значение — БАЛАНС: прежняя авторская агенда драконов стояла 10.
constexpr int kDragonLeashCells = 10;

}  // namespace

void ai_dragon(MacroHandle self, ecs::MacroNpcRuntime& rt, ecs::Pools& pools,
               const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    // Преамбула рефлексов — та же и в том же порядке, что у четырёх
    // думающих модулей (M-307, см. ai_artel). Повтор — НОРМА (DOD п.5).
    const ecs::NPCKind& kind = store_ctx(ctx).kind[self.slot];
    scent_squad_deposit(self, kind, ctx);
    if (squad_threat_step(self, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(self, kind, rt, pools, ctx)) return;
    // Логово самозалечивается: тело без дома объявляет домом место, где
    // проснулось (спавн-дверь анкеты пишет честную клетку раньше).
    const std::uint32_t at = store_ctx(ctx).cell[self.slot].idx;
    if (dragon_lair(rt) == ecs::kNoCell) dragon_set_lair(rt, at);
    const std::uint32_t lair = dragon_lair(rt);
    // ПОВОДОК — в клетках пути от логова, дверью над индексами.
    const bool inLeash =
        cell_within(at, lair, kDragonLeashCells, ctx.mapW);

    if (ctx.squads && ctx.mw.store && ctx.mw.gs && inLeash) {
        const MacroHandle prey =
            nearest_weaker_squad(self, kDragonLeashCells, ctx);
        MacroStore& st = store_ctx(ctx);
        if (st.valid(prey)) {
            if (st.cell[self.slot].idx == st.cell[prey.slot].idx) {
                if (prey.slot == ctx.playerFlagSlot) return;   // дом ушёл с `.home`
                if (cell_is_leased(ctx.mw.session, st.cell[self.slot].idx,
                                   ctx.mapW)) return;
                const AutoBattleOutcome o = resolve_auto_battle(
                    auto_battle_side_of(st, self),
                    auto_battle_side_of(st, prey),
                    Ambush::SideA, *ctx.rng);
                settle_auto_battle(ctx.mw, self, prey, o);
                if (!macro_dead(st, self)) {
                    // После боя — стоять: погоня отпущена, колонка пуста.
                    chase_release(st.goal[self.slot]);
                    rt.state = std::uint8_t(NS::Idle);
                    charge_squad_recovery(rt,
                        std::uint32_t((5 + rand_int(ctx, 8)) * kAiTicks));
                }
                return;
            }
            // Добыча ДВИЖЕТСЯ — род цели «сквад»: адрес каждую думку
            // пересчитывают ноги свипа. Настигнув, остаётся Chasing.
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
    // Улетел за радиус или добычи нет и стоит не дома — домой.
    // «ДОМА» — КЛЕТКА ЛОГОВА (M-461): ноги сажают в клетку цели, и вопрос
    // модуля — тот же, равенством индексов. Здесь стоял порог `fromLair <
    // 2.0f` — повтор порога ног, без которого севший за клетку до логова
    // числился «не дома» каждую думку.
    const bool atLair = at == lair;
    if (!inLeash
        || (rt.state != std::uint8_t(NS::Wandering) && !atLair)) {
        ecs::goal_set(g, lair, 0, 0, std::uint8_t(NS::Idle));
        rt.state = std::uint8_t(NS::Returning);
        return;
    }
    // Дома и сыт: кружи по округе логова.
    if (rt.state == std::uint8_t(NS::Idle)) {
        // Сел (прибытие назвали ноги) — постоял. Какое плечо кончилось,
        // говорит сама цель: её адрес прибытием не гасится. Домой — короткая
        // передышка, с круга — долгая (числа прежние, обе авторские).
        if (ecs::march_arrived(rt)) {
            charge_squad_recovery(rt,
                g.cell == lair
                    ? std::uint32_t((3 + rand_int(ctx, 5)) * kAiTicks)
                    : std::uint32_t((10 + rand_int(ctx, 20)) * kAiTicks));
        }
        if (!squad_recovering(rt)) {
            ecs::goal_set(g,
                          cell_nearby(lair, kDragonLeashCells, ctx),
                          0, 0, std::uint8_t(NS::Idle));
            rt.state = std::uint8_t(NS::Wandering);
        }
        return;
    }
    if (rt.state == std::uint8_t(NS::Wandering)) return;   // везут ноги
    // Состояние, которого дракон не ведёт (Chasing без добычи, Fleeing):
    // встать и перерешить. Погоня без добычи кончилась — колонка пуста
    // (`chase_release`, M-468); у бегства «чего ищу» и так ноль.
    chase_release(g);
    rt.state = std::uint8_t(NS::Idle);
    charge_squad_recovery(rt,
        std::uint32_t((3 + rand_int(ctx, 5)) * kAiTicks));
}

}  // namespace sm
