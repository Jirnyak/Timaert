// ── РОЖДЕНИЕ МЕСТА = СТРОКА + ТЕЛО, ОДНОЙ ДВЕРЬЮ (M-90 шаг 5) ────────────
//
// Место есть неподвижный сквад: всякая основа города, деревни, шпиля, руины
// идёт СЮДА, а не в add_landmark напрямую, — дверь кладёт строку-индекс
// (add_landmark: род/имя/адрес) И рождает ТЕЛО в MacroStore (слот с плечом:
// склад, интересы, счёт нужд, благополучие, анкета landmark_sheet, слава).
// Исключение одно — путь ЗАГРУЗКИ: строки приходят из файла (read_landmark),
// тела — записями блока макро-сквадов, их сшивает relink_place_bodies.
//
// Свой заголовок, а не npc_spawn: рождению тела места хватает header-only
// дверей (store_birth, cell_index, landmark_sheet) — тянуть за ним весь
// спавн сквадов значило бы заставить каждый тест генезиса линковать цепь
// npc_spawn.cpp ради трёх присваиваний.
#pragma once

#include <cstdio>
#include <cstring>

#include "macro/characters.h"     // landmark_sheet — анкета места по роду
#include "macro/place_body.h"
#include "macro/state.h"
#include "macro/store.h"

namespace sm {

// Тело места — обычный слот store: всё плечо прежней строки Landmark лежит
// его колонками. kind.type остаётся НУЛЕВОЙ строкой: вопрос «каким листом
// дерётся лидер» месту не задаёт никто — бой места ведут ГОЛОВЫ его
// контейнера (auto_battle.h), а анкета берётся строкой стола по роду
// (landmark_sheet; «став колонкой, она впервые позволит месту отличаться
// от своего вида» — вердикт флипа). Ось рода — runtime.squadType (1..8).
// kind.factionIdx — транзитный зеркальный байт строки до ломтика F: истину
// фракции места несёт колонка строки (settlement_faction_index).
inline MacroHandle make_place_body(MacroStore& st, const GameState& gs,
                                   Landmark& lm) {
    const MacroHandle h = store_birth(st);
    if (!st.valid(h)) return h;   // отказ капа уже прозвучал вслух
    st.spawnId[h.slot] = ecs::MacroSpawnId{std::uint32_t(lm.id)};
    st.cell[h.slot]    = ecs::MacroCell{ecs::cell_index(lm.x, lm.y, gs.mapW)};
    st.visual[h.slot]  = ecs::MacroVisual{float(lm.x), float(lm.y), 0.0f};
    st.kind[h.slot]    = ecs::NPCKind{0u, std::uint16_t(lm.factionIdx)};
    std::memcpy(st.name[h.slot].text, lm.name, sizeof lm.name);
    st.sheet[h.slot]   = landmark_sheet(lm.type);
    ecs::MacroNpcRuntime rt{};            // pools — НУЛЯМИ (CANON S6)
    rt.homeSettlementId = lm.id;          // дом места — оно само
    rt.targetX   = float(lm.x);
    rt.targetY   = float(lm.y);
    rt.state     = std::uint8_t(NPCState::Idle);
    rt.squadType = std::uint8_t(lm.type);
    st.runtime[h.slot] = rt;
    lm.bodyBits = macro_handle_bits(h);
    return h;
}

inline Landmark& birth_landmark(GameState& gs, MacroStore& st,
                                Landmark&& lm) {
    Landmark& row = add_landmark(gs, std::move(lm));
    make_place_body(st, gs, row);
    return row;
}

// Сшивка после загрузки: тела приехали записями блока макро-сквадов,
// строки — блоком мест; ординал один (M-37), значит сшивка — один проход
// по слотам с бинарным поиском строки. Строка без тела называется ВСЛУХ.
inline void relink_place_bodies(GameState& gs, MacroStore& st) {
    for (Landmark& lm : gs.landmarks) lm.bodyBits = kMacroHandleNoneBits;
    for (std::uint32_t slot = 0; slot < kMacroEntityCap; ++slot) {
        if (!st.alive[slot]) continue;
        const std::ptrdiff_t i =
            landmark_index_by_id(gs, int(st.spawnId[slot].index));
        if (i < 0) continue;   // ординал сквада, не места — одно пространство
        gs.landmarks[std::size_t(i)].bodyBits =
            macro_handle_bits(handle_at(st, std::uint16_t(slot)));
    }
    for (const Landmark& lm : gs.landmarks)
        if (lm.bodyBits == kMacroHandleNoneBits)
            std::fprintf(stderr,
                         "[place] СТРОКА %d «%.32s» НЕ НАШЛА ТЕЛА после "
                         "загрузки\n",
                         lm.id, lm.name);
}

} // namespace sm
