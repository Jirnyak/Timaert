// ── СВИДЕТЕЛЬ ЗАКОНА ПОДПИСИ ФИЧИ (M-433 п.1) ─────────────────────────────
//
// ЧТО ОН ОХРАНЯЕТ, И ЭТО ЗАКОН, А НЕ СЕГОДНЯШНЕЕ ПОВЕДЕНИЕ (§8 п.5). Вердикт
// владельца 2026-11-08: подпись фичи «вывести из существующего контента».
// Значит у подписи НЕТ своего носителя — она ВЫВОД из колонок, которые у
// строки уже есть и у которых уже есть свои читатели. Отсюда четыре
// утверждения, и каждое ловит свой класс дефекта:
//
//   1) ОТВЕТ ЕСТЬ У КАЖДОГО РОДА, У КОТОРОГО ЕСТЬ НОСИТЕЛЬ. Пустых ровно
//      двое, и оба НАЗВАНЫ: `FT_None` (на клетке не построено ничего — пусто
//      ПО ЗАКОНУ) и `FT_Field` (пашня: её `numberRow` есть `Count`
//      намеренно, `yieldItem` пуст, рода она не несёт — выводить НЕ ИЗ ЧЕГО,
//      и это названный остаток наряда). Список пустых утверждается ЦЕЛИКОМ:
//      появится третий — прибор потребует сказать, кто он;
//   2) АВТОРСКАЯ КОЛОНКА ЕСТЬ ОСТАТОК ВЫВОДА, А НЕ ВТОРОЙ ОТВЕТ.
//      `FeatureDef::caption` несут РОВНО те строки, у которых нет ни одной
//      выводящей колонки. Авторское слово на строке, которая и так выводится,
//      было бы двумя ответами на один вопрос о мире (DOD п.6) — и красит
//      здесь;
//   3) ЯКОРЬ НА КАЖДУЮ КОЛОНКУ — ОТНОШЕНИЕМ, НЕ ПЕРЕСЧЁТОМ ФОРМУЛЫ (§8 п.5,
//      п.6 «тождество одной формулы слепо к самой формуле»). Шесть шахт
//      обязаны дать ШЕСТЬ РАЗНЫХ подписей, пять поселений — ПЯТЬ РАЗНЫХ:
//      вывод, свалившийся к одному источнику или к умолчанию, схлопнет
//      группу, и схлопывание видно без знания правильных слов;
//   4) КЛЮЧ ВЫХОДА РЕЗОЛВИТСЯ. Это ЗАКОН, независимый от подписи: `yieldItem`
//      есть авторский ключ каталога (ЗАКОН СЛОВАРЯ), и опечатка в нём молча
//      отменяет добычу шахты, а не только её подпись.
//
// СЛОВ ЗДЕСЬ НЕ УТВЕРЖДАЕТСЯ НИ ОДНОГО — НАРОЧНО. Подписи суть КОНТЕНТ и
// БАЛАНС (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ п.1), владелец переписывает их строкой
// каталога свободно и молча; прибитый литерал сделал бы прибор тормозом этой
// правки (§8 п.7).
//
// ── ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ (ПРОГНАНА 2026-11-08) ───────
//   КРАСНЫЕ — детектор обязан увидеть, и увидел:
//     · `FT_Road.caption` → `nullptr` (у ленты отобрали единственный
//       носитель) ................................ КРАСНЫЙ, 1 FAIL из 5;
//     · `FT_IronMine.caption = "Mine"` на строке, которая и так выводится,
//       то есть ВТОРОЙ ОТВЕТ ..................... КРАСНЫЙ, 1 FAIL из 5;
//     · `FT_IronMine.yieldItem` `"iron"` → `"irn"` (ключ не резолвится) —
//       красит ДВА утверждения разом, вывод и ключ . КРАСНЫЙ, 2 FAIL из 5;
//     · все шесть шахт переведены на `"stone"`, то есть группа схлопнулась
//       в один источник .......................... КРАСНЫЙ, 1 FAIL из 5.
//   МОЛЧАЩИЕ — детектор обязан пройти мимо, иначе он шум, а не детектор:
//     · `FT_Road.caption` `"Road"` → `"Roadway"` (правка СЛОВА, то есть
//       контента) ................................ ЗЕЛЁНЫЙ, 5/5;
//     · `FT_GoldMine.mapColor` → другой цвет (правка соседней колонки,
//       к подписи не относящейся) ................ ЗЕЛЁНЫЙ, 5/5.
//   НЕГОДНАЯ МУТАЦИЯ, ЗАПИСАНА ЧТОБЫ ЕЁ НЕ СДЕЛАЛИ ЗАНОВО:
//     · `FT_Pasture.numberRow` `Livestock` → `Vein` — выглядит как снос
//       клаузы 3, но ряд жилы отдаёт `yieldCreature == Count` и
//       `yieldItem == nullptr`, так что подпись пустеет и красит
//       утверждение 1 — то же самое, что мутация ключа выше, и о клаузе 3
//       она не говорит НИЧЕГО. Чтобы судить клаузу 3, бить надо по
//       `yieldCreature` строки ряда, а она в `macro_stock.cpp`.
#include "check.h"

#include "macro/feature_caption.h"

#include <cstdio>
#include <cstring>

