// ── ТЕСТ ПРИБОРА НА СКАЧОК ВРЕМЕНИ TU (наряд M-127) ───────────────────────
//
// Прибор проверяется ФИКСТУРОЙ, а не тем, что лежит в `build/` у запустившего:
// вердикт обязан быть детерминированным, а стенное время сборки таким не
// бывает. Свидетель — РЕАЛЬНАЯ пара из журнала: 8.1 → 124.5 с (M-127), и она
// ОБЯЗАНА краснеть ДВАЖДЫ: и когда прошлое уцелело в журнале, и когда ninja
// журнал сжала, а прошлое помнит только своя ведомость (на этом дереве
// 2026-09-26: 1291 TU, история у НУЛЯ — то есть второй случай и есть обычный).
//
// Негативные контроли (§8 п.6) — рядом, и каждый падал бы, огрубей прибор:
//   · обратное направление 124.5 → 8.1 (лечение) — молчит;
//   · замеренный шум машины 1.31 → 4.26 (3.25×) — молчит;
//   · мелкий TU 0.02 → 0.30 (15×, но дешевле медианы) — молчит;
//   · 0 мс → 200 с — кричит (0 мс значит «дешевле разрешения», не «бесплатно»);
//   · не-TU выходы (линковка, `ctest`) — не судятся вовсе;
//   · ведомость после регресса хранит ЛУЧШЕЕ, а не последнее — иначе регресс
//     объявил бы себя нормой на второй сборке и прибор замолчал бы сам.
// Плюс контракт ФОРМАТА на живом журнале: разбор проверяется против того, что
// ninja реально пишет, иначе фикстура доказывала бы только саму себя.
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "tu_time_jump.h"

namespace {

using sm::build::TuBest;
using sm::build::TuTimeReport;

// Одна строка журнала ninja: start_ms \t end_ms \t mtime \t output \t hash.
std::string row(long long startMs, long long endMs, const char* output) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%lld\t%lld\t1790000000000000000\t%s\tdeadbeef\n",
                  startMs, endMs, output);
    return std::string(buf);
}

// Дерево-фон: двадцать здоровых TU по 0.40 с задают медиану прогона, то есть
// пол шума. Без населения у прибора нет масштаба — ровно этим он и отличается
// от выдуманной абсолютной секунды.
std::string background() {
    std::string log = "# ninja log v7\n";
    for (int i = 0; i < 20; ++i) {
        char name[128];
        std::snprintf(name, sizeof(name), "CMakeFiles/timaert.dir/src/core/f%02d.cpp.o", i);
        log += row(0, 400, name);
    }
    return log;
}

const char* kSubject = "CMakeFiles/timaert.dir/src/app/main.cpp.o";
const std::vector<TuBest> kNoBaseline;

void test_witness_jump_from_log_history() {
    // РЕАЛЬНАЯ пара регресса 2026-09-25: 8.1 → 124.5 с, обе записи в журнале.
    std::string log = background();
    log += row(0, 8100, kSubject);
    log += row(0, 124500, kSubject);

    const TuTimeReport r = sm::build::scan_ninja_log(log, kNoBaseline);
    CHECK(r.tuCount == 21, "21 TU в фикстуре: 20 фоновых + подсудимый");
    CHECK(r.comparable == 1, "прошлое есть ровно у подсудимого");
    CHECK(r.fresh == 20, "фоновые TU видим впервые — и это сказано вслух");
    CHECK(r.medianSec > 0.39 && r.medianSec < 0.41, "медиана прогона = 0.40 с");
    CHECK_OR_RETURN(r.jumps.size() == 1, "пара 8.1 → 124.5 с ОБЯЗАНА краснеть");
    CHECK(r.jumps[0].output == kSubject, "виновный назван по имени выхода");
    CHECK(r.jumps[0].baselineSec > 8.09 && r.jumps[0].baselineSec < 8.11,
          "база — прошлое время этого же TU, 8.1 с");
    CHECK(r.jumps[0].latestSec > 124.49 && r.jumps[0].latestSec < 124.51,
          "свежее время — 124.5 с");
    CHECK(r.jumps[0].ratio > 15.3 && r.jumps[0].ratio < 15.4,
          "скачок 15.4× — и он выше порога 7×");
}

void test_witness_jump_from_baseline() {
    // ОБЫЧНЫЙ случай: ninja журнал сжала, прошлое помнит только ведомость.
    std::string log = background();
    log += row(0, 124500, kSubject);
    const std::vector<TuBest> baseline{TuBest{kSubject, 8.1}};

    const TuTimeReport r = sm::build::scan_ninja_log(log, baseline);
    CHECK(r.comparable == 1, "ведомость даёт прошлое там, где журнал его стёр");
    CHECK_OR_RETURN(r.jumps.size() == 1,
                    "8.1 → 124.5 с краснеет и по сжатому журналу — иначе прибор "
                    "слеп ровно тогда, когда он нужен");
    CHECK(r.jumps[0].ratio > 15.3 && r.jumps[0].ratio < 15.4, "тот же 15.4×");
}

