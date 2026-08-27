// THE player's squad on the macro map — an ordinary squad entity carrying an
// ordinary squad's components, told from every other squad by two things and
// two things only: a reserved ordinal (`ecs::kPlayerSquadOrdinal`) and a mark
// (`ecs::PlayerSquadTag`).
//
// «ИГРОК = НПЦ» (CANON S4). The flag `ecs::PlayerTag` answers a DIFFERENT
// question — «кем я управляю сейчас» — and moves: onto a possessed lord, onto
// a body underground. That is why it cannot be the thing that identifies his
// party, and why the two tags are two components.
//
// This entity used to be a HUSK: `Position` + `PlayerTag`, recreated every
// macro tick, deliberately invisible to render / proximity / AI, while the
// real party lived beside it in PlayerState as a roster, a bag and a head of
// its own. Every consumer of those was a player-specific path — a second kind
// of squad with its own projection, its own battle side and its own casualty
// bookkeeping. The merge of 2026-08-27 collapsed them: the roster is
// `ecs::SquadRoster`, the bag `ecs::NpcInventory`, the head `AgentMemory`, and
// all three ride the same macro-snapshot record every lord's do.
//
// What has NOT moved yet: where he stands, how hurt he is, how tired, and his
// sheet. PlayerState still owns those; `ensure_macro_player_entity` projects
// them onto the entity on EVERY walk, so the copies cannot drift by more than
// a tick until they collapse too.
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

// What the player REMEMBERS: the ordinary AgentMemory on the same entity, the
// same component every squad leader carries. It sat on PlayerState as a second
// store until 2026-08-27; the macro record already saved the entity's copy, so
// the field was a duplicate the save wrote twice and nothing read back.
// The player's ONE signed fractional stamina carry — `MacroNpcRuntime::spCarry`
// on his squad entity, the very field every lord on the map keeps. It used to
// be two unsigned accumulators on App (a spend-only TravelStamina and a
// regen-only slot in PlayerRecoveryAccumulator), which between them could not
// even express the state his own bar was in: a debt with a fraction owed.
float* player_sp_carry(ecs::World& world);

AgentMemory* player_head(ecs::World& world);
const AgentMemory* player_head(const ecs::World& world);

} // namespace sm
