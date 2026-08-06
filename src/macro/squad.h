// Squad lifecycle helpers — the macro side of "THE macro entity is a squad"
// (macrosim.md, ecs::SquadRoster doctrine). The squad IS its leader entity;
// what lives here is what happens to the roster around the leader's own
// life and death. Header-only: pure ECS + army.h record moves, no engine,
// no renderer, so every layer (subworld leave, the coming auto-resolve,
// tests) settles squads through the same functions.
#pragma once

#include "ecs/world.h"
#include "macro/army.h"

namespace sm {

// Owner ruling 3 (macrosim.md): kill the leader and the squad lives on,
// FACELESS, until the fight ends — only then do the survivors stop being a
// squad and fall into the deserter pool, out of which the macro sim later
// raises deserter and bandit bands. "The fight ends" is the caller's word:
// the subworld says it on leave(), the auto-resolve will say it when its
// battle settles. Sweeps every squad whose leader is Dead but whose roster
// still holds members; a live leader's squad is never touched, and a swept
// roster is emptied so the pool can never be paid twice for the same men.
// Returns how many soldiers walked away.
inline int drain_dead_leader_squads(ecs::World& w, SoldierSquad& deserterPool) {
    int moved = 0;
    for (auto [e, roster] :
         w.reg.view<ecs::SquadRoster, ecs::Dead>().each()) {
        (void)e;
        if (roster.members.empty()) continue;
        add_soldiers(deserterPool, roster.members);
        moved += int(roster.members.size());
        roster.members.clear();
    }
    return moved;
}

} // namespace sm
