// ── ЗАКОН ПОРЯДКА ОБХОДА СКВАДОВ (эпик 2, шаг 1а; владелец, 2026-09-25) ────
//
// Порядок, в котором свип трогает сквады, есть ЗАКОН МИРА, а не свойство
// хранилища: свип делит ОДИН RNG (TickContext.rng = runtime.jitter), один
// пул дезертиров и первую-подходящую заявку ротации (claim_standing), поэтому
// «кто думает первым» меняет мир.
//
// До этой двери порядок задавали внутренности EnTT — порядок вставки в пул,
// пермутируемый swap-удалением каждой смерти. Два следствия, оба дефекты:
//   1) детерминизм мира висел на кишке чужого контейнера и не пережил бы
//      смену хранилища (эпик 2: гладкий массив вместо registry);
//   2) СЕЙВ/ЗАГРУЗКА МЕНЯЛИ ЭВОЛЮЦИЮ МИРА: снапшот пишет сквады по ординалу
//      (macro_snapshot.cpp сортирует), значит после загрузки пулы стоят в
//      ординальном порядке, а живой мир к этому моменту перемешан смертями —
//      тот же сид шёл дальше ДВУМЯ разными историями.
//
// Закон: обход ПО ОРДИНАЛУ РОЖДЕНИЯ (ecs::MacroSpawnId, монотонный, никогда
// не переиздаётся) — старшие первыми, сквад игрока (kPlayerSquadOrdinal)
// последним по построению. Гладкий массив исполнит закон размещением; до
// него — этой сборкой-сортировкой. Проходы, не мутирующие мир (визуалы,
// счётчики UI), под закон не подпадают: их порядок ничего не меняет.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/store.h"

namespace sm {

// СЛОТ — ключ мира (эпик 2 шаг 3): всякий читатель порядка спрашивает
// колонки store по нему напрямую. Поле энтити умерло с мостом (кластер 7).
struct SquadWalkEntry {
    std::uint32_t  ordinal;
    std::uint16_t  slot;
};
// Перепись штабелей (core/stacks.h «каркас клеток: порядок закона») цитирует
// этот размер ЛИТЕРАЛОМ — включить сюда stacks.h нельзя (цикл через store.h).
// Названный размер стоит под компилятором, не в прозе (DOD п.10).
static_assert(sizeof(SquadWalkEntry) == 8,
              "строка порядка закона: 8 Б — дрейф правит строку переписи");

// Закон порядка: население — сами слоты store, обход — голый скан байта
// alive (32 КиБ на кап, цена видна в точке — вердикт владельца 2026-09-29
// «голый цикл»).
template <typename Pred>
inline void collect_squads_by_ordinal(const MacroStore& st,
                                      std::vector<SquadWalkEntry>& out,
                                      Pred keep) {
    out.clear();
    for (std::uint32_t slot = 0; slot < kMacroEntityCap; ++slot) {
        if (st.alive[slot] == 0) continue;
        if (!keep(std::uint16_t(slot))) continue;
        out.push_back({st.spawnId[slot].index, std::uint16_t(slot)});
    }
    std::sort(out.begin(), out.end(),
              [](const SquadWalkEntry& a, const SquadWalkEntry& b) {
                  return a.ordinal < b.ordinal;
              });
}

} // namespace sm