namespace {

bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

// (1) У КАЖДОГО РОДА С НОСИТЕЛЕМ ЕСТЬ ОТВЕТ, И ПУСТЫЕ НАЗВАНЫ ПОИМЁННО.
void test_every_rod_with_a_carrier_answers()
{
    int answered = 0;
    int emptyUnexpected = 0;
    for (std::size_t i = 0; i < std::size_t(sm::FT_Count); ++i) {
        const sm::FeatureType f = sm::FeatureType(i);
        const char* cap = sm::feature_caption(f);
        const bool expectedEmpty = (f == sm::FT_None || f == sm::FT_Field);
        // Слова ПЕЧАТАЮТСЯ, но не утверждаются: владельцу они нужны глазами
        // (он их автор и перепишет строкой), а прибору — нет (§8 п.4).
        std::printf("[caption] %2zu → «%s»\n", i, cap);
        if (cap[0]) {
            ++answered;
            if (expectedEmpty) {
                ++emptyUnexpected;   // названный остаток ожил — скажи, кто он
                std::printf("[caption] %zu: закон звал его пустым, а он "
                            "отвечает «%s»\n", i, cap);
            }
        } else if (!expectedEmpty) {
            ++emptyUnexpected;
            std::printf("[caption] род %zu молчит, а носитель у него есть\n", i);
        }
    }
    std::printf("[caption] подписей %d из %zu\n", answered,
                std::size_t(sm::FT_Count));
    CHECK(emptyUnexpected == 0,
          "подпись отвечает КАЖДОМУ роду с носителем; пустых ровно двое — "
          "FT_None (не построено ничего) и FT_Field (выводить не из чего)");
}

// (2) АВТОРСКОЕ СЛОВО — РОВНО ТАМ, ГДЕ ВЫВОДИТЬ НЕ ИЗ ЧЕГО.
void test_authored_column_is_the_remainder_not_a_second_answer()
{
    int authored = 0;
    int overlapping = 0;
    for (const sm::FeatureDef& d : sm::kFeatureDefs) {
        const bool derivable =
            d.kind != sm::SquadType::None || d.yieldItem != nullptr
            || d.numberRow != sm::ResourceFieldId::Count;
        if (d.caption) {
            ++authored;
            if (derivable) {
                ++overlapping;
                std::printf("[caption] строка %d несёт И авторское слово, И "
                            "выводящую колонку — ответов стало два\n",
                            int(d.type));
            }
        }
    }
    std::printf("[caption] авторских слов %d, пересечений с выводом %d\n",
                authored, overlapping);
    CHECK(authored > 0 && overlapping == 0,
          "`FeatureDef::caption` есть ОСТАТОК вывода: её несут только строки "
          "без выводящей колонки, иначе это второй ответ (DOD п.6)");
}

// (3) ЯКОРЬ ОТНОШЕНИЕМ: группа не схлопывается к одному источнику.
void test_groups_do_not_collapse_to_one_source()
{
    const char* mines[std::size_t(sm::FT_Count)] = {};
    const char* rooted[std::size_t(sm::FT_Count)] = {};
    int nMines = 0, nRooted = 0;
    for (const sm::FeatureDef& d : sm::kFeatureDefs) {
        const char* cap = sm::feature_caption(d.type);
        if (d.kind != sm::SquadType::None) rooted[nRooted++] = cap;
        else if (d.numberRow == sm::ResourceFieldId::Vein && d.yieldItem)
            mines[nMines++] = cap;
    }
    int mineClashes = 0, rootedClashes = 0;
    for (int i = 0; i < nMines; ++i)
        for (int j = i + 1; j < nMines; ++j)
            if (same(mines[i], mines[j])) ++mineClashes;
    for (int i = 0; i < nRooted; ++i)
        for (int j = i + 1; j < nRooted; ++j)
            if (same(rooted[i], rooted[j])) ++rootedClashes;
    std::printf("[caption] шахт %d (совпадений %d), поселений %d "
                "(совпадений %d)\n", nMines, mineClashes, nRooted,
                rootedClashes);
    CHECK(nMines > 1 && mineClashes == 0,
          "шахты обязаны различаться подписями — одинаковые значат, что "
          "вывод свалился к одному источнику, а не прочёл колонку строки");
    CHECK(nRooted > 1 && rootedClashes == 0,
          "поселения обязаны различаться подписями — схлопывание группы "
          "видно без знания правильных слов");
}

// (4) КЛЮЧ ВЫХОДА РЕЗОЛВИТСЯ — закон, независимый от подписи.
void test_every_yield_key_resolves_in_the_catalog()
{
    int keys = 0, unresolved = 0;
    for (const sm::FeatureDef& d : sm::kFeatureDefs) {
        if (!d.yieldItem) continue;
        ++keys;
        if (sm::item_def_at(sm::item_index(d.yieldItem)) == nullptr) {
            ++unresolved;
            std::printf("[caption] строка %d: ключ выхода «%s» каталогом не "
                        "резолвится — добыча отменена МОЛЧА\n",
                        int(d.type), d.yieldItem);
        }
    }
    std::printf("[caption] ключей выхода %d, нерезолвимых %d\n", keys,
                unresolved);
    CHECK(keys > 0 && unresolved == 0,
          "`FeatureDef::yieldItem` есть авторский ключ каталога: опечатка в "
          "нём отменяет добычу шахты, а не только её подпись");
}

} // namespace

int main()
{
    test_every_rod_with_a_carrier_answers();
    test_authored_column_is_the_remainder_not_a_second_answer();
    test_groups_do_not_collapse_to_one_source();
    test_every_yield_key_resolves_in_the_catalog();
    return sm::test::report("feature_caption_test");
}
