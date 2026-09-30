// THE landmark enumeration over the ONE roster (gs.landmarks, CANON S9
// 2026-08-29). Every consumer (map draw loop, hover pick, minimap,
// collect_landmarks, the cell grid) walks THIS visitor and dispatches on the
// registry row (landmark_registry.h), so a new landmark kind = its registry
// row + its entry in the yield order below — no consumer is touched. The
// yield order is the ONE cell-ownership priority ("first hit at a cell"
// agrees with the subworld's idea of who owns the cell).
//
// Split from landmark_registry.h because the visitor needs the full
// GameState definition, and the registry table is included by far lighter
// headers (map_data.h, seasons.h).
#pragma once
#include "macro/features.h"
#include "macro/labour.h"   // souls_flock — паства места (переворот v122)
#include "macro/landmark_registry.h"
#include "macro/state.h"

namespace sm {

struct LandmarkView {
    LandmarkType type;
    int  id;          // WORLD-unique subject ordinal (M-37: один эмитент
                      // на сквады и места — nextMacroSpawnOrdinal; «id
                      // within its kind» died with the three-vector storage)
    int  x, y;
    const char* name; // display name; never null, may be ""
    int  population;  // ПАСТВА места (souls_flock: worked-число у поселения,
                      // головы толпы у данжа) — производное, не колонка
    bool depleted = false; // Spire only — DERIVED from the worked layer at
                           // the spire's cell (0 = orb drained); never a
                           // Landmark column since v120
};

// Cell-ownership priority (CANON S9): the order kinds are yielded IS the one
// law of who owns a contested cell — the same order the old three-vector walk
// had (cities first, then villages, then spires). Storage is one vector in
// creation order (state.h gs.landmarks); the priority lives here, once.
inline constexpr LandmarkType kLandmarkYieldOrder[] = {
    LandmarkType::City, LandmarkType::Village, LandmarkType::Spire,
    LandmarkType::Ruin, LandmarkType::Lair, LandmarkType::Shrine,
    LandmarkType::Mine, LandmarkType::Tower,
};

template <class F>
void for_each_landmark(const GameState& gs, F&& fn) {
    for (LandmarkType t : kLandmarkYieldOrder) {
        for (const auto& lm : gs.landmarks) {
            if (lm.type != t) continue;
            // БЕЗЫМЯННЫЙ — ЧЕСТНЫЙ СЛУЧАЙ ИМЕНОВАННОГО ТИПА (вердикт
            // владельца №10, 2026-09-17: «просто нули вместо чар строки
            // имени»), и что он показывает — колонка `label` ЕГО строки
            // реестра, одной системой для всех видов. Здесь стояла ветка
            // по ВИДУ с именем «Spire» литералом: тот же текст, что в
            // колонке рядом, только недоступный ни руине, ни логову, ни
            // шахте — они показывали пустую строку, хотя их label ждал.
            const char* name = lm.name.c_str();
            if (lm.name.empty()) name = landmark_def(t).label.data();
            const bool depleted = lm.type == LandmarkType::Spire
                && worked_read(gs, lm.x, lm.y) == 0;
            fn(LandmarkView{lm.type, lm.id, lm.x, lm.y, name,
                            souls_flock(gs, lm), depleted});
        }
    }
}

// ── ШТАМП ФИЧ ПОСЕЛЕНИЙ (вердикт владельца 2026-09-30; ЗАКОН ГЕНЕРАЦИИ
// п.6: фичи поселений — байты слоя фич, генерация ставит их ДО сквадов).
// «Что стоит на клетке» отвечает байт фичи; «кто здесь живёт» — сквад.
// Один проход ПОСЛЕ дорог: клетка поселения — мощёный (город) или
// грунтовый (деревня) узел сети, штамп её перекрывает (прецедент моста).
// Свежесть — тот же закон, что у LandmarkGrid: генезис и загрузка зовут
// этот проход рядом с build_landmark_grid; смерть места — смена ВИДА
// (set_landmark_type), за ней тот же перепёк.
inline void stamp_settlement_features(const GameState& gs, FeatureLayer& f) {
    for (const auto& lm : gs.landmarks) {
        FeatureType ft = FT_None;
        switch (lm.type) {
            case LandmarkType::City:    ft = FT_City; break;
            case LandmarkType::Village: ft = FT_Village; break;
            case LandmarkType::Spire:   ft = FT_Spire; break;
            case LandmarkType::Ruin:    ft = FT_Ruin; break;
            case LandmarkType::None:
            case LandmarkType::Lair:
            case LandmarkType::Shrine:
            case LandmarkType::Mine:
            case LandmarkType::Tower:
            case LandmarkType::Count:   break;   // мир их пока не ставит
        }
        if (ft == FT_None) continue;
        f.set(lm.x, lm.y, ft);
    }
}

} // namespace sm
