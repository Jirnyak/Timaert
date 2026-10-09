// ВЕДОМОСТЬ «ЗАКОН → НОСИТЕЛЬ» (M-243) — ПОСЛЕДНЯЯ ДЫРА КОНТРОЛЯ.
//
// Замысел владельца, дословно: «чтобы потом модульно агенты уже не могли
// засрать а структура костяк и асерты и комплятор всё держали ядро крепко и
// было незвожноа спагетификауия и энтропия кода». Механизм дыры замерен:
// ядро сходится ровно настолько, насколько дотягивается СТЕНА (канал
// sub→macro под arch_guard тает 89 → 46; ЗАКОН ТРЁХ ДВЕРЕЙ без стены — ноль
// дверей в дереве). Обязательный корпус чтения ≈ 2.5–3× окна агента, поэтому
// закон-ПРОЗА не исполняется, а ПРОЕЦИРУЕТСЯ, каждой сессией по-своему.
//
// ЧТО СУДИТ ЭТОТ ПРИБОР. Каждому закону AGENTS.md сопоставлена строка
// ведомости `tests/data/law_carriers.txt` с ВЕРДИКТОМ и АДРЕСОМ носителя:
//   СТЕНА     — нарушение несобираемо (static_assert, тип, -Wswitch);
//   РАТЧЕТ    — гейт красный + тающий белый список;
//   СВИДЕТЕЛЬ — поведенческий тест, целенаправленно охраняющий закон;
//   ПРОЗА     — носителя НЕТ; счёт ПРОЗЫ пинится точно в CMakeLists и может
//               только убывать — он и есть остаток дыры контроля.
// Храповик ДВУСТОРОННИЙ: закон без строки ведомости — красный (unlisted);
// строка про закон, которого в AGENTS.md больше нет, — красный (stale).
//
// КЛЮЧ ЗАКОНА — ДОСЛОВНЫЙ ЗАГОЛОВОК (вердикт владельца 2026-10-09, вариант
// «отпечаток»): правка формулировки закона даёт пару unlisted+stale и
// ЗАСТАВЛЯЕТ пересудить носитель — это желаемое поведение, а не поломка;
// AGENTS.md этим прибором не правится ни байтом.
//
// МЕРА «СКОЛЬКО ВСЕГО ЗАКОНОВ» — НАЗВАНА, ГРАММАТИКА МЕХАНИЧЕСКАЯ:
//   (а) каждый `### `-заголовок внутри «## 3. ЗАКОНЫ ВЛАДЕЛЬЦА»; заголовок,
//       в чьём теле живут жирные подзаконы `**N. ЗАКОН …**`, — КОНТЕЙНЕР
//       (сегодня это «ЧЕТЫРЕ ИНЖЕНЕРНЫХ ЗАКОНА») и заменяется ими;
//   (б) сами жирные подзаконы `**N. ЗАКОН …**`;
//   (в) вне §3: `## `/`### `-заголовки и жирные пули `- **…**`, чей текст
//       несёт слово «ЗАКОН» (DOD-ЗАКОН, ТЕСТОВЫЙ ЗАКОН, ЗАКОН НУЛЕВОЙ,
//       ЗАКОН СТАБИЛЬНОСТИ, СПРАЙТ-ЗАКОН). У многострочной жирной пули
//       ключом служит её ПЕРВАЯ строка — дословно, как лежит в файле.
//   Методические разделы (§5 МЕТОД, §8 правила 1-11 поимённо, §12 стиль)
//   населением НЕ являются: мера — законы, не метод. Расширение населения —
//   решение владельца, ведомость примет новые строки тем же храповиком.
//
// ГРАНИЦА ПРИБОРА, ЧЕСТНО: ведомость утверждает, что носитель ПО АДРЕСУ
// СУЩЕСТВУЕТ (файл есть, символ в нём есть), и ничего больше. ГОДНОСТЬ
// носителя остаётся за §8 — негативный контроль с записанной таблицей
// «мутация → исход» обеих полярностей; в этом дереве однажды стояли 25
// файлов с CHECK(true, …), и строка ведомости такого не отличит.
#include "check.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef TIMAERT_SOURCE_DIR
#error "TIMAERT_SOURCE_DIR must name the repo root - see CMakeLists.txt"
#endif

