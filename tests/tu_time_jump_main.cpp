// ── ТАРГЕТ `tu_time_jump` — ПРИБОР НАД РЕАЛЬНЫМ ЖУРНАЛОМ ───────────────────
//
//     ./build/tu_time_jump                 # журнал и ведомость рядом, в build/
//     ./build/tu_time_jump <.ninja_log> [<ведомость>]
//
// Код выхода: 0 — скачков нет; 1 — есть, и они названы; 2 — прибор ОСЛЕП
// (журнал не прочитан, или в нём нет ни одного TU). Слепота — не «всё хорошо»,
// и она говорится отдельным кодом ровно поэтому.
//
// ЗАПУСКАТЬ ПОСЛЕ СБОРКИ: прибор читает то, что ninja уже записала, и
// обновляет свою ведомость лучших времён. Ведомость и есть память прибора —
// ninja свой журнал сжимает (см. шапку `tu_time_jump.h`).
//
// ПОЧЕМУ ЭТО ТАРГЕТ, А НЕ ГЕЙТ `check` (вердикт владельца 2026-09-26): время в
// журнале СТЕННОЕ, оно помнит загрузку машины, холодные сборки и -j — красный
// `check` из-за того, что во время сборки смотрели кино, был бы ложью прибора.
// Красным `check` делает СТЕНА ПАМЯТИ (`header_cost_test`): она детерминирована.
// Логика самого прибора под тестом — `tu_time_jump_test`, на фикстуре.
#include <cstdio>
#include <string>
#include <vector>

#include "tu_time_jump.h"

int main(int argc, char** argv) {
    const char* logPath = (argc > 1) ? argv[1] : ".ninja_log";
    const std::string defaultBaseline =
        std::string(logPath).substr(0, std::string(logPath).find_last_of('/') + 1) +
        "tu_time_baseline.tsv";
    const char* basePath = (argc > 2) ? argv[2] : defaultBaseline.c_str();

    std::string text;
    if (!sm::build::read_text_file(logPath, text)) {
        std::fprintf(stderr, "tu_time_jump: не прочитан журнал '%s'\n", logPath);
        return 2;
    }

    const std::vector<sm::build::TuBest> baseline = sm::build::load_baseline(basePath);
    const sm::build::TuTimeReport r = sm::build::scan_ninja_log(text, baseline);
    if (r.tuCount == 0) {
        std::fprintf(stderr,
                     "tu_time_jump: в '%s' ни одного TU — прибор ослеп, "
                     "это не вердикт «чисто»\n",
                     logPath);
        return 2;
    }

    std::printf("tu_time_jump: %s\n", logPath);
    std::printf("  TU в журнале: %d — сравнимы %d, впервые видим %d\n", r.tuCount,
                r.comparable, r.fresh);
    std::printf("  медиана свежих времён: %.2f с; самый дорогой: %.2f с (%s)\n",
                r.medianSec, r.slowestSec, r.slowest.c_str());
    std::printf("  порог скачка: %.1f× против лучшего прошлого этого же TU\n",
                sm::build::kJumpRatio);

    if (!sm::build::save_baseline(basePath, r.best))
        std::fprintf(stderr, "tu_time_jump: ведомость '%s' НЕ записана — "
                             "следующий прогон будет слеп\n", basePath);
    else
        std::printf("  ведомость: %zu TU в '%s'\n", r.best.size(), basePath);

    if (r.comparable == 0)
        std::printf("  сравнивать не с чем: это ПЕРВЫЙ прогон по этой ведомости, "
                    "а не «чисто»\n");

    if (r.jumps.empty()) {
        std::printf("  скачков нет\n");
        return 0;
    }

    std::printf("  СКАЧКИ (%zu):\n", r.jumps.size());
    for (const sm::build::TuJump& j : r.jumps)
        std::printf("    %6.1f×  %8.2f → %8.2f с  %s\n", j.ratio, j.baselineSec,
                    j.latestSec, j.output.c_str());
    std::fprintf(stderr,
                 "tu_time_jump: %zu TU подорожал(и) против себя же в %.1f× и "
                 "больше — искать тело, которое платит каждый включивший "
                 "(AGENTS §5 п.13); механизм называть -ftime-trace, а не "
                 "рассуждением\n",
                 r.jumps.size(), sm::build::kJumpRatio);
    return 1;
}