void test_baseline_keeps_the_best_not_the_last() {
    // Ведомость после регресса обязана хранить 8.1, а не 124.5: иначе на
    // второй сборке прибор сравнит 124.5 с 124.5 и объявит регресс нормой.
    std::string log = background();
    log += row(0, 124500, kSubject);
    const std::vector<TuBest> baseline{TuBest{kSubject, 8.1}};

    const TuTimeReport r = sm::build::scan_ninja_log(log, baseline);
    double stored = -1.0;
    for (const TuBest& b : r.best)
        if (b.output == kSubject) stored = b.bestSec;
    CHECK(stored > 8.09 && stored < 8.11,
          "ведомость хранит ЛУЧШЕЕ время TU, а не последнее");
    CHECK(int(r.best.size()) == r.tuCount, "ведомость покрывает все TU прогона");

    // И второй прогон по этой же ведомости обязан кричать снова — регресс не
    // «привыкается».
    const TuTimeReport again = sm::build::scan_ninja_log(log, r.best);
    CHECK(again.jumps.size() == 1, "пока регресс не вылечен, прибор кричит каждый раз");
}

void test_cure_is_not_a_jump() {
    // Лечение — то же отношение, только вниз. Прибор, который кричит на
    // починку, глушится в первый же день.
    std::string log = background();
    log += row(0, 124500, kSubject);
    log += row(0, 8100, kSubject);

    const TuTimeReport r = sm::build::scan_ninja_log(log, kNoBaseline);
    CHECK(r.comparable == 1, "история есть — сравнение состоялось");
    CHECK(r.jumps.empty(), "подешевевший TU скачком НЕ является");
}

void test_machine_noise_is_not_a_jump() {
    // Замер 2026-09-26 по `build-reldeb/.ninja_log`: худшее подорожание TU
    // против себя же от одной лишь загрузки машины — 3.25× (journal_test.cpp.o).
    // Порог 7× стоит выше этого ЗАМЕРА, а не выше догадки.
    std::string log = background();
    log += row(0, 1310, kSubject);
    log += row(0, 4260, kSubject);

    const TuTimeReport r = sm::build::scan_ninja_log(log, kNoBaseline);
    CHECK(r.jumps.empty(), "замеренный шум машины 3.25× молчит");
}

void test_cheap_tu_cannot_hold_the_build() {
    // 15×, но 0.30 с дешевле медианы 0.40 с: такой TU физически не может
    // держать время сборки. Без этого пола прибор тонул бы в мелочи.
    std::string log = background();
    log += row(0, 20, kSubject);
    log += row(0, 300, kSubject);

    const TuTimeReport r = sm::build::scan_ninja_log(log, kNoBaseline);
    CHECK(r.jumps.empty(), "TU дешевле медианы прогона скачком не считается");
}

void test_zero_baseline_is_resolution_not_free() {
    // 0 мс = «дешевле разрешения журнала». Считать это нулём — значит потерять
    // отношение и промолчать на самом громком случае из возможных.
    std::string log = background();
    log += row(0, 0, kSubject);
    log += row(0, 200000, kSubject);

    const TuTimeReport r = sm::build::scan_ninja_log(log, kNoBaseline);
    CHECK_OR_RETURN(r.jumps.size() == 1, "0 мс → 200 с ОБЯЗАНО краснеть");
    CHECK(r.jumps[0].ratio > 1000.0, "база — разрешение журнала, 1 мс");
}

void test_only_translation_units_are_judged() {
    // Линковка и `ctest` плывут на порядок сильнее компиляции: `CMakeFiles/check`
    // держит время ВСЕЙ сюиты. Их шум глушил бы прибор.
    std::string log = background();
    log += row(0, 1000, "CMakeFiles/check");
    log += row(0, 91700, "CMakeFiles/check");
    log += row(0, 2000, "timaert");
    log += row(0, 60000, "timaert");

    const TuTimeReport r = sm::build::scan_ninja_log(log, kNoBaseline);
    CHECK(r.tuCount == 20, "не-TU выходы в счёт не идут");
    CHECK(r.jumps.empty(), "скачок линковки/ctest прибор не судит");
}

void test_real_log_format() {
    // КОНТРАКТ ФОРМАТА. Фикстуру пишу я — значит она доказывает только то, что
    // я умею её же и разобрать. Живой журнал пишет ninja: разъедется формат
    // (v8, наносекунды, новая колонка) — молчать об этом нельзя. Времена здесь
    // НЕ утверждаются: они стенные.
#ifdef TIMAERT_NINJA_LOG
    std::string text;
    CHECK_OR_RETURN(sm::build::read_text_file(TIMAERT_NINJA_LOG, text),
                    "живой " TIMAERT_NINJA_LOG " читается");
    const TuTimeReport r = sm::build::scan_ninja_log(text, kNoBaseline);
    CHECK(r.tuCount > 0, "в живом журнале разобран хотя бы один TU");
    CHECK(r.medianSec > 0.0, "медиана живого прогона положительна");
    CHECK(!r.slowest.empty(), "самый дорогой TU живого журнала назван");
    std::printf("  живой журнал: %d TU, медиана %.2f с, дороже всех %.2f с (%s)\n",
                r.tuCount, r.medianSec, r.slowestSec, r.slowest.c_str());
#else
    CHECK(false, "TIMAERT_NINJA_LOG обязан быть определён: сборка проекта — Ninja (§9)");
#endif
}

} // namespace

int main() {
    test_witness_jump_from_log_history();
    test_witness_jump_from_baseline();
    test_baseline_keeps_the_best_not_the_last();
    test_cure_is_not_a_jump();
    test_machine_noise_is_not_a_jump();
    test_cheap_tu_cannot_hold_the_build();
    test_zero_baseline_is_resolution_not_free();
    test_only_translation_units_are_judged();
    test_real_log_format();
    return sm::test::report("tu_time_jump_test");
}
