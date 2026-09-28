#include "macro/loot_pool.h"

#include <span>

namespace sm {

int loot_issue(int budgetValue, const LootContext& ctx, RngFn rng,
               Inventory& into) {
    if (budgetValue <= 0 || !rng) return 0;
    const std::span<const ItemDef> catalog = item_catalog();
    const int rows = int(catalog.size());
    if (rows <= 0) return 0;

    // Три колонки контекста сходятся в ОДНУ дверь — второго вывода «насколько
    // богата вещь» в мире нет.
    const std::uint8_t power = affix_power(ctx.level, ctx.danger, ctx.wealthMul);

    int left = budgetValue;
    int spent = 0;
    // ПРЕДЕЛ ЦИКЛА — РАЗМЕР САМОГО КАТАЛОГА, и это не число с потолка: за одну
    // выдачу режиссёр берёт не больше строк, чем их есть. Бюджет, который он
    // не сумел разместить за проход, возвращается звонящему неистраченным —
    // честнее, чем крутить каталог по кругу, пока не опустеет казна.
    for (int step = 0; step < rows; ++step) {
        // ── Выбор строки: равновероятно среди тех, что бюджет ПОКРЫВАЕТ ───
        // Распределение здесь намеренно плоское. Веса по роду вещи и теги
        // («что уместно в пещере, а что в городском доме») — это МАСКА ТЕГОВ
        // строки каталога, названная строкой наряда M-17 и не построенная;
        // поставить сюда веса ДО неё значило бы вписать числа с потолка в
        // фундаментальную систему. Сегодня дверь честна по СТОИМОСТИ, и это
        // ровно то, за что отвечает пул.
        int affordable = 0;
        for (const ItemDef& d : catalog) {
            if (d.value > 0 && d.value <= left) ++affordable;
        }
        if (affordable == 0) break;   // бюджета не хватает даже на дешевейшее
        int pick = int(rng() * float(affordable));
        if (pick >= affordable) pick = affordable - 1;   // rng() == 1.0f
        if (pick < 0) pick = 0;
        int ordinal = -1;
        for (int i = 0; i < rows; ++i) {
            const ItemDef& d = catalog[std::size_t(i)];
            if (d.value <= 0 || d.value > left) continue;
            if (pick-- == 0) { ordinal = i; break; }
        }
        if (ordinal < 0) break;
        const ItemDef& row = catalog[std::size_t(ordinal)];

        // ── Экземпляр: через ту же единственную дверь выдачи аффиксов ──────
        // `grant_affixes` сама отказывает ненадеваемому (slotMask 0), поэтому
        // ветки «это вещь или это зерно» здесь нет и быть не должно.
        ItemRef instance{};
        instance.def = std::uint16_t(ordinal);
        instance.count = 1;
        grant_affixes(instance, power, rng);
        // ЦЕНА СПРАШИВАЕТСЯ У ЭКЗЕМПЛЯРА, А НЕ У СТРОКИ: аффикс несёт свою
        // стоимость (`value_of` её и складывает), и списать базовую значило
        // бы отдать миру больше, чем взять с казны, — дюп через режиссёра.
        int unit = value_of(instance);
        if (unit > left) {
            // Аффикс сделал экземпляр дороже бюджета. Чистый экземпляр по
            // карману всегда — строку выбирали именно по этому условию.
            instance = ItemRef{};
            instance.def = std::uint16_t(ordinal);
            instance.count = 1;
            unit = row.value;
        }
        if (unit <= 0) break;   // строка без цены неоплатна: считать нечем

        // ── Сколько: НАДЕВАЕМОЕ — ЭКЗЕМПЛЯРОМ, СЫПУЧЕЕ — СТАКОМ ───────────
        // Различие взято у КОЛОНКИ строки, а не у числа: у надеваемой вещи
        // есть `slotMask`, свой сид и свои аффиксы — она и есть экземпляр
        // (`same_kind_as` их именно поэтому и различает); у зерна, руды и
        // досок колонки надевания нет, и стак есть их естественная форма.
        const int fits = left / unit;
        instance.count = row.slotMask != 0 ? 1 : fits;
        if (instance.count <= 0) break;

        // Кредит прежде дебета (закон сохранения CANON S5): полный контейнер
        // ОТКАЗЫВАЕТ, стоимость остаётся у звонящего, и «выдано» не говорится
        // о вещах, которых никто не получил.
        if (!into.add_ref(instance)) break;
        const int paid = unit * instance.count;
        spent += paid;
        left -= paid;
        if (left <= 0) break;
    }
    return spent;
}

} // namespace sm
