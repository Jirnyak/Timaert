// THE behaviour column — one vocabulary for every living thing in the world.
//
// This used to be two enums that could never meet: `AIBehaviour` for humanoids
// (npc.h) and `FaunaAi` for creatures (fauna.h). Two enums meant two spawn
// paths, two stance mappings and, underneath them, the assumption that a man
// and a beast are different KINDS of thing. They are not (CANON.md S16, owner
// 2026-08-20): a lord may be a dragon, a demon or a goblin, so what a row DOES
// is one column with one set of values.
//
// Each value names one function in npc_ai.cpp for the macro layer and folds to
// one subworld stance through `subworld_ai_for` (sub/spawn.h) — the law being
// that a row which raids on the map raids on the ground.
#pragma once
#include <cstdint>

namespace sm {

enum class AIBehaviour : std::uint8_t {
    // ONE loop for every gathering profession (owner: a profession per
    // resource, rows not code): find the worksite the profession's row names
    // (forest cell / home field / home deposit), work it through the
    // resource-field registry, haul the commodity home. The per-profession
    // nuance is a kGathererDefs row (npc_ai.cpp); with no worksite or no wired
    // layer the man falls back to the home wander, fail closed.
    Gatherer = 0, Trader, Nomad,
    Aggressive, Patrol, Teleporter, Wanderer,
    // The city's trading agent (W2b): remembers the home market at departure
    // (AgentMemory MarketSnapshot), carries exports to the city's villages in
    // its OWN bag and hauls back what the snapshot says the city LACKS. Falls
    // back to the old nomad wander when the world has no villages.
    CaravanTrade,
    // Follows the waypoint route in the squad's SquadOrders (Session 15, Inc
    // 7). No type row uses it and no label names it: the dispatcher selects it
    // whenever a squad CARRIES a route — the route's presence is the order
    // (owner's ruling), not a second behaviour knob.
    Waypoints,
    // Runs from what it cannot fight. Came in with the creature rows (the old
    // `FaunaAi::Flee`): a rabbit's whole behaviour, and the honest column for
    // any row that is prey rather than a fighter.
    Flee,
    Count,
};

// Which rows FIGHT when threatened — the behaviour column read as a stance.
// ONE answer for both layers: the subworld combat stance (sub/spawn.h
// `subworld_ai_for`) and the macro pursue decision (npc_ai.cpp threat step)
// both delegate here.
inline constexpr bool combatant_behaviour(AIBehaviour ai) {
    return ai == AIBehaviour::Aggressive || ai == AIBehaviour::Patrol;
}

} // namespace sm
