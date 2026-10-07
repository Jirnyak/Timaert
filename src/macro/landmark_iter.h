// ОБХОД НЕПОДВИЖНЫХ СКВАДОВ — ПО СЛОТАМ ГЛАДКОЙ ПАМЯТИ.
//
// ЗДЕСЬ СТОЯЛО «over the ONE list (gs.landmarks)» — АДРЕС МЁРТВ (правка
// 2026-10-06): ни `gs.landmarks`, ни `std::vector<Landmark>` не существует,
// место есть неподвижный СКВАД того же массива (ЗАКОН ПОЛЯ п.4), и оба
// обхода ниже идут слотами `MacroStore`. Шапка пережила своё хранилище и
// называла читателю структуру, которой нет, — ровно тот род лжи, из-за
// которого §0 запрещает считать шапку доказательством.
//
// Всякий потребитель ходит ЭТИМ визитором и решает по СТРОКЕ реестра
// (landmark_registry.h), поэтому новый неподвижный род = его строка плюс
// место в порядке выдачи ниже, и ни один потребитель не правится. Порядок
// выдачи есть ОДИН закон владения клеткой («первое попадание на клетке»
// согласовано с тем, кто владеет клеткой по мнению субмира).
//
// Split from landmark_registry.h because the visitor needs the full
// GameState definition, and the registry table is included by far lighter
// headers (map_data.h, seasons.h).
#pragma once
#include "macro/features.h"
#include "macro/labour.h"   // souls_flock — паства места (переворот v122)
#include "macro/landmark_registry.h"   // kLandmarkYieldOrder — закон клетки
#include "macro/macro_world.h"   // конверт слоёв — вход визитора (п.8)
#include "macro/spires.h"   // spire_orb — ОДИН вывод об орбе (M-233 п.8)
#include "macro/state.h"

