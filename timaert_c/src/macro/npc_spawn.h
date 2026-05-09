// Macro NPC spawning — faithful port of `spawnNPCs()` from `src/game/npc.ts`.
//
// Per-settlement: 2-4 peasants, 1-2 woodcutters, optional merchant
// (60 % chance), 1-2 guards. Plus global pools sized off settlement
// count: caravans (30 %), bandits (30 % + 2), witches (10 %),
// sorceresses (5 %). Plus per-village peasant gatherers + woodcutters.
//
// Inventory / army / character generation is **deferred** — the AI tick
// only needs Position + NPCKind + MacroNpcRuntime. Spawning more data is
// safe to add later without changing this signature.
#pragma once
#include <cstdint>
#include "ecs/world.h"
#include "macro/state.h"
#include "macro/map_generator.h"

namespace sm {

void spawn_macro_npcs(GameState& gs, ecs::World& w,
                      const TerrainData& terrain, std::uint32_t seed);

// Reverse of the internal `faction_idx()` mapping. Returns a stable
// faction-id string ("empire" / "magika" / "timaert" / "bandits" /
// "cults") for an NPC's `factionIdx`. Falls back to "empire" for
// unknown indices. Public so UI panels can look up the faction in
// `gs.factions` for display name + color.
const char* faction_id_for_idx(std::uint16_t idx);

} // namespace sm
