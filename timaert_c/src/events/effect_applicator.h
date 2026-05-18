// Apply pure-data effects from event stream to player state.
#pragma once
#include <span>
#include "events/event_bus.h"
#include "events/quests/quest_types.h"
#include "macro/state.h"

namespace sm {

void apply_events(std::span<const GameEvent> events, PlayerState& p);
void apply_events(const std::vector<GameEvent>& events, PlayerState& p);

} // namespace sm
