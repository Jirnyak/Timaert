// ПРИБОР НА ЗАКОН АДРЕСА: клетка мира не сворачивается делением. Никогда.
//
// ПОЧЕМУ ПРИБОР, А НЕ РЕВЬЮ. AGENTS §3 ЗАКОН АДРЕСА п.4 говорит дословно:
// «Свернуть КЛЕТКУ МИРА через `wrapi` — дефект». Закон записан M-97
// (`89e16b02`). После этого в `src/app/main.cpp` появилось ТРИ новых таких
// сайта из четырёх, снесённых нарядом M-124 — то есть закон, живущий только в
// тексте и в глазах ревьюера, не держит ничего. Свидетеля у него не было: ни
// один тест не краснел, потому что на мире-степени-двойки деление и маска дают
// ПОБИТОВО ОДНО И ТО ЖЕ. Дефект тут не в ответе, а в цене (`std::int64_t %` по
// рантайм-делителю, ~20-40 тактов против одного) и в том, что у вопроса «где
// эта клетка» становится два ответа (DOD п.6).
//
// У СВЁРТКИ МИРА ЧЕТЫРЕ НАПИСАНИЯ, И ПРИБОР СУДИТ ВСЕ ЧЕТЫРЕ. Наряд M-169
// показал, что стена на одном `wrapi` — это стена с тремя проходами рядом: тот
// же заворот по стороне мира жив как `wrapf`, как голый `%` и как рукописный
// `y * W + x` мимо `cell_of`. Каждое из них отвечает на тот же вопрос «где эта
// клетка» своим способом, то есть ровно то, что запрещает DOD п.6. Замер САМИМ
// ПРИБОРОМ на 2026-09-28, 179 файлов: `wrapf` 10 · `%` 23 · индекс 31 = 64
// сайта, 56 различных строк (одна строка кода несёт до трёх сайтов — `((x % W)
// + W) % W` это три). В список идут СТРОКИ, поэтому его длина 56, а не 64.
//
//   1. `wrapi` по стороне мира — СТЕНА: ноль, без послаблений. Достигнут
//      нарядом M-124 и обязан остаться нулём.
//   2. `wrapf` по стороне мира  ┐
//   3. `%` по стороне мира      ├─ ТАЮЩИЙ БЕЛЫЙ СПИСОК `tests/data/world_fold_legacy.txt`
//   4. рукописный `y * W + x`   ┘
//
// ПОЧЕМУ ТРИ ИЗ ЧЕТЫРЁХ — СПИСКОМ, А НЕ СТЕНОЙ. Стена, поднятая над непустым
// каналом, не гейт, а блокировка чужой работы: она краснеет в первый же день и
// заставляет либо чинить 42 сайта одним заходом (запрещено §5 п.5 — «не резать
// скриптом по имени»), либо ослабить порог (запрещено §8 ЗАКОН НУЛЕВОЙ п.4).
// Список останавливает РОСТ долга сегодня, а долг тает по ходу обычной работы.
// Владелец, дословно: «лучше уж в каждом конкретном случае смотреть и
// разбираться, типа решать где послаблять, чем когда всё просто протекает» —
// строка списка есть НАЗВАННОЕ решение, где послаблено; молчание решением не
// является. Сверка со списком ДВУСТОРОННЯЯ: новый сайт вне списка = РОСТ,
// строка списка без сайта = список врёт о мире, которого уже нет.
//
// КЛЮЧ СТРОКИ СПИСКА — файл, правило и ТЕКСТ строки, но НЕ её номер. Номер
// печатается в ошибке, потому что человеку нужен адрес; в ключ он не входит,
// потому что иначе правка где-то выше по файлу гноила бы весь список разом, и
// список начал бы требовать ухода за собой вместо того, чтобы его сокращать.
//
// ЧТО ИМЕННО СУДИТСЯ У `wrapi`/`wrapf`, И ПОЧЕМУ ИМЕННО ТАК. Перепись M-124
// расклассифицировала ВСЕ `wrapi` дерева по ВТОРОМУ АРГУМЕНТУ — по тому, какой
// период ему передают:
//   · `b.cols`/`b.rows`, `c.cols`/`c.rows` — bucket-сетки: 18 вызовов, ЗАКОННО
//     (сторона сетки есть `сторона мира / размер клетки` и степенью двойки быть
//     не обязана);
//   · `py`, `p` — период шума: 12 вызовов, ЗАКОННО, AGENTS называет этот случай
//     единственным законным применением `wrapi` поимённо;
//   · `src/sub/` — вне закона по построению: ЗАКОН АДРЕСА п.5, «у субмира своё
//     адресное пространство… сводить их с макро-адресом ЗАПРЕЩЕНО». Поэтому
//     каталог не сканируется вовсе, а не прощается;
//   · СТОРОНА МИРА — 23 вызова, все снесены M-124. Ноль — это и есть стена.
//
// ЧЕГО ЭТОТ ПРИБОР НЕ УМЕЕТ, СКАЗАНО ВСЛУХ. Отличить «сторона мира» от «период
// сетки» можно ТОЛЬКО по имени: типа у них одного — `int`. Значит прибор
// слепнет от переименования. Для `wrapi`/`wrapf` список имён узкий и прицельный
// (`mapW`, `td.width`…), а для `%` и рукописного индекса он ШИРЕ и включает
// односимвольные псевдонимы `W`/`H`/`w`/`h`, которыми полдерева зовёт ту же
// сторону. Широкий список ловит лишнее — и это сознательный выбор: лишний сайт
// стоит ОДНОЙ строки списка и больше никогда не кричит, а пропущенный сайт
// стоит дефекта. Ошибаться в эту сторону здесь дешевле, и цена названа числом,
// а не обещанием. У слепоты есть срок: владелец 2026-09-28 вынес вердикт «МИР
// КВАДРАТНЫЙ ИЗОТРОПНЫЙ ВСЕГДА» — пара `mapW`/`mapH` подлежит сносу в пользу
// одной стороны (наряд в реестре). Настоящая стена — ТИП стороны мира, а не
// имя, и она станет возможна ровно тогда.
//
// Стрижка комментариев и литералов ниже может только ОСЛЕПИТЬ прибор (убрать
// текст), но не оболгать: ложного срабатывания из неё не выходит по построению.
#include "check.h"

