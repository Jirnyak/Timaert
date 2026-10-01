// ── ГЕЙТ НА ССЫЛКИ ДОКУМЕНТОВ ──────────────────────────────────────────────
//
// Судит, а не правит: вторая дверь к тому же ядру (`doc_refs.cpp`), правящая —
// таргет `docs_sync`. Разбора ссылок в проекте один (§8 п.5).
//
// ЧТО ОН ОХРАНЯЕТ (закон, а не текущее поведение — §8 п.5 ЗАКОНА НУЛЕВОГО):
// номер строки в документе есть ПРОИЗВОДНОЕ от имени или отпечатка, и потому
//   1. ссылка не может сгнить молча — сдвиг красит гейт и лечится одной
//      командой, а не поиском руками;
//   2. ссылку БЕЗ имени и отпечатка нельзя добавить: отслеживать её нечем.
//      Уже написанные такие живут в белом списке, который умеет только ТАЯТЬ —
//      это пассивная ревизия: кто правит абзац, приводит его ссылки в форму.
//
// ПОЧЕМУ БЕЛЫЙ СПИСОК, А НЕ МИГРАЦИЯ ОДНИМ ДНЁМ. Перепись 2026-09-27: ссылок с
// номером 1251 в четырёх доках законов, из них проверяемых вообще 351, сгнивших
// среди них 120. Массовая правка 1251 ссылки — не одна сессия, и скриптом по
// имени она запрещена (§5 п.5, шрам сплошной замены свёртки). Гейт на НОВОЕ
// останавливает рост долга сегодня; долг тает по ходу обычной работы.
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "doc_refs.h"

#ifndef TIMAERT_SOURCE_DIR
#error "TIMAERT_SOURCE_DIR must name the repo root - see CMakeLists.txt"
#endif