namespace {

namespace fs = std::filesystem;

constexpr const char* kRoot = TIMAERT_SOURCE_DIR;
constexpr const char* kAgents = "AGENTS.md";
constexpr const char* kLedger = "tests/data/law_carriers.txt";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string& src) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : src) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::string collapse(std::string_view s) {
    std::string out;
    bool space = true;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!space) out.push_back(' ');
            space = true;
        } else { out.push_back(c); space = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool starts_with(const std::string& s, std::string_view p) {
    return s.rfind(p, 0) == 0;
}

// Жирный подзакон: `**N. ЗАКОН …` (N — одна цифра; ЧЕТЫРЕ ИНЖЕНЕРНЫХ — 1..4).
bool is_bold_sublaw(const std::string& line) {
    if (!starts_with(line, "**")) return false;
    std::size_t i = 2;
    if (i >= line.size() || line[i] < '0' || line[i] > '9') return false;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i + 1 >= line.size() || line[i] != '.' || line[i + 1] != ' ')
        return false;
    return line.find("ЗАКОН", i) != std::string::npos;
}

// Ключ жирной строки: текст между открывающими `**` (или `- **`) и
// закрывающими `**`; у многострочной пули закрывающих нет — ключом служит
// хвост первой строки дословно.
std::string bold_lead(const std::string& line, std::size_t open) {
    const std::size_t close = line.find("**", open);
    const std::size_t end = close == std::string::npos ? line.size() : close;
    return collapse(line.substr(open, end - open));
}

// ── ГРАММАТИКА НАСЕЛЕНИЯ ──────────────────────────────────────────────────
// Вся работа — над строками AGENTS.md; возвращает дословные ключи законов.
std::vector<std::string> collect_laws(const std::vector<std::string>& lines) {
    std::size_t s3begin = lines.size(), s3end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (starts_with(lines[i], "## 3.")) { s3begin = i; break; }
    }
    for (std::size_t i = s3begin + 1; i < lines.size(); ++i) {
        if (starts_with(lines[i], "## ")) { s3end = i; break; }
    }

    std::vector<std::string> laws;

    // (а)+(б): §3 — ### заголовки; контейнер с жирными подзаконами внутри
    // заменяется ими.
    std::vector<std::size_t> heads;
    for (std::size_t i = s3begin; i < s3end; ++i)
        if (starts_with(lines[i], "### ")) heads.push_back(i);
    for (std::size_t h = 0; h < heads.size(); ++h) {
        const std::size_t from = heads[h];
        const std::size_t to = h + 1 < heads.size() ? heads[h + 1] : s3end;
        std::vector<std::string> subs;
        for (std::size_t i = from + 1; i < to; ++i)
            if (is_bold_sublaw(lines[i])) subs.push_back(bold_lead(lines[i], 2));
        if (subs.empty()) laws.push_back(collapse(lines[from].substr(4)));
        else for (std::string& s : subs) laws.push_back(std::move(s));
    }

    // (в): вне §3 — заголовки и жирные пули со словом ЗАКОН.
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i >= s3begin && i < s3end) continue;
        const std::string& t = lines[i];
        if (starts_with(t, "## ") && t.find("ЗАКОН") != std::string::npos) {
            laws.push_back(collapse(t.substr(3)));
        } else if (starts_with(t, "### ")
                   && t.find("ЗАКОН") != std::string::npos) {
            laws.push_back(collapse(t.substr(4)));
        } else if (starts_with(t, "- **")) {
            const std::string lead = bold_lead(t, 4);
            if (lead.find("ЗАКОН") != std::string::npos)
                laws.push_back(lead);
        }
    }
    return laws;
}

// ── ВЕДОМОСТЬ ─────────────────────────────────────────────────────────────
struct Row {
    std::string verdict;  // СТЕНА | РАТЧЕТ | СВИДЕТЕЛЬ | ПРОЗА
    std::string key;      // дословный заголовок закона
    std::string address;  // символ@путь или путь; пусто у ПРОЗЫ
};

bool verdict_known(const std::string& v) {
    return v == "СТЕНА" || v == "РАТЧЕТ" || v == "СВИДЕТЕЛЬ" || v == "ПРОЗА";
}

std::vector<Row> read_ledger(const fs::path& p, int& badFormat) {
    std::vector<Row> rows;
    for (const std::string& line : split_lines(read_file(p))) {
        if (line.empty() || line[0] == '#') continue;
        // Третье поле у ПРОЗЫ отсутствует вовсе: хвостовой TAB стригут
        // редакторы, и требовать его значило бы ловить не нарушение, а
        // невидимый пробел (поймано первым прогоном: 7 строк ПРОЗЫ выпали).
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) {
            ++badFormat;
            std::fprintf(stderr, "  СТРОКА ВЕДОМОСТИ БЕЗ ЕДИНОГО TAB: %s\n",
                         line.c_str());
            continue;
        }
        const std::size_t t2 = line.find('\t', t1 + 1);
        const std::size_t keyEnd =
            t2 == std::string::npos ? line.size() : t2;
        rows.push_back({line.substr(0, t1),
                        collapse(line.substr(t1 + 1, keyEnd - t1 - 1)),
                        t2 == std::string::npos
                            ? std::string()
                            : collapse(line.substr(t2 + 1))});
    }
    return rows;
}

