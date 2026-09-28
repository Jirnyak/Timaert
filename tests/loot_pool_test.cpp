// РЕЖИССЁР ЛУТА: СТОИМОСТЬ СОХРАНЯЕТСЯ, И ЭТО ЕДИНСТВЕННЫЙ ЕГО ЗАКОН.
//
// Что здесь охраняется (закон мира, а не сегодняшнее поведение):
//   · пул есть КАЗНА — сколько дверь сказала «потрачено», ровно столько
//     стоимости и легло в контейнер. Меньше — мир обеднел молча, больше —
//     дюп через режиссёра;
//   · потраченное НИКОГДА не больше бюджета: остаток остаётся у звонящего;
//   · пустая казна выдаёт НОЛЬ и не трогает контейнер — это и есть
//     сегодняшнее состояние мира (вход пула структурно нулевой), поэтому
//     дверь обязана быть корректна именно на нём;
//   · полный контейнер ОТКАЗЫВАЕТ, и стоимость при этом не сгорает;
//   · дверь АГНОСТИЧНА: ей дают число и контекст, а не мир.
//
// Свидетель сам рождает своё предусловие (AGENTS §8 п.11): контейнеры,
// бюджеты и поток RNG строятся здесь, ни один не берётся у генератора мира.

#include "check.h"

#include "macro/currency.h"    // inventory_value — цена контейнера одной дверью
#include "macro/items.h"
#include "macro/loot_pool.h"

#include <cstdint>

using namespace sm;

