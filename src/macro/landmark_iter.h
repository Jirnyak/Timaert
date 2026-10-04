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
#include "macro/landmark_registry.h"   // kLandmarkYieldOrder — закон клетки
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

// (kLandmarkYieldOrder живёт в landmark_registry.h: сетке мест нужен тот же
// закон приоритета клетки, а итератора она тянуть не должна — флип M-90
// отправил паству в ТЕЛО места, и этот файл теперь тащит store.)

// `st` — тела мест (M-90 шаг 5): паства данжа — головы склада ТЕЛА.
template <class F>
void for_each_landmark(const GameState& gs, const MacroStore& st, F&& fn) {
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
            const char* name = lm.name;
            if (lm.name[0] == '\0') name = landmark_def(t).label.data();
            const bool depleted = lm.type == LandmarkType::Spire
                && worked_read(gs, lm.x, lm.y) == 0;
            fn(LandmarkView{lm.type, lm.id, lm.x, lm.y, name,
                            souls_flock(gs, st, lm), depleted});
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
// ТЕРРАИН В СИГНАТУРЕ — ПО ЗАКОНУ, А НЕ ДЛЯ УДОБСТВА (M-212). Поселение есть
// ФИЧА СО СКВАДОМ ПОВЕРХ (владелец 2026-10-03), значит и воду оно проходит той
// же единственной дверью штампа, что дорога и пашня. До этого штамп мест воду
// не проверял ВООБЩЕ и держался на том, что размещатель ставит их на сушу, —
// неявная зависимость, живущая до первого размещателя, который решит иначе.
inline void stamp_settlement_features(const GameState& gs, const TerrainData& td,
                                      FeatureLayer& f) {
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
            // Подвижные роды оси (M-90 шаг 3а): у сквада, который ХОДИТ,
            // байта фичи нет и быть не может — фича говорит «что СТОИТ на
            // клетке», а он на ней не стоит, он через неё идёт. Ветки
            // выписаны, чтобы `-Wswitch` и дальше называл забытое: `default`
            // здесь проглотил бы следующий НЕПОДВИЖНЫЙ род молча.
            case LandmarkType::Artel:
            case LandmarkType::Caravan:
            case LandmarkType::Collector:
            case LandmarkType::Count:   break;   // мир их пока не ставит
        }
        if (ft == FT_None) continue;
        stamp_feature(f, td, lm.x, lm.y, ft);
    }
}

} // namespace sm