#include <algorithm>
#include <cstddef>
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

// Каталоги, где живёт МАКРОМИР и всё, что адресует его клетки. `src/sub/`
// отсутствует намеренно — см. шапку. `src/core` добавлен 2026-10-04: свёртка
// стороны мира через `wrapi` ВЫЖИЛА в самой канонической двери
// (`torus_step_toward@src/core/torus.h`, звалась маршем на каждом шаге), и
// прибор молчал ровно потому, что домашний каталог дверей не сканировался.
constexpr std::string_view kScanDirs[] = {
    "src/core",
    "src/macro", "src/app", "src/content", "src/ecs", "src/ui", "src/events",
};

constexpr const char* kLegacyList = "tests/data/world_fold_legacy.txt";

// Имена, которыми в этом дереве зовётся СТОРОНА МИРА у ВЫЗОВА свёртки. Узкий
// прицельный список: второй аргумент `wrapi`/`wrapf` — целое выражение, и
// подстрока в нём читается однозначно.
constexpr std::string_view kWorldSideNames[] = {
    "mapW", "mapH", "mapSide", "terrain.width", "terrain.height",
    "td.width", "td.height",
};

// Имена стороны мира у ОПЕРАТОРА (`%`, `*`). Здесь сравнение ТОЧНОЕ, по целому
// идентификатору, поэтому в список можно класть односимвольные псевдонимы: они
// не подстроки и `width` не совпадёт с `widthPx`. Список шире предыдущего
// сознательно — см. шапку, «ошибаться дешевле в эту сторону».
constexpr std::string_view kSideTokens[] = {
    "mapW", "mapH", "mapSide", "width", "height", "W", "H", "w", "h",
};

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}

