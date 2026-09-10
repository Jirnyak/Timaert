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
// What has NOT moved yet: his sheet — PlayerState still owns it. WHERE he
// stands moved HERE with подпосадка 4 (2026-09-10, v88): his cell is the
// ordinary ecs::MacroCell on this entity, stepped by the input walker and
// glided by the one MacroVisual integrator, with no scalar copy anywhere.
// How hurt and how tired he is moved HERE with landing 4 (2026-09-10): his
// three bars are the ordinary ecs::Pools on this entity — player_pools()
// below is the one door.
#pragma once
#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/character_sheet.h"
#include "macro/entry_context.h"
#include "macro/spell_book_state.h"
#include "macro/state.h"

namespace sm {

// Ensure exactly one player squad + one macro PlayerTag flag exist. Called
// once at world boot and at the top of every macro (non-subworld) tick: it
// creates the squad on first call (spawn cell derived from the world — the
// realm's first city), re-stamps the tags a snapshot restore cannot carry,
// and keeps the sheet's derivatives honest. It does NOT touch the squad's
// MacroCell — where he stands is the entity's own truth (подпосадка 4).
// Idempotent and cheap; never touches a live scene body (that lifecycle is
// owned by SubworldEngine).
void ensure_macro_player_entity(GameState& gs, ecs::World& world);

// (No reattach_player_to_macro_spawn since v87. The flag is not re-derived
// after a load any more: PlayerTag rides the macro snapshot as an honest byte
// of the possessed record — restore_macro_ecs re-stamps it, and the load-path
// genesis raises no macro bodies at all. The re-derivation this function did
// was half of SAVE-5: it existed because "whom do I control" lived outside
// the snapshot, in PlayerState::possessedMacroSpawnId — also dead.)

// «КЕМ Я НА КАРТЕ» — the ONE holder of the macro PlayerTag flag: his own
// squad by default, a possessed lord while he wears one (подпосадка 4,
// owner 2026-09-10: «игрок это просто флажок для сквада, что на него инпут
// и камера»). Input drives THIS entity's cell; the camera may follow THIS
// entity's visual. Returns entt::null before the world exists. The view
// holds 0 or 1 entity — a scan of a ≤1 pool, no cache needed.
inline entt::entity player_flag_entity(ecs::World& world) {
    for (auto e : world.reg.view<ecs::PlayerTag>()) return e;
    return entt::null;
}

// The flag holder's cell and map-glide visual — the SAME components every
// squad keeps (MacroCell = the one number that is his position's truth,
// MacroVisual = what the eye sees between cells). nullptr before the world
// exists. These replaced the gs.player.x/y scalars: the last duplicate
// store of «where he stands» died with подпосадка 4 (v88). Inline like the
// components they read: macro passes ask them without linking the app-side
// creation door.
inline ecs::MacroCell* player_flag_cell(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<ecs::MacroCell>(e);
}
inline ecs::MacroVisual* player_flag_visual(ecs::World& world) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return nullptr;
    return world.reg.try_get<ecs::MacroVisual>(e);
}

// THE macro jump (escape teleport, console goto, subworld exit door): set
// the flag holder's cell and erase the entry edge (a jump is not a walk —
// SubworldEngine::enter must fall back to the centre). The VISUAL is left
// to the one glide integrator on purpose (owner 2026-09-10: «универсально
// без игрокового кода») — a short hop glides, a far jump snaps via the
// integrator's own teleport backstop.
inline void player_jump_to_cell(GameState& gs, ecs::World& world,
                                int x, int y) {
    const entt::entity e = player_flag_entity(world);
    if (e == entt::null) return;
    auto& reg = world.reg;
    reg.emplace_or_replace<ecs::MacroCell>(
        e, ecs::cell_index(x, y, gs.mapW));
    // A jump is not a walk: no entry edge for the next subworld enter, and
    // the think cadence restarts (the accumulator doubles as the player's
    // entry-tick clock — same kAiTicks law as every squad's think).
    if (auto* rt = reg.try_get<ecs::MacroNpcRuntime>(e)) {
        rt->entryDir = kEntryDirNone;
        rt->entryTicks = 0;
        rt->tickAccum = 0;
    }
}

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

// THE player's three bars — the ordinary ecs::Pools on his squad entity, the
// very block every lord and every scene body keeps (landing 4, owner
// 2026-09-09/10: «полосы на тело, никакого особенного игрока»). It was
// PlayerState::combatStats: a nine-field private store with three cached rest
// rates, projected onto this block every tick and saved TWICE. Returns
// nullptr before the world exists; there are no bars to read then.
ecs::Pools* player_pools(ecs::World& world);
const ecs::Pools* player_pools(const ecs::World& world);

// THE player's spellbook — the ordinary SpellBook component on his squad
// entity, the block every macro body is born with (§41 root 3, v89). It was
// PlayerState::spellBook: a one-copy store that made casting, sustained
// drains and spire-teaching player-only mechanics. Same family as
// player_pools/player_inventory: HIS OWN squad by the reserved ordinal —
// body-native casting through a possessed body's book arrives the day NPC
// casting does. nullptr before the world exists.
SpellBook* player_spellbook(ecs::World& world);
const SpellBook* player_spellbook(const ecs::World& world);

// «His sheet changed» — the ONE call every such moment makes (creation,
// level-up, point spend, learning, gear on/off, console): ceilings and march
// caches follow the EFFECTIVE sheet through THE door every lord's do
// (squad.h refresh_body_from_sheet), each bar preserving its fraction
// («доля у всех», owner 2026-09-10). No-op before the world exists.
void refresh_player_body(PlayerState& player, ecs::World& world);

AgentMemory* player_head(ecs::World& world);
const AgentMemory* player_head(const ecs::World& world);

// EVERYTHING STANDING ON THE PLAYER, summed once. His sheet is the character
// he built; what acts in the world is that character PLUS what he is wearing
// (ecs::BodyEquipment on his squad entity) and what is currently burning on
// him (sustained spells). Both are rows of the one bonus registry, so both are
// the same sum — and because the sum is read through a modified COPY
// (`effective_sheet`), taking the coat off or letting the spell lapse is
// simply not adding it next time.
BonusTotals player_standing_bonuses(ecs::World& world,
                                    const PlayerState& player);

// The sheet the world should actually ask about him — THE door (phase 4,
// owner 2026-09-06): «финальный лист после всех источников — прокачка,
// врождённое, предмет — и его везде использует». Every reader of his numbers
// (bars, damage, march, carry, prices, XP, UI) walks through here; writes
// (creation, level-up, learning) go to the BASE sheet, never to this copy.
CharacterSheet player_effective_sheet(ecs::World& world,
                                      const PlayerState& player);

} // namespace sm
