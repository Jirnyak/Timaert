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
    void tick(std::vector<Quest>& active,
              EventBus& bus,
              GameState& gs);

    void accept(std::vector<Quest>& active,
                Quest q,
                const PlayerState& player,
                EventBus& bus);
    void abandon(std::vector<Quest>& active, const std::string& id, EventBus& bus);
    bool is_known(const std::vector<Quest>& active,
                  const PlayerState& player,
                  const std::string& id) const;
};

} // namespace sm
