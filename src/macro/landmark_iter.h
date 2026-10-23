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
// «Все места мира» = живые слоты store с архетипом Place. Проход капом,
// а не населением (ЗАКОН СТАБИЛЬНОСТИ: структурная цена — константа).
template <class F>
void for_each_place(const MacroStore& st, F&& fn) {
    for (std::uint32_t slot = 0; slot < kUnifiedCap; ++slot) {
        if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
        if (squad_archetype(SquadType(st.runtime[slot].squadType)) != SquadArchetype::Place)
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
        for (std::uint32_t slot = 0; slot < kUnifiedCap; ++slot) {
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

// (`stamp_settlement_features` УНИЧТОЖЕН 2026-10-23, вердикт «фичи делают
// 1 раз ПРИ ГЕНЕРАЦИИ МИРА»: он выводил слой фич ИЗ ТЕЛ сквадов — обратное
// направление. Фичу рода ставит дверь рождения места в момент выбора клетки
// (`birth_place@src/macro/place_birth.h`, колонка `kLandmarks.feature`).)

} // namespace sm
