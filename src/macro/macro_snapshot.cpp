#include "macro/macro_snapshot.h"

#include <algorithm>

#include "macro/state.h"

namespace sm {

std::vector<MacroNpcRecord> snapshot_macro_ecs(ecs::World& w) {
    std::vector<MacroNpcRecord> out;
    auto& reg = w.reg;
    // The view names every component make_npc — the ONE creation door —
    // emplaces. The player's own squad matches it and is saved BY it: since
    // the merge (2026-08-27) he is an ordinary squad carrying an ordinary
    // roster and bag, told apart only by his reserved ordinal and his tag.
    // Tags are not snapshot state — ensure_macro_player_entity re-stamps
    // PlayerSquadTag every time it is walked through, load included.
    auto view = reg.view<ecs::MacroSpawnId, ecs::Position, ecs::VisualPos,
                         ecs::NPCKind, ecs::Health, ecs::NpcLevel,
                         ecs::MacroNpcRuntime, ecs::NpcTraits,
                         ecs::NpcCharacter, ecs::NpcInventory,
                         ecs::SquadRoster>();
    for (auto e : view) {
        MacroNpcRecord m{};
        m.spawnId   = view.get<ecs::MacroSpawnId>(e);
        m.pos       = view.get<ecs::Position>(e);
        m.visual    = view.get<ecs::VisualPos>(e);
        m.kind      = view.get<ecs::NPCKind>(e);
        m.health    = view.get<ecs::Health>(e);
        m.level     = view.get<ecs::NpcLevel>(e);
        m.runtime   = view.get<ecs::MacroNpcRuntime>(e);
        m.traits    = view.get<ecs::NpcTraits>(e);
        m.character = view.get<ecs::NpcCharacter>(e);
        m.inventory = view.get<ecs::NpcInventory>(e).inv;
        m.roster    = view.get<ecs::SquadRoster>(e).squad;
        if (const auto* orders = reg.try_get<ecs::SquadOrders>(e)) {
            m.orders = *orders;
            m.hasOrders = 1;
        }
        if (const auto* mem = reg.try_get<AgentMemory>(e)) m.memory = *mem;
        if (const auto* eq = reg.try_get<ecs::BodyEquipment>(e)) m.gear = eq->gear;
        m.dead = reg.all_of<ecs::Dead>(e) ? 1 : 0;
        out.push_back(std::move(m));
    }
    // Registry iteration order is an implementation detail; the ordinal is
    // the identity. Sorting makes one world state one byte stream.
    std::sort(out.begin(), out.end(),
              [](const MacroNpcRecord& a, const MacroNpcRecord& b) {
                  return a.spawnId.index < b.spawnId.index;
              });
    return out;
}

void restore_macro_ecs(const std::vector<MacroNpcRecord>& records,
                       ecs::World& w, GameState& gs) {
    auto& reg = w.reg;
    std::uint32_t maxOrdinal = 0;
    bool any = false;
    for (const MacroNpcRecord& m : records) {
        auto e = reg.create();
        reg.emplace<ecs::MacroSpawnId>(e, m.spawnId);
        reg.emplace<ecs::Position>(e, m.pos);
        reg.emplace<ecs::VisualPos>(e, m.visual);
        reg.emplace<ecs::NPCKind>(e, m.kind);
        reg.emplace<ecs::Health>(e, m.health);
        reg.emplace<ecs::NpcLevel>(e, m.level);
        reg.emplace<ecs::MacroNpcRuntime>(e, m.runtime);
        reg.emplace<ecs::NpcTraits>(e, m.traits);
        reg.emplace<ecs::NpcCharacter>(e, m.character);
        reg.emplace<ecs::NpcInventory>(e, ecs::NpcInventory{m.inventory});
        reg.emplace<ecs::SquadRoster>(e, ecs::SquadRoster{m.roster});
        if (m.hasOrders) reg.emplace<ecs::SquadOrders>(e, m.orders);
        reg.emplace<AgentMemory>(e, m.memory);
        // Only a body that WORE something gets the container back: the opt-in
        // is part of the contract, not an accident of the load order.
        if (worn_cells(m.gear) > 0) {
            reg.emplace<ecs::BodyEquipment>(e, ecs::BodyEquipment{m.gear});
        }
        if (m.dead) reg.emplace<ecs::Dead>(e);
        if (!any || m.spawnId.index > maxOrdinal) maxOrdinal = m.spawnId.index;
        any = true;
    }
    // The counter must sit ABOVE every living ordinal or a future runtime
    // spawn would reissue an identity (problems.md 19.24). The save carries
    // the counter; this is the self-heal for any drift.
    if (any && gs.nextMacroSpawnOrdinal <= maxOrdinal)
        gs.nextMacroSpawnOrdinal = maxOrdinal + 1u;
}

} // namespace sm
