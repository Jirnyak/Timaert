// Macro player entity (macro-4a) — the player's movable PlayerTag "flag" as a
// persistent macro-side ECS entity.
//
// The player is "an NPC with a flag": the flag is `ecs::PlayerTag`, and for the
// possession command (§8 Increment 5) to MOVE it between entities it must live
// on a real entity on BOTH sides of the macro<->subworld seam. In a subworld the
// flag rides a full combat actor built by `SubworldEngine::spawn_player_entity`;
// on the macro map this projection is its home.
//
// The macro flag is deliberately MINIMAL — `Position` + `PlayerTag` only — so it
// stays invisible to render / proximity / AI (which key off `NPCKind` +
// `MacroNpcRuntime`) and to the subworld reapers (which key off `SubworldTag`).
// `gs.player` remains the authoritative scalar; the entity's `Position` is a
// one-way projection of it, so nothing here changes the save format.
#pragma once
#include "ecs/world.h"
#include "macro/state.h"

namespace sm {

// Ensure exactly one macro PlayerTag flag exists and its Position mirrors the
// authoritative player scalar (`gs.player.x/y`). Called once at world boot and
// at the top of every macro (non-subworld) tick: it creates the flag on first
// call, re-creates it after a subworld leave (which tears down all PlayerTag
// entities), and otherwise re-syncs its Position. Idempotent and cheap — the
// PlayerTag view holds 0 or 1 entity. It never touches a live subworld combat
// flag (that lifecycle is owned by SubworldEngine).
void ensure_macro_player_entity(GameState& gs, ecs::World& world);

// Inc 5e-2 (possession persistence). Re-attach the player flag to the macro NPC
// identified by the save-stable ordinal `id` (its `ecs::MacroSpawnId`) after a
// load has regenerated the macro NPCs from `worldSeed`. Moves the single
// PlayerTag flag onto that NPC — destroying the freshly-created hero husk, never
// a real macro NPC — and sets its Position to the loaded player scalar
// (`px`,`py`). Returns false, changing nothing (so the caller keeps the ordinary
// hero husk), when `id < 0` or no live macro NPC carries that ordinal (it died
// before the save, or the seed changed). Call AFTER ensure_macro_player_entity at
// boot, so a husk already exists to hand the flag over from.
bool reattach_player_to_macro_spawn(ecs::World& world, int id, float px, float py);

// THE player's squad entity, by its reserved ordinal — and his ROSTER, which
// is an ordinary ecs::SquadRoster on it (owner, 2026-08-27). It used to be
// `PlayerState::army`, a squad of its own kind sitting beside the entity, and
// every consumer of it was a player-specific path. Returns null / nullptr
// before the world exists; callers treat that as "no men", which is what an
// absent squad means.
entt::entity player_squad_entity(ecs::World& world);
SoldierSquad* player_roster(ecs::World& world);
const SoldierSquad* player_roster(const ecs::World& world);

// …and his BAG, which is the ordinary ecs::NpcInventory every macro body
// carries. It was `PlayerState::inventory`: the last large field that made the
// player a different kind of thing from the squads around him.
Inventory* player_inventory(ecs::World& world);
const Inventory* player_inventory(const ecs::World& world);

} // namespace sm
