// Apply pure-data effects from event stream to player state.
#pragma once
#include "events/event_bus.h"
#include "macro/state.h"

namespace sm {

void apply_events(const std::vector<GameEvent>& events, PlayerState& p);

} // namespace sm
