// Guards of the spell DATA registry (`kSpellDefs@src/tables/spells.h`).
//
// ЗДЕСЬ СТОЯЛ ЗАПРЕТ, КОТОРОГО В ЗАКОНАХ НЕТ (снято 2026-11-08 ревизией).
// Шапка говорила «ORDINALS ARE APPEND-ONLY… reordering or inserting mid-table
// must turn this red», и восемь пинов «ординал → id» это исполняли. Довод был
// «индекс строки едет в сейве (`Spire.spellOrdinal`)», и он мёртв дважды:
// колонки `spellOrdinal` в проекте нет вовсе (два упоминания, оба проза), а
// §10 разрешает ломать ординалы ПРЯМО: «Ломай раскладку, порядок полей,
// ординалы… свободно и молча». ЗАКОН ЛЕСТНИЦЫ п.3 добавляет: «„дописать в
// хвост, чтобы сохранённый ординал не переехал“ — МЁРТВЫЙ довод. Перестановка
// ординалов ЗАКОННА». То есть свидетель охранял не закон, а СЛУЧАЙ, и по §8
// п.7 («тест никогда не должен мешать развитию») он был тормозом: новая
// строка в СВОЮ группу (а лестница требует именно этого) красила его ложно.
//
// ЧТО ИЗ НЕГО ЖИВОЕ И ОСТАЛОСЬ: авторская строка не смеет ИСЧЕЗНУТЬ молча —
// это закон о содержании каталога, и он не зависит от порядка. Поэтому ниже
// утверждаются ИМЕНА, а не индексы, и правка порядка строк проходит молча.
//
// И ПОПУТНО НАЙДЕН НАСТОЯЩИЙ НОСИТЕЛЬ ПОРЯДКА, КОТОРОГО ШАПКА НЕ НАЗЫВАЛА:
// `magic_segment_matches_spells()@src/tables/actions.h` — `static_assert`,
// требующий, чтобы магический отрезок таблицы действий шёл СТРОКА В СТРОКУ с
// `kSpellDefs`. Это закон ПАРЫ таблиц, а не запрет перестановки: он говорит
// «чини ПОРЯДОК, а не стену», то есть переставлять можно — обе вместе. Пины
// же запрещали перестановку вообще, дублируя стену слабее её (§8 п.6).
//
// МУТАЦИЯ → ИСХОД (замерено 2026-11-08, обе полярности):
//   М1 переставить две строки ТОЛЬКО в `kSpellDefs` → **НЕСОБИРАЕМО** стеной
//      `magic_segment_matches_spells`, свидетель до прогона не доходит;
//   М2 переименовать `haste` ТОЛЬКО в `kSpellDefs` → **НЕСОБИРАЕМО** той же
//      стеной (id — ключ пары);
//   М3 переименовать `haste` в ОБЕИХ таблицах (стена довольна) →
//      **КРАСНЫЙ** свидетель, «FAIL 1 of 8», строка 41 — ровно тот закон,
//      который этот файл теперь и несёт.
#include "check.h"
#include "bus/anketa.h"

#include <string_view>

namespace {

using namespace sm;

void test_shipped_rows_do_not_vanish() {
    // Имена, уже отданные миру. Порядок этого списка НИЧЕГО не утверждает —
    // он и не может: строка вправе переехать в свою группу лестницы.
    constexpr std::string_view kShipped[] = {
        "fireball", "ice_shard", "magic_bolt", "lightning_chain",
        "energy_beam", "armageddon", "haste", "flight",
    };
    int samples = 0, missing = 0;
    for (const std::string_view id : kShipped) {
        ++samples;
        if (spell_find(id) == nullptr) ++missing;
    }
    CHECK(samples > 0 && missing == 0,
          "ни одна отданная миру строка каталога не исчезла — имя ищется "
          "дверью, поэтому перестановка строк проходит молча (§10)");
    CHECK(kSpellCount >= samples,
          "каталог не усох ниже числа отданных строк");
}

void test_lookup_roundtrip() {
    int samples = 0, bad = 0;
    for (int i = 0; i < kSpellCount; ++i) {
        ++samples;
        if (spell_find(kSpellDefs[i].id) != &kSpellDefs[i]) ++bad;
        if (spell_ordinal(kSpellDefs[i].id) != i) ++bad;
    }
    CHECK(samples > 0 && bad == 0,
          "every row is found by its own id and maps back to its own ordinal");
    // The detector can see absence: an id not in the table finds nothing.
    CHECK(spell_find("no_such_spell") == nullptr
              && spell_ordinal("no_such_spell") == -1,
          "an unknown id must resolve to nothing");
}

void test_keys_are_unique() {
    int samples = 0, dupIds = 0, dupHashes = 0;
    for (int i = 0; i < kSpellCount; ++i) {
        for (int j = i + 1; j < kSpellCount; ++j) {
            ++samples;
            if (std::string_view(kSpellDefs[i].id) == kSpellDefs[j].id)
                ++dupIds;
            if (stable_spell_id(kSpellDefs[i].id)
                    == stable_spell_id(kSpellDefs[j].id))
                ++dupHashes;
        }
    }
    CHECK(samples > 0 && dupIds == 0,
          "spell ids are the registry key — no two rows may share one");
    CHECK(dupHashes == 0,
          "the wire hash must separate every pair of shipped ids");
}

// A flavor array is a null-terminated prefix: entries, then nulls, no holes —
// spell_flavor_count() and every UI loop depend on it.
bool has_flavor_hole(const char* const (&items)[kMaxSpellFlavorItems]) {
    const int n = spell_flavor_count(items);
    for (int i = n; i < kMaxSpellFlavorItems; ++i)
        if (items[i]) return true;
    return false;
}

void test_rows_are_sane() {
    int samples = 0, bad = 0;
    for (const SpellDef& s : kSpellDefs) {
        ++samples;
        // Цена и охват уехали на СТРОКУ ДЕЙСТВИЯ (M-369, форма (А)): цену
        // судит action_table_test, «кастуем хоть где-то» стало невыразимым —
        // ось охвата авторится enum-ом из трёх значений, четвёртого нет.
        const bool ok =
            s.id && s.id[0] && s.name && s.name[0]
            && s.description && s.description[0]
            && s.tier >= 1 && s.tier <= 5
            && (!s.sustained || s.manaDrain > 0.0f)
            && s.statusEffect != nullptr
            && !has_flavor_hole(s.pros)
            && !has_flavor_hole(s.cons);
        if (!ok) ++bad;
    }
    CHECK(samples > 0 && bad == 0,
          "every row: named + described, tier 1..5, sustained spells drain, "
          "flavor arrays hole-free");
    // Negative control for the hole detector itself: a hole must be seen.
    const char* holed[kMaxSpellFlavorItems] = {"a", nullptr, "c", nullptr,
                                               nullptr};
    CHECK(has_flavor_hole(holed),
          "the flavor-hole detector must flag a null before a non-null");
}

} // namespace

int main() {
    test_shipped_rows_do_not_vanish();
    test_lookup_roundtrip();
    test_keys_are_unique();
    test_rows_are_sane();
    return sm::test::report("spell_registry_test");
}
