// ПРИБОР ЗАКОНА ИЗОТРОПИИ (наряд M-256, 2026-10-12) — РАТЧЕТ над src/sub.
//
// Закон (AGENTS, ЗАКОН ИЗОТРОПИИ): «ВСЯКАЯ ВЕЛИЧИНА СИМУЛЯЦИИ СУБМИРА, У
// КОТОРОЙ ЕСТЬ ТРИ ОСИ, СЧИТАЕТСЯ ПО ТРЁМ… ПРИЗНАК НАРУШЕНИЯ: dx*dx + dy*dy
// в гейте досягаемости». Владелец: «ты поймал без Z нарушение выжигай
// нещадно».
//
// ШРАМ, КУПИВШИЙ ПРИБОР. «E войти в дверь» появлялась, когда игрок летел
// высоко НАД дверью: `aimed_prop` мерил наземный проп через
// `structure_surface_dist2` — расстояние до прямоугольника В ПЛОСКОСТИ XY, —
// а соседний `aimed_corpse` в ТОМ ЖЕ резолвере честно мерил `dist3sq`. Ни 117
// тестов, ни 66 смоуков этого не видели: смоуки сами телепортировали тело
// `set_player_pos` (двигает только XY) и жали E, вися в шести метрах над
// порогом. Поймал ВЛАДЕЛЕЦ в живом прогоне — то есть у закона не было
// носителя вовсе, и это ровно то, что здесь исправляется.
//
// ЧТО ПРИБОР ДЕЛАЕТ. Находит в `src/sub/**.{h,cpp}` ПЛОСКИЕ суммы квадратов
// (`A*A + B*B` без третьего слагаемого) и сверяет МУЛЬТИМНОЖЕСТВО найденного
// с ведомостью `tests/data/isotropy_planar.txt`. Классов два:
//   природа — величина ПЛОСКАЯ ПО ПРИРОДЕ, и закон это разрешает дословно:
//             «след стопы на земле, ходовой шаг по плоскости, радиус зоны на
//             карте, сетка соседства». Сюда же генерация карты (решётка
//             тайлов), планарный ГРАДИЕНТ высоты (∂h/∂x, ∂h/∂y — это не
//             вертикаль, а две производные), широкая фаза, заведомо
//             консервативная к 3D-узкой, и рендер (ТУПИК — что ушло в
//             рендер, поведением не становится);
//   долг    — НАСТОЯЩЕЕ НАРУШЕНИЕ, названное поимённо и ждущее наряда.
//             Счёт долга пинится и может только убывать.
//
// ГРАНИЦЫ, НАЗВАННЫЕ ВСЛУХ — ИНАЧЕ ПРИБОР ПРОДАЁТ БОЛЬШЕ, ЧЕМ УМЕЕТ:
//  1. Он судит ФОРМУ выражения, а не СМЫСЛ гейта. «природа» в ведомости —
//     утверждение ЧЕЛОВЕКА, проверенное руками 2026-10-12; прибор лишь не
//     даёт списку расти молча.
//  2. Ключ строки — (класс, файл, пара операндов), без номера строки: номер
//     производное, а не факт (та же причина, что у `arch_legacy.txt`).
//     Следствие названо честно: ПОДМЕНА одного законного `dx/dy` в файле на
//     незаконный `dx/dy` в том же файле мультимножеством НЕ ловится. Ловится
//     всякий НОВЫЙ сайт и всякая новая пара операндов.
//  3. Плоскую величину можно построить и мимо этой формы — уронив `z` при
//     вызове помощника, сравнив `std::abs(dx) < r && std::abs(dy) < r`,
//     спрятав сумму за `length2d`. Такие сайты прибор НЕ видит; `length2d`
//     разобран руками (все звонящие — планарный шаг/скорость), box-форма
//     найдена замером нулевой.
//  4. Выражение режется по ОПЕРАТОРАМ (`;{}`), а не по строкам: clang-format
//     рвёт длинные суммы, и построчный скан слеп к ним. Это не косметика —
//     построчная версия прибора теряла ЧЕТЫРЕ сайта (`gens/mountain.cpp`,
//     `gens/village_palisade.cpp` и ДВА в `engine.cpp`), то есть 79 против 75.
//
// НЕГАТИВНЫЙ КОНТРОЛЬ — ПРОГНАН И ЗАПИСАН ЧИСЛАМИ (§8 п.6), ОБЕ ПОЛЯРНОСТИ.
// Прогнанный и не записанный контроль доказывает, что детектор видел дефект
// ОДИН РАЗ, в голове автора; следующий не может ни повторить его, ни узнать,
// что ловилось. Код выхода снят БЕЗ пайпа: `tail` съедает его и показывает 0.
//
//   мутация                                             | исход
//   ----------------------------------------------------|--------------------
//   `within_arm_of` переписан 3D → 2D (убран `dz*dz`)   | КРАСНЫЙ, код 1:
//     — настоящий гейт предела руки стал плоским        |   «1 сверх 6»,
//                                                       |   назвал строку 4714,
//                                                       |   сумм 79 → 80
//   комментарий переписан, код не тронут                | МОЛЧИТ, код 0:
//     — ПРОЗА обязана быть невидимой прибору            |   79 · 74 · 5 без
//                                                       |   изменений
//   строка ведомости удалена (один `dx/dy` в cave.cpp)  | КРАСНЫЙ, код 1:
//     — рост долга мимо списка                          |   «1 сверх 1»
//   строка ведомости без сайта в дереве (`zz/qq`)       | КРАСНЫЙ, код 1:
//     — список обязан ТАЯТЬ, а не копить мёртвое        |   «УСТАРЕЛА»
//
// Детектор, краснеющий на всё, — не детектор: ровно поэтому вторая строка
// таблицы обязательна, и она МОЛЧИТ.
#include "check.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kRoot = TIMAERT_SOURCE_DIR;
constexpr const char* kScanDir = "src/sub";
constexpr const char* kLedger = "tests/data/isotropy_planar.txt";

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string squeeze(const std::string& s) {
    std::string out;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    return out;
}

