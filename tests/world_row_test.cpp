// СТРОКА МИРА — одно пространство ординалов (M-73, шаг 1 эпика).
//
// Закон, который тут охраняется: всякий род мира — предмет или существо —
// адресуется ОДНИМ сквозным номером, граница диапазонов ВЫВОДИТСЯ из размера
// предметного каталога, и сдвиг базы живёт только в двери (world_row.h).
// Утверждаются ИНВАРИАНТЫ (обратимость, смежность, непересечение), никогда
// пересказанные числа: ни одного литерала-границы в этом файле нет.
#include "check.h"

#include "macro/world_row.h"

#include <cstdint>

int main() {
    using namespace sm;

    const std::uint16_t items = world_item_rows();
    const std::uint16_t total = world_row_count();
    CHECK(items > 0u, "предметный каталог не пуст");
    CHECK(total > items, "за предметами стоят существа");
    CHECK(int(total) == int(items) + int(NPCType::Count),
          "пространство строк — ровно оба каталога, встык и без дыр");

    // ── Обратимость и непересечение: каждый предмет ─────────────────────
    int itemChecked = 0;
    for (int i = 0; i < int(items); ++i) {
        const std::uint16_t row = world_row_of_item(i);
        CHECK(world_row_is_item(row), "предметная строка опознаётся предметом");
        CHECK(!world_row_is_creature(row), "и никогда существом");
        CHECK(item_of_world_row(row) == i, "дверь обратима для предмета");
        CHECK(creature_of_world_row(row) == NPCType::Count,
              "чужой диапазон отвечает отказом, не соседним родом");
        ++itemChecked;
    }
    CHECK(itemChecked > 0, "перепись предметов реально прошла");

    // ── Обратимость и непересечение: каждое существо ────────────────────
    int creatureChecked = 0;
    for (int t = 0; t < int(NPCType::Count); ++t) {
        const std::uint16_t row = world_row_of_creature(NPCType(t));
        CHECK(world_row_is_creature(row), "строка существа опознаётся существом");
        CHECK(!world_row_is_item(row), "и никогда предметом");
        CHECK(creature_of_world_row(row) == NPCType(t),
              "дверь обратима для существа");
        CHECK(item_of_world_row(row) == -1,
              "чужой диапазон отвечает отказом, не соседним родом");
        ++creatureChecked;
    }
    CHECK(creatureChecked > 0, "перепись существ реально прошла");

    // ── Шов диапазонов: граница НЕ особая точка ─────────────────────────
    CHECK(world_row_is_item(std::uint16_t(items - 1u)),
          "последняя предметная строка — предмет");
    CHECK(world_row_is_creature(items), "первая строка за границей — существо");
    CHECK(creature_of_world_row(items) == NPCType(0),
          "существо номер ноль стоит ровно на границе");

    // ── За концом пространства — отказ, а не сосед ──────────────────────
    CHECK(!world_row_is_item(total) && !world_row_is_creature(total),
          "строки за концом пространства не существует");
    CHECK(item_of_world_row(total) == -1, "предметная дверь отказывает");
    CHECK(creature_of_world_row(total) == NPCType::Count,
          "дверь существ отказывает");

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ: сдвиг базы — несущий ───────────────────────
    // Наивный код, кладущий сырой ординал существа в строку мира БЕЗ двери,
    // ОБЯЗАН врать: такой номер лежит в предметном диапазоне и прочтётся
    // предметом. Если этот check упадёт — база перестала сдвигать, и весь
    // тест выше проверяет тождество.
    const std::uint16_t naive = std::uint16_t(NPCType::Peasant);
    CHECK(world_row_is_item(naive) && !world_row_is_creature(naive),
          "сырой ординал существа без сдвига обязан читаться предметом");
    CHECK(world_row_of_creature(NPCType::Peasant) != naive,
          "дверь обязана сдвинуть базу, а не вернуть сырой ординал");

    return sm::test::report("world_row_test");
}
