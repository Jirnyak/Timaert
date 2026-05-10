#include "events/node_registry.h"
#include "events/event_types.h"
#include "events/logic_nodes.h"
#include <utility>

namespace sm {

namespace {

LogicNode codex_unlock_node(const char* id,
                            const char* label,
                            EventTag trigger,
                            const char* codexId) {
    LogicNode n;
    n.id = id;
    n.label = label;
    ConditionSlot c;
    c.isEvent = true;
    c.tag = trigger;
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(id);
    n.tags.push_back("system");
    n.effect = [codexId](NodeContext& ctx) {
        GameEvent ev{EventTag::CodexUnlock};
        ev.s1 = codexId;
        ctx.bus->emit(ev);
    };
    return n;
}

LogicNode level_up_dialog_node() {
    LogicNode n;
    n.id = "sys_level_up";
    n.label = "Level Up";
    ConditionSlot c;
    c.isEvent = true;
    c.tag = EventTag::PlayerLevelUp;
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(n.id);
    n.tags.push_back("system");
    n.effect = [](NodeContext& ctx) {
        int level = 0;
        for (const auto& ev : ctx.bus->last_tick_events()) {
            if (ev.tag == EventTag::PlayerLevelUp) {
                level = ev.ix;
                break;
            }
        }

        GameEvent dialog{EventTag::ShowDialog};
        dialog.s1 = "Level Up!";
        dialog.s2 = "You have reached level " + std::to_string(level)
                  + "! Your abilities grow stronger.";
        dialog.ix = 1;
        ctx.bus->emit(dialog);
    };
    return n;
}

} // namespace

void register_builtin_nodes(LogicNodeEngine& logic) {
    logic.add(level_up_dialog_node());

    logic.add(codex_unlock_node("sys_settlement_codex",
        "Settlement Codex Unlock",
        EventTag::SettlementVisit,
        "settlements"));

    logic.add(codex_unlock_node("sys_people_codex",
        "People Codex Unlock",
        EventTag::NpcGreeted,
        "people"));
}

} // namespace sm