// Адрес носителя: `символ@путь` — файл существует И несёт символ; голый
// `путь` — файл существует. Та же форма, что у адресов SKELETON (§0 п.2).
bool address_holds(const std::string& addr, std::string* why) {
    const std::size_t at = addr.find('@');
    const std::string path = at == std::string::npos ? addr
                                                     : addr.substr(at + 1);
    const fs::path full = fs::path(kRoot) / fs::path(path);
    if (!fs::is_regular_file(full)) {
        *why = "файла нет: " + path;
        return false;
    }
    if (at != std::string::npos) {
        const std::string symbol = addr.substr(0, at);
        if (read_file(full).find(symbol) == std::string::npos) {
            *why = "символа нет в файле: " + addr;
            return false;
        }
    }
    return true;
}

struct Diff {
    int unlisted = 0;
    int stale = 0;
};

Diff compare(const std::vector<std::string>& laws,
             const std::vector<Row>& rows, bool loud) {
    Diff d;
    std::set<std::string> listed;
    for (const Row& r : rows) listed.insert(r.key);
    std::set<std::string> present(laws.begin(), laws.end());
    for (const std::string& law : laws)
        if (listed.count(law) == 0) {
            ++d.unlisted;
            if (loud)
                std::fprintf(stderr, "  ЗАКОН БЕЗ СТРОКИ ВЕДОМОСТИ: %s\n",
                             law.c_str());
        }
    for (const Row& r : rows)
        if (present.count(r.key) == 0) {
            ++d.stale;
            if (loud)
                std::fprintf(stderr, "  СТРОКА БЕЗ ЗАКОНА (устарела): %s\n",
                             r.key.c_str());
        }
    return d;
}

// ── НЕГАТИВНЫЕ КОНТРОЛИ ───────────────────────────────────────────────────
// Прибор, который не умеет краснеть, бесполезен (§8 п.6): грамматика,
// храповик и проверка адреса показывают ОБЕ полярности на синтетике;
// живые мутации дерева — таблицей в теле коммита.
void test_detectors_actually_see() {
    const std::vector<std::string> doc = split_lines(
        "## 1. ШАПКА\n"
        "- **ЗАКОН ПУЛИ: ЖИРНЫЙ ЛИД\n"
        "- **просто жирная пуля без слова**\n"
        "## 3. ЗАКОНЫ ВЛАДЕЛЬЦА\n"
        "### ЗАКОН ПЕРВЫЙ\n"
        "текст\n"
        "### КОНТЕЙНЕР ЧЕТЫРЁХ\n"
        "**1. ЗАКОН ВНУТРЕННИЙ А.** текст\n"
        "**2. ЗАКОН ВНУТРЕННИЙ Б.** текст\n"
        "### БЕЗ СЛОВА В ЗАГОЛОВКЕ\n"
        "## 4. DOD-ЗАКОН (X)\n"
        "### ЗАКОН ХВОСТОВОЙ\n");
    const std::vector<std::string> laws = collect_laws(doc);
    CHECK(laws.size() == 7,
          "грамматика: 2 из §3 + 2 подзакона вместо контейнера + 3 вне §3 "
          "(пуля со словом, DOD, хвостовой ###)");
    std::set<std::string> got(laws.begin(), laws.end());
    CHECK(got.count("КОНТЕЙНЕР ЧЕТЫРЁХ") == 0,
          "контейнер с подзаконами населением не является");
    CHECK(got.count("1. ЗАКОН ВНУТРЕННИЙ А.") == 1
              && got.count("2. ЗАКОН ВНУТРЕННИЙ Б.") == 1,
          "жирные подзаконы стали населением");
    CHECK(got.count("БЕЗ СЛОВА В ЗАГОЛОВКЕ") == 1,
          "внутри §3 слово ЗАКОН в заголовке не требуется — там всё законы");
    CHECK(got.count("ЗАКОН ПУЛИ: ЖИРНЫЙ ЛИД") == 1
              && got.count("просто жирная пуля без слова") == 0,
          "вне §3 пуля входит только со словом ЗАКОН в жирном лиде");

    // Храповик — обе полярности на синтетике.
    const std::vector<Row> rows = {{"ПРОЗА", "ЗАКОН ПЕРВЫЙ", ""},
                                   {"ПРОЗА", "ЗАКОН МЁРТВЫЙ", ""}};
    const Diff d = compare({"ЗАКОН ПЕРВЫЙ", "ЗАКОН НОВЫЙ"}, rows, false);
    CHECK(d.unlisted == 1, "закон без строки ведомости замечен");
    CHECK(d.stale == 1, "строка без закона замечена");
    const Diff clean = compare({"ЗАКОН ПЕРВЫЙ"},
                               {{"ПРОЗА", "ЗАКОН ПЕРВЫЙ", ""}}, false);
    CHECK(clean.unlisted == 0 && clean.stale == 0,
          "целая ведомость молчит — детектор, краснеющий на всё, не детектор");

    // Проверка адреса — обе полярности. Мёртвый символ СКЛЕИВАЕТСЯ в
    // рантайме: литерал целиком лежал бы в исходнике этого же теста, и
    // детектор «находил» бы его в самом себе (поймано первым прогоном).
    std::string why;
    CHECK(address_holds("collect_laws@tests/law_carrier_test.cpp", &why),
          "живой адрес символ@путь проходит");
    const std::string dead =
        std::string("xyzzy_") + "absent_symbol" + "@tests/check.h";
    CHECK(!address_holds(dead, &why), "мёртвый символ в живом файле ловится");
    CHECK(!address_holds("tests/нет_такого_файла.cpp", &why),
          "мёртвый путь ловится");
}

}  // namespace

