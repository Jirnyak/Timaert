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

} // namespace

void register_builtin_nodes(LogicNodeEngine& logic) {
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
