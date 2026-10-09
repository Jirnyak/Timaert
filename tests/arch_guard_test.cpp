// ГЕЙТ АРХИТЕКТУРЫ: ЗАКОН, КОТОРЫЙ НЕ ПРОВЕРЯЕТ МАШИНА, БУДЕТ НАРУШЕН.
//
// Владелец, дословно (наряд M-169): «будем переводить всё на такие проверки,
// зашьём архитектуру одобренную мной в правила; лучше уж в каждом конкретном
// случае смотреть и разбираться, типа решать где послаблять, чем когда всё
// просто протекает».
//
// ДОКАЗАТЕЛЬСТВО НУЖДЫ, ОПЛАЧЕННОЕ В ДЕНЬ ПОСТАНОВКИ НАРЯДА. ЗАКОН ПОЛЯ записан
// в `AGENTS.md` дословно и заглавными — и был нарушен ЧЕТЫРЬМЯ каскадами биома в
// одном слое. ЗАКОН АДРЕСА записан там же — и в фикстуре
// `pathfinding_parity_test` годами жил мир 2×1, не квадрат. Проза не удержала ни
// разу. Машина (`doc_refs_test`) за ту же сессию поймала агента четырежды,
// включая обход, которого он не заметил сам.
//
// И У САМОЙ МАШИНЫ БЫЛА НАЙДЕНА ДЫРА — НАШЁЛ ЕЁ ВЛАДЕЛЕЦ. `doc_refs_test`
// утверждает «долг не растёт» и «список только тает», но НЕ утверждает
// «утверждение не теряет адрес». Агент погасил строку белого списка, СТЕРЕВ
// адрес вместо миграции, — и прибор это одобрил. Прибор был ровно на одно
// утверждение короче закона; правило R3 ниже — это утверждение.
//
// ЧТО СУДИТ ЭТОТ ПРИБОР, А ЧТО — КОМПИЛЯТОР. Разделение труда жёсткое:
//   · ЗАКОН судит КОМПИЛЯТОР. Строка мира с вектором, строкой, картой, умным
//     указателем или виртуальным методом не собирается (`core/row_law.h`,
//     93 контура в 17 заголовках). Обойти нельзя, время прогона ноль.
//   · ПОЛНОТУ ПРИМЕНЕНИЯ судит прибор. У компиляторной стены ровно одна дыра:
//     новый тип можно завести БЕЗ контура, и тогда она его не увидит. Правило
//     R1 закрывает ровно её и больше ничего.
// Прибор НЕ берётся судить то, что уже судит компилятор: это был бы второй
// ответ на один вопрос (DOD п.6).
//
// ПЯТЬ ПРАВИЛ:
//   R1 КОНТУР НА МЕСТЕ — СТЕНА, без послаблений. В заголовке-строке у каждой
//      структуры есть `TIMAERT_ROW`. Достигнуто 93 из 93 в день постройки.
//   R2 УЗКИЙ КАНАЛ — ТАЮЩИЙ СПИСОК. `#include "macro/…"` из `src/sub/`
//      перечислены поимённо; новая нитка красит гейт и печатает файл.
//   R3 ВЕРДИКТ НЕСЁТ АДРЕС — ТАЮЩИЙ СПИСОК. Утверждение ПРАВДА/РАСХОЖДЕНИЕ в
//      `SKELETON.md` без адреса точки исполнения.
//   R4 ENTT ТОЛЬКО ТАЕТ — ТАЮЩИЙ СПИСОК ФАЙЛОВ (M-188 ступень 1, владелец
//      2026-09-29: «не просто отказ от ENTT на словах а чтобы жёстко было
//      запрещено»). Файл src с entt-КОДОМ (`entt::` или включение entt)
//      перечислен поимённо; новый файл с entt — красная сюита. Мера — код,
//      не слово в комментарии: комментарии чистит кластер 7, прибор судит
//      исполняемое. Когда список дотает до нуля, контур сменяет СТЕНА —
//      выпил FetchContent (включение entt перестаёт существовать
//      препроцессором, финал M-188).
//   R5 КАРКАС КАТАЛОГОВ ЗАКРЫТ — СТЕНА (M-188 ступень 1, владелец: «делать
//      скелет архитектуры жёстким … добавляли новое в ограниченные
//      рамки/граничные условия/каркас»). Перечень каталогов верхнего уровня
//      `src/` закрыт генеральной схемой; новый каталог — красный до вердикта
//      владельца, исчезнувший — тоже событие каркаса, и тоже красный.
//
// ПОЧЕМУ ДВА ИЗ ТРЁХ — СПИСКОМ, А НЕ СТЕНОЙ. Стена над непустым каналом — не
// гейт, а блокировка чужой работы: она краснеет в первый же день и заставляет
// либо чинить всё одним заходом, либо ослабить порог (запрещено §8 ЗАКОН
// НУЛЕВОЙ п.4). Список останавливает РОСТ долга сегодня, а долг тает по ходу
// обычной работы. Строка списка есть НАЗВАННОЕ решение, где послаблено;
// молчание решением не является.
//
// И ЗАМЕТЬТЕ: ЭТОТ СПИСОК ПРОЧНЕЕ, ЧЕМ У `doc_refs`. Нарушение в коде либо есть,
// либо его нет — растопить его ПЕРЕФОРМУЛИРОВКОЙ нельзя, поэтому дыра, найденная
// владельцем у `doc_refs`, здесь не воспроизводится: строка R2 гаснет только
// вместе с включением, строка R3 — только вместе с появлением адреса или со
// сносом самого утверждения.
//
// ЧЕГО ЭТОТ ПРИБОР НЕ УМЕЕТ, СКАЗАНО ВСЛУХ. R2 считает ПРЯМЫЕ включения; долг
// транзитивный он не видит вовсе, а он больше: 40 из 42 TU субмира тянут
// `macro/` через цепочку заголовков. Настоящая стена канала — РАЗНЫЕ ПУТИ
// ВКЛЮЧЕНИЯ У РАЗНЫХ ТАРГЕТОВ (ЗАКОН ПАКЕТНОЙ ШИНЫ п.7), и она поднимается
// ПОСЛЕДНЕЙ, когда канал уже пуст. До неё R2 — это то, что можно поставить
// сегодня, и он честно называет свой потолок.
#include "check.h"