int main() {
    test_detectors_actually_see();

    const std::vector<std::string> agents =
        split_lines(read_file(fs::path(kRoot) / kAgents));
    const std::vector<std::string> laws = collect_laws(agents);
    CHECK(!laws.empty(), "население законов прочитано из AGENTS.md");

    int badFormat = 0;
    const std::vector<Row> rows =
        read_ledger(fs::path(kRoot) / kLedger, badFormat);
    CHECK(!rows.empty(), "ведомость law_carriers.txt читается и непуста");
    CHECK(badFormat == 0, "каждая строка ведомости — три поля через TAB");

    int badVerdict = 0, prozaWithAddress = 0, carrierNoAddress = 0,
        deadAddress = 0, proza = 0, carried = 0;
    for (const Row& r : rows) {
        if (!verdict_known(r.verdict)) {
            ++badVerdict;
            std::fprintf(stderr, "  НЕИЗВЕСТНЫЙ ВЕРДИКТ «%s»: %s\n",
                         r.verdict.c_str(), r.key.c_str());
            continue;
        }
        if (r.verdict == "ПРОЗА") {
            ++proza;
            if (!r.address.empty()) {
                ++prozaWithAddress;
                std::fprintf(stderr,
                             "  ПРОЗА С АДРЕСОМ (так не бывает): %s\n",
                             r.key.c_str());
            }
            continue;
        }
        if (r.address.empty()) {
            ++carrierNoAddress;
            std::fprintf(stderr, "  ВЕРДИКТ БЕЗ АДРЕСА НОСИТЕЛЯ: %s\n",
                         r.key.c_str());
            continue;
        }
        std::string why;
        if (!address_holds(r.address, &why)) {
            ++deadAddress;
            std::fprintf(stderr, "  АДРЕС НОСИТЕЛЯ МЁРТВ (%s): %s\n",
                         why.c_str(), r.key.c_str());
            continue;
        }
        ++carried;
    }
    CHECK(badVerdict == 0, "вердикты только из четырёх слов");
    CHECK(prozaWithAddress == 0, "у ПРОЗЫ адреса нет — иначе она не ПРОЗА");
    CHECK(carrierNoAddress == 0, "у СТЕНЫ/РАТЧЕТА/СВИДЕТЕЛЯ адрес обязателен");
    CHECK(deadAddress == 0,
          "каждый адрес носителя жив: файл существует, символ в нём есть");

    const Diff d = compare(laws, rows, true);
    CHECK(d.unlisted == 0,
          "у каждого закона AGENTS.md есть строка ведомости — новый закон "
          "без носителя (хотя бы честной ПРОЗЫ) не заводится");
    CHECK(d.stale == 0,
          "в ведомости нет строк про законы, которых больше нет, — "
          "ведомость усыхает вместе с AGENTS.md");

    std::printf("law_carrier: законов %zu · строк ведомости %zu\n",
                laws.size(), rows.size());

    char scope[220];
    std::snprintf(scope, sizeof(scope),
                  "носитель есть у %d из %zu законов · прозы %d — ведомость "
                  "судит СУЩЕСТВОВАНИЕ носителя, не годность",
                  carried, laws.size(), proza);
    return sm::test::report("law_carrier_test", scope);
}
