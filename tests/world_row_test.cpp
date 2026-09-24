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

    // ═══ ЗАКОН ДВУХ ОБЛАСТЕЙ ЕДИНОГО КОНТЕЙНЕРА (M-71, слияние) ═════════
    // Свойства, не пересказ: предметы селятся снизу, существа — плотной
    // областью сверху; порядок области зеркалит старый плотный ростер.
    {
        Inventory inv{};
        CHECK(creatures_empty(inv), "пустой контейнер — пустая область");
        CHECK(inv.creature_first() == kMaxInventorySlots,
              "граница пустой области — за концом массива");

        // Предмет ложится в НИЖНИЙ слот, существо — в ВЕРХНИЙ.
        CHECK(inv.add_of(0, 5), "предмет встал");
        CHECK(!inv.slots[0].empty(), "предмет живёт снизу");
        CHECK(creatures_push_stack(inv, NPCType::Peasant, 1, 10),
              "генерик-стак встал");
        CHECK(!inv.slots[kMaxInventorySlots - 1].empty(),
              "существо живёт сверху");
        CHECK(inv.creature_first() == kMaxInventorySlots - 1,
              "область — ровно один слот");
        CHECK(creature_heads(inv) == 10, "головы считаются по count");

        // Стакование: тот же род и уровень сливается, иной уровень — нет.
        CHECK(creatures_push_stack(inv, NPCType::Peasant, 1, 6), "долив");
        CHECK(creature_slot_count(inv) == 1 && creature_heads(inv) == 16,
              "генерики одного рода и уровня — ОДИН слот");
        CHECK(creatures_push_stack(inv, NPCType::Peasant, 2, 1),
              "другой уровень");
        CHECK(creature_slot_count(inv) == 2,
              "другой уровень — другой слот");

        // Душа не стакуется и несёт count == 1 по построению.
        const SoldierRecord soul = make_soldier(
            std::uint16_t(NPCType::Peasant), 1, 777u);
        CHECK(creatures_push(inv, soul), "душа вошла");
        CHECK(creature_slot_count(inv) == 3, "душа взяла свой слот");
        CHECK(creature_heads(inv) == 18, "головы: 16 + 1 + душа");
        CHECK(creature_heads_of(inv, NPCType::Peasant) == 18,
              "счёт по роду видит все слоты рода");

        // Предметные двери НЕ трогают существ: подсчёт и снятие по
        // предметному ординалу слепы к области (непересечение диапазонов).
        CHECK(inv.count_of(0) == 5, "предметный счёт не видит существ");

        // ЗАКОН ХОДОКА: свежие уходят первыми — душа пришла последней.
        SoldierRecord walker{};
        CHECK(creatures_pop_back(inv, walker), "ходок вышел");
        CHECK(walker.entityId == 777u, "и это НОВЕЙШИЙ — душа");
        CHECK(creature_slot_count(inv) == 2, "слот души умер с ней");

        // УДАР ВЕДОМОСТИ: генерик того же рода/уровня, старейший первым.
        const SoldierRecord g = make_soldier(
            std::uint16_t(NPCType::Peasant), 1, 0u);
        CHECK(creatures_remove_one(inv, g), "генерик снят");
        CHECK(creature_heads_of(inv, NPCType::Peasant) == 16,
              "снята одна голова стака");

        // РЕМОНТ ОБЛАСТИ: смерть слота в середине затыкается новейшим —
        // дырок не бывает (негативный контроль: плотность после снятия).
        CHECK(creatures_remove_one(
                  inv, make_soldier(std::uint16_t(NPCType::Peasant), 2, 0u)),
              "слот уровня 2 умер (одна голова)");
        for (int i = inv.creature_first(); i < kMaxInventorySlots; ++i) {
            CHECK(!inv.slots[std::size_t(i)].empty()
                      && world_row_is_creature(inv.slots[std::size_t(i)].def),
                  "область существ плотна после снятия");
        }

        // ПЕРЕНОС: слот уходит целиком, исток пустеет, порядок сохранён.
        Inventory pool{};
        const int moved = creatures_move(pool, inv);
        CHECK(moved == 15, "пересажены все головы (15 генериков)");
        CHECK(creatures_empty(inv), "исток пуст");
        CHECK(creature_heads(pool) == 15, "цель приняла всех");
        CHECK(inv.count_of(0) == 5, "предметы переносом существ не тронуты");

        // ГОЛОВЫ ПОШТУЧНО: развёртка стака даёт адрес (slot, index).
        int heads = 0;
        for (const CreatureHead h : creature_heads_range(pool)) {
            CHECK(h.kind == std::uint16_t(NPCType::Peasant), "род головы");
            CHECK(h.index >= 0 && h.index < 15, "индекс внутри стака");
            ++heads;
        }
        CHECK(heads == 15, "развёртка прошла все головы");
    }

    // ═══ НЕГАТИВНЫЙ КОНТРОЛЬ ОБЛАСТЕЙ ═══════════════════════════════════
    // Предметная дверь снятия ОБЯЗАНА отказать строке существа: снятие
    // существа мимо типизированной двери сломало бы плотность области.
    {
        Inventory inv{};
        CHECK(creatures_push_stack(inv, NPCType::Peasant, 1, 3), "стак");
        const int row = int(world_row_of_creature(NPCType::Peasant));
        CHECK(inv.count_of(row) == 3,
              "ординальный счёт честен и для существа");
        CHECK(!inv.remove_of(row, 1),
              "предметная дверь снятия отказывает существу");
        CHECK(creature_heads(inv) == 3, "и ничего не тронула");
    }

    return sm::test::report("world_row_test");
}