// Комментарии и строковые литералы вон: `wrapi(` в прозе — не вызов, а `%` в
// `"%d"` — не деление. Длина и разбивка на строки СОХРАНЯЮТСЯ (вырезанное
// заменяется пробелами, переводы строк остаются), потому что по смещению в
// очищенном тексте прибор обязан назвать номер строки в исходнике.
std::string strip_comments_and_literals(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    const auto blank = [&out](char c) { out.push_back(c == '\n' ? '\n' : ' '); };
    for (std::size_t i = 0; i < src.size();) {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') blank(src[i++]);
        } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            blank(src[i++]);
            blank(src[i++]);
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/'))
                blank(src[i++]);
            if (i < src.size()) blank(src[i++]);
            if (i < src.size()) blank(src[i++]);
        } else if (src[i] == '"' || src[i] == '\'') {
            const char quote = src[i];
            blank(src[i++]);
            while (i < src.size() && src[i] != quote) {
                if (src[i] == '\\' && i + 1 < src.size()) blank(src[i++]);
                if (i < src.size()) blank(src[i++]);
            }
            if (i < src.size()) blank(src[i++]);
        } else {
            out.push_back(src[i]);
            ++i;
        }
    }
    return out;
}

bool names_world_side(std::string_view arg) {
    for (std::string_view n : kWorldSideNames) {
        if (arg.find(n) != std::string_view::npos) return true;
    }
    return false;
}

// Идентификатор СРАЗУ ЗА оператором, с двумя снятыми обёртками: приведение
// (`std::size_t(`, `int(`, `float(`) и уточнение (`gs.`, `td.`). Снимается
// именно хвост после последней точки: спрашивают про `td.width` — отвечать
// должно «width», иначе каждое новое поле-владелец потребовало бы своей строки
// в словаре, а словарь обязан говорить о ВЕЛИЧИНЕ, не о том, где она лежит.
std::string operand_token(const std::string& src, std::size_t at) {
    std::size_t i = at;
    for (;;) {
        while (i < src.size() && (src[i] == ' ' || src[i] == '\n' ||
                                  src[i] == '\t' || src[i] == '('))
            ++i;
        const std::size_t start = i;
        while (i < src.size() && (ident_char(src[i]) || src[i] == ':')) ++i;
        // Приведение вида `std::size_t(`: имя, за которым сразу скобка. Съесть
        // его и спросить снова — за ним стоит настоящий операнд.
        if (i < src.size() && src[i] == '(' && i > start) continue;
        std::string tok = src.substr(start, i - start);
        // Уточнение `gs.mapW` / `td.width`: съесть точку и взять хвост.
        while (i < src.size() && src[i] == '.') {
            ++i;
            const std::size_t s2 = i;
            while (i < src.size() && ident_char(src[i])) ++i;
            tok = src.substr(s2, i - s2);
        }
        return tok;
    }
}

// Идентификатор СЛЕВА от оператора — с той же разборкой обёрток, но задом
// наперёд: `double(mapW) *` отдаёт `mapW`, `std::size_t(y) *` отдаёт `y`,
// голое `W *` отдаёт `W`.
std::string left_operand_token(const std::string& src, std::size_t at) {
    std::size_t i = at;
    while (i > 0 && (src[i - 1] == ' ' || src[i - 1] == '\n' ||
                     src[i - 1] == '\t'))
        --i;
    if (i == 0) return {};
    std::size_t s = i, e = i;
    if (src[i - 1] == ')') {  // приведение: заглянуть ВНУТРЬ скобок
        int depth = 0;
        std::size_t j = i - 1;
        for (;;) {
            if (src[j] == ')') ++depth;
            else if (src[j] == '(' && --depth == 0) break;
            if (j == 0) return {};
            --j;
        }
        s = j + 1;
        while (s < src.size() && src[s] == ' ') ++s;
        e = s;
        while (e < src.size() &&
               (ident_char(src[e]) || src[e] == ':' || src[e] == '.'))
            ++e;
    } else {
        while (s > 0 && (ident_char(src[s - 1]) || src[s - 1] == '.')) --s;
    }
    std::string tok = src.substr(s, e - s);
    const std::size_t dot = tok.rfind('.');
    if (dot != std::string::npos) tok = tok.substr(dot + 1);
    return tok;
}

