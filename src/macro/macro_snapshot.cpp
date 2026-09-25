#include "macro/macro_snapshot.h"

#include <algorithm>

#include "macro/state.h"
#include "macro/store.h"

namespace sm {

std::vector<MacroNpcRecord> snapshot_macro_ecs(ecs::World& w) {
    std::vector<MacroNpcRecord> out;
    auto& reg = w.reg;
    // ФЛИП 1в (M-106): состояние лежит колонками store; entt даёт только
    // мост MacroSlot и теги. Формат записи НЕ двигается — ординал был и
    // остался идентичностью, сортировка ниже прежняя.
    MacroStore& st = store_of(w);
    for (auto e : reg.view<ecs::MacroSlot>()) {
        const std::uint16_t slot = reg.get<ecs::MacroSlot>(e).slot;
        MacroNpcRecord m{};
        m.spawnId   = st.spawnId[slot];
        m.cell      = st.cell[slot];
        m.visual    = st.visual[slot];
        m.kind      = st.kind[slot];
        m.pools     = st.pools[slot];
        m.level     = st.level[slot];
        m.runtime   = st.runtime[slot];
        m.traits    = st.traits[slot];
        m.character = st.character[slot];
        m.book      = st.spellBook[slot];
        m.inventory = st.inventory[slot].inv;
        {
            const ecs::SquadRoster& ro = st.roster[slot];
            // Счёт едет вместе с ростером, которому он выставлен (v105);
            // сами существа — в m.inventory (единый контейнер, M-71).
            for (int c = 0; c < kCommodityCount; ++c)
                m.rosterNeedDebt[c] = ro.needDebt[c];
            m.rosterWageDebt = ro.wageDebt;
        }
        // Колонки у всех (закон гладкой памяти): предикаты формата прежние —
        // «есть приказ» = waypointCount > 0, «есть лист» = у всех с флипа.
        if (st.orders[slot].waypointCount > 0) {
            m.orders = st.orders[slot];
            m.hasOrders = 1;
        }
        m.sheet = st.sheet[slot];
        m.hasSheet = 1;
        m.designOrdinal = st.designTag[slot].ordinal;
        m.memory = st.memory[slot];
        m.gear = st.gear[slot].gear;
        m.dead = st.dead[slot] ? 1 : 0;
        // The flag rides the snapshot honestly (v87) — restore re-stamps it,
        // no re-derivation from a second store.
        m.playerFlag = reg.all_of<ecs::PlayerTag>(e) ? 1 : 0;
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
    MacroStore& st = store_of(w);
    for (const MacroNpcRecord& m : records) {
        const MacroHandle h = store_birth(st);
        if (!st.valid(h)) break;   // отказ капа уже прозвучал вслух
        auto e = reg.create();
        reg.emplace<ecs::MacroSlot>(e, h.slot);
        st.spawnId[h.slot]   = m.spawnId;
        st.cell[h.slot]      = m.cell;
        st.visual[h.slot]    = m.visual;
        st.kind[h.slot]      = m.kind;
        st.pools[h.slot]     = m.pools;
        st.level[h.slot]     = m.level;
        st.runtime[h.slot]   = m.runtime;
        st.traits[h.slot]    = m.traits;
        st.character[h.slot] = m.character;
        st.spellBook[h.slot] = m.book;
        st.inventory[h.slot] = ecs::NpcInventory{m.inventory};
        {
            ecs::SquadRoster ro{};
            for (int c = 0; c < kCommodityCount; ++c)
                ro.needDebt[c] = m.rosterNeedDebt[c];
            ro.wageDebt = m.rosterWageDebt;
            st.roster[h.slot] = ro;
        }
        if (m.hasOrders) st.orders[h.slot] = m.orders;
        if (m.hasSheet) st.sheet[h.slot] = m.sheet;
        st.designTag[h.slot] = ecs::DesignCharacterTag{m.designOrdinal};
        st.memory[h.slot] = m.memory;
        st.gear[h.slot] = ecs::BodyEquipment{m.gear};
        if (m.dead) st.dead[h.slot] = 1;
        // The two player marks, by their two sources of truth (CANON S2/S4):
        // PlayerSquadTag = the reserved ordinal spelled as a tag (derived),
        // PlayerTag = the honest byte the save carries (owner 2026-09-10 —
        // «честно просто смотрится у кого флажок игрок»). Before this the
        // load-path genesis raised a second player squad and every door kept
        // pointing at it, ghosting the restored one (SAVE-5).
        if (m.spawnId.index == ecs::kPlayerSquadOrdinal)
            reg.emplace<ecs::PlayerSquadTag>(e);
        if (m.playerFlag) reg.emplace<ecs::PlayerTag>(e);
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