namespace {

// ── Детерминированный поток (RngFn — float(*)(), состояние файловое) ───────
// LCG Numerical Recipes: воспроизводимость важнее качества — свидетелю нужен
// ОДИН И ТОТ ЖЕ поток в двух прогонах, чтобы спрашивать детерминизм.
std::uint32_t g_rngState = 1u;
void rng_reset(std::uint32_t seed) { g_rngState = seed; }
float rng_lcg() {
    g_rngState = g_rngState * 1664525u + 1013904223u;
    return float(g_rngState >> 8) / float(1u << 24);
}
float rng_zero() { return 0.0f; }

// Сумма по БАЗОВЫМ ценам строк — то, что заплатил бы режиссёр, если бы
// спрашивал цену у СТРОКИ, а не у ЭКЗЕМПЛЯРА. Негативный контроль ниже
// стоит ровно на разнице этих двух сумм.
int base_value_of_container(const Inventory& inv) {
    int total = 0;
    for (const ItemRef& s : inv.slots) {
        if (s.empty()) continue;
        const ItemDef* d = item_def_at(int(s.def));
        total += (d ? d->value : 0) * s.count;
    }
    return total;
}

// Сколько ЕДИНИЦ лежит в контейнере. Средняя стоимость единицы (потрачено /
// единиц) — наблюдаемое следствие закона выбора, и оно не переписывает ни
// строчки продакшен-логики: свидетель не считает ни силу строки, ни вес.
long long units_in(const Inventory& inv) {
    long long n = 0;
    for (const ItemRef& s : inv.slots) n += s.count;
    return n;
}

int affixed_stacks(const Inventory& inv) {
    int n = 0;
    for (const ItemRef& s : inv.slots) {
        if (!s.empty() && affix_count(s) > 0) ++n;
    }
    return n;
}

// ── 1. Пустая казна: ноль выдано, контейнер нетронут ──────────────────────
void test_empty_treasury() {
    const LootContext ctx{};
    Inventory bag{};
    rng_reset(7u);
    CHECK(loot_issue(0, ctx, &rng_lcg, bag) == 0,
          "нулевой бюджет не выдаёт ничего");
    CHECK(loot_issue(-500, ctx, &rng_lcg, bag) == 0,
          "отрицательный бюджет не выдаёт ничего (и не платит миру)");
    CHECK(loot_issue(1000, ctx, nullptr, bag) == 0,
          "без потока случайности режиссёр не выдаёт, а отказывает");
    CHECK(bag.used_slots() == 0,
          "отказ режиссёра НЕ трогает контейнер ни одним слотом");
    CHECK(inventory_value(bag) == 0, "и не оставляет в нём стоимости");
}

// ── 2. ЗАКОН: сказано «потрачено N» ⇒ в контейнере ровно на N больше ──────
void test_value_is_conserved() {
    int samples = 0, mismatches = 0, overspends = 0;
    for (int budget = 1; budget <= 100000; budget = budget * 3 + 1) {
        for (std::uint32_t seed = 1u; seed <= 16u; ++seed) {
            const LootContext ctx{std::uint8_t(seed * 13u), 1.0f + float(seed) * 0.1f,
                                  int(seed)};
            Inventory bag{};
            rng_reset(seed * 2654435761u + std::uint32_t(budget));
            const int spent = loot_issue(budget, ctx, &rng_lcg, bag);
            ++samples;
            if (inventory_value(bag) != spent) ++mismatches;
            if (spent > budget || spent < 0) ++overspends;
        }
    }
    CHECK(samples > 0, "цикл сохранения стоимости обязан был померить хоть раз");
    CHECK(mismatches == 0,
          "стоимость в контейнере обязана совпасть с потраченной ДО ЕДИНИЦЫ");
    CHECK(overspends == 0,
          "потраченное никогда не больше бюджета и никогда не отрицательно");
}

// ── 3. Аффиксный путь ПРОЙДЕН, и он же — негативный контроль ──────────────
// Без этого п.2 вакуумен: если аффиксы не выпали ни разу, «цена экземпляра»
// и «цена строки» совпадают тождественно, и закон никто не проверил.
void test_affix_path_is_exercised_and_priced() {
    // Богатый контекст: высокий уровень × полный байт опасности × богатое
    // место — все три колонки контекста говорят одной дверью affix_power.
    const LootContext ctx{255, 2.0f, 30};
    int withAffixes = 0, pricedByRow = 0, rounds = 0;
    for (std::uint32_t seed = 1u; seed <= 64u; ++seed) {
        Inventory bag{};
        rng_reset(seed * 40503u + 17u);
        const int spent = loot_issue(20000, ctx, &rng_lcg, bag);
        ++rounds;
        if (affixed_stacks(bag) > 0) {
            ++withAffixes;
            // НЕГАТИВНЫЙ КОНТРОЛЬ, КОТОРЫЙ РЕАЛЬНО ПАДАЕТ: заряди дверь
            // базовой ценой строки — и сумма разойдётся с потраченным.
            // Разойтись она обязана, иначе аффикс ничего не стоит и вся
            // проверка выше ни о чём.
            if (base_value_of_container(bag) == spent) ++pricedByRow;
        }
    }
    CHECK(rounds > 0, "цикл аффиксов обязан был отработать");
    CHECK(withAffixes > 0,
          "богатый контекст ОБЯЗАН хоть раз выдать аффиксную вещь — иначе "
          "закон цены экземпляра не проверен ни разу");
    CHECK(pricedByRow == 0,
          "цена ЭКЗЕМПЛЯРА обязана отличаться от цены СТРОКИ там, где выпал "
          "аффикс: заряжать базовую значило бы отдать миру больше, чем взято");
}

// ── 4. Полный контейнер отказывает, стоимость не сгорает ──────────────────
void test_full_container_refuses() {
    Inventory full{};
    // Предусловие рождается здесь: 1024 несливаемых стака (сид различает их,
    // same_kind_as его и сравнивает), так что свободного слота не остаётся и
    // слиться новому не с чем.
    const int ordinal = item_index("wood");
    CHECK(ordinal >= 0, "каталог обязан знать строку, которой забивают слоты");
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        ItemRef s{};
        s.def = std::uint16_t(ordinal);
        s.count = 1;
        s.seed = std::uint32_t(i) + 1u;   // 0 = «не прокатан», а нам нужен прокат
        full.slots[std::size_t(i)] = s;
    }
    CHECK(full.full(), "фикстура обязана быть ПОЛНОЙ, иначе проверка ни о чём");
    const int before = inventory_value(full);
    const LootContext ctx{};
    rng_reset(99u);
    const int spent = loot_issue(100000, ctx, &rng_lcg, full);
    CHECK(spent == 0, "полный контейнер отказывает, и режиссёр НЕ списывает казну");
    CHECK(inventory_value(full) == before,
          "отказанная выдача не меняет стоимости контейнера ни на единицу");
}

// ── 5. Один поток случайности — одна выдача ───────────────────────────────
void test_deterministic() {
    const LootContext ctx{64, 1.4f, 12};
    Inventory a{}, b{};
    rng_reset(2026u);
    const int sa = loot_issue(5000, ctx, &rng_lcg, a);
    rng_reset(2026u);
    const int sb = loot_issue(5000, ctx, &rng_lcg, b);
    CHECK(sa == sb, "та же казна и тот же поток тратят одинаково");
    CHECK(sa > 0, "и тратят хоть что-то — иначе сравнивались два нуля");
    int diff = 0;
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        const ItemRef& x = a.slots[std::size_t(i)];
        const ItemRef& y = b.slots[std::size_t(i)];
        if (x.def != y.def || x.count != y.count || !x.same_kind_as(y)) {
            if (!(x.empty() && y.empty())) ++diff;
        }
    }
    CHECK(diff == 0, "и кладут слот в слот одно и то же");
}

