#include "doc_refs.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <map>
#include <string_view>

namespace fs = std::filesystem;

namespace sm::docrefs {
namespace {

// Каталоги дерева, которые прибор индексирует. `build*` не индексируется
// НИКОГДА: там лежит `CMakeFiles/_CMakeLTOTest-CXX/src/main.cpp` из шести
// строк, и голое имя `main.cpp` при переписи 2026-09-27 сматчилось именно в
// него — восемнадцать ложных «отказов» из тридцати. Резолвер, видящий сборку,
// врёт про мир.
// Каталоги исходников, а не всё дерево: `balance_out*` — выводы прибора
// баланса, `artifacts`/`Testing` — мусор сборки, и файл, найденный там, был бы
// ответом не о мире, а о прошлом прогоне.
constexpr std::string_view kIndexDirs[] = {"src",     "tests",   "shaders",
                                          "assets",  "tools",   "cmake",
                                          "data",    "history"};

// Расширения, за которыми стоит код или документ ЭТОГО дерева. Список закрыт
// нарочно: иначе «.0» из `0.25…4.0` и «.g» из «e.g» становятся путями.
constexpr std::string_view kExt[] = {
    ".h", ".hpp", ".cpp", ".c", ".md", ".txt", ".sh", ".py",
    ".frag", ".vert", ".comp", ".glsl", ".tsv", ".json", ".cmake"};

// Кавычки отпечатка и тире диапазона живут в UTF-8 по нескольку байт; держим
// их строками, а не литералами char, чтобы не разбирать байты руками.
constexpr std::string_view kQuoteOpen = "\xc2\xab";   // «
constexpr std::string_view kQuoteClose = "\xc2\xbb";  // »
constexpr std::string_view kEnDash = "\xe2\x80\x93";  // –
constexpr std::string_view kEmDash = "\xe2\x80\x94";  // —

bool is_path_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' ||
           c == '-';
}
bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool known_ext(std::string_view t) {
    for (auto e : kExt)
        if (t.size() > e.size() && t.ends_with(e)) return true;
    return false;
}

std::string_view basename_of(std::string_view p) {
    const std::size_t slash = p.rfind('/');
    return slash == std::string_view::npos ? p : p.substr(slash + 1);
}

// ── ИНДЕКС ДЕРЕВА ─────────────────────────────────────────────────────────
struct Index {
    std::map<std::string, std::vector<std::string>, std::less<>> byBase;
    int count = 0;

