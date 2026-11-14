// ОБХОД НЕПОДВИЖНЫХ СКВАДОВ — ПО СЛОТАМ ГЛАДКОЙ ПАМЯТИ.
//
// Место есть неподвижный СКВАД того же массива (ЗАКОН ПОЛЯ п.4), и обход ниже
// идёт слотами `MacroStore`, отбирая архетип Rooted по оси рода
// (`squad_archetype@src/tables/squad_type.h`). «Чьей считается клетка, на
// которой стоят двое», здесь не решается: это закон `kRootedYieldOrder`, и
// отвечает на него каркас — `settlement_at@src/macro/squad_index.h`.
// (Визитор с видом места `for_each_rooted_view` и его `RootedView` жили
// здесь и умерли M-488: последний читатель — наведение карты — спрашивает
// клетку каркасом, а не сканом капа × родов.)
#pragma once
#include "bus/store.h"            // MacroStore, kUnifiedCap — обход слотами
#include "tables/squad_type.h"    // squad_archetype — ось рода

namespace sm {

// ── ОБХОД МЕСТ — СЛОТЫ ПО ОСИ РОДА (ломтик F: штабель строк умер) ────────
// «Все места мира» = живые слоты store с архетипом Rooted. Проход капом,
// а не населением (ЗАКОН СТАБИЛЬНОСТИ: структурная цена — константа).
template <class F>
void for_each_rooted(const MacroStore& st, F&& fn) {
    for (std::uint32_t slot = 0; slot < kUnifiedCap; ++slot) {
        if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
        if (squad_archetype(SquadType(st.runtime[slot].squadType)) != SquadArchetype::Rooted)
            continue;
        fn(std::uint16_t(slot));
    }
}

// (`stamp_settlement_features` УНИЧТОЖЕН 2026-10-23, вердикт «фичи делают
// 1 раз ПРИ ГЕНЕРАЦИИ МИРА»: он выводил слой фич ИЗ ТЕЛ сквадов — обратное
// направление. Фичу рода ставит дверь рождения места в момент выбора клетки
// (`birth_squad@src/macro/birth_squad.h`, колонка `kind` строки фичи).)

} // namespace sm