#include <algorithm>
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
constexpr const char* kLegacyList = "tests/data/arch_legacy.txt";

// ЗАГОЛОВКИ-СТРОКИ: файлы, чьё содержимое есть СТРОКИ МИРА — компоненты тела,
// строки каталогов существ, предметов, спеллов, биомов, фракций. Список ведётся
// руками сознательно: «строка мира» — суждение о СМЫСЛЕ файла, и вывести его из
// текста нельзя. Он РАСТЁТ по мере того, как заголовки приводятся к контуру;
// файл, где сегодня живут вектор или ссылка (`macro/state.h`, `macro/politik.h`,
// `macro/chronicle.h` — 16 типов, замер M-169), сюда не вносится, пока это не
// починено: наряд в реестре, а не красный гейт.
constexpr std::string_view kRowHeaders[] = {
    "src/ecs/components.h",        "src/tables/npc.h",
    "src/tables/items.h",          "src/tables/body_parts.h",
    "src/tables/role_weights.h",
    "src/tables/attributes.h",     "src/macro/anketa.h",
    "src/tables/army.h",           "src/tables/bonus.h",
    "src/tables/biomes.h",         "src/tables/damage_types.h",
    "src/tables/spells.h",         "src/tables/sprite_rows.h",
    "src/tables/commodity.h",      "src/tables/faction.h",
    "src/tables/celestial.h",      "src/tables/seasons.h",
    "src/macro/landmark_registry.h",
};

constexpr std::string_view kSubDir = "src/sub";
constexpr std::string_view kSrcDir = "src";
constexpr std::string_view kSkeleton = "SKELETON.md";

// R5: ЗАКРЫТЫЙ ПЕРЕЧЕНЬ КАТАЛОГОВ ВЕРХНЕГО УРОВНЯ src/ — генеральная схема
// (AGENTS, ГЕНЕРАЛЬНАЯ СХЕМА; вердикт владельца 2026-09-29, M-188).
// Расширяется ТОЛЬКО вердиктом владельца — новая строка здесь и есть запись
// его решения.
constexpr std::string_view kTopDirs[] = {
    "app", "assets", "content", "core", "ecs", "events",
    "gpu",  "macro",  "sub",     "tables", "ui",
};

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

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
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

// ── ВЕДОМОСТЬ ТАЮЩЕГО СПИСКА ──────────────────────────────────────────────
struct Site {
    std::string rule;   // channel | verdict
    std::string file;
    std::string text;
    int line = 0;       // только для печати; в ключ НЕ входит
};

