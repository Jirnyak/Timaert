// Macro NPC spawning — faithful port of `spawnNPCs()` from `src/game/npc.ts`.
//
// Per-settlement: 2-4 peasants, 1-2 woodcutters, optional merchant
// (60 % chance), 1-2 guards. Plus global pools sized off settlement
// count: caravans (30 %), bandits (30 % + 2), witches (10 %),
// sorceresses (5 %). Plus per-village peasant gatherers + woodcutters.
//
// Spawned entities carry the gameplay-visible NPC data used by the native
// proximity UI: health, level, inventory, traits, and visual identity.
#pragma once
#include <cstdint>
#include "ecs/world.h"
#include "macro/state.h"
#include "macro/map_generator.h"

namespace sm {

void spawn_macro_npcs(GameState& gs, ecs::World& w,
                      const TerrainData& terrain, std::uint32_t seed);

// Spawn ONE macro NPC of the named registry type near macro cell (x, y) —
// the consumer half of the SpawnEntity event (quest onAccept is the producer:
// s1 = type token, ix/iy = cell, a = level). Token resolves case-insensitively
// against kNpcTypeDefs labels (npc_type_from_label); unknown token spawns
// nothing and returns false. Aggressive types join "bandits"; civil types take
// the faction of the land they stand on. Level > 0 pins the NPC's level.
bool spawn_npc_at(GameState& gs, ecs::World& w, const TerrainData& terrain,
                  const char* typeToken, int x, int y, int level);

// NOTE: idx→faction-id lookups live in macro/faction.h (faction_id_for_index)
// — ONE registry, one index space for humanoids and monsters alike.

} // namespace sm
