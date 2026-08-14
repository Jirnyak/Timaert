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
//
// `followups`: events this application decided to ANNOUNCE (a SpireDepleted
// teaches its spell and announces SpellLearned for the observers — quests and
// logic nodes watch the fact, not the spire). The applicator never emits
// directly: the caller owns the bus and the emit-after-the-span-walk order
// (emitting mid-walk grows the vector under the live span). Null = the caller
// has no bus (tests) and the announcements are dropped.
void apply_events(std::span<const GameEvent> events, GameState& gs,
                  std::vector<GameEvent>* followups = nullptr);
void apply_events(const std::vector<GameEvent>& events, GameState& gs,
                  std::vector<GameEvent>* followups = nullptr);

} // namespace sm
