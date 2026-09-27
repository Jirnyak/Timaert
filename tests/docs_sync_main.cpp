// ── `docs_sync`: ПЕРЕПИСАТЬ НОМЕРА СТРОК В ДОКАХ ───────────────────────────
//
//     cmake --build build --target docs_sync
//
// Дверь, которая ПРАВИТ. Вторая дверь к тому же ядру — `doc_refs_test` — только
// судит; разбора ссылок в проекте один (§8 п.5: второй копии парсера нет).
//
// ПОЧЕМУ ЭТО ОТДЕЛЬНЫЙ ТАРГЕТ, А НЕ ЧАСТЬ СБОРКИ. Сборка, молча правящая файлы
// дерева, отбирает у `git status --short` смысл — а он в этом проекте стоит
// первым шагом каждой сессии (§5 п.12). Здесь правка — ЯВНОЕ действие, и её
// результат виден дифом.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "doc_refs.h"

#ifndef TIMAERT_SOURCE_DIR
#error "TIMAERT_SOURCE_DIR must name the repo root - see CMakeLists.txt"
#endif

int main(int argc, char** argv) {
    const std::string arg = argc > 1 ? argv[1] : "";
    // Без аргументов — правит; `--dry` только называет, ничего не трогая;
    // `--legacy` печатает белый список — тем же разбором, что его проверяет,
    // иначе список и гейт разъехались бы уже на второй правке.
    const bool write = arg != "--dry" && arg != "--legacy";

    const sm::docrefs::Scan scan =
        sm::docrefs::scan_tree(TIMAERT_SOURCE_DIR, {});

    if (arg == "--legacy") {
        std::vector<std::string> keys;
        for (const sm::docrefs::Ref& r : scan.refs) {
            using K = sm::docrefs::Kind;
            if (r.kind == K::Untracked || r.kind == K::NameElsewhere ||
                r.kind == K::NeedleMany || r.kind == K::NeedleGone)
                keys.push_back(sm::docrefs::legacy_key(r));
        }
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        for (const std::string& k : keys) std::printf("%s\n", k.c_str());
        return 0;
    }
    std::string report;
    const int changed =
        sm::docrefs::apply_sync(TIMAERT_SOURCE_DIR, scan, write, &report);

    std::printf("docs_sync: документов %zu, файлов дерева %d, ссылок %zu\n",
                scan.docs.size(), scan.filesIndexed, scan.refs.size());
    if (changed == 0) {
        std::printf("  сдвигов нет — номера в доках свежие\n");
    } else {
        std::printf("  %s %d номер(ов):\n", write ? "переписано" : "нашлось",
                    changed);
        std::fputs(report.c_str(), stdout);
    }

    // Что машина НЕ правит, она обязана назвать: молчание здесь читалось бы
    // как «всё в порядке», а это ровно тот род зелёного, против которого
    // написан §8 п.2.
    int human = 0;
    for (const sm::docrefs::Ref& r : scan.refs) {
        using K = sm::docrefs::Kind;
        if (r.kind == K::Ok || r.kind == K::NeedsSync || r.kind == K::Untracked)
            continue;
        if (human++ == 0)
            std::printf("\n  РУКАМИ (машина не угадывает):\n");
        std::printf("    %s:%d  %s  [%s]  %s", r.doc.c_str(), r.docLine,
                    r.raw.c_str(), r.needleText.c_str(),
                    sm::docrefs::kind_name(r.kind));
        if (r.found != 0) std::printf(", ближайшее вхождение :%d", r.found);
        std::printf("\n");
    }
    if (human != 0) std::printf("  итого руками: %d\n", human);
    return 0;
}
