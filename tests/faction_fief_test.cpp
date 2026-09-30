// ФЕОДАЛЬНЫЙ ПУЛ РЁБЕР (macro/factions.h, v121) — законы, которые держат
// три двери tithe_edge/tithe_edge_add/tithe_edge_remove:
//   · ИНВАРИАНТ ПУЛА: отрезки строк лежат в порядке индекса фракции без дыр
//     (fiefStart[i] = Σ fiefCount[j<i]), внутри отрезка рёбра отсортированы
//     по ординалу вассала — на этом стоит бинарный поиск;
//   · вставка идемпотентна; ординал 0 — «никто» и ребра не заводит (закон
//     нуля-ординала); удаление прощает долг и сдвигает последующие отрезки;
//   · кап пула отказывает ГРОМКО (nullptr), а не молча пишет мимо.
// Негативный контроль: детектор инварианта обязан ВИДЕТЬ руками сломанный
// пул (§8 п.6 — иначе мы верим слепому детектору).
#include "check.h"

#include "macro/factions.h"

#include <cstdint>

namespace {

using namespace sm;

// Детектор инварианта пула — перевычисления продакшена здесь нет: двери
// ничего подобного не считают, они инвариант ПОДДЕРЖИВАЮТ, а он их судит.
bool pool_invariant_holds(const FactionState& f) {
    int expectStart = 0;
    for (int i = 0; i < kMaxFactions; ++i) {
        const FactionRow& r = f.rows[i];
        if (r.fiefStart != expectStart) return false;
        for (int k = r.fiefStart + 1; k < r.fiefStart + r.fiefCount; ++k) {
            if (!(f.fief[k - 1].vassal < f.fief[k].vassal)) return false;
        }
        expectStart += r.fiefCount;
    }
    return expectStart == f.fiefTotal;
}

void test_insert_search_remove_hold_the_invariant() {
    FactionState f{};
    claim_registry_rows(f);

    // Вставки вразнобой по трём фракциям и не по порядку вассалов.
    CHECK(tithe_edge_add(f, 2, 300, 40) != nullptr, "ребро (2,300) встало");
    CHECK(tithe_edge_add(f, 0, 500, 10) != nullptr, "ребро (0,500) встало");
    CHECK(tithe_edge_add(f, 2, 100, 40) != nullptr, "ребро (2,100) встало");
    CHECK(tithe_edge_add(f, 5, 200, 90) != nullptr, "ребро (5,200) встало");
    CHECK(tithe_edge_add(f, 0, 400, 10) != nullptr, "ребро (0,400) встало");
    CHECK(f.fiefTotal == 5, "пул держит пять рёбер");
    CHECK(pool_invariant_holds(f), "инвариант пула после вставок");

    // Идемпотентность: повторная вставка возвращает ТО ЖЕ ребро.
    TitheEdge* e = tithe_edge_add(f, 2, 100, 40);
    e->owedValue = 77;
    CHECK(tithe_edge_add(f, 2, 100, 40) == e, "повторная вставка = то же ребро");
    CHECK(f.fiefTotal == 5, "идемпотентная вставка пул не растит");

    // Поиск: своё ребро в своей строке, чужая строка отвечает «нет».
    CHECK(tithe_edge(f, 2, 100) == e, "бинарный поиск нашёл ребро");
    CHECK(tithe_edge(f, 0, 100) == nullptr, "чужая строка ребра не видит");
    CHECK(tithe_edge(f, 2, 999) == nullptr, "несуществующий вассал — nullptr");

    // Закон нуля-ординала: 0 — «никто», ребра не бывает.
    CHECK(tithe_edge_add(f, 2, 0, 40) == nullptr, "вассал 0 ребра не заводит");
    CHECK(tithe_edge_add(f, 2, 100, 0) == nullptr, "сюзерен 0 ребра не заводит");
    CHECK(tithe_edge(f, 2, 0) == nullptr, "поиск по нулю — «никто»");

    // Удаление: долг прощён (ребра нет), последующие отрезки съехали.
    tithe_edge_remove(f, 2, 100);
    CHECK(tithe_edge(f, 2, 100) == nullptr, "снятое ребро мертво");
    CHECK(f.fiefTotal == 4, "пул похудел на одно");
    CHECK(pool_invariant_holds(f), "инвариант пула после удаления");
    CHECK(tithe_edge(f, 5, 200) != nullptr
              && tithe_edge(f, 5, 200)->suzerain == 90,
          "ребро строки ПОСЛЕ точки удаления живо и цело");

    // Негативный контроль детектора: руками сломанный порядок ВИДЕН.
    FactionState broken{};
    claim_registry_rows(broken);
    CHECK(tithe_edge_add(broken, 1, 10, 5) != nullptr, "контрольное ребро 1");
    CHECK(tithe_edge_add(broken, 1, 20, 5) != nullptr, "контрольное ребро 2");
    CHECK(pool_invariant_holds(broken), "контрольный пул до поломки цел");
    const std::int32_t keep = broken.fief[0].vassal;
    broken.fief[0].vassal = broken.fief[1].vassal + 1;   // порядок сломан
    CHECK(!pool_invariant_holds(broken),
          "детектор ВИДИТ сломанный порядок (негативный контроль)");
    broken.fief[0].vassal = keep;
}

void test_cap_refuses_loudly() {
    static FactionState f{};   // 793 КиБ — не для стека
    claim_registry_rows(f);
    for (int i = 0; i < kMaxTitheEdges; ++i) {
        CHECK_OR_RETURN(tithe_edge_add(f, i % kFactionCount, i + 1, 999999)
                            != nullptr,
                        "пул принимает ровно кап рёбер");
    }
    CHECK(f.fiefTotal == kMaxTitheEdges, "пул полон");
    CHECK(tithe_edge_add(f, 0, kMaxTitheEdges + 7, 999999) == nullptr,
          "кап отказывает nullptr-ом, а не пишет мимо");
    CHECK(pool_invariant_holds(f), "полный пул держит инвариант");
}

} // namespace

int main() {
    test_insert_search_remove_hold_the_invariant();
    test_cap_refuses_loudly();
    return sm::test::report("faction_fief_test");
}
