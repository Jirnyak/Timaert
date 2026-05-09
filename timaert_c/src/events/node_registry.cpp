#include "events/node_registry.h"
#include "events/event_bus.h"
#include "events/event_types.h"
#include "macro/state.h"

namespace sm {

void register_builtin_nodes(EventBus& bus, GameState& gs) {
    // Level-up roll-over: when XP overflows, advance levels deterministically.
    bus.on(EventTag::PlayerLevelUp, [&gs](const GameEvent&) {
        auto& ld = gs.player.levelData;
        while (ld.exp >= ld.expToNext) {
            ld.exp     -= ld.expToNext;
            ld.level   += 1;
            ld.expToNext = exp_to_next_level(ld.level);
        }
    });

    // First-visit codex unlocks.
    bus.on(EventTag::SettlementVisit, [&gs](const GameEvent&) {
        for (const auto& c : gs.player.codexUnlocked)
            if (c == "settlements") return;
        gs.player.codexUnlocked.emplace_back("settlements");
    });
    bus.on(EventTag::NpcGreeted, [&gs](const GameEvent&) {
        for (const auto& c : gs.player.codexUnlocked)
            if (c == "people") return;
        gs.player.codexUnlocked.emplace_back("people");
    });

    // Quest completion → small reputation bump with quest-giver faction (s2).
    bus.on(EventTag::QuestCompleted, [&gs](const GameEvent& e) {
        if (!e.s2.empty()) gs.player.reputation[e.s2] += 5;
    });
}

} // namespace sm