    // Кандидаты на путь, как он написан в документе. Пусто — файла нет;
    // больше одного — имя в дереве не одно, и тогда автор обязан дописать
    // каталог (в этом дереве таких имён ровно одно: `dispatch.{h,cpp}`).
    [[nodiscard]] std::vector<std::string> resolve(std::string_view p) const {
        const auto it = byBase.find(basename_of(p));
        if (it == byBase.end()) return {};
        if (p.find('/') == std::string_view::npos) return it->second;
        std::vector<std::string> keep;
        for (const auto& cand : it->second)
            if (cand == p || cand.ends_with(std::string("/").append(p)))
                keep.push_back(cand);
        return keep;
    }
};

Index index_tree(const std::string& root) {
    Index idx;
    // Файлы КОРНЯ индексируются тоже: доки ссылаются друг на друга
    // (`SKELETON.md`), на сборку (`CMakeLists.txt`) и на смоуки (`smoke.sh`),
    // и без корня прибор объявлял бы их несуществующими — 2026-09-27 он так и
    // сделал, завысив «файла нет» вдвое. Рекурсии тут нет: каталоги корня
    // перечислены списком выше, и `build*` в него не входит.
    {
        std::error_code ec;
        fs::directory_iterator it(root, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string rel = it->path().filename().string();
            idx.byBase[rel].push_back(rel);
            ++idx.count;
        }
    }
    for (auto dir : kIndexDirs) {
        std::error_code ec;
        const fs::path base = fs::path(root) / std::string(dir);
        if (!fs::is_directory(base, ec)) continue;
        // Исключений в проекте нет (§6), поэтому обход только с `error_code`:
        // итератор, брошенный на нечитаемом каталоге, иначе убил бы процесс.
        fs::recursive_directory_iterator it(base, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string rel =
                fs::relative(it->path(), root, ec).generic_string();
            if (rel.empty()) continue;
            idx.byBase[std::string(basename_of(rel))].push_back(rel);
            ++idx.count;
        }
    }
    for (auto& [_, v] : idx.byBase) std::sort(v.begin(), v.end());
    return idx;
}

// ── ИГОЛКА: ГДЕ ОНА ЛЕЖИТ В ФАЙЛЕ ─────────────────────────────────────────
// Имя ищется по ГРАНИЦЕ СЛОВА (иначе `at` нашлось бы внутри `data`), отпечаток
// — подстрокой: он и есть дословная цитата строки.
bool line_has(const std::string& line, const std::string& needle, bool word) {
    for (std::size_t p = line.find(needle); p != std::string::npos;
         p = line.find(needle, p + 1)) {
        if (!word) return true;
        const bool leftOk = p == 0 || !is_ident_char(line[p - 1]);
        const std::size_t after = p + needle.size();
        const bool rightOk =
            after >= line.size() || !is_ident_char(line[after]);
        if (leftOk && rightOk) return true;
    }
    return false;
}

// ── РАЗБОР СТРОКИ ДОКУМЕНТА ───────────────────────────────────────────────
// Отпечаток ищется СПРАВА от ссылки: `path:530 «bool plough_field_cell(`.
bool read_fingerprint(const std::string& line, std::size_t after,
                      std::string& out) {
    std::size_t p = after;
    if (p < line.size() && line[p] == '`') ++p;
    while (p < line.size() && (line[p] == ' ' || line[p] == ',')) ++p;
    if (line.compare(p, kQuoteOpen.size(), kQuoteOpen) != 0) return false;
    p += kQuoteOpen.size();
    const std::size_t end = line.find(kQuoteClose, p);
    if (end == std::string::npos) return false;
    out = line.substr(p, end - p);
    // ЭЛИДИРОВАННАЯ цитата иголкой быть не может: «…» означает вырезанную
    // середину, и дословно она не совпадёт НИКОГДА — прибор обвинял бы такую
    // ссылку вечно и всегда зря (поймано прогоном 2026-09-27).
    if (out.find("\xe2\x80\xa6") != std::string::npos ||
        out.find("...") != std::string::npos)
        return false;
    return !out.empty();
}

// Имя ищется СЛЕВА, вплотную: `plough_field_cell` (`macro_stock.cpp:530`).
// «Вплотную» — не вкус: имя, стоящее через полстроки прозы, принадлежит не
// этой ссылке, а соседней. Перепись 2026-09-27 ловила ровно так и дала 234
// «гнили» вместо 120 — половина была чужими именами из той же клетки таблицы.
bool read_name(const std::string& line, std::size_t refBegin,
               std::string& out) {
    std::size_t p = refBegin;
    // Ссылка сама может стоять в бэктиках — перешагнуть открывающий.
    if (p > 0 && line[p - 1] == '`') --p;
    // Между именем и ссылкой законны только связки: « (», «, », « — », «: ».
    int skipped = 0;
    while (p > 0 && skipped < 8) {
        if (line[p - 1] == ' ' || line[p - 1] == '(' || line[p - 1] == ',' ||
            line[p - 1] == ':' || line[p - 1] == ';' || line[p - 1] == '-') {
            --p;
            ++skipped;
            continue;
        }
        if (p >= kEmDash.size() &&
            line.compare(p - kEmDash.size(), kEmDash.size(), kEmDash) == 0) {
            p -= kEmDash.size();
            ++skipped;
            continue;
        }
        break;
    }
    if (p == 0 || line[p - 1] != '`') return false;
    const std::size_t open = line.rfind('`', p - 2);
    if (open == std::string::npos) return false;
    const std::string group = line.substr(open + 1, p - 2 - open);
    // Группа, которая сама есть путь, именем не является: `macro_stock.h`
    // нашлось бы в файле на строке `#include` — это не адрес утверждения.
    if (group.find('/') != std::string::npos || known_ext(group)) return false;
    // Из группы берётся ПОСЛЕДНИЙ идентификатор: `MacroStore::spawnId`,
    // `TreeLayer::at` — адресует то, что стоит ближе к ссылке.
    std::size_t end = group.size();
    while (end > 0 && !is_ident_char(group[end - 1])) --end;
    std::size_t start = end;
    while (start > 0 && is_ident_char(group[start - 1])) --start;
    if (end - start < 3) return false;
    // Имя не начинается с ЦИФРЫ. Без этого прибор брал якорем число: в группе
    // `kMaxSquadSlots = 256` последний идентификатор — «256», и поиск «256» по
    // файлу отвечал про совсем другое место. Так родились 122 ложных «иголки в
    // файле нет» на первом прогоне 2026-09-27 — почти все с якорем-числом.
    if (is_digit(group[start])) return false;
    out = group.substr(start, end - start);
    return true;
}

int parse_int(const std::string& s, std::size_t begin, std::size_t end) {
    int v = 0;
    std::from_chars(s.data() + begin, s.data() + end, v);
    return v;
}

}  // namespace

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Ok: return "цела";
        case Kind::NeedsSync: return "СДВИГ ОТПЕЧАТКА (правится docs_sync)";
        case Kind::NeedleMany: return "отпечаток не один — нужен человек";
        case Kind::NameElsewhere:
            return "старая форма «имя+номер»: имя в файле есть, но не там";
        case Kind::NeedleGone: return "ИГОЛКИ В ФАЙЛЕ НЕТ";
        case Kind::PastEof: return "СТРОКИ НЕТ — ФАЙЛ КОРОЧЕ";
        case Kind::NoFile: return "ФАЙЛА НЕТ";
        case Kind::Ambiguous: return "ИМЯ ФАЙЛА НЕ ОДНО — допиши каталог";
        case Kind::Untracked: return "не отслеживается (нет имени рядом)";
    }
    return "?";
}

