// Built-in system event nodes. Mirrors node-registry.ts.
//
// Nodes consume last-tick events through LogicNodeEngine and emit follow-up
// events. Player-state mutation stays in effect_applicator.
#pragma once

namespace sm {

class LogicNodeEngine;

void register_builtin_nodes(LogicNodeEngine& logic);

} // namespace sm
