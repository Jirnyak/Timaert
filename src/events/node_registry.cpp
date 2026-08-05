#include "events/node_registry.h"
#include "events/event_types.h"
#include "events/logic_nodes.h"
#include <memory>
#include <string>
#include <utility>

namespace sm {

namespace {

void add_continue_choice(GameEvent& dialog, const char* label) {
    dialog.dialogChoices = std::make_shared<std::vector<DialogChoicePayload>>();
    dialog.dialogChoices->push_back(DialogChoicePayload{label ? label : "Continue", {}, {}});
    dialog.ix = static_cast<int>(dialog.dialogChoices->size());
}

const GameEvent* find_settlement_enter_event(const EventBus& bus) {
    for (const auto& ev : bus.last_tick_events()) {
        if (ev.tag == EventTag::SettlementVisit
            || ev.tag == EventTag::PlayerEnterSettlement) {
            return &ev;
        }
    }
    return nullptr;
}

// An "enc_random" node used to sit here: every 15 walked cells it rolled a
// 1-12% die (from its OWN hardcoded-seed RNG, world-independent) and, on a
// hit, popped a UNIFORMLY RANDOM row of content::encounters() — no biome, no
// danger zone, no faction territory, no time of day, no player state. Removed
// by owner ruling (2026-08-05): events must arise from GAME CONTEXT and
// STATE, never from an unconditional random roll over a list — that trigger
// was the old disease, not a system. The encounter TABLE (content/plot/
// encounters.cpp) and the whole ShowDialog-with-choices → effect_applicator
// path stay: they are the presentation half a future context-driven trigger
// (work_vector №3) will drive.

// A "sys_level_up" node used to sit here, waiting on EventTag::PlayerLevelUp to
// pop a "Level Up!" dialog. Nothing in the project ever emitted that event —
// all four award_exp call sites (effect_applicator, quest_engine, the subworld
// kill path, the addexp console command) take a LevelData and no bus — so the
// node could not fire, and the player has never seen the dialog. Removed rather
// than left as furniture (owner's call, 2026-08-05); the event tag stays in
// event_types.h for whoever wires levelling feedback up for real.

LogicNode settlement_dialog_node() {
    LogicNode n;
    n.id = "sys_settlement";
    n.label = "Settlement Greeting";
    ConditionSlot c;
    c.isEvent = false;
    c.check = [](const EventBus& bus, const PlayerState&) {
        return find_settlement_enter_event(bus) != nullptr;
    };
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(n.id);
    n.tags.push_back("system");
    n.effect = [](NodeContext& ctx) {
        std::string name = "Unknown";
        if (const GameEvent* ev = find_settlement_enter_event(*ctx.bus)) {
            if (!ev->s1.empty()) name = ev->s1;
        }

        GameEvent dialog{EventTag::ShowDialog};
        dialog.s1 = "Welcome to " + name;
        dialog.s2 = "The gates open before you. Merchants hawk their wares and guards patrol the walls.";
        add_continue_choice(dialog, "Enter");
        ctx.bus->emit(dialog);
    };
    return n;
}

} // namespace

void register_builtin_nodes(LogicNodeEngine& logic) {
    logic.add(settlement_dialog_node());
    logic.activate("sys_settlement");
}

} // namespace sm