std::string key_of(const Site& s) {
    return s.rule + "\t" + s.file + "\t" + s.text;
}

// ── R1: КОНТУР НА МЕСТЕ ───────────────────────────────────────────────────
// Имя из `TIMAERT_ROW(sm::ecs::Foo);` — хвост после последнего `::`. Хвост, а
// не всё выражение: контур пишется квалифицированным именем, а структура
// объявлена коротким, и сравнивать их иначе значило бы вести словарь
// пространств имён — второй словарь ради ничего.
std::string tail_after_scope(const std::string& qualified) {
    const std::size_t at = qualified.rfind(':');
    return at == std::string::npos ? qualified : qualified.substr(at + 1);
}

void collect_contours(const std::string& src, std::set<std::string>& out) {
    for (std::string_view macro : {"TIMAERT_ROW(", "TIMAERT_ROW_BYTES("}) {
        for (std::size_t at = src.find(macro); at != std::string::npos;
             at = src.find(macro, at + 1)) {
            // Объявление самого макроса в `core/row_law.h` вызовом не является;
            // сюда оно и не попадает — сканируются только заголовки-строки.
            if (at > 0 && ident_char(src[at - 1])) continue;
            std::size_t i = at + macro.size();
            const std::size_t start = i;
            while (i < src.size() && (ident_char(src[i]) || src[i] == ':')) ++i;
            if (i > start) out.insert(tail_after_scope(src.substr(start, i - start)));
        }
    }
}

void collect_structs(const std::vector<std::string>& lines,
                     std::vector<std::pair<std::string, int>>& out) {
    constexpr std::string_view kKey = "struct ";
    for (std::size_t n = 0; n < lines.size(); ++n) {
        const std::string& l = lines[n];
        if (l.rfind(kKey, 0) != 0) continue;  // только НАЧАЛО строки: вложенные
                                              // структуры живут с отступом и
                                              // строкой мира не являются
        std::size_t i = kKey.size();
        const std::size_t start = i;
        while (i < l.size() && ident_char(l[i])) ++i;
        if (i == start) continue;
        // `struct Foo;` — предобъявление, а не определение: контура не просит.
        std::size_t j = i;
        while (j < l.size() && l[j] == ' ') ++j;
        if (j < l.size() && l[j] == ';') continue;
        out.emplace_back(l.substr(start, i - start), int(n) + 1);
    }
}

int test_contour_is_everywhere() {
    int missing = 0, structs = 0, headers = 0;
    for (std::string_view rel : kRowHeaders) {
        const fs::path p = fs::path(kRoot) / fs::path(rel);
        const std::string src = read_file(p);
        if (src.empty()) {
            std::fprintf(stderr, "  ЗАГОЛОВОК-СТРОКА НЕ ЧИТАЕТСЯ: %s\n",
                         std::string(rel).c_str());
            ++missing;
            continue;
        }
        ++headers;
        std::set<std::string> contoured;
        collect_contours(src, contoured);
        std::vector<std::pair<std::string, int>> declared;
        collect_structs(split_lines(src), declared);
        for (const auto& [name, line] : declared) {
            ++structs;
            if (contoured.count(name) != 0) continue;
            ++missing;
            std::fprintf(stderr,
                         "  СТРОКА МИРА БЕЗ КОНТУРА: %s:%d  struct %s  —  "
                         "допиши TIMAERT_ROW(<ns>::%s); в блок контура внизу "
                         "файла (core/row_law.h)\n",
                         std::string(rel).c_str(), line, name.c_str(),
                         name.c_str());
        }
    }
    CHECK(headers == int(std::size(kRowHeaders)),
          "все заголовки-строки на месте и читаются");
    CHECK(structs > 50,
          "структуры в заголовках-строках ЕСТЬ — иначе судить нечего и "
          "зелёный ничего не значит");
    CHECK(missing == 0,
          "у каждой строки мира есть контур: без него новый тип обходит "
          "компиляторную стену молча (AGENTS §8 п.6)");
    return structs;
}

