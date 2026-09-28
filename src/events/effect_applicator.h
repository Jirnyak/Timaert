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
// `bag` is the container an effect pays into (a gold reward, a fine): the
// player's bag is an ordinary NpcInventory on his squad entity now, so the
// applicator is handed the container rather than reaching into PlayerState.
// Null = no world yet; a paying effect simply does not pay.
// `book` — the player's SpellBook component (v89), handed in exactly like
// `bag`: a spell-learn effect writes knowledge into the body that owns it,
// and there is no PlayerState field to write to any more.
// Null = no world yet; a learn effect simply does not land.
//
// `pools` and `sheet` used to ride here too, for the string-verb arm of
// EventTag::ApplyEffect. Both the tag and the arm were deleted 2026-09-28
// (M-116) — nothing in the world emitted that tag — so the parameters went
// with their only reader rather than staying as furniture (DOD п.9).
void apply_events(std::span<const GameEvent> events, GameState& gs,
                  Inventory* bag, SpellBook* book,
                  std::vector<GameEvent>* followups = nullptr);
void apply_events(const std::vector<GameEvent>& events, GameState& gs,
                  Inventory* bag, SpellBook* book,
                  std::vector<GameEvent>* followups = nullptr);

} // namespace sm
