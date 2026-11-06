// Apply pure-data effects from event stream to player state.
#pragma once
#include <span>
#include "events/event_bus.h"
#include "macro/state.h"

namespace sm {

// `followups`: events this application decided to ANNOUNCE (a SpireDepleted
// teaches its spell and announces SpellLearned for the observers). The
// applicator never emits directly: the caller owns the bus and the
// emit-after-the-span-walk order (emitting mid-walk grows the vector under
// the live span). Null = the caller has no bus (tests) and the
// announcements are dropped.
// `book` — the player's SpellBook component (v89): a spell-learn effect
// writes knowledge into the body that owns it. Null = no world yet; a learn
// effect simply does not land.
//
// `pools`/`sheet` (ApplyEffect arm, M-116) and `bag` (PlayerGoldChange arm,
// M-410) used to ride here; each parameter left with its only reader rather
// than staying as furniture (DOD п.9). GameState остаётся: журнал ленты
// сессии пишет сюда.
void apply_events(std::span<const GameEvent> events, GameState& gs,
                  SpellBook* book,
                  std::vector<GameEvent>* followups = nullptr);
void apply_events(const std::vector<GameEvent>& events, GameState& gs,
                  SpellBook* book,
                  std::vector<GameEvent>* followups = nullptr);

} // namespace sm
