// Apply pure-data effects from event stream to player state.
#pragma once
#include <span>
#include "events/event_bus.h"
#include "events/quests/quest_types.h"
#include "macro/state.h"

namespace sm {

void apply_events(std::span<const GameEvent> events, PlayerState& p);
void apply_events(const std::vector<GameEvent>& events, PlayerState& p);
bool queue_player_level_up_if_needed(EventBus& bus,
                                     std::span<const GameEvent> appliedEvents,
                                     const LevelData& before,
                                     const LevelData& after);

} // namespace sm
