// Apply pure-data effects from event stream to player state.
#pragma once
#include <span>
#include "events/event_bus.h"
#include "events/quests/quest_types.h"
#include "macro/state.h"

namespace sm {

// Takes the whole GameState, not just the player: a ReputationChange moves the
// player's row in the ONE relation matrix (gs.factions), which is where his
// standing lives now — there is no reputation map on PlayerState to write to.
void apply_events(std::span<const GameEvent> events, GameState& gs);
void apply_events(const std::vector<GameEvent>& events, GameState& gs);

} // namespace sm
