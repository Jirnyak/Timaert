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
#include "macro/features.h"    // feature_put — фича ДО тела
#include "macro/landmark_registry.h" // колонка фичи рода
#include "macro/state.h"
#include "macro/store.h"

namespace sm {

// kind.type остаётся НУЛЕВОЙ строкой: вопрос «каким листом дерётся лидер»
// месту не задаёт никто — бой места ведут ГОЛОВЫ его контейнера
// (auto_battle.h), а анкета берётся строкой стола по роду (landmark_sheet).
// СНАЧАЛА ФИЧА, СКВАД ПОВЕРХ (вердикт владельца 2026-10-23: «ДЕРЕВНИ ГОРОДА
// это сначало фичи ВСЁ ТОЛЬКО ФИЧИ потом поверх них ложатся сквады»; фичи
// делаются ОДИН РАЗ при генерации). Дверь рождения места ставит фичу рода
// (колонка строки kLandmarks) В МОМЕНТ ВЫБОРА КЛЕТКИ — до тела; слой фич
// производен от сида и тел не читает никогда. nullptr-слой — фикстура без
// фич, законно (fail-open как у всякого слоя конверта).
inline MacroHandle birth_place(GameState& gs, MacroStore& st, SquadType kind,
                               int x, int y, std::int16_t factionIdx = -1,
                               const char* name = nullptr,
                               FeatureLayer* fl = nullptr,
                               const TerrainData* td = nullptr) {
    if (fl && td) {
        const FeatureType ft = landmark_def(kind).feature;
        if (ft != FT_None) feature_put(*fl, *td, x, y, ft);
    }
    const MacroHandle h = store_birth(st);
    if (!st.valid(h)) return h;   // отказ капа уже прозвучал вслух
    const int id = int(h.id);     // ординал выдала дверь рождения (M-220)
    st.cell[h.slot]    = ecs::MacroCell{ecs::cell_index(x, y, gs.mapW)};
    st.sub[h.slot]     = {};   // рождение — центр клетки (M-244)
    st.kind[h.slot]    = ecs::NPCKind{0u, std::uint16_t(factionIdx)};
    if (name != nullptr)
        std::strncpy(st.name[h.slot].text, name,
                     sizeof st.name[h.slot].text - 1);
    st.sheet[h.slot]   = landmark_sheet(kind);
    ecs::MacroNpcRuntime rt{};            // pools — НУЛЯМИ (CANON S6)
    // Цель новорождённого — «ничего» (kNoCell): колонку цели обнуляет сама
    // дверь рождения, стоять на месте цели не требует (M-1).
    rt.state     = std::uint8_t(NPCState::Idle);
    rt.squadType = std::uint8_t(kind);
    st.runtime[h.slot] = rt;
    // ДОМ МЕСТА — ОНО САМО: self-ребро Parent одной записью (M-245), той же
    // дверью, что у всякого рождения.
    set_relation(st, id, id, Stance::Parent);
    ++gs.navEpoch;                        // событие состава (закон add_landmark)
    return h;
}

} // namespace sm
