// Built-in system event nodes. Mirrors node-registry.ts.
//
// These are wired into the event_bus by `register_builtin_nodes`. Each
// node observes an event topic and may emit follow-up events / mutate
// GameState. Add a new system behaviour = one entry here.
#pragma once

namespace sm {

struct GameState;
class EventBus;

void register_builtin_nodes(EventBus& bus, GameState& gs);

} // namespace sm