bool read_lines(const std::string& path, std::vector<std::string>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return true;
}

std::string legacy_key(const Ref& r) {
    // Номера строки ДОКУМЕНТА в ключе нет — иначе белый список гнил бы ровно
    // так же, как ссылки, которые он терпит, и правка абзаца выше ломала бы
    // его целиком. Ключ — документ и ссылка, как она написана.
    return r.doc + '\t' + r.raw;
}

Scan scan_tree(const std::string& root, const std::vector<std::string>& docs) {
    Scan scan;
    const Index idx = index_tree(root);
    scan.filesIndexed = idx.count;

    scan.docs = docs;
    if (scan.docs.empty()) {
        std::error_code ec;
        fs::directory_iterator it(root, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".md") continue;
            const std::string name = it->path().filename().string();
            // ЖУРНАЛ ИСТОРИИ НЕ СУДИТСЯ, и это вопрос РОДА, а не объёма:
            // `problems.md` сам объявляет себя журналом «баг → причина → фикс»,
            // чьи «записи ниже — история и хранятся как есть». Адрес в такой
            // записи есть снимок момента, и требовать от него совпадения с
            // сегодняшним кодом — то же, что требовать этого от сообщения
            // коммита. Живые доки — все остальные, включая промты: их читает
            // исполнитель ПЕРЕД кодом, и там адрес обязан быть верен сейчас.
            if (name == "problems.md") continue;
            scan.docs.push_back(name);
        }
        std::sort(scan.docs.begin(), scan.docs.end());
    }

    // Кэш прочитанных исходников: один файл читается один раз на прогон.
    std::map<std::string, std::vector<std::string>, std::less<>> src;

    for (const std::string& doc : scan.docs) {
        std::vector<std::string> lines;
        if (!read_lines((fs::path(root) / doc).string(), lines)) {
            scan.notes.push_back("не прочитан документ: " + doc);
            continue;
        }
        for (std::size_t li = 0; li < lines.size(); ++li) {
            const std::string& line = lines[li];
            std::string lastPath;  // путь для коротких `:NNN` этой же строки
            for (std::size_t i = 0; i < line.size();) {
                std::size_t refBegin = i;
                std::string path;
                bool inherited = false;

                if (is_path_char(line[i]) &&
                    (i == 0 || !is_path_char(line[i - 1]))) {
                    std::size_t j = i;
                    while (j < line.size() && is_path_char(line[j])) ++j;
                    // Курсор двигается по КОНЦУ СКАНА, а не по концу токена:
                    // хвостовую точку из токена ниже срезает обрезка, и если
                    // двигаться по обрезанному, одинокая точка прозы оставила
                    // бы `i` на месте — прибор зависал бы намертво (поймано
                    // прогоном 2026-09-27, а не рассуждением).
                    const std::size_t scanEnd = j;
                    // Точка и тире конца ФРАЗЫ путём не являются:
                    // «…@doc_refs_fixture.md.» дало бы токен с хвостовой
                    // точкой, и расширение не распознавалось бы вовсе.
                    while (j > i && (line[j - 1] == '.' || line[j - 1] == '-'))
                        --j;
                    const std::string tok = line.substr(i, j - i);
                    if (!known_ext(tok)) {
                        i = scanEnd;
                        continue;
                    }
                    lastPath = tok;
                    path = tok;
                    i = j;
                    // Форма БЕЗ НОМЕРА: `symbol@path`. Гнить тут нечему —
                    // номера нет, а имя правка соседей не двигает. Это и есть
                    // правильная форма утверждения «эта вещь живёт вот здесь».
                    if (refBegin > 0 && line[refBegin - 1] == '@') {
                        std::size_t s = refBegin - 1;
                        while (s > 0 && is_ident_char(line[s - 1])) --s;
                        if (refBegin - 1 - s >= 3) {
                            Ref sym;
                            sym.doc = doc;
                            sym.docLine = static_cast<int>(li) + 1;
                            sym.path = tok;
                            sym.needle = Needle::Name;
                            sym.needleText = line.substr(s, refBegin - 1 - s);
                            sym.raw = sym.needleText + '@' + tok;
                            const auto c = idx.resolve(tok);
                            if (c.empty()) {
                                sym.kind = Kind::NoFile;
                            } else if (c.size() > 1) {
                                sym.kind = Kind::Ambiguous;
                            } else {
                                sym.resolved = c.front();
                                std::vector<std::string> body;
                                if (!read_lines(
                                        (fs::path(root) / sym.resolved).string(),
                                        body))
                                    scan.notes.push_back(
                                        "не прочитан исходник: " + sym.resolved);
                                bool hit = false;
                                for (const std::string& b : body)
                                    if (line_has(b, sym.needleText, true)) {
                                        hit = true;
                                        break;
                                    }
                                sym.kind = hit ? Kind::Ok : Kind::NeedleGone;
                            }
                            scan.refs.push_back(std::move(sym));
                            continue;
                        }
                    }
                    if (!(i + 1 < line.size() && line[i] == ':' &&
                          is_digit(line[i + 1])))
                        continue;  // путь без номера — не ссылка на строку
                } else if (line[i] == ':' && i + 1 < line.size() &&
                           is_digit(line[i + 1]) &&
                           (i == 0 || !is_path_char(line[i - 1]))) {
                    if (lastPath.empty()) {
                        ++i;
                        continue;
                    }
                    path = lastPath;
                    inherited = true;
                } else {
                    ++i;
                    continue;
                }

                // ── номер и, если есть, конец диапазона ──
                const std::size_t numBegin = i + 1;
                std::size_t p = numBegin;
                while (p < line.size() && is_digit(line[p])) ++p;
                Ref r;
                r.lineA = parse_int(line, numBegin, p);
                r.lineB = r.lineA;
                std::size_t dashLen = 0;
                if (p < line.size() && line[p] == '-')
                    dashLen = 1;
                else if (line.compare(p, kEnDash.size(), kEnDash) == 0)
                    dashLen = kEnDash.size();
                if (dashLen != 0 && p + dashLen < line.size() &&
                    is_digit(line[p + dashLen])) {
                    r.sep = line.substr(p, dashLen);
                    const std::size_t b = p + dashLen;
                    std::size_t q = b;
                    while (q < line.size() && is_digit(line[q])) ++q;
                    r.lineB = parse_int(line, b, q);
                    p = q;
                }
                i = p;

                r.doc = doc;
                r.docLine = static_cast<int>(li) + 1;
                r.path = inherited ? std::string() : path;
                r.inheritedPath = inherited;
                r.numBegin = numBegin;
                r.numEnd = p;
                r.raw = (inherited ? std::string(":") : path + ":") +
                        line.substr(numBegin, p - numBegin);

                // ── иголка ──
                if (read_fingerprint(line, p, r.needleText))
                    r.needle = Needle::Fingerprint;
                else if (read_name(line, refBegin, r.needleText))
                    r.needle = Needle::Name;

                // ── вердикт ──
                const auto cands = idx.resolve(path);
                if (cands.empty()) {
                    r.kind = Kind::NoFile;
                } else if (cands.size() > 1) {
                    r.kind = Kind::Ambiguous;
                } else {
                    r.resolved = cands.front();
                    auto it = src.find(r.resolved);
                    if (it == src.end()) {
                        std::vector<std::string> body;
                        // Файл в индексе есть, а прочитать не смогли — это
                        // аномалия прибора, а не факт о ссылке: смолчать тут
                        // значит объявить файл пустым и обвинить ссылку.
                        if (!read_lines((fs::path(root) / r.resolved).string(),
                                        body))
                            scan.notes.push_back("не прочитан исходник: " +
                                                 r.resolved);
                        it = src.emplace(r.resolved, std::move(body)).first;
                    }
                    const std::vector<std::string>& body = it->second;
                    const bool word = r.needle == Needle::Name;
                    if (inherited) {
                        // Унаследованный путь — ДОГАДКА ПАРСЕРА, и обвинять по
                        // догадке нельзя ни в чём, даже в очевидном: 43 из 54
                        // «строки нет» первого прогона были ровно этим —
                        // короткая `:NNN` из абзаца про шейдер, примеренная к
                        // файлу, о котором речь шла двумя клетками таблицы
                        // раньше. Такая ссылка числится легаси, пока автор не
                        // напишет путь, и вердикта о ней прибор не выносит.
                        r.kind = Kind::Untracked;
                        scan.refs.push_back(std::move(r));
                        continue;
                    }
                    // Строка за концом файла судится ДО иголки и НЕЗАВИСИМО от
                    // неё: это единственный род отказа, который объективен без
                    // всякого разбора смысла — файла такой длины просто нет,
                    // и нужен ли тут якорь, вопрос уже второй.
                    if (r.lineB > static_cast<int>(body.size())) {
                        r.kind = Kind::PastEof;
                        scan.refs.push_back(std::move(r));
                        continue;
                    }
                    if (r.needle == Needle::None) {
                        r.kind = Kind::Untracked;
                        scan.refs.push_back(std::move(r));
                        continue;
                    }
                    std::vector<int> hits;
                    for (std::size_t k = 0; k < body.size(); ++k)
                        if (line_has(body[k], r.needleText, word))
                            hits.push_back(static_cast<int>(k) + 1);
                    const bool inside =
                        std::any_of(hits.begin(), hits.end(), [&](int h) {
                            return h >= r.lineA && h <= r.lineB;
                        });
                    const auto nearest = [&] {
                        return *std::min_element(
                            hits.begin(), hits.end(), [&](int a, int b) {
                                return std::abs(a - r.lineA) <
                                       std::abs(b - r.lineA);
                            });
                    };
                    if (inside) {
                        r.kind = Kind::Ok;
                        r.found = r.lineA;
                    } else if (hits.empty()) {
                        r.kind = Kind::NeedleGone;
                    } else if (!word) {
                        // Отпечаток: вхождение одно — правит машина; много —
                        // называем ближайшее и отдаём человеку.
                        r.found = nearest();
                        r.kind = hits.size() == 1 ? Kind::NeedsSync
                                                  : Kind::NeedleMany;
                    } else {
                        // Имя: машина не правит НИКОГДА (см. шапку заголовка).
                        r.kind = Kind::NameElsewhere;
                        r.found = nearest();
                    }
                }
                scan.refs.push_back(std::move(r));
            }
        }
    }
    return scan;
}