namespace {

using sm::docrefs::Kind;
using sm::docrefs::Ref;

constexpr const char* kRoot = TIMAERT_SOURCE_DIR;
constexpr const char* kLegacyList = "tests/data/doc_refs_legacy.txt";
constexpr const char* kFixture = "tests/data/doc_refs_fixture.md";

// ТВЁРДЫМ отказом, который не терпится никогда, объявлены РОВНО ТРИ рода — и
// все три объективны БЕЗ разбора смысла, то есть якорь в них не участвует
// вовсе: файла нет, строки нет (файл короче), имя файла в дереве не одно. Их
// сегодня 21, и все 21 починены руками в этой же сессии.
//
// Остальное терпится белым списком — по слову владельца (2026-09-27): «лучше
// недобздеть чем перебздеть… лучше пропустить одну ссылку и потом вручную её
// обновить». Причина числом: вердикты, опирающиеся на ЯКОРЬ, на первом прогоне
// дали 122 «иголки в файле нет», из которых настоящими были единицы —
// остальное якорь-число, обрывок фразы, цитата владельца. Прибор, который
// обвиняет зря, заставляет подгонять доки под себя, а это ровно то, чем
// кончается ЗАКОН НУЛЕВОЙ п.4.
bool whitelistable(Kind k) {
    return k == Kind::Untracked || k == Kind::NameElsewhere ||
           k == Kind::NeedleMany || k == Kind::NeedleGone;
}

// Сверка со списком — ДВЕ величины, и обе обязаны быть нулём. `unlisted` ловит
// РОСТ долга (новая ссылка старой формы), `stale` — врущий список (строка,
// которой в доках уже нет). Функция одна, потому что на неё стоит негативный
// контроль ниже: механизм «список умеет только таять» иначе остался бы
// проверенным руками, то есть непроверенным.
struct LegacyDiff {
    int unlisted = 0;
    int stale = 0;
};

LegacyDiff compare_legacy(const std::vector<Ref>& refs,
                          const std::set<std::string>& allowed, bool loud) {
    LegacyDiff d;
    std::set<std::string> seen;
    for (const Ref& r : refs) {
        if (!whitelistable(r.kind)) continue;
        const std::string key = sm::docrefs::legacy_key(r);
        seen.insert(key);
        if (allowed.count(key) != 0) continue;
        ++d.unlisted;
        if (loud)
            std::fprintf(stderr, "  НОВАЯ СТАРОФОРМЕННАЯ %s:%d  %s  %s\n",
                         r.doc.c_str(), r.docLine, r.raw.c_str(),
                         sm::docrefs::kind_name(r.kind));
    }
    for (const std::string& key : allowed)
        if (seen.count(key) == 0) {
            ++d.stale;
            if (loud)
                std::fprintf(stderr, "  УСТАРЕЛА СТРОКА СПИСКА  %s\n",
                             key.c_str());
        }
    return d;
}

// ГРАНИЦА ПРИБОРА, ЧИСЛАМИ. Она не выводится из зелени и обязана ехать в
// вердикте: почти половина разобранных ссылок — «не отслеживается» (проза,
// внешние имена, пути вне дерева), а ЧИСЛА внутри документов не судит никто и
// не может (`doc_refs.h`: у числа обязана стоять МЕРА, а мере свидетеля нет).
struct Boundary {
    std::size_t docs = 0;       // документов корня прочитано
    std::size_t refs = 0;       // ссылок разобрано
    int untracked = 0;          // из них вне слежения
};

Boundary test_real_docs() {
    const sm::docrefs::Scan scan = sm::docrefs::scan_tree(kRoot, {});

    CHECK(scan.notes.empty(), "прибор прочитал все документы корня");
    CHECK(scan.filesIndexed > 100, "дерево проиндексировано (файлов > 100)");
    CHECK(scan.docs.size() >= 3, "документов корня найдено не меньше трёх");
    // Счёт обязателен: прибор, прошедший по пустому списку, отчитался бы
    // успехом, и это ровно тот род зелёного, против которого §8 п.2.
    CHECK(scan.refs.size() > 100, "ссылок разобрано больше ста");

    std::vector<std::string> legacy;
    CHECK(sm::docrefs::read_lines(std::string(kRoot) + "/" + kLegacyList,
                                  legacy),
          "белый список легаси-ссылок читается");
    std::set<std::string> allowed;
    for (const std::string& l : legacy)
        if (!l.empty() && l[0] != '#') allowed.insert(l);

    std::map<Kind, int> tally;
    int hardBroken = 0, needSync = 0;
    for (const Ref& r : scan.refs) {
        ++tally[r.kind];
        if (r.kind == Kind::NeedsSync) {
            ++needSync;
            std::fprintf(stderr, "  СДВИГ %s:%d  %s -> :%d  [%s]\n",
                         r.doc.c_str(), r.docLine, r.raw.c_str(), r.found,
                         r.needleText.c_str());
        } else if (r.kind != Kind::Ok && !whitelistable(r.kind)) {
            ++hardBroken;
            std::fprintf(stderr, "  ПОЛОМАНА %s:%d  %s  [%s]  %s\n",
                         r.doc.c_str(), r.docLine, r.raw.c_str(),
                         r.needleText.c_str(), sm::docrefs::kind_name(r.kind));
        }
    }

    CHECK(hardBroken == 0,
          "поломанных ссылок нет: файл есть, строка есть, имя файла одно");
    CHECK(needSync == 0,
          "номера свежие — иначе: cmake --build build --target docs_sync");

    const LegacyDiff diff = compare_legacy(scan.refs, allowed, true);
    CHECK(diff.unlisted == 0,
          "ссылок старой формы не добавляли: рядом с номером обязано стоять "
          "имя в бэктиках или отпечаток «цитатой»");
    // Список может только ТАЯТЬ. Строка, которой в доках больше нет,
    // вычёркивается — иначе список сам станет тем, что он охраняет: записью,
    // которая помнит мир, какого уже нет.
    CHECK(diff.stale == 0, "в белом списке нет строк, которых в доках уже нет");

    std::printf(
        "doc_refs: документов %zu, файлов дерева %d, ссылок %zu\n"
        "  цела %d · сдвиг отпечатка %d · отпечаток не один %d\n"
        "  имя+номер (старая форма) %d · иголки нет %d · строки нет %d\n"
        "  файла нет %d · имя файла не одно %d · не отслеживается %d\n"
        "  белый список: %zu строк (может только таять)\n",
        scan.docs.size(), scan.filesIndexed, scan.refs.size(),
        tally[Kind::Ok], tally[Kind::NeedsSync], tally[Kind::NeedleMany],
        tally[Kind::NameElsewhere], tally[Kind::NeedleGone],
        tally[Kind::PastEof], tally[Kind::NoFile], tally[Kind::Ambiguous],
        tally[Kind::Untracked], allowed.size());

    return Boundary{scan.docs.size(), scan.refs.size(), tally[Kind::Untracked]};
}

// ── НЕГАТИВНЫЙ КОНТРОЛЬ ───────────────────────────────────────────────────
// Прибор, который не умеет краснеть, бесполезен (§8 п.6). Фикстура несёт по
// одной намеренно гнилой ссылке на КАЖДЫЙ род отказа, и утверждается точный
// СОСТАВ вердиктов, а не их число: «семь отказов» прошло бы и тогда, когда
// семь ссылок упали одним и тем же родом, а шесть детекторов мертвы.
void test_fixture_detects_every_rot() {
    const sm::docrefs::Scan scan = sm::docrefs::scan_tree(kRoot, {kFixture});
    std::map<Kind, int> got;
    for (const Ref& r : scan.refs) ++got[r.kind];

    const std::pair<Kind, int> expect[] = {
        {Kind::Ok, 3},  // по имени на строке, по отпечатку, и `symbol@path`
        {Kind::NeedsSync, 1},     {Kind::NeedleMany, 1},
        {Kind::NameElsewhere, 1}, {Kind::NeedleGone, 1},
        {Kind::PastEof, 1},       {Kind::NoFile, 1},
        {Kind::Ambiguous, 1},     {Kind::Untracked, 1},
    };
    int expectedTotal = 0;
    for (const auto& [kind, n] : expect) {
        expectedTotal += n;
        if (got[kind] != n)
            std::fprintf(stderr, "  фикстура: %s — ждали %d, вышло %d\n",
                         sm::docrefs::kind_name(kind), n, got[kind]);
        CHECK(got[kind] == n, sm::docrefs::kind_name(kind));
    }
    CHECK(static_cast<int>(scan.refs.size()) == expectedTotal,
          "в фикстуре ровно одиннадцать ссылок — лишнюю прибор не выдумал");

    // Сам негативный контроль утверждается: три рода из одиннадцати ЦЕЛЫ,
    // значит прибор не валит всё подряд — иначе вера в детектор была бы верой
    // в шум.
    CHECK(got[Kind::Ok] == 3, "детектор не валит всё подряд: три ссылки целы");

    // И правка НЕ ТРОГАЕТ то, чего не понимает: из восьми гнилых машина берётся
    // ровно за ОДНУ — сдвинувшийся отпечаток с единственным вхождением. Это и
    // есть граница автоправки, записанная числом, а не обещанием.
    std::string report;
    const int fixable = sm::docrefs::apply_sync(kRoot, scan, false, &report);
    CHECK(fixable == 1, "машина правит ровно одну из восьми гнилых ссылок");
}

// Негативный контроль на САМ МЕХАНИЗМ СПИСКА. Без него «список умеет только
// таять» было бы обещанием: сверка множеств — ровно то место, где опечатка
// делает гейт вечно зелёным, и никакой прогон этого не покажет.
void test_legacy_list_mechanism() {
    const sm::docrefs::Scan fx = sm::docrefs::scan_tree(kRoot, {kFixture});
    std::vector<std::string> keys;
    for (const Ref& r : fx.refs)
        if (whitelistable(r.kind)) keys.push_back(sm::docrefs::legacy_key(r));
    CHECK_OR_RETURN(keys.size() >= 2,
                    "в фикстуре есть хотя бы две терпимые ссылки");

    const std::set<std::string> full(keys.begin(), keys.end());
    const LegacyDiff clean = compare_legacy(fx.refs, full, false);
    CHECK(clean.unlisted == 0 && clean.stale == 0,
          "полный список даёт ноль расхождений — детектор не шумит");

    std::set<std::string> minusOne = full;
    minusOne.erase(minusOne.begin());
    const LegacyDiff grown = compare_legacy(fx.refs, minusOne, false);
    CHECK(grown.unlisted >= 1 && grown.stale == 0,
          "ссылка вне списка ловится как РОСТ долга");

    std::set<std::string> plusGhost = full;
    plusGhost.insert("doc_refs_fixture.md\tghost_that_no_doc_mentions.cpp:1");
    const LegacyDiff ghost = compare_legacy(fx.refs, plusGhost, false);
    CHECK(ghost.stale == 1 && ghost.unlisted == 0,
          "строка списка без ссылки в доках ловится как УСТАРЕВШАЯ");
}

}  // namespace

int main() {
    const Boundary b = test_real_docs();
    test_fixture_detects_every_rot();
    test_legacy_list_mechanism();

    char scope[200];
    std::snprintf(scope, sizeof(scope),
                  "доков %zu · ссылок %zu · вне слежения %d · числа в доках НЕ "
                  "судит", b.docs, b.refs, b.untracked);
    return sm::test::report("doc_refs_test", scope);
}
