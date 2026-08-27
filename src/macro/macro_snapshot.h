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
// verbatim, the roster as its SoldierRecord rows, the two opt-ins (orders,
// death) as explicit flags. PlayerTag is deliberately absent — the flag's
// identity already persists as PlayerState::possessedMacroSpawnId and is
// re-attached by ordinal after restore, the same door possession always used.
#pragma once
#include <cstdint>
#include <vector>

#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/agent_memory.h"

namespace sm {

struct GameState;

struct MacroNpcRecord {
    ecs::MacroSpawnId    spawnId{};
    ecs::Position        pos{};
    ecs::VisualPos       visual{};
    ecs::NPCKind         kind{};
    ecs::Health          health{};
    ecs::NpcLevel        level{};
    ecs::MacroNpcRuntime runtime{};
    ecs::NpcTraits       traits{};
    ecs::NpcCharacter    character{};
    ecs::SquadOrders     orders{};          // meaningful iff hasOrders
    AgentMemory          memory{};          // what the leader remembers (v28)
    std::uint8_t         hasOrders = 0;
    std::uint8_t         dead = 0;
    Inventory            inventory;         // NpcInventory.inv
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