// ── R2: УЗКИЙ КАНАЛ ───────────────────────────────────────────────────────
void collect_channel(std::vector<Site>& out) {
    const fs::path dir = fs::path(kRoot) / fs::path(kSubDir);
    if (!fs::is_directory(dir)) return;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext == ".cpp" || ext == ".h") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        const std::string rel = fs::relative(p, fs::path(kRoot)).generic_string();
        const std::vector<std::string> lines = split_lines(read_file(p));
        for (std::size_t n = 0; n < lines.size(); ++n) {
            const std::string t = collapse(lines[n]);
            if (t.rfind("#include \"macro/", 0) != 0) continue;
            // Текст ключа — только сам include, без хвостового комментария:
            // иначе правка комментария читалась бы как новая нитка.
            const std::size_t end = t.find('"', 10);
            const std::string inc =
                end == std::string::npos ? t : t.substr(0, end + 1);
            out.push_back({"channel", rel, inc, int(n) + 1});
        }
    }
}

// ── R4: ENTT ТОЛЬКО ТАЕТ ──────────────────────────────────────────────────
// Строка несёт entt-КОД, если в ней `entt::` или включение entt. Голое слово
// «entt» в комментарии («entt-мост», «умирает в 1е») кодом не является:
// такие следы чистятся кластером 7 руками, а прибор судит то, что видит
// компилятор.
bool line_carries_entt(const std::string& t) {
    if (t.find("entt::") != std::string::npos) return true;
    const std::size_t inc = t.find("#include");
    return inc != std::string::npos
        && t.find("entt", inc) != std::string::npos;
}

void collect_entt(std::vector<Site>& out) {
    const fs::path dir = fs::path(kRoot) / fs::path(kSrcDir);
    if (!fs::is_directory(dir)) return;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext == ".cpp" || ext == ".h") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        const std::vector<std::string> lines = split_lines(read_file(p));
        for (std::size_t n = 0; n < lines.size(); ++n) {
            if (!line_carries_entt(lines[n])) continue;
            // Ключ — ФАЙЛ, не строка: список тает файлами, и правка внутри
            // уже больного файла долгом не считается (лечит его только ноль
            // entt-строк).
            out.push_back({"entt",
                           fs::relative(p, fs::path(kRoot)).generic_string(),
                           "entt", int(n) + 1});
            break;
        }
    }
}

// ── R6: КАП СУБМИРА РАЗВЕДЁН (шаг 0 M-150) ───────────────────────────────
// После развода субмир меряется СВОИМ капом (`kMaxSubObjects@src/sub/caps.h`);
// имя макро-капа `kUnifiedCap` или включение `core/caps.h` в `src/sub/` —
// рецидив закрытого расхождения. Судится то, что видит компилятор:
// комментарии срезаются (стена без стрижки ловит собственные некрологи —
// замерено на FT_Spire). Законный остаток один: спавн читает ЖИВЫЕ СЛОТЫ
// MacroStore — это МАКРО-население через канал-долг (уедет фреймом M-171),
// и его строка тает вместе с каналом. Ключ — ФАЙЛ, как у R4: список тает
// файлами.
bool line_reads_macro_cap(const std::string& line) {
    std::string code = line;
    const std::size_t comment = code.find("//");
    if (comment != std::string::npos) code.resize(comment);
    constexpr std::string_view kName = "kUnifiedCap";
    const std::size_t at = code.find(kName);
    if (at != std::string::npos) {
        const bool leftIdent = at > 0 && ident_char(code[at - 1]);
        const std::size_t end = at + kName.size();
        const bool rightIdent = end < code.size() && ident_char(code[end]);
        if (!leftIdent && !rightIdent) return true;
    }
    return collapse(code).find("#include \"core/caps.h\"") != std::string::npos;
}

void collect_subcap(std::vector<Site>& out) {
    const fs::path dir = fs::path(kRoot) / fs::path(kSubDir);
    if (!fs::is_directory(dir)) return;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext == ".cpp" || ext == ".h") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        const std::vector<std::string> lines = split_lines(read_file(p));
        for (std::size_t n = 0; n < lines.size(); ++n) {
            if (!line_reads_macro_cap(lines[n])) continue;
            out.push_back({"subcap",
                           fs::relative(p, fs::path(kRoot)).generic_string(),
                           "kUnifiedCap", int(n) + 1});
            break;
        }
    }
}

// ── R5: КАРКАС КАТАЛОГОВ ЗАКРЫТ ───────────────────────────────────────────
// Чистая сверка двух множеств — на неё стоит негативный контроль ниже.
struct DirDiff {
    std::vector<std::string> extra;    // в дереве, но не в схеме
    std::vector<std::string> missing;  // в схеме, но не в дереве
};

