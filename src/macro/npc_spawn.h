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
#include <array>
#include <cstdint>
#include <vector>
#include "ecs/world.h"
#include "macro/npc.h"
#include "macro/state.h"
#include "macro/map_generator.h"

namespace sm {

// `deposits` (optional): the village-context professions — a village whose
// hinterland holds a live vein raises the matching gatherer (miner /
// quarryman / clay-digger); specialisation is CONTEXT, never a village
// type (owner ruling, R2). nullptr = no mining professions spawn.
struct DepositLayer;
void spawn_macro_npcs(GameState& gs, ecs::World& w,
                      const TerrainData& terrain, std::uint32_t seed,
                      const DepositLayer* deposits = nullptr);

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

// ── Squad creation as DATA (Session 15, Inc 7) ────────────────────────────
//
// One spec, one door: whoever wants a squad on the map — the console, the
// player's patrol order, a future macro-sim raiser of deserter bands —
// states WHAT it wants and this function makes it through make_npc (the ONE
// creation path: ordinal, sheet-derived hp, bag, traits, roster, all of it)
// plus the roster rows and an optional waypoint route. The route's presence
// IS the order (owner's ruling); a new KIND of squad AI is a type row with
// its own ai column, never a field here.
struct SquadSpec {
    NPCType leaderType  = NPCType::Peasant;
    int     leaderLevel = -1;          // -1 = the row's default + roll
    int     x = 0, y = 0;              // macro cell (wrapped, nudged to land)
    int     factionIndex = -1;         // -1 = the land decides (politik)
    int     homeSettlementId = -1;
    std::vector<SoldierRecord> members;   // roster rows, caller-authored
    std::uint8_t waypointCount = 0;
    std::array<std::int16_t, 16> waypoints{};   // 8 × (x, y)
};

// Returns the leader entity (the squad IS its leader), entt::null on a bad
// map. Runtime ordinals continue past the current maximum — the same rule
// (and the same known reuse hole, problems.md 19.24) as spawn_npc_at.
entt::entity spawn_squad(GameState& gs, ecs::World& w,
                         const TerrainData& terrain, const SquadSpec& spec);

} // namespace sm
