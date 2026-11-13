// ИИ-МОДУЛЬ СКВАДА РОДА Artel — неопределённая артель: стоит дома и раз
// в сутки решает, кем стать. Строка `kAiModuleRows`; окно —
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

// ═════ МОДУЛЬ Artel — неопределённая артель ═════════════════════════════════
// Артель без дела: стоит дома и раз в сутки решает, кем стать (владелец: «как
// нейтрино осцилляции артель может менять свой тип на другой»; сюда же ляжет
// «ходить торговать» и прочее третье — модуль расширяется строками условий).
//
// КЕМ МОЖЕТ СТАТЬ И КОГДА — прописано здесь: пахарями, охотниками, льноводами,
// лесорубами или шахтёрами — тем типом, чей груз одного выхода дом ценит выше
// всего, при трёх условиях: место работы этого типа есть в округе; дом ценит
// его товар выше пола; живой артели этого типа у дома ещё нет. Ни один тип не
// проходит — стоит дома до следующего круга.
//
// ПАМЯТЬ МОДУЛЯ: слово 0 — сколько думок до следующего круга решения.
enum : int { kArtelMemWait, kArtelMemCount };
static_assert(kArtelMemCount <= ecs::kModuleMemSlots,
              "слова неопределённой артели помещаются в память модуля");

void artel_think(AiSelf& s, const AiView& view, AiPult& pult) {
    const std::uint32_t home = view.home(s.h);
    const std::uint16_t homeSlot = view.home_slot(s.h);
    if (home == ecs::kNoCell || homeSlot == kMacroNoSlot) return;
    ecs::SquadGoal& g = s.goal;
    if (s.rt.state != std::uint8_t(NS::Idle)) return;   // в пути — везут ноги
    if (s.cell.idx != home) {
        ecs::goal_set(g, home, 0, 0, std::uint8_t(NS::Idle));
        s.rt.state = std::uint8_t(NS::Returning);
        return;
    }
    if (ecs::march_arrived(s.rt)) {
        // ДОМА: отдать дому всё добытое — сделкой, как всякий сквад.
        for (int c = 0; c < kCommodityCount; ++c) {
            const int item = commodity_item_index(c);
            const int n = s.bag.count_of(item);
            if (n > 0) pult.hand(s.h.slot, homeSlot, item, n);
        }
    }
    // (Харча неопределённая артель не ждёт особо: она и так стоит дома, и
    //  дом грузит её своим суточным кругом, как всякого ребёнка у порога.)
    // КРУГ РЕШЕНИЯ — РАЗ В СУТКИ: слово убывает на думку.
    std::uint32_t& wait = s.rt.mem[kArtelMemWait];
    if (wait > 0) {
        wait = sub_sat(wait, std::uint32_t(1));   // угасание счёта ожидания
        return;
    }
    wait = kAiThinksPerDay;

    // ГРУЗ ОДНОГО ВЫХОДА, КАКИМ ЕГО ОЦЕНИТ ДОМ: сколько единиц товара вместит
    // спина, той же оценкой запаса, какой меряет сделка.
    const std::int64_t backG = carry_capacity_g(s.rt, s.bag);
    SquadType best = SquadType::Artel;
    std::int64_t bestGain = 0;
    const auto consider = [&](SquadType t, ResourceFieldId row,
                              std::uint32_t site) {
        if (site == ecs::kNoCell) return;               // работы нет
        const int product = view.yield_at(row, site);
        if (product < 0) return;
        const RowSide homeRow = view.row_of(homeSlot, product);
        if (worth_arriving(homeRow, 1) <= 1) return;    // дом завален
        if (view.has_sibling(s.h, t)) return;           // такая у дома есть
        const std::int64_t load = std::max<std::int64_t>(
            1, backG / std::max<std::int64_t>(
                   1, unit_mass_g(item_def_at(product))));
        const std::int64_t gain = lot_take(homeRow, load);
        if (gain > bestGain) {
            bestGain = gain;
            best = t;
        }
    };
    consider(SquadType::Ploughmen, ResourceFieldId::Wheat,
             view.field_near(FT_Field, home, home));
    consider(SquadType::Hunters, ResourceFieldId::Fauna,
             view.fauna_near(home, home));
    consider(SquadType::Flaxers, ResourceFieldId::Wheat,
             view.field_near(FT_FlaxField, home, home));
    consider(SquadType::Woodcutters, ResourceFieldId::Trees,
             view.tree_near(home, home));
    consider(SquadType::Miners, ResourceFieldId::Vein, view.vein_near(home));
    if (best == SquadType::Artel) return;   // никем — стою до нового круга
    // СМЕНА ТИПА: память зануляется, новый модуль начнёт с чистого Idle дома.
    s.rt.squadType = std::uint8_t(best);
    for (std::uint32_t& w : s.rt.mem) w = 0;
    ecs::goal_drop(g);
    s.rt.state = std::uint8_t(NS::Idle);
}

}  // namespace

void ai_artel(MacroHandle e, ecs::MacroNpcRuntime& rt,
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
    artel_think(self, view, pult);
}

}  // namespace sm