DirDiff compare_top_dirs(const std::set<std::string>& found) {
    DirDiff d;
    std::set<std::string> allowed;
    for (std::string_view s : kTopDirs) allowed.insert(std::string(s));
    for (const std::string& f : found)
        if (allowed.count(f) == 0) d.extra.push_back(f);
    for (const std::string& a : allowed)
        if (found.count(a) == 0) d.missing.push_back(a);
    return d;
}

void test_closed_top_dirs() {
    std::set<std::string> found;
    for (const auto& e :
         fs::directory_iterator(fs::path(kRoot) / fs::path(kSrcDir)))
        if (e.is_directory()) found.insert(e.path().filename().string());
    const DirDiff d = compare_top_dirs(found);
    for (const std::string& x : d.extra)
        std::fprintf(stderr,
                     "  НОВЫЙ КАТАЛОГ ВНЕ КАРКАСА: src/%s — каркас закрыт "
                     "генеральной схемой, расширение только вердиктом "
                     "владельца (M-188)\n",
                     x.c_str());
    for (const std::string& x : d.missing)
        std::fprintf(stderr,
                     "  КАТАЛОГ КАРКАСА ИСЧЕЗ: src/%s — снос слоя есть "
                     "событие каркаса, и решает его владелец (M-188)\n",
                     x.c_str());
    CHECK(!found.empty(), "каталоги под src/ ЕСТЬ — иначе судить нечего");
    CHECK(d.extra.empty() && d.missing.empty(),
          "каркас каталогов src/ совпадает с генеральной схемой (M-188)");
}

// ── R3: ВЕРДИКТ НЕСЁТ АДРЕС ───────────────────────────────────────────────
// Адрес имеет три законные формы (AGENTS §0 п.2): `symbol@path`,
// `путь:N «отпечаток»` и старая `` `имя` (`путь:N`) ``. Все три опознаются по
// двум приметам — собачка перед путём либо путь с двоеточием и числом.
bool has_address(const std::string& s) {
    for (std::size_t i = 0; i + 2 < s.size(); ++i) {
        if (s[i] == '@' && (ident_char(s[i + 1]))) {
            // `symbol@path.ext` — за собачкой обязан идти путь с точкой.
            std::size_t j = i + 1;
            while (j < s.size() && (ident_char(s[j]) || s[j] == '/' ||
                                    s[j] == '.' || s[j] == '-'))
                ++j;
            if (s.find('.', i) < j) return true;
        }
        if (s[i] == ':' && s[i + 1] >= '0' && s[i + 1] <= '9') {
            // `путь:N` — перед двоеточием обязан стоять путь с расширением.
            std::size_t j = i;
            while (j > 0 && (ident_char(s[j - 1]) || s[j - 1] == '/' ||
                             s[j - 1] == '.' || s[j - 1] == '-'))
                --j;
            if (s.substr(j, i - j).find('.') != std::string::npos) return true;
        }
    }
    return false;
}

bool carries_verdict(const std::string& s) {
    return s.find("ПРАВДА") != std::string::npos ||
           s.find("РАСХОЖДЕНИЕ") != std::string::npos;
}

// ГРАНИЦА УТВЕРЖДЕНИЯ. Строка таблицы (`|`) есть утверждение целиком: тезис,
// свидетельство и вердикт лежат в разных ячейках ОДНОЙ строки, и адрес законно
// стоит в любой. Абзац прозы обрывается пустой строкой, заголовком, строкой
// таблицы и НАЧАЛОМ ПУНКТА СПИСКА — без последнего бюллетень склеивается в одну
// единицу и одалживает адрес соседнего пункта.
bool starts_new_unit(const std::string& t) {
    if (t.empty() || t[0] == '#' || t[0] == '|') return true;
    if (t.rfind("- ", 0) == 0 || t.rfind("* ", 0) == 0) return true;
    std::size_t i = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
    return i > 0 && i + 1 < t.size() && t[i] == '.' && t[i + 1] == ' ';
}

