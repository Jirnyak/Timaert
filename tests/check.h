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

// The verdict, and the only one. Print a summary and hand ctest its exit code.
// Zero checks is a failure — loudly, by name, so it cannot be read as "passed".
[[nodiscard]] inline int report(const char* testName) {
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
    std::printf("OK %s (%d checks)\n", testName, g_checks);
    return 0;
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
