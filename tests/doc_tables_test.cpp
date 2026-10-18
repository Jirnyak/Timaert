// ГЕЙТ ЦЕЛОСТНОСТИ ТАБЛИЦ ДОКОВ — ловит РАЗОРВАННУЮ строку реестра.
//
// ШРАМ, КУПИВШИЙ ПРИБОР, — МОЙ, И СВЕЖИЙ (2026-10-18, сессия M-264). Дописывая
// строку наряда M-264, я вставил в ЯЧЕЙКУ таблицы переводы строк: markdown от
// этого обрывает строку, и десять абзацев уехали НАРУЖУ таблицы, а сама таблица
// расклеилась на две. Ни один прибор этого не увидел: `docs_sync` сверяет
// НОМЕРА ссылок, `doc_refs_test` — АДРЕСА, и оба считают файл потоком строк, а
// не таблицей. Поймал я случайно, пересчитав колонки руками; владелец велел
// завести прибор («да давай также… если не повредит»).
//
// ЧТО СУДИТСЯ ЖЁСТКО — РОВНО ОДИН КЛАСС, НЕДВУСМЫСЛЕННЫЙ. (1) Строка, начатая
// `|` и НЕ закрытая `|`, — это и есть разорванная или недописанная строка.
// (2) СИРОТА: непустая строка без `|`, зажатая между двумя строками таблицы, —
// ровно то, что получилось у меня. Оба условия не имеют ложных срабатываний по
// построению: в здоровом доке их ноль, и замер это подтвердил (после закрытия
// двух пред-существующих незакрытых строк — `SKELETON.md` и `macro-registry.md`
// несли их до этой сессии).
//
// ЧТО ТОЛЬКО ПЕЧАТАЕТСЯ, А НЕ СУДИТСЯ, И ПОЧЕМУ. Равенство числа колонок
// ШАПКЕ судить НЕЛЬЗЯ: в доках 60+ строк несут `|` ПРОЗОЙ внутри ячейки
// (разделитель в тексте), markdown рисует им лишнюю колонку, и это
// пред-существующий косметический долг. Гейт на нём был бы красным с рождения,
// то есть мешал бы развитию вопреки §8 п.7, — поэтому число таких строк
// ПЕЧАТАЕТСЯ как ведомость, а вердикта не несёт. Разделять приборы по
// «недвусмысленное — судим, спорное — печатаем» дешевле, чем спорить с красным
// гейтом на каждой правке доков.
//
// Подсчёт колонок игнорирует `|` внутри бэктиков: в этих доках код в ячейках
// обычное дело, и наивный счётчик врал бы (первая редакция прибора дала 72
// нарушения, из которых настоящими не были почти все).

#include "check.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

#ifndef TIMAERT_SOURCE_DIR
#define TIMAERT_SOURCE_DIR "."
#endif

const char* kDocs[] = {
    "AGENTS.md", "CANON.md", "SKELETON.md", "macro-registry.md",
    "bugs.md",   "problems.md", "release.md", "README.md", "lore.md",
    "NEXT_SESSION.md",
};

