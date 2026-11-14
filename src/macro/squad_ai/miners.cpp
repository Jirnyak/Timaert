// ИИ-МОДУЛЬ СКВАДА РОДА Miners — шахтёры: живая жила округи → выемка →
// всё добытое дому. Строка `kAiModuleRows`; окно —
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

// ═════ МОДУЛЬ Miners — шахтёры ══════════════════════════════════════════════
// Что даёт жила, решает фича шахты на её клетке: товар шахтёров — вопрос к
// миру о клетке цели, а не слово модуля.
constexpr std::uint8_t kMinersAct = action_slot("mine_vein");
static_assert(archetype_of(action_def(int(kMinersAct)).type)
                      == ActionArchetype::Labour
                  && action_def(int(kMinersAct)).spRatePerDay > 0,
              "акт шахтёров — трудовая строка с темповой ценой");

void miners_think(AiSelf& s, const AiView& view, AiPult& pult) {
    static const int kFood = commodity_item_index(hunger_commodity_ordinal());
    const std::uint32_t home = view.home(s.h);
    const std::uint16_t homeSlot = view.home_slot(s.h);
    if (home == ecs::kNoCell || homeSlot == kMacroNoSlot) return;
    ecs::SquadGoal& g = s.goal;
    const auto go_home = [&]() {
        ecs::goal_set(g, home, g.what, 0, std::uint8_t(NS::Idle));
        s.rt.state = std::uint8_t(NS::Returning);
    };
    // КЕМ МОГУТ СТАТЬ ШАХТЁРЫ: только неопределённой артелью — когда живой
    // жилы в округе нет или дом ценит её металл по полу. Память зануляется.
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
        const std::uint32_t site = view.vein_near(home);
        const int product = site != ecs::kNoCell
            ? view.yield_at(ResourceFieldId::Vein, site) : -1;
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
        // ОБЪЁМ АКТА — по объекту на рабочего юнита; в долг артель не трудится.
        const int workers = work_units(s.bag);
        const std::int64_t cycleCost = pult.act_cost(s, kMinersAct, workers);
        if (std::int64_t(s.pools.sp) < cycleCost) { go_home(); return; }
        const int product = view.yield_at(ResourceFieldId::Vein, g.cell);
        const int got = pult.gather(s, ResourceFieldId::Vein, kMinersAct,
                                    g.cell, workers);
        // Жила, выработанная до нуля, — факт мира; лес и пашня отрастают
        // своим законом и факта не пишут. Судится по остатку после акта.
        // Число факта жилы — ЧТО выработано: строка реестра поля + 1.
        if (got > 0 && view.field_read(ResourceFieldId::Vein, g.cell) == 0) {
            pult.fact(int(view.rooted_id(homeSlot)), FactKind::Drained,
                      g.cell, int(ResourceFieldId::Vein) + 1);
        }
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

void ai_miners(MacroHandle e, ecs::MacroNpcRuntime& rt,
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
    miners_think(self, view, pult);
}

}  // namespace sm
