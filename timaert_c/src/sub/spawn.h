// Subworld spawn — populates the ECS with creatures from the per-cell
// fauna table. Mirrors `subworld/spawn.ts` populator: each visible cell
// rolls its own table once per cell entry, scaled by the world tile area.
#pragma once
#include <cstdint>
#include <vector>
#include "ecs/world.h"
#include "sub/seamless_manager.h"
#include "sub/fauna.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/army.h"

namespace sm::sub {

// ── Per-cell population (seamless persistence) ───────────────────────────
//
// Populate ONE cell of the 3×3 window — the offset (ox,oy) ∈ {-1,0,1}² from the
// window centre — with its own world creatures: fauna rolled from THAT cell's
// resolved table plus, if it is a settlement, its citizens. Faction / AI /
// colour / radius all come from the FaunaEntry — the engine stays creature-
// agnostic. Everything lands in the cell's sub-region
// [ (ox+1)*kCellSize .. +kCellSize )².
//
// This does NOT clear first: the engine orchestrates clear / rebase / despawn
// (see the helpers below) so that content shared between the old and new 3×3
// windows survives a seam crossing untouched — only cells that actually leave
// the window are evicted, and only cells newly brought in are spawned. That is
// the fix for "a city vanishes when you step one cell out of it".
//
// Fauna is fully deterministic from `cellSeed` (the cell's ABSOLUTE macro seed
// from resolve_context): re-entering a cell reproduces the same procedural set,
// which is where the future per-macro-cell "visitation age" counter plugs in —
// mix that epoch into `cellSeed` and re-entries vary controllably with zero
// per-entity storage. Settlement citizens are re-derived from the persistent
// macro context, so cities are preserved across re-entry by construction.
//
// `landmarkPop` is settlement population (0 if none) and feeds the √(pop/100)
// level bonus from `subworld/spawn.ts::deriveContextScale`. `zoneLevel` is the
// macro zones difficulty (0..9); zones >2 add (z-2) levels and 1+(z-2)*0.18
// hp/damage multipliers.
void spawn_cell_npcs(ecs::World& w,
                     Biome biome,
                     FeatureType feature,
                     LandmarkKind landmark,
                     const SeamlessSubworldManager& mgr,
                     int ox,
                     int oy,
                     std::uint32_t cellSeed,
                     int landmarkPop = 0,
                     int zoneLevel   = 0);

// Destroy every world-owned subworld creature (fauna + citizens), preserving the
// player-side projections (PlayerTag / PlayerSoldierTag) that follow the player
// across re-centres rather than belonging to a cell. Used for a clean slate on
// enter / leave; the per-cell path above avoids it on ordinary seam crossings.
void clear_subworld_world_entities(ecs::World& w);

// Shift every SubworldTag entity's Position + VisualPos by (dxTiles,dyTiles).
// Applied on a seam re-centre with (-dx*kCellSize, -dy*kCellSize) so that fixed
// physical content — and the player's own squad — track the recentred composite
// window instead of drifting by one macro cell each crossing.
void rebase_subworld_entities(ecs::World& w, float dxTiles, float dyTiles);

// Destroy world-owned subworld creatures whose Position now lies outside the
// composite window [0,kFullSize)²; player-side projections are never evicted.
// Run after rebase on a re-centre to evict exactly the cells that left the 3×3.
void despawn_subworld_entities_outside_window(ecs::World& w);

// Project the player's macro squad into the current subworld as real ECS
// NPC entities. The macro SoldierSquad remains the persistent source of truth.
void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const SeamlessSubworldManager& mgr,
                        float playerX,
                        float playerY,
                        std::uint32_t seed);

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed);

// ── Possession / вселение (Inc 5c) ───────────────────────────────────────
//
// The player is one PlayerTag flag riding an ECS body (the "player is an NPC
// with a flag" model). Possession MOVES that single flag onto another live
// body so the player inhabits it; because every consumer — camera, input,
// incoming combat, minimap, and (on exit) the macro landing — was made
// flag-following in Inc 5a, they all follow with no per-consumer change.
//
// D2 (literal flag move) + D3 (body-native): the possessed body keeps its OWN
// Health / Combat / sheet — NO hero stats are stamped onto it, and gs.player is
// preserved untouched as the revert target. The engine's sync/reconcile become
// body-native by testing the ONE discriminator these functions maintain: the
// hero body (spawn_player_entity) carries no NPCKind, every possessable scene
// body does.
//
// current_player_body: the single entity currently carrying PlayerTag, or
// entt::null (never null mid-subworld — exactly one flag is always live).
entt::entity current_player_body(ecs::World& w);

// Move the player flag onto `target` (must be a live, positioned scene body).
// Removes PlayerTag from the current body; if that body was the hero husk (no
// NPCKind) it is destroyed — the hero's canonical state lives in gs.player, so
// nothing is lost and no inert, un-rendered, un-AI'd zombie is stranded in the
// scene. A vacated FOREIGN body keeps all its components and, with the flag
// gone, its AI / rendering / targetability resume automatically (every such
// path is PlayerTag-gated). Emplaces PlayerTag on `target`. No-op returning
// false if target is null / invalid / unpositioned / already the player. Pure
// ECS: the caller re-mirrors the position scalars from the new body afterwards.
bool possess_entity(ecs::World& w, entt::entity target);

} // namespace sm::sub
