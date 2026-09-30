#include "macro/macro_snapshot.h"

#include <algorithm>

#include "macro/state.h"
#include "macro/store.h"

namespace sm {

std::vector<MacroNpcRecord> snapshot_macro_ecs(const MacroStore& st) {
    std::vector<MacroNpcRecord> out;
    // 6.3 (M-106 1е): население — живые слоты store, голый цикл по alive.
    // Формат записи НЕ двигается — ординал был и остался идентичностью,
    // сортировка ниже прежняя.
    for (std::size_t s32 = 0; s32 < kMacroEntityCap; ++s32) {
        const std::uint16_t slot = std::uint16_t(s32);
        if (st.alive[slot] == 0) continue;
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
        m.name      = st.name[slot];
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
        // «Кем я управляю» — колонка анкеты (5б): провод несёт флаг ДАРОМ
        // вместе с записью; кэши GameState пересоберёт резолв загрузки.
        m.playerFlag = st.playerFlag[slot].on ? 1 : 0;
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
                       MacroStore& st, GameState& gs) {
    std::uint32_t maxOrdinal = 0;
    bool any = false;
    for (const MacroNpcRecord& m : records) {
        const MacroHandle h = store_birth(st);
        if (!st.valid(h)) break;   // отказ капа уже прозвучал вслух
        st.spawnId[h.slot]   = m.spawnId;
        st.cell[h.slot]      = m.cell;
        st.visual[h.slot]    = m.visual;
        st.kind[h.slot]      = m.kind;
        st.pools[h.slot]     = m.pools;
        st.level[h.slot]     = m.level;
        st.runtime[h.slot]   = m.runtime;
        st.traits[h.slot]    = m.traits;
        st.character[h.slot] = m.character;
        st.name[h.slot]      = m.name;
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
        // Флажок игрока — колонкой записи (5б); кэши GameState (биты)
        // пересобирает resolve_player_handles_after_load ПОСЛЕ этого
        // восстановления — сканом колонок, один раз на загрузке (SAVE-5
        // закрыт тем же законом: генезис на загрузке не гоняется, второй
        // сквад игрока не рождается).
        st.playerFlag[h.slot] = PlayerFlag{m.playerFlag};
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
