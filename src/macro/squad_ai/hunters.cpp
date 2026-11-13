// ИИ-МОДУЛЬ СКВАДА РОДА Hunters — охотники: богатая зверем клетка у дома
// → охота → всё добытое дому. Строка `kAiModuleRows`; окно —
// `macro/squad_ai/module_window.h`.

#include "macro/squad_ai/module_window.h"

#include <algorithm>
#include <cstdint>

#include "bus/anketa.h"            // inventory_mass_g, Inventory
#include "macro/deal.h"            // unit_mass_g
#include "macro/econ_day.h"        // hunger_commodity_ordinal
#include "macro/economy.h"         // gain_units, worth_arriving
#include "macro/squad.h"           // carry_capacity_g
#include "tables/actions.h"        // action_slot — строка акта модуля
#include "tables/commodity.h"
#include "tables/items.h"          // item_def_at

namespace sm {

namespace {

// ═════ МОДУЛЬ Hunters — охотники ════════════════════════════════════════════
constexpr std::uint8_t kHuntersAct = action_slot("hunt_fauna");
static_assert(archetype_of(action_def(int(kHuntersAct)).type)
                      == ActionArchetype::Labour
                  && action_def(int(kHuntersAct)).spRatePerDay > 0,
              "акт охотников — трудовая строка с темповой ценой");

void hunters_think(AiSelf& s, const AiView& view, AiPult& pult) {
    static const int kFood = commodity_item_index(hunger_commodity_ordinal());
    const std::uint32_t home = view.home(s.h);
    const std::uint16_t homeSlot = view.home_slot(s.h);
    if (home == ecs::kNoCell || homeSlot == kMacroNoSlot) return;
    ecs::SquadGoal& g = s.goal;
    const auto go_home = [&]() {
        ecs::goal_set(g, home, g.what, 0, std::uint8_t(NS::Idle));
        s.rt.state = std::uint8_t(NS::Returning);
    };
    // КЕМ МОГУТ СТАТЬ ОХОТНИКИ: только неопределённой артелью — когда зверя у
    // дома нет или дом ценит дичь по полу. Память зануляется.
    const auto become_artel = [&]() {
        s.rt.squadType = std::uint8_t(SquadType::Artel);
        for (std::uint32_t& w : s.rt.mem) w = 0;
        ecs::goal_drop(g);
        s.rt.state = std::uint8_t(NS::Idle);
    };
    const auto back_full = [&](int product) {
        return product >= 0 && s.bag.count_of(product) > 0
            && carry_capacity_g(s.rt, s.bag) - inventory_mass_g(s.bag)
                   < unit_mass_g(item_def_at(product));
    };

    if (s.rt.state == std::uint8_t(NS::Idle)) {
        const bool atHome = s.cell.idx == home;
        if (atHome && ecs::march_arrived(s.rt)) {
            // ДОМА: отдать дому всё добытое — сделкой, как всякий сквад.
            for (int c = 0; c < kCommodityCount; ++c) {
                const int item = commodity_item_index(c);
                const int n = s.bag.count_of(item);
                if (n > 0) pult.hand(s.h.slot, homeSlot, item, n);
            }
        }
        if (atHome) {
            const RowSide mine = view.row_of(s.h.slot, kFood);
            if (mine.net < 0
                && gain_units(view.row_of(homeSlot, kFood), mine, -mine.net,
                              true, 0, 1) > 0) {
                return;   // жду погрузки харча
            }
        }
        const std::uint32_t site = view.fauna_near(s.cell.idx, home);
        const int product = site != ecs::kNoCell
            ? view.yield_at(ResourceFieldId::Fauna, site) : -1;
        if (!atHome) {
            // В поле: полная спина или пустая округа — сперва домой.
            if (back_full(product) || product < 0) go_home();
            else {
                ecs::goal_set(g, site,
                              ecs::goal_what(ecs::GoalKind::Cell, 0), 0,
                              std::uint8_t(NS::Working));
                s.rt.state = std::uint8_t(NS::Traveling);
            }
            return;
        }
        // ДОМА: работы нет или дом завален моим товаром — меняю тип.
        if (product < 0
            || worth_arriving(view.row_of(homeSlot, product), 1) <= 1) {
            become_artel();
            return;
        }
        // БЮДЖЕТ ЦИКЛОВ — сколько циклов вместит спина при нынешних руках.
        const std::int64_t perCycleG = std::max<std::int64_t>(
            1, std::int64_t(work_units(s.bag))
                   * unit_mass_g(item_def_at(product)));
        const std::int64_t freeG =
            carry_capacity_g(s.rt, s.bag) - inventory_mass_g(s.bag);
        ecs::goal_set(g, site, ecs::goal_what(ecs::GoalKind::Cell, 0),
                      std::uint16_t(std::clamp<std::int64_t>(
                          freeG / perCycleG, 1, 0xFFFF)),
                      std::uint8_t(NS::Working));
        s.rt.state = std::uint8_t(NS::Traveling);
        return;
    }
    if (s.rt.state == std::uint8_t(NS::Working)) {
        if (squad_recovering(s.rt)) return;   // висит долг прошлого цикла
        // ОБЪЁМ АКТА — по зверю на рабочего юнита; в долг артель не трудится.
        const int workers = work_units(s.bag);
        const std::int64_t cycleCost = pult.act_cost(s, kHuntersAct, workers);
        if (std::int64_t(s.pools.sp) < cycleCost) { go_home(); return; }
        const int product = view.yield_at(ResourceFieldId::Fauna, g.cell);
        const int got = pult.gather(s, ResourceFieldId::Fauna, kHuntersAct,
                                    g.cell, workers);
        if (got > 0 && !back_full(product)
            && std::int64_t(s.pools.sp) >= cycleCost && ecs::goal_spend(g)) {
            return;
        }
        go_home();
        return;
    }
    // Traveling/Returning: к цели везут ноги свипа.
}

}  // namespace

void ai_hunters(MacroHandle e, ecs::MacroNpcRuntime& rt,
                ecs::Pools& pools, const TickContext& ctx) {
    if (!ctx.mw.store || !ctx.mw.gs) return;
    // Рефлексы — выбор модуля: след, угроза, охота по запаху.
    const ecs::NPCKind& kind = store_ctx(ctx).kind[e.slot];
    scent_squad_deposit(e, kind, ctx);
    if (squad_threat_step(e, kind, rt, pools, ctx)) return;
    if (scent_hunt_step(e, kind, rt, pools, ctx)) return;
    AiSelf self{e, store_ctx(ctx).cell[e.slot], rt, pools,
                store_ctx(ctx).goal[e.slot],
                store_ctx(ctx).inventory[e.slot].inv,
                store_ctx(ctx).sheet[e.slot]};
    const AiView view(ctx);
    AiPult pult(ctx);
    hunters_think(self, view, pult);
}

}  // namespace sm
