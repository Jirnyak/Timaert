#include "macro/loot_pool.h"

#include "macro/fauna.h"   // danger_match_weight — ТА ЖЕ дверь совпадения

#include <algorithm>
#include <cmath>
#include <span>

namespace sm {

namespace {

// ── СТОИМОСТНАЯ СИЛА СТРОКИ — ВЫВЕДЕНА, НИКОГДА НЕ АВТОРЕНА ───────────────
// Близнец `spawn_strength@src/macro/fauna.cpp`, и близнец намеренный: там
// сила строки есть нормированный log₂ её БОЕВОЙ мощи, здесь — нормированный
// log₂ её СТОИМОСТИ. Слабейшая строка → 0, дражайшая → 255. Смысл вывода тот
// же, что у родителя: колонки силы не существует, значит ей нечем разъехаться
// с числами, которые мир на самом деле считает. Подорожала строка — она сама
// переехала в богатый контекст, и ни одной правки кода при этом не нужно.
//
// Логарифм, а не сама стоимость: каталог тянется от 1 (медная монета) до
// 12 800 (золотая руда), и на линейной шкале весь мир, кроме трёх строк,
// слипся бы в нулевой байт.
std::uint8_t value_strength(int defIdx) {
    static const std::vector<std::uint8_t> table = [] {
        const std::span<const ItemDef> cat = item_catalog();
        std::vector<double> L(cat.size(), 0.0);
        double lo = 1e30, hi = -1e30;
        for (std::size_t i = 0; i < cat.size(); ++i) {
            L[i] = std::log2(std::max(1.0, double(cat[i].value)));
            lo = std::min(lo, L[i]);
            hi = std::max(hi, L[i]);
        }
        std::vector<std::uint8_t> out(cat.size(), 0);
        const double span = std::max(1e-6, hi - lo);
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = std::uint8_t(std::lround(255.0 * (L[i] - lo) / span));
        }
        return out;
    }();
    return defIdx >= 0 && defIdx < int(table.size())
        ? table[std::size_t(defIdx)] : 0;
}

} // namespace

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
        // ── ВЫБОР СТРОКИ — ТОТ ЖЕ ЗАКОН, ЧТО У СПАВНА ЖИВНОСТИ ────────────
        //
        //   вес(строка, контекст) = danger_match_weight(
        //                               value_strength(строка), power)
        //
        // Это ДОСЛОВНО закон `roll_spawns@src/macro/fauna.cpp`, взятый за
        // стоимость вместо боевой мощи: там контекст клетки выбирает, КТО в
        // ней живёт, здесь контекст выдачи выбирает, ЧТО в ней лежит. Дверь
        // совпадения одна на оба — симметричная вокруг «сила == контекст»,
        // ополовинивающаяся на каждый `kDangerHalfLife` расхождения, с полом
        // 1: дорогая вещь в нищей лачуге есть исчезающе малая вероятность,
        // ЛИТЕРАЛЬНО, и никогда не ноль. Ни отсечек, ни полос, ни авторских
        // чисел — состав мешка перетекает вслед за контекстом.
        //
        // Почему это, а не таблица весов по роду вещи: таблица была бы ВТОРЫМ
        // словарём рядом с `habitat` и горстью чисел с потолка, а выведенная
        // сила не может разъехаться с ценами, которые мир и так считает.
        std::uint64_t total = 0;
        for (int i = 0; i < rows; ++i) {
            const ItemDef& d = catalog[std::size_t(i)];
            if (d.value <= 0 || d.value > left) continue;
            total += danger_match_weight(value_strength(i), power);
        }
        if (total == 0) break;   // бюджета не хватает даже на дешевейшее
        double roll = double(rng()) * double(total);
        int ordinal = -1;
        for (int i = 0; i < rows; ++i) {
            const ItemDef& d = catalog[std::size_t(i)];
            if (d.value <= 0 || d.value > left) continue;
            ordinal = i;   // последняя покрытая строка — приют для rng() == 1.0f
            roll -= double(danger_match_weight(value_strength(i), power));
            if (roll <= 0.0) break;
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