std::vector<std::string> read_lines(const std::string& path, bool& ok) {
    std::vector<std::string> out;
    std::ifstream in(path);
    ok = in.good();
    if (!ok) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::string trimmed(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool is_row(const std::string& s) { return !s.empty() && s[0] == '|'; }

// Разделительная строка таблицы: только `|`, `-`, `:`, пробелы.
bool is_separator(const std::string& s) {
    if (!is_row(s)) return false;
    for (char c : s)
        if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
    return true;
}

int count_cells(const std::string& s) {
    int pipes = 0;
    bool inBacktick = false;
    for (char c : s) {
        if (c == '`') inBacktick = !inBacktick;
        else if (c == '|' && !inBacktick) ++pipes;
    }
    return pipes - 1;  // ведущая и замыкающая трубы рамкой, а не ячейкой
}

} // namespace

int main() {
    const std::string root = std::string(TIMAERT_SOURCE_DIR) + "/";

    int docsSeen = 0, rowsSeen = 0;
    int unterminated = 0, orphans = 0, widthMismatch = 0, tornTails = 0;
    std::string firstUnterminated, firstOrphan, firstTorn;

    for (const char* name : kDocs) {
        bool ok = false;
        const std::vector<std::string> lines = read_lines(root + name, ok);
        if (!ok) continue;   // документа может не быть — это не дефект прибора
        ++docsSeen;

        int width = -1;  // ширина текущей таблицы, задаётся разделителем
        bool inFence = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string s = trimmed(lines[i]);
            if (s.rfind("```", 0) == 0) { inFence = !inFence; width = -1; continue; }
            if (inFence) continue;
            if (!is_row(s)) {
                // ОБОРВАННЫЙ ХВОСТ строки: текст, который трубой КОНЧАЕТСЯ, но
                // не начинается. Это и есть вторая половина разорванной строки,
                // и именно её упустила первая редакция прибора — её негативный
                // контроль прошёл ЗЕЛЁНЫМ (мутация A), потому что обрубок
                // случайно кончился трубой и выглядел законной строкой, а хвост
                // лежал между ПУСТЫМИ строками, то есть сиротский тест (ниже)
                // его не видел. Класс замерен на здоровых доках: РОВНО НОЛЬ,
                // поэтому судится жёстко. Блоки ``` исключены: там труба — код.
                if (!s.empty() && s.back() == '|') {
                    ++tornTails;
                    if (firstTorn.empty()) {
                        std::ostringstream os;
                        os << name << ":" << (i + 1);
                        firstTorn = os.str();
                    }
                }
                // СИРОТА: непустой текст без трубы между двумя строками таблицы.
                const bool prevRow = i > 0 && is_row(trimmed(lines[i - 1]));
                const bool nextRow =
                    i + 1 < lines.size() && is_row(trimmed(lines[i + 1]));
                if (!s.empty() && prevRow && nextRow) {
                    ++orphans;
                    if (firstOrphan.empty()) {
                        std::ostringstream os;
                        os << name << ":" << (i + 1);
                        firstOrphan = os.str();
                    }
                }
                width = -1;  // таблица кончилась
                continue;
            }
            ++rowsSeen;
            if (s.size() < 2 || s.back() != '|') {
                ++unterminated;
                if (firstUnterminated.empty()) {
                    std::ostringstream os;
                    os << name << ":" << (i + 1);
                    firstUnterminated = os.str();
                }
            }
            if (is_separator(s)) { width = count_cells(s); continue; }
            if (width > 0 && count_cells(s) != width) ++widthMismatch;
        }
    }

    CHECK(docsSeen > 0, "прибор обязан был прочитать хотя бы один док");
    CHECK(rowsSeen > 100,
          "строк таблиц обязано быть много — иначе прибор смотрит не туда");

    // ЖЁСТКИЙ ВЕРДИКТ: ровно те два класса, что не имеют ложных срабатываний.
    if (unterminated != 0)
        std::printf("  первая незакрытая строка: %s\n", firstUnterminated.c_str());
    CHECK(unterminated == 0,
          "строка таблицы обязана закрываться трубой: незакрытая строка значит "
          "разорванную или недописанную строку");

    if (tornTails != 0)
        std::printf("  первый оборванный хвост строки: %s\n", firstTorn.c_str());
    CHECK(tornTails == 0,
          "текст, который трубой КОНЧАЕТСЯ, но не начинается, есть вторая половина "
          "разорванной строки таблицы");

    if (orphans != 0)
        std::printf("  первая сирота внутри таблицы: %s\n", firstOrphan.c_str());
    CHECK(orphans == 0,
          "непустой текст без трубы между строками таблицы — расклеенная таблица: "
          "ячейка с переводами строк уехала наружу");

    // ВЕДОМОСТЬ БЕЗ ВЕРДИКТА (см. шапку): `|` прозой внутри ячейки.
    std::printf("doc_tables: документов %d · строк таблиц %d · "
                "лишняя колонка от трубы в прозе %d (ведомость, не вердикт)\n",
                docsSeen, rowsSeen, widthMismatch);

    return sm::test::report("doc_tables_test");
}
