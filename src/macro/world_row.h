// СТРОКА МИРА — ОДНО ПРОСТРАНСТВО ОРДИНАЛОВ (CANON:5566, вердикт владельца
// 2026-09-22; наряд M-73, шаг 1 эпика единой таблицы объектов).
//
// Всякая строка мира — предмет или существо — несёт ОДИН сквозной номер:
//
//   предметы   [0, world_item_rows())
//   существа   [world_item_rows(), world_row_count())
//
// Имена в коде НЕ переезжают: `NPCType::Peasant` остаётся собой, предметный
// ординал остаётся индексом каталога. Сдвиг базы живёт ЗДЕСЬ, в двери, и
// только здесь — контейнер (шаг 2) хранит строку мира, а спрашивает её
// всегда через эти функции.
//
// ЛОВУШКА, НАЗВАННАЯ КАНОНОМ ЗАРАНЕЕ: граница диапазона ВЫВОДИТСЯ из размера
// каталога (`item_catalog().size()`), вписанная константа была бы возвратом
// второго словаря. Поэтому здесь нет ни одного литерала.
//
// Авторских таблиц внутри по-прежнему две (kCatalog предметов, kNpcTypeDefs
// существ) — это удобство авторинга, а не второй словарь: на вопрос «что за
// строка мира N» отвечает ровно эта дверь, одна на проект (вердикт владельца
// 2026-09-24: «технически это единая таблица объектов»).
#pragma once
#include <cstdint>

#include "macro/items.h"   // item_catalog() — предметная половина строк
#include "macro/npc.h"     // NPCType — половина существ

namespace sm {

// Граница диапазонов — размер предметного каталога, посчитанный, не вписанный.
inline std::uint16_t world_item_rows() noexcept {
    return std::uint16_t(item_catalog().size());
}
inline std::uint16_t world_row_count() noexcept {
    return std::uint16_t(world_item_rows()
                         + std::uint16_t(NPCType::Count));
}

inline bool world_row_is_item(std::uint16_t row) noexcept {
    return row < world_item_rows();
}
inline bool world_row_is_creature(std::uint16_t row) noexcept {
    return row >= world_item_rows() && row < world_row_count();
}

// Туда: род → строка мира.
inline std::uint16_t world_row_of_item(int itemOrdinal) noexcept {
    return std::uint16_t(itemOrdinal);
}
inline std::uint16_t world_row_of_creature(NPCType t) noexcept {
    return std::uint16_t(world_item_rows() + std::uint16_t(t));
}

// Обратно: строка мира → род. Fail-closed на чужом диапазоне: предмету — −1,
// существу — NPCType::Count; молча «почти правильного» ответа не бывает.
inline int item_of_world_row(std::uint16_t row) noexcept {
    return world_row_is_item(row) ? int(row) : -1;
}
inline NPCType creature_of_world_row(std::uint16_t row) noexcept {
    return world_row_is_creature(row)
        ? NPCType(row - world_item_rows())
        : NPCType::Count;
}

} // namespace sm
