// ── РОЖДЕНИЕ МЕСТА = ТЕЛО В STORE, ОДНОЙ ДВЕРЬЮ (ломтик F) ───────────────
//
// Место есть неподвижный сквад: всякая основа города, деревни, шпиля, руины
// идёт СЮДА. Строки-индекса больше не существует — вся идентичность места
// лежит колонками его тела: ординал (spawnId, единый эмитент M-37), ось рода
// (runtime.squadType 1..8), имя (name), адрес (cell), фракция
// (kind.factionIdx — с ломтика F это ИСТИНА, зеркала в строке больше нет).
// Путь загрузки сюда не ходит: тела приезжают записями блока макро-сквадов
// целиком (relink строк умер вместе со строками).
//
// Дверь делает ДВЕ вещи — рождает тело и объявляет СОБЫТИЕ (navEpoch),
// по которому поднимается всё запечённое от состава (закон прежней
// add_landmark, перенесён дословно).
//
// Свой заголовок, а не npc_spawn: рождению тела места хватает header-only
// дверей (store_birth, cell_index, landmark_sheet) — тянуть за ним весь
// спавн сквадов значило бы заставить каждый тест генезиса линковать цепь
// npc_spawn.cpp ради нескольких присваиваний.
#pragma once

#include <cstring>

#include "macro/characters.h"     // landmark_sheet — анкета места по роду
#include "macro/state.h"
#include "macro/store.h"

namespace sm {

// kind.type остаётся НУЛЕВОЙ строкой: вопрос «каким листом дерётся лидер»
// месту не задаёт никто — бой места ведут ГОЛОВЫ его контейнера
// (auto_battle.h), а анкета берётся строкой стола по роду (landmark_sheet).
inline MacroHandle birth_place(GameState& gs, MacroStore& st, SquadType kind,
                               int x, int y, std::int16_t factionIdx = -1,
                               const char* name = nullptr) {
    const MacroHandle h = store_birth(st);
    if (!st.valid(h)) return h;   // отказ капа уже прозвучал вслух
    const int id = int(gs.nextMacroSpawnOrdinal++);   // единый эмитент (M-37)
    st.spawnId[h.slot] = ecs::MacroSpawnId{std::uint32_t(id)};
    st.cell[h.slot]    = ecs::MacroCell{ecs::cell_index(x, y, gs.mapW)};
    st.visual[h.slot]  = ecs::MacroVisual{float(x), float(y), 0.0f};
    st.kind[h.slot]    = ecs::NPCKind{0u, std::uint16_t(factionIdx)};
    if (name != nullptr)
        std::strncpy(st.name[h.slot].text, name,
                     sizeof st.name[h.slot].text - 1);
    st.sheet[h.slot]   = landmark_sheet(kind);
    ecs::MacroNpcRuntime rt{};            // pools — НУЛЯМИ (CANON S6)
    rt.homeSettlementId = id;             // дом места — оно само
    rt.targetX   = float(x);
    rt.targetY   = float(y);
    rt.state     = std::uint8_t(NPCState::Idle);
    rt.squadType = std::uint8_t(kind);
    st.runtime[h.slot] = rt;
    ++gs.navEpoch;                        // событие состава (закон add_landmark)
    return h;
}

} // namespace sm
