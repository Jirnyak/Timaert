// Quest engine — objective evaluation + reward dispatch. Mirrors quest-engine.ts.
#pragma once
#include <vector>
#include "events/quests/quest_types.h"
#include "events/event_bus.h"
#include "macro/state.h"

namespace sm {

class QuestEngine {
public:
    // Evaluate every active quest objective against last-tick events + player.
    // Marks objectives complete, completes quests, dispatches rewards.
    // `bag` is the container rewards are paid into and delivery objectives are
    // checked against — the player's bag is an ordinary NpcInventory on his
    // squad entity now (macro/player_entity.h), so the engine is handed the
    // container instead of reaching into PlayerState for one. Null = no world
    // yet: nothing is paid and no delivery can complete.
    void tick(std::vector<Quest>& active,
              EventBus& bus,
              GameState& gs,
              Inventory* bag);

    void accept(std::vector<Quest>& active,
                Quest q,
                const PlayerState& player,
                EventBus& bus);
    void abandon(std::vector<Quest>& active, const std::string& id, EventBus& bus);
    bool is_known(const std::vector<Quest>& active,
                  const PlayerState& player,
                  const std::string& id) const;
};

// Project active quests onto gs.markers as gold "!" quest pins — one per
// incomplete objective whose completion is anchored to a world cell (VisitCell,
// FindLocation, DeliverItems -> target settlement, WaitAt, InteractCell; a
// DestroyNpc "kill N" objective has no fixed cell and is skipped). Rebuilds the
// whole "quest_" marker set, so it is idempotent and also reconciles stale pins
// carried in from a loaded save. Universal: no per-quest special-casing — the
// cell resolver mirrors eval_objective() field-for-field.
void rebuild_quest_markers(GameState& gs, const std::vector<Quest>& active);

} // namespace sm
