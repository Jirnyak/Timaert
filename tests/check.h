// THE assertion authority for every test in this tree. There is no other way
// to fail a test, and that is the whole point.
//
// WHY THIS EXISTS. Three tests spelled their failure as
//
//     int  fail(const char* m) { ...; return 1; }
//     bool test_x() { ...; return fail("did not roll over"); }
//
// and `int 1` converted to `bool true` = PASS. Every failure in those files
// read as success; `world_tick_parity_test`, `macro_npc_ai_parity_test` and
// section 7 of `material_seam_test` were green for months while asserting
// nothing. No compiler flag catches this — verified on an isolate: -Wall
// -Wextra, -Wconversion, -Wint-in-bool-context and even -Weverything are all
// silent on this conversion. A flag was never going to be the defence.
//
// So the defence is structural: there is no status for anyone to return. A
// check writes into a counter; `main` ends with
//
//     return sm::test::report("my_test");
//
// and a verdict cannot be inverted, forgotten or swallowed, because nobody
// carries one.
//
// THE SECOND RULE, and the one that would have caught those three files on the
// day they broke: A TEST THAT RAN ZERO CHECKS IS A FAILED TEST. The same rule
// catches the whole family they belong to — a loop over an empty vector, an
// early return when a fixture did not build, a measurement whose sampling
// condition never fired. All of those end the same way today: silently green.
// Not any more; `report()` fails them by counting.
//
// Usage:
//     #include "check.h"
//     namespace { void test_thing() { CHECK(a == b, "a must equal b"); } }
//     int main() { test_thing(); return sm::test::report("thing_test"); }
#pragma once
#include <cstdio>

namespace sm::test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void check(bool ok, const char* what, const char* file, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::fprintf(stderr, "  FAIL %s:%d - %s\n", file, line, what);
}

inline int checks()   { return g_checks; }
inline int failures() { return g_failures; }

// THE THIRD RULE, И ОНО ПРО СТРОКУ ВЕРДИКТА, А НЕ ПРО КОД ВОЗВРАТА.
// `add_test()` потребляет ОДИН БИТ — код возврата, — а покрытие убывает МОЛЧА:
// удали `CHECK` из тела, сузи границу цикла, выпиши вызов из `main` — ни одно
// утверждение не упало, прогон вышел нулём, ctest ЗЕЛЁНЫЙ, и в дифе рядом с
// тестом ничего не видно. Напечатанный счёт читал НИКТО.
//
// Лечение — не второй счётчик, а ПИН НА НАПЕЧАТАННУЮ СТРОКУ:
// `PASS_REGULAR_EXPRESSION` в `CMakeLists.txt` матчит ровно эту строку, поэтому
// тест зелен только когда прогон ДОШЁЛ ДО КОНЦА И сел на ожидаемый счёт. Три
// свойства в одном регексе: регресс двигает число и регекс промахивается; креш
// до печати строки не печатает её вовсе; устаревший бинарник печатает старый
// счёт. Код возврата не умеет ни одного из трёх.
//
// ЧУЖОЙ ШРАМ, КУПИВШИЙ ЭТО. В соседнем проекте ту же задачу решали
// `WILL_FAIL`, который ИНВЕРТИРУЕТ код возврата. Это один бит «что-то упало»,
// никогда счёт: шесть из семи растяжек там были ЗАКРЫТЫМИ пинами, поэтому
// регресс любой из них двигал счёт провалов 2 → 3 — всё ещё не ноль, всё ещё
// инвертировано, ctest ЗЕЛЁНЫЙ. Шесть «гейтов», которые гейтами не были. Та же
// дыра глотала и креш: умри до печати — код возврата всё равно не ноль.
//
// ПОЧЕМУ СЧЁТ И ГРАНИЦА ПЕЧАТАЮТСЯ ОДНОЙ СТРОКОЙ. CTest ИЛИ-ит список
// `PASS_REGULAR_EXPRESSION`: два элемента означают «любой из двух», то есть
// строго СЛАБЕЕ одного. Потребовать оба числа можно только тогда, когда они
// стоят рядом — отсюда перегрузка с `scope` ниже, а не вторая строка печати.
//
// The verdict, and the only one. Print a summary and hand ctest its exit code.
// Zero checks is a failure — loudly, by name, so it cannot be read as "passed".
[[nodiscard]] inline int report(const char* testName, const char* scope) {
    if (g_checks == 0) {
        std::fprintf(stderr,
                     "FAIL %s: the test ran ZERO checks - it cannot pass.\n",
                     testName);
        return 1;
    }
    if (g_failures > 0) {
        std::fprintf(stderr, "FAIL %s: %d of %d checks failed\n",
                     testName, g_failures, g_checks);
        return 1;
    }
    // ГРАНИЦА ПРИБОРА ЕДЕТ В ЕГО ЖЕ ВЕРДИКТЕ, и это не украшение. Зелёный гейт
    // читается как «проверено всё» — а он проверил то, что умеет. Пока число
    // непроверенного стоит в PASS и запинено, PASS невозможно прочесть шире,
    // чем он есть, и расширение охвата становится видимым решением в дифе.
    if (scope && scope[0] != '\0') {
        std::printf("OK %s (%d checks) [%s]\n", testName, g_checks, scope);
    } else {
        std::printf("OK %s (%d checks)\n", testName, g_checks);
    }
    return 0;
}

// Свидетель без границы: у поведенческого теста её нет — он судит ПОВЕДЕНИЕ,
// а не охват дерева, и приписывать ему «границу» значило бы выдумывать число.
[[nodiscard]] inline int report(const char* testName) {
    return report(testName, nullptr);
}

} // namespace sm::test

// One check. The message states what MUST hold, so a failure line reads as the
// broken promise; file:line comes from the compiler, never from a stale string.
#define CHECK(expr, why) \
    ::sm::test::check(static_cast<bool>(expr), (why), __FILE__, __LINE__)

// A check whose failure makes the rest of THIS function meaningless (a fixture
// that did not build, a lookup that found nothing). The failure is recorded
// first, so bailing out never hides it. Only valid in a void function.
#define CHECK_OR_RETURN(expr, why)                       \
    do {                                                 \
        if (!static_cast<bool>(expr)) {                   \
            ::sm::test::check(false, (why), __FILE__, __LINE__); \
            return;                                      \
        }                                                \
        ::sm::test::check(true, (why), __FILE__, __LINE__); \
    } while (0)