// ── 6. Бедная казна: дешевейшая строка — предел, а не отказ ───────────────
void test_poor_treasury_still_pays() {
    int cheapest = 0;
    for (const ItemDef& d : item_catalog()) {
        if (d.value > 0 && (cheapest == 0 || d.value < cheapest)) cheapest = d.value;
    }
    CHECK(cheapest > 0, "в каталоге обязана быть хоть одна оплатная строка");
    const LootContext ctx{};
    Inventory poor{};
    rng_reset(5u);
    CHECK(loot_issue(cheapest - 1, ctx, &rng_zero, poor) == 0,
          "казна беднее дешевейшей строки не выдаёт ничего");
    CHECK(poor.used_slots() == 0, "и контейнера не трогает");
    Inventory exact{};
    rng_reset(5u);
    const int spent = loot_issue(cheapest, ctx, &rng_zero, exact);
    CHECK(spent == cheapest,
          "казна ровно в дешевейшую строку тратится ДО НУЛЯ, а не наполовину");
    CHECK(inventory_value(exact) == spent, "и снова сходится по стоимости");
}

// ── 7. ЗАКОН ВЫБОРА: СОСТАВ МЕШКА ТЕЧЁТ ЗА КОНТЕКСТОМ ─────────────────────
// Режиссёр стоит на той же двери совпадения, что и спавн живности: вес строки
// симметричен вокруг «стоимостная сила == богатство контекста». Наблюдаемое
// следствие, за которое свидетель и держится, — СРЕДНЯЯ БАЗОВАЯ ЦЕНА СТРОКИ на
// единицу: богатый контекст обязан набивать мешок дороже бедного при ТОМ ЖЕ
// бюджете. Свидетель не считает ни силу, ни вес — он смотрит на мешок.
//
// СРАВНЕНИЕ ТРЁХТОЧЕЧНОЕ, И ЭТО НЕ ПЕРЕСТРАХОВКА, А ВЫВОД НЕГАТИВНОГО
// КОНТРОЛЯ. `affix_power` кормит ОДНИМ байтом и выбор строки, и щедрость
// аффиксов: аффиксная вещь в богатом контексте дороже, съедает больше бюджета
// и оставляет меньше дешёвых единиц — поэтому пара «бедный против богатого»
// расходится ДАЖЕ при равномерных весах (проверено: контроль её не покраснил
// ни по цене экземпляра, ни по базовой). Разделяет только ЛЕСТНИЦА: чтобы
// средняя цена росла на КАЖДОЙ ступени, нужен именно закон выбора — на
// равномерных весах ступень посередине ломается, и контроль краснеет.
void test_context_steers_composition() {
    struct Sample { long long base = 0, units = 0; int rounds = 0; };
    const auto measure = [](const LootContext& ctx) {
        Sample s{};
        for (std::uint32_t seed = 1u; seed <= 96u; ++seed) {
            Inventory bag{};
            rng_reset(seed * 2246822519u + 1u);
            (void)loot_issue(4000, ctx, &rng_lcg, bag);
            s.base += base_value_of_container(bag);
            s.units += units_in(bag);
            ++s.rounds;
        }
        return s;
    };
    // Три ступени ОДНОЙ лестницы контекста (уровень × опасность × богатство —
    // все три сходятся в `affix_power`), а не «до порога / после порога».
    const Sample poor   = measure(LootContext{0,   1.0f, 1});
    const Sample middle = measure(LootContext{96,  1.4f, 8});
    const Sample rich   = measure(LootContext{255, 2.0f, 40});
    CHECK(poor.rounds > 0 && middle.rounds > 0 && rich.rounds > 0,
          "все три контекста обязаны были отработать");
    CHECK(poor.units > 0 && middle.units > 0 && rich.units > 0,
          "каждый контекст обязан был хоть что-то выдать — иначе сравниваются "
          "три пустых мешка");
    const double cheap = double(poor.base) / double(poor.units);
    const double mid   = double(middle.base) / double(middle.units);
    const double dear  = double(rich.base) / double(rich.units);
    CHECK(mid > cheap && dear > mid,
          "состав мешка ТЕЧЁТ за контекстом: средняя базовая цена единицы "
          "растёт на КАЖДОЙ ступени лестницы, а не переключается порогом");
    // Хвост закона (пол веса 1, «исчезающе малая вероятность, но не ноль»)
    // держит свидетель самой двери совпадения — `subworld_spawn_parity_test`
    // уже утверждает и пик 1024, и пол 1. Второй копии здесь нет намеренно.
}

} // namespace

int main() {
    test_empty_treasury();
    test_value_is_conserved();
    test_affix_path_is_exercised_and_priced();
    test_full_container_refuses();
    test_deterministic();
    test_poor_treasury_still_pays();
    test_context_steers_composition();
    return sm::test::report("loot_pool_test");
}
