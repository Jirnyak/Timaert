// The macro-ECS snapshot (Session 17) — the save's view of the living map.
//
// The save is a full snapshot of the MACRO world and of nothing else
// (AGENTS.md → Persistence). Until this header the macro ECS was the one part
// of that world the save could not see: every load cleared the registry and
// re-spawned lords from the seed, so a killed squad rose again, a levelled
// leader forgot his campaigns, and a runtime ordinal could be reissued to a
// stranger (problems.md 19.24).
//
// A MacroNpcRecord is ONE macro entity flattened to rows: the POD components
// verbatim, the roster as its SoldierRecord rows, the opt-ins (orders, death,
// the player flag) as explicit flags. PlayerTag rides HONESTLY (owner verdict
// 2026-09-10: «сейв честно хранит снимок всего мира… и потом честно просто
// смотрится у кого флажок игрок»): it used to be re-derived after restore
// from PlayerState::possessedMacroSpawnId — a second store of "who is
// controlled" outside the snapshot — while the load-path genesis raised a
// SECOND player squad the doors then pointed at, ghosting the restored one
// (postdemoaudit SAVE-5). PlayerSquadTag is NOT stored: it is the reserved
// ordinal spelled as a tag, so restore re-derives it from spawnId.
#pragma once
#include <cstdint>
#include <vector>

#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/agent_memory.h"
#include "macro/character_sheet.h"
#include "macro/spell_book_state.h"

namespace sm {

struct GameState;

struct MacroNpcRecord {
    ecs::MacroSpawnId    spawnId{};
    ecs::MacroCell       cell{};
    ecs::MacroVisual       visual{};
    ecs::NPCKind         kind{};
    ecs::Pools           pools{};
    ecs::NpcLevel        level{};
    ecs::MacroNpcRuntime runtime{};
    ecs::NpcTraits       traits{};
    ecs::NpcCharacter    character{};
    // The body's KNOWLEDGE of the spell registry (§41 root 3, v89): two
    // 256-bit planes + the active ordinal — a component like the pools,
    // born all-zero with every squad and ridden verbatim.
    SpellBook            book{};
    ecs::SquadOrders     orders{};          // meaningful iff hasOrders
    AgentMemory          memory{};          // what the leader remembers (v28)
    // The OWNED sheet of a NAMED character (v90, owner 2026-09-10 ММОРПГ-
    // модель: «у каждого персистентного персонажа свой лист и идёт в
    // сейв»). Opt-in like orders: a transient crew derives its generic
    // sheet from its row and writes nothing — a stored copy of a
    // derivable sheet would be a second truth.
    CharacterSheet       sheet{};           // meaningful iff hasSheet
    std::uint8_t         hasSheet = 0;
    std::uint8_t         hasOrders = 0;
    std::uint8_t         dead = 0;
    // Ординал строки стола анкет (v92, macro/characters.h) — −1 у всякого
    // обычного сквада. Едет байтами, восстанавливается тегом: смерть
    // навсегда держится именно этим — генезис на загрузке не гоняется, и
    // погибшая анкета в снапшоте просто отсутствует.
    std::int16_t         designOrdinal = -1;
    // «Кем я управляю» — ecs::PlayerTag as one honest byte (v87). At most one
    // record of a save carries 1: the player's own squad, or a possessed lord.
    std::uint8_t         playerFlag = 0;
    Inventory            inventory;         // NpcInventory.inv
    // What this body WEARS (ecs::BodyEquipment). Opt-in on the entity, so a
    // record whose `anatomy` cells are all empty simply writes a zero count —
    // the crowd costs four bytes each and the gear rides whole for the few
    // bodies that have any.
    Equipment            gear;
    SoldierSquad roster;                    // SquadRoster.squad (no leader)
};

// Flatten every persistent macro NPC (the view is keyed by MacroSpawnId — the
// component only make_npc emplaces) into records, sorted by ordinal so the
// payload bytes are deterministic for one same world state.
std::vector<MacroNpcRecord> snapshot_macro_ecs(ecs::World& w);

// Re-embody the records in an (already cleared of macro NPCs) registry.
// Also self-heals gs.nextMacroSpawnOrdinal to stay ABOVE every restored
// ordinal — the counter must never reissue a living identity.
void restore_macro_ecs(const std::vector<MacroNpcRecord>& records,
                       ecs::World& w, GameState& gs);

} // namespace sm