void collect_verdicts(std::vector<Site>& out) {
    const std::vector<std::string> lines =
        split_lines(read_file(fs::path(kRoot) / fs::path(kSkeleton)));
    std::string unit;
    int unitLine = 0;
    const auto flush = [&]() {
        if (!unit.empty() && carries_verdict(unit) && !has_address(unit)) {
            std::string t = collapse(unit);
            if (t.size() > 140) t = t.substr(0, 140);
            out.push_back({"verdict", std::string(kSkeleton), t, unitLine});
        }
        unit.clear();
        unitLine = 0;
    };
    for (std::size_t n = 0; n < lines.size(); ++n) {
        const std::string t = collapse(lines[n]);
        if (starts_new_unit(t)) {
            flush();
            // ЗАГОЛОВОК УТВЕРЖДЕНИЕМ НЕ ЯВЛЯЕТСЯ: он называет раздел, а не
            // поведение кода, и адреса иметь не обязан.
            if (t.empty() || t[0] == '#') continue;
            unit = t;
            unitLine = int(n) + 1;
            if (t[0] == '|') flush();  // строка таблицы — единица сама по себе
            continue;
        }
        if (unit.empty()) unitLine = int(n) + 1;
        unit += " " + t;
    }
    flush();
}

// ── СВЕРКА СО СПИСКОМ ─────────────────────────────────────────────────────
// Две величины, обе обязаны быть нулём. `unlisted` ловит РОСТ долга, `stale` —
// врущий список. Функция одна, потому что на неё стоит негативный контроль:
// сверка множеств — ровно то место, где опечатка делает гейт вечно зелёным.
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
            std::fprintf(stderr, "  НОВОЕ НАРУШЕНИЕ [%s] %s:%d  %s\n",
                         s.rule.c_str(), s.file.c_str(), s.line,
                         s.text.c_str());
    }
    for (const std::string& key : allowed)
        if (seen.count(key) == 0) {
            ++d.stale;
            if (loud) {
                std::string shown = key;
                for (char& c : shown) if (c == '\t') c = ' ';
                std::fprintf(stderr, "  УСТАРЕЛА СТРОКА СПИСКА  %s\n",
                             shown.c_str());
            }
        }
    return d;
}

// ГРАНИЦА ПРИБОРА — ЭТО ЕГО ЖИВОЙ ДОЛГ. Три тающих списка не утверждают, что
// архитектура чиста; они утверждают, что долг НЕ ВЫРОС. Разница принципиальна,
// и без этих чисел в вердикте зелёный `arch_guard` читается как первое, тогда
// как верно второе (см. шапку: стена канала ещё не стоит).
struct Boundary {
    std::size_t channel = 0;   // включений macro/ из src/sub
    std::size_t verdicts = 0;  // безадресных вердиктов в SKELETON.md
    std::size_t entt = 0;      // файлов src с entt-кодом
};

Boundary test_melting_rules() {
    std::vector<Site> sites;
    collect_channel(sites);
    const std::size_t channelCount = sites.size();
    collect_verdicts(sites);
    const std::size_t verdictCount = sites.size() - channelCount;
    collect_entt(sites);
    const std::size_t enttCount = sites.size() - channelCount - verdictCount;
    collect_subcap(sites);
    const std::size_t subcapCount =
        sites.size() - channelCount - verdictCount - enttCount;

    CHECK(channelCount > 0,
          "включения macro/ из src/sub ЕСТЬ — иначе судить нечего, и зелёный "
          "означал бы сломанный обход, а не пустой канал");
    CHECK(verdictCount > 0,
          "безадресные вердикты в SKELETON.md ЕСТЬ — см. выше, тот же довод");
    // У R4 проверки `enttCount > 0` НЕТ сознательно: пока список непуст,
    // сломанный обход краснит УСТАРЕВШИМИ строками списка; а ноль — цель
    // наряда (M-188), и в тот день контур сменяет стена FetchContent, а не
    // вечно красный счётчик.

    std::vector<std::string> legacy;
    {
        std::ifstream in(std::string(kRoot) + "/" + kLegacyList);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            legacy.push_back(line);
        }
    }
    CHECK(!legacy.empty(), "белый список архитектурного долга читается");
    std::set<std::string> allowed;
    for (const std::string& l : legacy)
        if (!l.empty() && l[0] != '#') allowed.insert(l);

    const LegacyDiff diff = compare_legacy(sites, allowed, true);
    CHECK(diff.unlisted == 0,
          "архитектурный долг не вырос: новых включений macro/ в src/sub, "
          "новых безадресных вердиктов в SKELETON.md, новых файлов с entt "
          "(M-188) и новых чтений макро-капа из src/sub (шаг 0 M-150) нет");
    CHECK(diff.stale == 0, "в белом списке нет строк, которых в дереве уже нет");

    std::printf(
        "arch_guard: канал %zu включений · безадресных вердиктов %zu · "
        "файлов с entt %zu · макро-кап в sub %zu\n"
        "  строк списка %zu (может только таять)\n",
        channelCount, verdictCount, enttCount, subcapCount, allowed.size());

    return Boundary{channelCount, verdictCount, enttCount};
}