bool token_is_world_side(const std::string& tok) {
    for (std::string_view n : kSideTokens)
        if (tok == n) return true;
    return false;
}

// Второй аргумент каждого вызова `name(...)` в тексте, со смещением вызова.
// Скобки считаются, так что вложенный вызов и тернарник внутри первого
// аргумента запятую не крадут; перенос строки роли не играет — разбирается весь
// файл целиком. Разбор ОДИН на оба спеллинга свёртки: второй копии нет (§8 п.5).
std::vector<std::pair<std::string, std::size_t>> call_second_args(
    const std::string& src, std::string_view call) {
    std::vector<std::pair<std::string, std::size_t>> args;
    for (std::size_t at = src.find(call); at != std::string::npos;
         at = src.find(call, at + 1)) {
        // `word_wrapi(` вызовом `wrapi` не является; `sm::wrapi(` — является.
        if (at > 0 && ident_char(src[at - 1])) continue;
        std::size_t i = at + call.size();
        int depth = 1;
        std::size_t commaAt = std::string::npos;
        for (; i < src.size() && depth > 0; ++i) {
            const char c = src[i];
            if (c == '(') ++depth;
            else if (c == ')') --depth;
            else if (c == ',' && depth == 1 && commaAt == std::string::npos) {
                commaAt = i;
            }
        }
        if (depth != 0 || commaAt == std::string::npos) continue;  // не вызов
        args.emplace_back(src.substr(commaAt + 1, (i - 1) - (commaAt + 1)), at);
    }
    return args;
}

// ── САЙТЫ ТАЮЩЕГО СПИСКА ──────────────────────────────────────────────────
// Три правила отдают сайты в ОДНУ ведомость: вопрос у них один («где эта
// клетка»), значит и список один, и растопить его можно только починкой кода.
struct Site {
    std::string file;   // путь от корня репозитория
    std::string rule;   // wrapf | mod | index
    std::string text;   // строка исходника, схлопнутые пробелы
    int line = 0;       // только для печати; в ключ НЕ входит — см. шапку
};

std::string key_of(const Site& s) {
    return s.file + "\t" + s.rule + "\t" + s.text;
}

int line_at(const std::string& src, std::size_t pos) {
    return 1 + int(std::count(src.begin(), src.begin() + std::ptrdiff_t(pos),
                              '\n'));
}