int apply_sync(const std::string& root, const Scan& scan, bool write,
               std::string* report) {
    int changed = 0;
    for (const std::string& doc : scan.docs) {
        std::vector<Ref> todo;
        for (const Ref& r : scan.refs)
            // Правится РОВНО одно: сдвинувшийся отпечаток с явно написанным
            // путём. Имя машина не двигает (шапка заголовка), унаследованный
            // путь — догадка парсера, а править по догадке значит портить.
            if (r.doc == doc && r.kind == Kind::NeedsSync &&
                r.needle == Needle::Fingerprint && !r.inheritedPath)
                todo.push_back(r);
        if (todo.empty()) continue;

        std::vector<std::string> lines;
        if (!read_lines((fs::path(root) / doc).string(), lines)) continue;

        // Справа налево внутри строки: правка номера меняет её длину, и
        // смещения ссылок левее остались бы верными только в этом порядке.
        std::sort(todo.begin(), todo.end(), [](const Ref& a, const Ref& b) {
            if (a.docLine != b.docLine) return a.docLine > b.docLine;
            return a.numBegin > b.numBegin;
        });
        for (const Ref& r : todo) {
            std::string& line = lines[static_cast<std::size_t>(r.docLine) - 1];
            std::string repl = std::to_string(r.found);
            if (r.lineB != r.lineA)
                repl += r.sep + std::to_string(r.found + (r.lineB - r.lineA));
            if (report) {
                *report += "  " + doc + ':' + std::to_string(r.docLine) + "  " +
                           r.raw + " -> " + r.path + ':' + repl + "  [" +
                           r.needleText + "]\n";
            }
            line.replace(r.numBegin, r.numEnd - r.numBegin, repl);
            ++changed;
        }
        if (!write) continue;
        std::ofstream out((fs::path(root) / doc).string(), std::ios::binary);
        for (const std::string& l : lines) out << l << '\n';
    }
    return changed;
}

}  // namespace sm::docrefs