// ── НЕГАТИВНЫЕ КОНТРОЛИ ───────────────────────────────────────────────────
// Прибор, который не умеет краснеть, бесполезен (§8 п.6), и сам контроль
// утверждается: проверяется не только что детектор ловит, но и что он МОЛЧИТ
// там, где закон соблюдён.
void test_detectors_actually_see() {
    // R1: структура без контура обязана быть замечена, с контуром — нет.
    const std::string withContour =
        "struct Alpha { int a; };\nstruct Beta { int b; };\n"
        "TIMAERT_ROW(sm::Alpha);\nTIMAERT_ROW_BYTES(sm::ecs::Beta, 4);\n";
    std::set<std::string> c;
    collect_contours(withContour, c);
    std::vector<std::pair<std::string, int>> d;
    collect_structs(split_lines(withContour), d);
    CHECK(d.size() == 2, "обе структуры найдены");
    CHECK(c.count("Alpha") == 1 && c.count("Beta") == 1,
          "оба контура найдены, и квалифицированное имя сведено к короткому");

    const std::string missing = "struct Alpha { int a; };\nstruct Gamma {\n";
    std::set<std::string> c2;
    collect_contours(missing, c2);
    std::vector<std::pair<std::string, int>> d2;
    collect_structs(split_lines(missing), d2);
    CHECK(d2.size() == 2 && c2.empty(),
          "структуры без контура ловятся — иначе R1 был бы верой");

    // Предобъявление и вложенная структура контура не просят: первое ничего не
    // объявляет, вторая не строка мира.
    std::vector<std::pair<std::string, int>> d3;
    collect_structs(split_lines("struct Fwd;\n    struct Nested { int x; };\n"),
                    d3);
    CHECK(d3.empty(),
          "предобъявление и вложенная структура контура не требуют");

    // R6: детектор макро-капа видит КОД и молчит на прозе — иначе стена
    // ловила бы собственные некрологи (грабля FT_Spire, замерено 2026-10-07).
    CHECK(line_reads_macro_cap("    static std::uint32_t buf[kUnifiedCap];"),
          "чтение макро-капа в коде субмира ловится");
    CHECK(line_reads_macro_cap("#include \"core/caps.h\"  // кап"),
          "включение core/caps.h ловится и с хвостовым комментарием");
    CHECK(!line_reads_macro_cap("// kUnifiedCap@src/core/caps.h — некролог"),
          "имя в комментарии — проза, не код: детектор молчит");
    CHECK(!line_reads_macro_cap("std::array<float, kMaxSubObjects> x;"),
          "своё имя субмира детектор не трогает");

    // R3: три законные формы адреса опознаются, а безадресное — нет.
    CHECK(has_address("дверь `cell_of@src/core/torus.h` — ПРАВДА"),
          "форма symbol@path опознана");
    CHECK(has_address("ПРАВДА | src/macro/state.h:860 «saveName»"),
          "форма путь:N с отпечатком опознана");
    CHECK(has_address("РАСХОЖДЕНИЕ | `Politik` (`src/macro/politik.h:71`)"),
          "старая форма имя+номер опознана");
    CHECK(!has_address("РАСХОЖДЕНИЕ | канал узкий и названный — 82 раза"),
          "утверждение без адреса НЕ считается адресованным");
    // Голое число с двоеточием адресом не является — иначе «16:9» и «S18.2»
    // гасили бы настоящие дыры молча.
    CHECK(!has_address("ПРАВДА | соотношение 16:9 и раздел S18.2"),
          "число с двоеточием адресом не считается");
    CHECK(carries_verdict("| что-то | ПРАВДА |"), "вердикт ПРАВДА виден");
    CHECK(!carries_verdict("| что-то | сделано |"),
          "строка без вердикта в счёт не идёт");

    // R4: entt-КОД опознаётся, комментарий и похожие слова — нет.
    CHECK(line_carries_entt("    entt::registry reg;"),
          "тип entt:: опознан как код");
    CHECK(line_carries_entt("#include <entt/entt.hpp>"),
          "включение entt опознано как код");
    CHECK(!line_carries_entt("// entt-мост умирает в 1е"),
          "слово entt в комментарии кодом НЕ считается");
    CHECK(!line_carries_entt("Inventory entity; // энтити"),
          "слово entity кодом entt не считается");
    CHECK(!line_carries_entt("#include \"sub/possess.h\""),
          "включение без entt кодом не считается");

    // R5: лишний и исчезнувший каталог ловятся, точный каркас молчит.
    {
        std::set<std::string> exact;
        for (std::string_view s : kTopDirs) exact.insert(std::string(s));
        const DirDiff clean = compare_top_dirs(exact);
        CHECK(clean.extra.empty() && clean.missing.empty(),
              "точный каркас даёт ноль расхождений — детектор не шумит");
        std::set<std::string> plus = exact;
        plus.insert("dungeon_v2");
        const DirDiff grown = compare_top_dirs(plus);
        CHECK(grown.extra.size() == 1 && grown.missing.empty(),
              "каталог вне схемы ловится как рост");
        std::set<std::string> minus = exact;
        minus.erase("macro");
        const DirDiff shrunk = compare_top_dirs(minus);
        CHECK(shrunk.missing.size() == 1 && shrunk.extra.empty(),
              "исчезнувший каталог каркаса ловится");
    }

    // Заголовок — не утверждение, даже если несёт слово вердикта.
    std::vector<Site> heads;
    {
        std::vector<Site> tmp;
        // Прямая проверка границы единицы: заголовок начинает новую единицу и
        // сам в неё не входит.
        CHECK(starts_new_unit("## ПРАВДА и РАСХОЖДЕНИЕ — легенда"),
              "заголовок начинает новую единицу");
        CHECK(starts_new_unit("- пункт списка"),
              "пункт списка начинает новую единицу — иначе одолжит чужой адрес");
        CHECK(starts_new_unit("| строка | таблицы |"),
              "строка таблицы есть единица сама по себе");
        CHECK(!starts_new_unit("продолжение абзаца"),
              "обычная строка абзац не рвёт");
        heads = tmp;
    }
    CHECK(heads.empty(), "контроль границы единицы отработал без сайтов");
}