std::string collapse(const std::string& s) {
    std::string out;
    bool ws = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { ws = true; continue; }
        if (ws && !out.empty()) out += ' ';
        ws = false;
        out += c;
    }
    return out;
}

bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'
        || c == '[' || c == ']';
}

// ── ОПЕРАНД УМНОЖЕНИЯ ────────────────────────────────────────────────────
// Слева/справа от `*` стоит либо ИМЯ (возможно с `.`/`->`/`[i]`), либо
// СБАЛАНСИРОВАННАЯ скобка: форма `(a - b) * (a - b)` в дереве живая
// (`gens/mountain.cpp` — планарный градиент высоты), и слепой к ней прибор
// оставлял бы дыру ровно того размера, в которую пишут новое нарушение.
bool left_operand(const std::string& s, std::size_t star, std::string& out) {
    std::size_t j = star;
    while (j > 0 && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
    if (j == 0) return false;
    if (s[j - 1] == ')') {
        int depth = 0;
        std::size_t k = j;
        while (k > 0) {
            --k;
            if (s[k] == ')') ++depth;
            else if (s[k] == '(' && --depth == 0) {
                out = s.substr(k, j - k);
                return true;
            }
        }
        return false;
    }
    std::size_t k = j;
    while (k > 0 && ident_char(s[k - 1])) --k;
    // `a->b` и `a.b`: стрелку пропускаем вручную, точка уже в ident_char.
    while (k >= 2 && s[k - 1] == '>' && s[k - 2] == '-') {
        k -= 2;
        while (k > 0 && ident_char(s[k - 1])) --k;
    }
    if (k == j) return false;
    out = s.substr(k, j - k);
    return true;
}

bool right_operand(const std::string& s, std::size_t star, std::string& out,
                   std::size_t& end) {
    std::size_t j = star + 1;
    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
    if (j >= s.size()) return false;
    if (s[j] == '(') {
        int depth = 0;
        for (std::size_t k = j; k < s.size(); ++k) {
            if (s[k] == '(') ++depth;
            else if (s[k] == ')' && --depth == 0) {
                out = s.substr(j, k - j + 1);
                end = k + 1;
                return true;
            }
        }
        return false;
    }
    std::size_t k = j;
    while (k < s.size() && ident_char(s[k])) ++k;
    if (k == j) return false;
    out = s.substr(j, k - j);
    end = k;
    return true;
}

struct Square {
    std::size_t begin = 0, end = 0;   // границы `A * A` в тексте оператора
    std::string token;                // нормализованное `A`
};

std::vector<Square> squares_in(const std::string& stmt) {
    std::vector<Square> out;
    for (std::size_t i = 0; i < stmt.size(); ++i) {
        if (stmt[i] != '*') continue;
        if (i + 1 < stmt.size() && stmt[i + 1] == '*') continue;   // `**`
        if (i > 0 && stmt[i - 1] == '*') continue;
        std::string l, r;
        std::size_t end = 0;
        if (!left_operand(stmt, i, l)) continue;
        if (!right_operand(stmt, i, r, end)) continue;
        if (squeeze(l) != squeeze(r)) continue;
        Square sq;
        sq.begin = i - l.size();
        // точный левый край: отступим на длину операнда от первого непробела
        std::size_t j = i;
        while (j > 0 && std::isspace(static_cast<unsigned char>(stmt[j - 1]))) --j;
        sq.begin = j - l.size();
        sq.end = end;
        sq.token = squeeze(l);
        out.push_back(sq);
    }
    return out;
}

struct Site {
    std::string file;      // путь от корня
    int line = 0;          // печатается при жалобе, в ключ НЕ входит
    std::string pair;      // `A/B` нормализованное
};

// Два квадрата, склеенные ровно `+`, и НИ ОДНОГО третьего такого же рядом.
// Третий ищется В ОБЕ СТОРОНЫ: смотреть только вперёд — та самая ошибка,
// которая в `dx*dx + dy*dy + dz*dz` объявляла пару (dy,dz) «плоской». Она
// была поймана ТОЛЬКО расхождением счёта (92 против ожидаемых ~77), и это
// причина, по которой у самодельного прибора обязан быть свой контроль.
void collect_sites(const std::string& rel, const std::string& src,
                   std::vector<Site>& out) {
    std::size_t start = 0;
    for (std::size_t i = 0; i <= src.size(); ++i) {
        const bool brk = i == src.size() || src[i] == ';' || src[i] == '{'
                      || src[i] == '}';
        if (!brk) continue;
        const std::string raw = src.substr(start, i - start);
        const std::size_t off = start;
        start = i + 1;
        const std::string stmt = collapse(raw);
        if (stmt.find('*') == std::string::npos) continue;
        const std::vector<Square> sq = squares_in(stmt);
        if (sq.size() < 2) continue;
        int line = 1;
        for (std::size_t k = 0; k < off && k < src.size(); ++k)
            if (src[k] == '\n') ++line;
        for (std::size_t a = 0; a < sq.size(); ++a) {
            for (std::size_t b = a + 1; b < sq.size(); ++b) {
                if (sq[a].token == sq[b].token) continue;
                if (squeeze(stmt.substr(sq[a].end, sq[b].begin - sq[a].end))
                        != "+")
                    continue;
                bool third = false;
                for (std::size_t c = 0; c < sq.size() && !third; ++c) {
                    if (c == a || c == b) continue;
                    if (sq[c].begin >= sq[b].end
                        && squeeze(stmt.substr(sq[b].end,
                                               sq[c].begin - sq[b].end)) == "+")
                        third = true;
                    if (sq[c].end <= sq[a].begin
                        && squeeze(stmt.substr(sq[c].end,
                                               sq[a].begin - sq[c].end)) == "+")
                        third = true;
                }
                if (third) continue;
                out.push_back({rel, line, sq[a].token + "/" + sq[b].token});
            }
        }
    }
}

struct Row {
    std::string cls, file, pair;
};

std::vector<Row> read_ledger(const fs::path& p, int& badFormat) {
    std::vector<Row> rows;
    std::istringstream in(read_file(p));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t t1 = line.find('\t');
        const std::size_t t2 = t1 == std::string::npos
                                   ? std::string::npos
                                   : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) {
            ++badFormat;
            std::fprintf(stderr, "  СТРОКА ВЕДОМОСТИ НЕ ИЗ ТРЁХ ПОЛЕЙ: %s\n",
                         line.c_str());
            continue;
        }
        rows.push_back({collapse(line.substr(0, t1)),
                        collapse(line.substr(t1 + 1, t2 - t1 - 1)),
                        collapse(line.substr(t2 + 1))});
    }
    return rows;
}