namespace sm {

struct LandmarkView {
    SquadType type;
    int  id;          // WORLD-unique subject ordinal (M-37: один эмитент
                      // на сквады и места — nextMacroSpawnOrdinal; «id
                      // within its kind» died with the three-vector storage)
    int  x, y;
    const char* name; // display name; never null, may be ""
    int  population;  // ПАСТВА места (souls_flock: worked-число у поселения,
                      // головы толпы у данжа) — производное, не колонка
    bool depleted = false; // шпиль, чей орб забран — ОДИН вывод на весь мир
                           // (spire_orb@src/macro/spires.h); колонкой
                           // `Landmark` не был с v120, а СВОИМ написанием
                           // этого вывода перестал быть с M-233 п.8
};

// (kLandmarkYieldOrder живёт в landmark_registry.h: сетке мест нужен тот же
// закон приоритета клетки, а итератора она тянуть не должна — флип M-90
// отправил паству в ТЕЛО места, и этот файл теперь тащит store.)

// ── ОБХОД МЕСТ — СЛОТЫ ПО ОСИ РОДА (ломтик F: штабель строк умер) ────────
// «Все места мира» = живые слоты store с is_settlement_kind. Проход капом,
// а не населением (ЗАКОН СТАБИЛЬНОСТИ: структурная цена — константа).
template <class F>
void for_each_place(const MacroStore& st, F&& fn) {
    for (std::uint32_t slot = 0; slot < kMacroEntityCap; ++slot) {
        if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
        if (!is_settlement_kind(SquadType(st.runtime[slot].squadType)))
            continue;
        fn(std::uint16_t(slot));
    }
}

// КОНВЕРТ, А НЕ ПАРА СЫРЫХ СЛОЁВ (M-233 п.8): вид места несёт выкачанность,
// а она выводится из ФИЧИ клетки (ЗАКОН ПОЛЯ п.3), поэтому визитору нужен
// слой фич. Передавать его ОТДЕЛЬНЫМ аргументом запрещает шапка
// `macro_world.h` («never by a new argument at a call site») — и шрам там
// назван числом. Тела мест по-прежнему колонки `*w.store` (M-90 шаг 5).
// Пустой конверт законен: нет мира — обход пуст, нулевой вклад S6.
template <class F>
void for_each_landmark(const MacroWorld& w, F&& fn) {
    if (!w.gs || !w.store) return;
    const GameState& gs = *w.gs;
    const MacroStore& st = *w.store;
    for (SquadType t : kLandmarkYieldOrder) {
        for (std::uint32_t slot = 0; slot < kMacroEntityCap; ++slot) {
            if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
            if (SquadType(st.runtime[slot].squadType) != t) continue;
            // БЕЗЫМЯННЫЙ — ЧЕСТНЫЙ СЛУЧАЙ ИМЕНОВАННОГО ТИПА (вердикт
            // владельца №10, 2026-09-17: «просто нули вместо чар строки
            // имени»), и что он показывает — колонка `label` ЕГО строки
            // реестра, одной системой для всех видов. Здесь стояла ветка
            // по ВИДУ с именем «Spire» литералом: тот же текст, что в
            // колонке рядом, только недоступный ни руине, ни логову, ни
            // шахте — они показывали пустую строку, хотя их label ждал.
            const char* name = st.name[slot].text;
            if (name[0] == '\0') name = landmark_def(t).label.data();
            const auto& c = st.cell[slot];
            const int x = ecs::cell_x(c, gs.mapW);
            const int y = ecs::cell_y(c, gs.mapW);
            fn(LandmarkView{t, int(st.spawnId[slot].index), x, y, name,
                            souls_flock(gs, st, std::uint16_t(slot)),
                            spire_orb(w, x, y).depleted});
        }
    }
}

// ── ШТАМП ФИЧ ПОСЕЛЕНИЙ (вердикт владельца 2026-09-30; ЗАКОН ГЕНЕРАЦИИ
// п.6: фичи поселений — байты слоя фич, генерация ставит их ДО сквадов).
// «Что стоит на клетке» отвечает байт фичи; «кто здесь живёт» — сквад.
// Один проход ПОСЛЕ дорог: клетка поселения — мощёный (город) или
// грунтовый (деревня) узел сети, штамп её перекрывает (прецедент моста).
// Свежесть — тот же закон, что у каркаса клеток: генезис и загрузка зовут
// этот проход рядом с build_squad_index; смерть места — смена ВИДА
// (set_landmark_type), за ней тот же перепёк.
// ТЕРРАИН В СИГНАТУРЕ — ПО ЗАКОНУ, А НЕ ДЛЯ УДОБСТВА (M-212). Поселение есть
// ФИЧА СО СКВАДОМ ПОВЕРХ (владелец 2026-10-03), значит и воду оно проходит той
// же единственной дверью штампа, что дорога и пашня. До этого штамп мест воду
// не проверял ВООБЩЕ и держался на том, что размещатель ставит их на сушу, —
// неявная зависимость, живущая до первого размещателя, который решит иначе.
inline void stamp_settlement_features(const MacroStore& st, int mapW,
                                      const TerrainData& td,
                                      FeatureLayer& f) {
    for_each_place(st, [&](std::uint16_t slot) {
        const SquadType kind = SquadType(st.runtime[slot].squadType);
        FeatureType ft = FT_None;
        switch (kind) {
            case SquadType::City:    ft = FT_City; break;
            case SquadType::Village: ft = FT_Village; break;
            case SquadType::Spire:   ft = FT_Spire; break;
            case SquadType::Ruin:    ft = FT_Ruin; break;
            case SquadType::None:
            case SquadType::Lair:
            case SquadType::Shrine:
            case SquadType::Mine:
            case SquadType::Tower:
            // Подвижные роды оси (M-90 шаг 3а): у сквада, который ХОДИТ,
            // байта фичи нет и быть не может — фича говорит «что СТОИТ на
            // клетке», а он на ней не стоит, он через неё идёт. Ветки
            // выписаны, чтобы `-Wswitch` и дальше называл забытое: `default`
            // здесь проглотил бы следующий НЕПОДВИЖНЫЙ род молча.
            case SquadType::Artel:
            case SquadType::Caravan:
            case SquadType::Collector:
            case SquadType::Count:   break;   // мир их пока не ставит
        }
        if (ft == FT_None) return;
        const auto& c = st.cell[slot];
        stamp_feature(f, td, ecs::cell_x(c, mapW), ecs::cell_y(c, mapW), ft);
    });
}

} // namespace sm
