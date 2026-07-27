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

} // namespace sm