using Key = std::string;
Key key_of(const std::string& cls, const std::string& file,
           const std::string& pair) {
    return cls + '\t' + file + '\t' + pair;
}

} // namespace

int main() {
    const fs::path dir = fs::path(kRoot) / fs::path(kScanDir);
    CHECK(fs::exists(dir), "дерево субмира на месте — иначе прибор мерит пустоту");

    std::vector<Site> sites;
    int filesSeen = 0;
    std::vector<fs::path> files;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".h" && ext != ".cpp") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        ++filesSeen;
        const std::string rel =
            fs::relative(p, fs::path(kRoot)).generic_string();
        collect_sites(rel, read_file(p), sites);
    }
    CHECK(filesSeen > 0, "прибор прочёл хотя бы один файл субмира");
    CHECK(!sites.empty(),
          "плоские суммы квадратов в субмире НАЙДЕНЫ — ноль здесь означал бы "
          "сломанный детектор, а не чистое дерево (их там десятки)");

    int badFormat = 0;
    const std::vector<Row> rows =
        read_ledger(fs::path(kRoot) / fs::path(kLedger), badFormat);
    CHECK(badFormat == 0, "каждая строка ведомости — три поля через TAB");
    CHECK(!rows.empty(), "ведомость isotropy_planar.txt читается и непуста");

    int badClass = 0;
    std::map<Key, int> listed;
    int listedNature = 0, listedDebt = 0;
    for (const Row& r : rows) {
        if (r.cls != "природа" && r.cls != "долг") {
            ++badClass;
            std::fprintf(stderr, "  НЕИЗВЕСТНЫЙ КЛАСС «%s»: %s %s\n",
                         r.cls.c_str(), r.file.c_str(), r.pair.c_str());
            continue;
        }
        ++listed[key_of(r.cls, r.file, r.pair)];
        if (r.cls == "природа") ++listedNature; else ++listedDebt;
    }
    CHECK(badClass == 0, "классы только два: природа и долг");

    // Найденное сворачивается в мультимножество по (файл, пара) и
    // сопоставляется с суммой обоих классов ведомости.
    std::map<std::string, int> found;                 // файл\tпара → счёт
    std::map<std::string, std::vector<int>> whereLines;
    for (const Site& s : sites) {
        const std::string k = s.file + '\t' + s.pair;
        ++found[k];
        whereLines[k].push_back(s.line);
    }
    std::map<std::string, int> allowed;               // файл\tпара → счёт
    for (const auto& [k, n] : listed) {
        const std::size_t t1 = k.find('\t');
        allowed[k.substr(t1 + 1)] += n;
    }

    int unlisted = 0, stale = 0;
    for (const auto& [k, n] : found) {
        const int cap = allowed.count(k) ? allowed.at(k) : 0;
        if (n <= cap) continue;
        unlisted += n - cap;
        std::string lines;
        for (int l : whereLines[k]) lines += ' ' + std::to_string(l);
        std::fprintf(stderr,
                     "  ПЛОСКАЯ СУММА ВНЕ ВЕДОМОСТИ (%d сверх %d): %s "
                     "— строки:%s\n",
                     n - cap, cap, k.c_str(), lines.c_str());
    }
    for (const auto& [k, n] : allowed) {
        const int got = found.count(k) ? found.at(k) : 0;
        if (n <= got) continue;
        stale += n - got;
        std::fprintf(stderr,
                     "  УСТАРЕЛА СТРОКА ВЕДОМОСТИ (%d лишних из %d): %s\n",
                     n - got, n, k.c_str());
    }
    CHECK(unlisted == 0,
          "новая плоская сумма квадратов в src/sub без строки ведомости — "
          "ЗАКОН ИЗОТРОПИИ: у трёхосной величины считаются ТРИ оси, а плоская "
          "обязана назвать, почему она плоская ПО ПРИРОДЕ");
    CHECK(stale == 0,
          "ведомость не держит строк, которых в дереве больше нет — список "
          "умеет только ТАЯТЬ");

    // ДОЛГ ТАЕТ, А НЕ РАСТЁТ. Пин в CMakeLists точный: закрытие нарушения
    // правит число ТЕМ ЖЕ коммитом, то есть решение предъявлено в дифе.
    CHECK(listedDebt >= 0, "счёт долга посчитан");

    std::printf("isotropy: файлов %d · плоских сумм %zu · природа %d · "
                "долг %d\n",
                filesSeen, sites.size(), listedNature, listedDebt);

    char scope[220];
    std::snprintf(scope, sizeof(scope),
                  "плоских сумм %zu в src/sub · природа %d · долг %d — "
                  "прибор судит ФОРМУ выражения, не СМЫСЛ гейта",
                  sites.size(), listedNature, listedDebt);
    return sm::test::report("isotropy_law_test", scope);
}