void test_legacy_list_mechanism() {
    std::vector<Site> sites = {
        {"channel", "src/sub/a.cpp", "#include \"macro/npc.h\"", 3},
        {"verdict", "SKELETON.md", "| канал узкий | РАСХОЖДЕНИЕ |", 42},
    };
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
    plusGhost.insert("channel\tsrc/sub/ghost.cpp\t#include \"macro/ghost.h\"");
    const LegacyDiff ghost = compare_legacy(sites, plusGhost, false);
    CHECK(ghost.stale == 1 && ghost.unlisted == 0,
          "строка списка без сайта в дереве ловится как УСТАРЕВШАЯ");

    // Ключ несёт ПРАВИЛО: одинаковый текст в двух правилах — два разных сайта,
    // иначе гашение долга канала молча погасило бы долг вердиктов.
    const Site a{"channel", "f", "same text", 1};
    const Site b{"verdict", "f", "same text", 1};
    CHECK(key_of(a) != key_of(b), "ключ различает правила при одинаковом тексте");
}

} // namespace

int main() {
    const int structs = test_contour_is_everywhere();
    std::printf("arch_guard: контуров проверено %d в %zu заголовках-строках\n",
                structs, std::size(kRowHeaders));
    const Boundary b = test_melting_rules();
    test_closed_top_dirs();
    test_detectors_actually_see();
    test_legacy_list_mechanism();

    // ГРАНИЦА В ВЕРДИКТЕ: зелёный этого гейта означает «долг не вырос», а НЕ
    // «архитектура чиста», и числа живого долга стоят рядом с вердиктом ровно
    // затем, чтобы вторую фразу нельзя было прочесть из первой.
    char scope[200];
    std::snprintf(scope, sizeof(scope),
                  "долг канала %zu · безадресных вердиктов %zu · файлов entt "
                  "%zu — рост запрещён, чистота НЕ утверждается",
                  b.channel, b.verdicts, b.entt);
    return sm::test::report("arch_guard_test", scope);
}