// Строка, на которой стоит смещение, со схлопнутыми пробелами: ключ обязан
// пережить переформатирование отступов, иначе `clang-format` растопит список.
std::string line_text_at(const std::string& src, std::size_t pos) {
    std::size_t b = src.rfind('\n', pos);
    b = (b == std::string::npos) ? 0 : b + 1;
    std::size_t e = src.find('\n', pos);
    if (e == std::string::npos) e = src.size();
    std::string out;
    bool space = true;  // гасит ведущие пробелы
    for (std::size_t i = b; i < e; ++i) {
        const char c = src[i];
        if (c == ' ' || c == '\t') {
            if (!space) out.push_back(' ');
            space = true;
        } else {
            out.push_back(c);
            space = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void add_site(std::vector<Site>& out, const std::string& rel,
              const std::string& src, std::size_t pos, const char* rule) {
    out.push_back({rel, rule, line_text_at(src, pos), line_at(src, pos)});
}

// `wrapf` по стороне мира: тот же разбор второго аргумента, что у `wrapi`.
void scan_wrapf(std::vector<Site>& out, const std::string& rel,
                const std::string& src) {
    for (const auto& [arg, at] : call_second_args(src, "wrapf(")) {
        if (names_world_side(arg)) add_site(out, rel, src, at, "wrapf");
    }
}

// `%`, чей ПРАВЫЙ операнд есть сторона мира. `a %= b` и `%` в литерале сюда не
// попадают: первое — не тот оператор, второе вырезано стрижкой.
void scan_mod(std::vector<Site>& out, const std::string& rel,
              const std::string& src) {
    for (std::size_t i = 0; i + 1 < src.size(); ++i) {
        if (src[i] != '%') continue;
        if (src[i + 1] == '=' || (i > 0 && src[i - 1] == '%')) continue;
        if (token_is_world_side(operand_token(src, i + 1)))
            add_site(out, rel, src, i, "mod");
    }
}

// Рукописный индекс `y * <сторона> + x` — адрес клетки, посчитанный мимо
// `cell_of`. Ищется по СЕРЕДИНЕ выражения (`* сторона +`), потому что левый
// множитель бывает чем угодно — `std::size_t(y)`, `nb[1]`, `cur.y`.
void scan_index(std::vector<Site>& out, const std::string& rel,
                const std::string& src) {
    for (std::size_t i = 0; i + 1 < src.size(); ++i) {
        if (src[i] != '*') continue;
        if (src[i + 1] == '*' || src[i + 1] == '=' || src[i + 1] == '/')
            continue;
        if (i > 0 && (src[i - 1] == '*' || src[i - 1] == '/')) continue;
        const std::string tok = operand_token(src, i + 1);
        if (!token_is_world_side(tok)) continue;
        // СТОРОНА НА СТОРОНУ — ЭТО ПЛОЩАДЬ, А НЕ АДРЕС. Адрес умножает
        // КООРДИНАТУ на сторону; `mapW * mapW` и `W * H` считают размер мира, и
        // вносить их в тающий список нельзя: чинить там нечего, значит строка
        // не растает никогда и список перестанет быть тающим.
        if (token_is_world_side(left_operand_token(src, i))) continue;
        // За стороной обязан стоять `+`: `* W;` — умножение, а не адрес.
        std::size_t j = src.find(tok, i + 1);
        if (j == std::string::npos) continue;
        j += tok.size();
        while (j < src.size() && (src[j] == ' ' || src[j] == '\n' ||
                                  src[j] == '\t' || src[j] == ')'))
            ++j;
        if (j < src.size() && src[j] == '+')
            add_site(out, rel, src, i, "index");
    }
}

std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// ── ОБХОД ДЕРЕВА ──────────────────────────────────────────────────────────
struct Scan {
    int filesScanned = 0;
    int wrapiCalls = 0;
    int wrapiViolations = 0;
    std::vector<Site> sites;
};

Scan scan_tree() {
    const fs::path root{TIMAERT_SOURCE_DIR};
    Scan sc;
    for (std::string_view dir : kScanDirs) {
        const fs::path d = root / fs::path(dir);
        if (!fs::is_directory(d)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext == ".cpp" || ext == ".h") files.push_back(e.path());
        }
        // Порядок обхода каталога не определён стандартом; без сортировки
        // ведомость и её печать съезжали бы от машины к машине.
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files) {
            ++sc.filesScanned;
            const std::string rel =
                fs::relative(p, root).generic_string();
            const std::string src =
                strip_comments_and_literals(read_file(p));

            for (const auto& [arg, at] : call_second_args(src, "wrapi(")) {
                ++sc.wrapiCalls;
                if (!names_world_side(arg)) continue;
                ++sc.wrapiViolations;
                std::fprintf(stderr,
                             "  wrapi по СТОРОНЕ МИРА: %s:%d  ->  второй"
                             " аргумент «%s»\n",
                             rel.c_str(), line_at(src, at), arg.c_str());
            }
            scan_wrapf(sc.sites, rel, src);
            scan_mod(sc.sites, rel, src);
            scan_index(sc.sites, rel, src);
        }
    }
    return sc;
}

// Сверка со списком — ДВЕ величины, и обе обязаны быть нулём. `unlisted` ловит
// РОСТ долга (новый сайт свёртки), `stale` — врущий список (строка, которой в
// коде уже нет). Одна функция на обе, потому что на неё стоит негативный
// контроль ниже: механизм «список умеет только таять» иначе остался бы
// проверенным руками, то есть непроверенным.
struct LegacyDiff {
    int unlisted = 0;
    int stale = 0;
};

LegacyDiff compare_legacy(const std::vector<Site>& sites,
                          const std::set<std::string>& allowed, bool loud) {
    LegacyDiff d;
    std::set<std::string> seen;
    for (const Site& s : sites) {
        const std::string key = key_of(s);
        seen.insert(key);
        if (allowed.count(key) != 0) continue;
        ++d.unlisted;
        if (loud)
            std::fprintf(stderr,
                         "  НОВАЯ СВЁРТКА МИРА [%s] %s:%d  %s\n",
                         s.rule.c_str(), s.file.c_str(), s.line,
                         s.text.c_str());
    }
    for (const std::string& key : allowed)
        if (seen.count(key) == 0) {
            ++d.stale;
            if (loud) {
                std::string shown = key;
                for (char& c : shown)
                    if (c == '\t') c = ' ';
                std::fprintf(stderr, "  УСТАРЕЛА СТРОКА СПИСКА  %s\n",
                             shown.c_str());
            }
        }
    return d;
}

void test_world_fold() {
    const Scan sc = scan_tree();

    // Цикл, который меряет, обязан утверждать, что ПОМЕРИЛ (§8 п.3): иначе
    // пустой обход отчитается зелёным ровно тогда, когда прибор сломан.
    CHECK(sc.filesScanned > 100, "прибор действительно обошёл дерево");
    CHECK(sc.wrapiCalls > 0,
          "в сканируемых каталогах ЕСТЬ вызовы wrapi — иначе судить нечего "
          "и зелёный ничего не значит");
    CHECK(sc.wrapiViolations == 0,
          "клетка мира не сворачивается делением: wrapi по стороне мира = 0 "
          "(AGENTS §3 ЗАКОН АДРЕСА п.4 — свёртка есть МАСКА, cell_of/cell_step)");

    const std::vector<std::string> legacy =
        read_lines(std::string(TIMAERT_SOURCE_DIR) + "/" + kLegacyList);
    CHECK(!legacy.empty(), "белый список старых свёрток читается");
    std::set<std::string> allowed;
    for (const std::string& l : legacy)
        if (!l.empty() && l[0] != '#') allowed.insert(l);

    const LegacyDiff diff = compare_legacy(sc.sites, allowed, true);
    CHECK(diff.unlisted == 0,
          "новых свёрток мира не добавляли: заворот идёт МАСКОЙ через "
          "cell_of/cell_step/cell_x/cell_y (core/torus.h)");
    // Список может только ТАЯТЬ. Строка, которой в коде больше нет,
    // вычёркивается — иначе список сам станет тем, что он охраняет: записью,
    // которая помнит мир, какого уже нет.
    CHECK(diff.stale == 0, "в белом списке нет строк, которых в коде уже нет");

    int byRule[3] = {0, 0, 0};
    for (const Site& s : sc.sites) {
        if (s.rule == "wrapf") ++byRule[0];
        else if (s.rule == "mod") ++byRule[1];
        else ++byRule[2];
    }
    std::printf(
        "world_fold: файлов %d, вызовов wrapi %d (по стороне мира %d — СТЕНА)\n"
        "  тающий список: wrapf %d · %% %d · рукописный индекс %d — всего %zu\n"
        "  строк списка %zu (может только таять)\n",
        sc.filesScanned, sc.wrapiCalls, sc.wrapiViolations, byRule[0],
        byRule[1], byRule[2], sc.sites.size(), allowed.size());
}

// НЕГАТИВНЫЙ КОНТРОЛЬ: детектор обязан РЕАЛЬНО ловить нарушение, иначе
// `violations == 0` выше есть вера, а не измерение (§8 п.6). Контроль гоняет те
// же самые функции, что и обход дерева, — второй копии разбора нет.
void test_detector_actually_sees() {
    const std::string guilty =
        "int a = sm::wrapi(x + 1, app.gs.mapW);\n"
        "int b = wrapi(int(std::floor(v)),\n                  mapH);\n";
    const auto guiltyArgs =
        call_second_args(strip_comments_and_literals(guilty), "wrapi(");
    CHECK(guiltyArgs.size() == 2, "разбор нашёл оба вызова, включая переносный");
    int caught = 0;
    for (const auto& [a, at] : guiltyArgs) caught += names_world_side(a) ? 1 : 0;
    CHECK(caught == 2, "детектор ловит свёртку по стороне мира — оба спеллинга");

    // Законные периоды обязаны молчать, иначе прибор запретит то, что AGENTS
    // разрешает поимённо, и следующий агент начнёт подгонять мир под прибор.
    const std::string innocent =
        "int g = wrapi(cx0 + ox, b.cols);\n"
        "int h = wrapi(v, width / period);\n"
        "int k = wrapi(iy, p);\n";
    const auto innocentArgs =
        call_second_args(strip_comments_and_literals(innocent), "wrapi(");
    CHECK(innocentArgs.size() == 3, "разбор нашёл все три законных вызова");
    int falseAlarms = 0;
    for (const auto& [a, at] : innocentArgs)
        falseAlarms += names_world_side(a) ? 1 : 0;
    CHECK(falseAlarms == 0,
          "bucket-сетка и период шума прибор НЕ трогает (AGENTS §3 п.4 — "
          "единственное законное применение wrapi)");

    // Скобки не должны красть запятую: тернарник и вложенный вызов в ПЕРВОМ
    // аргументе — ровно та форма, что стояла в smoke.cpp:2084 до M-124.
    const auto nested = call_second_args(
        strip_comments_and_literals(
            "wrapi(baseY + (probe % 31 == 30 ? 41 : 0), b.rows);"),
        "wrapi(");
    CHECK(nested.size() == 1 && !names_world_side(nested[0].first),
          "вложенные скобки не сбивают разбор второго аргумента");

    // Прозе — молчание: закон цитируют в комментариях, и цитата не вызов.
    CHECK(call_second_args(strip_comments_and_literals(
                               "// свернуть через wrapi(x, mapW) запрещено\n"),
                           "wrapi(")
              .empty(),
          "wrapi в комментарии вызовом не считается");
}

// Негативный контроль на ТРИ НОВЫХ детектора — каждый порознь, и на каждый своя
// невиновная пара. «Нашлось три сайта» прошло бы и тогда, когда один детектор
// кричит трижды, а два мертвы, поэтому утверждается СОСТАВ по правилам.
void test_new_detectors_see_each_spelling() {
    const std::string guilty =
        "float nx = wrapf(cx + d, float(ctx.mapW));\n"
        "int x = int(rng.next_u32() % std::uint32_t(gs.mapW));\n"
        "std::size_t i = std::size_t(y) * W + x;\n";
    std::vector<Site> sites;
    const std::string src = strip_comments_and_literals(guilty);
    scan_wrapf(sites, "fixture.cpp", src);
    scan_mod(sites, "fixture.cpp", src);
    scan_index(sites, "fixture.cpp", src);
    int wrapf = 0, mod = 0, index = 0;
    for (const Site& s : sites) {
        wrapf += s.rule == "wrapf";
        mod += s.rule == "mod";
        index += s.rule == "index";
    }
    CHECK(wrapf == 1, "детектор wrapf видит свёртку float по стороне мира");
    CHECK(mod == 1, "детектор %% видит остаток по стороне мира сквозь приведение");
    CHECK(index == 1, "детектор индекса видит рукописный y * W + x");

    // Невиновные: период решётки, календарь и обычное умножение. Каждый — ровно
    // та форма, которую закон РАЗРЕШАЕТ; крикни прибор здесь, и следующий агент
    // пошёл бы переписывать законный код под прибор (ЗАКОН НУЛЕВОЙ п.4).
    const std::string innocent =
        "float f = wrapf(v, float(period));\n"
        "const int day = tick % kTicksPerDay;\n"
        "const int slot = i % kChronicleFacts;\n"
        "const std::size_t s = std::size_t(y) * b.cols + x;\n"
        "const int area = W * H + 1;\n"
        "const double d2 = double(mapW) * double(mapW) + 1.0;\n"
        "budget = cost * width;\n";
    std::vector<Site> clean;
    const std::string isrc = strip_comments_and_literals(innocent);
    scan_wrapf(clean, "fixture.cpp", isrc);
    scan_mod(clean, "fixture.cpp", isrc);
    scan_index(clean, "fixture.cpp", isrc);
    for (const Site& s : clean)
        std::fprintf(stderr, "  ЛОЖНАЯ ТРЕВОГА [%s] %s\n", s.rule.c_str(),
                     s.text.c_str());
    CHECK(clean.empty(),
          "период шума, календарь, bucket-сетка и простое умножение прибор "
          "НЕ трогает");

    // Литерал не код: `%` в формате печати остатком мира не является. Без
    // стрижки литералов этот сайт кричал бы в каждом printf дерева.
    std::vector<Site> lit;
    const std::string lsrc =
        strip_comments_and_literals("std::printf(\"%w %h\", a, b);\n");
    scan_mod(lit, "fixture.cpp", lsrc);
    CHECK(lit.empty(), "%% внутри строкового литерала свёрткой не считается");

    // Ключ не помнит номера строки: та же строка, сдвинутая переносами выше,
    // даёт ТОТ ЖЕ ключ — иначе список гнил бы от правок по соседству.
    std::vector<Site> moved;
    const std::string msrc = strip_comments_and_literals(
        "\n\n\nstd::size_t i = std::size_t(y) * W + x;\n");
    scan_index(moved, "fixture.cpp", msrc);
    CHECK(moved.size() == 1 && moved[0].line == 4,
          "номер строки считается верно и печатается");
    std::vector<Site> one;
    scan_index(one, "fixture.cpp",
               strip_comments_and_literals(
                   "std::size_t i =  std::size_t(y)  *  W  +  x;\n"));
    CHECK(one.size() == 1 && key_of(one[0]) == key_of(moved[0]),
          "ключ переживает сдвиг строки и переформатирование отступов");
}

// Негативный контроль на САМ МЕХАНИЗМ СПИСКА. Без него «список умеет только
// таять» было бы обещанием: сверка множеств — ровно то место, где опечатка
// делает гейт вечно зелёным, и никакой прогон этого не покажет.
void test_legacy_list_mechanism() {
    std::vector<Site> sites;
    scan_index(sites, "a.cpp",
               strip_comments_and_literals("i = std::size_t(y) * W + x;\n"));
    scan_mod(sites, "b.cpp",
             strip_comments_and_literals("int x = v % mapW;\n"));
    CHECK_OR_RETURN(sites.size() == 2, "в фикстуре два терпимых сайта");

    std::set<std::string> full;
    for (const Site& s : sites) full.insert(key_of(s));
    const LegacyDiff clean = compare_legacy(sites, full, false);
    CHECK(clean.unlisted == 0 && clean.stale == 0,
          "полный список даёт ноль расхождений — детектор не шумит");

    std::set<std::string> minusOne = full;
    minusOne.erase(minusOne.begin());
    const LegacyDiff grown = compare_legacy(sites, minusOne, false);
    CHECK(grown.unlisted == 1 && grown.stale == 0,
          "сайт вне списка ловится как РОСТ долга");

    std::set<std::string> plusGhost = full;
    plusGhost.insert("ghost.cpp\tindex\tno such line in any source");
    const LegacyDiff ghost = compare_legacy(sites, plusGhost, false);
    CHECK(ghost.stale == 1 && ghost.unlisted == 0,
          "строка списка без сайта в коде ловится как УСТАРЕВШАЯ");
}

} // namespace

int main() {
    test_world_fold();
    test_detector_actually_sees();
    test_new_detectors_see_each_spelling();
    test_legacy_list_mechanism();
    return sm::test::report("world_fold_law_test");
}
