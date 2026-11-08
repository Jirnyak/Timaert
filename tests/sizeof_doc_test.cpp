// ── ПРИБОР СВЕРКИ `sizeof` — ДОК ПРОТИВ КОМПИЛЯТОРА ────────────────────────
//
// ЧТО ОН СУДИТ, И ТОЛЬКО ЭТО. Документ, написавший «`sizeof(GameState) ==
// 836680`», высказывается не о прошлом, а о НАСТОЯЩЕМ: это СТОЯЧАЯ ПРЕТЕНЗИЯ
// «размер равен столько-то». У неё есть свидетель, и он лучший из возможных
// (§8 п.6) — `static_assert` в коде, то есть нарушение, которое невозможно
// СОБРАТЬ. Источник числа тут компилятор, а не замер, не память агента и не
// шапка модуля, поэтому подгонке такая сверка не поддаётся.
//
// ПОЧЕМУ ЭТОГО НЕ ДЕЛАЛ `doc_refs_test`. Он объявляет границу вслух в своём же
// вердикте: числа в доках не судит никто — и это верно для числа ЗАМЕРА
// («158.4 факта в день», «712.1 МиБ»), у которого свидетеля нет и быть не
// может. Но у ОДНОГО рода чисел свидетель есть; этот прибор берёт ровно тот род
// и ни байта больше.
//
// ЧТО ОН НАШЁЛ НА ПЕРВОМ ПРОГОНЕ. Стоячих претензий в судимых доках
// шестнадцать, и ШЕСТЬ врали:
//   · `GameState` — компилятор говорит 8456, доки называли 836680 (дважды),
//     49456 и 49496: расхождение до 99-кратного;
//   · `MacroWorld` — 104 в коде, 120 в доке;
//   · `ecs::SquadUpkeep` — 80 в коде, 72 в каноне.
// Девяносто девять крат не опечатка: агент, прочитавший такую строку, строит на
// ней довод о памяти и о капах.
//
// ── ГРАНИЦА, И ОНА ЕДЕТ В ВЕРДИКТЕ ────────────────────────────────────────
// Красным объявлен РОВНО ОДИН род — «в коде ассерт есть, число другое». Он
// объективен без разбора смысла. Всё прочее СЧИТАЕТСЯ И ПЕЧАТАЕТСЯ, но не
// обвиняет, потому что прибор, который обвиняет зря, заставляет подгонять доки
// под себя (ЗАКОН НУЛЕВОЙ п.4). Вне суда по построению:
//   · ДЕЛЬТА ЗАМЕРА («`sizeof(Chronicle)` 104 → 72») — первое число есть
//     ПРОШЛОЕ, и красить её значит требовать переписать верную прозу. В дереве
//     таких записей 40, и четыре из них про типы, чей СЕГОДНЯШНИЙ размер верен;
//   · ЗАЧЁРКНУТОЕ (`~~…~~`) — автор уже объявил строку снятой: 29 из 70;
//   · выведенная правая часть (`== kMaxInventorySlots * sizeof(ItemRef)`,
//     `== 96 * 64 + 4`) — числа не названо, названа формула; в коде таких 16;
//   · ТИП МЁРТВ — `PlaceDef`/`Place` уничтожены 2026-11-02, а доки квотируют их
//     ассерты как живой механизм. Это дефект ДРУГОГО класса (мёртвое имя, дом
//     `doc_refs`), и смешивать два обвинения в одном гейте нельзя;
//   · ВНЕШНИЙ ТИП (`std::string`) — его размер есть свойство библиотеки;
//   · ЖУРНАЛЫ. Политика рода документа НЕ пишется заново: берётся
//     ЕДИНСТВЕННАЯ дверь `judges_name_liveness@tests/doc_refs.cpp` — та же, что
//     у `doc_refs_test`. Второй список судимых доков был бы вторым ответом на
//     один вопрос (DOD п.6), и разъехались бы они молча.
//
// ── ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ (ПРОГНАНА 2026-11-08) ───────
//   КРАСНЫЕ — детектор обязан увидеть, и увидел:
//     · ассерт пробника `== 8` → `== 8 && true`, то есть размер перестал быть
//       НАЗВАННЫМ ....... КРАСНЫЙ, 2 из 27 (ЦЕЛА 1 → 0, АССЕРТА НЕТ 1 → 2).
//       Так ловится смерть ассерта под живым доком — и заодно видно, что
//       формула не считается числом;
//     · `== 9` → `== 16` в фикстуре, то есть ложь стала правдой . КРАСНЫЙ,
//       2 из 27 (ВРЁТ 2 → 1, ЦЕЛА 1 → 2): состав вердиктов перестаёт сходиться,
//       и детектор не умеет «просто не заметить» починку;
//     · `judges_name_liveness` переведён на «судить всё» .... КРАСНЫЙ по
//       построению: реестр нарядов несёт 40+ исторических записей замера
//       (эта мутация НЕ прогонялась — она ломает чужой прибор, и её исход
//       назван счётом из переписи, а не зеленью).
//   МОЛЧАЩИЕ — детектор обязан пройти мимо, иначе он шум, а не детектор:
//     · правка ДЕЛЬТЫ в фикстуре («8 → 16» на «8 → 32») ..... ЗЕЛЁНЫЙ, 27/27;
//     · правка ЗАЧЁРКНУТОГО числа (`~~… == 7~~` → `== 70`) .. ЗЕЛЁНЫЙ, 27/27;
//     · новая претензия с выведенной правой частью .......... ЗЕЛЁНЫЙ (растёт
//       запиненный счёт «вне суда» — то есть видно в дифе, а не молча);
//     · любое число в `macro-registry.md` .................. ЗЕЛЁНЫЙ.
//   НЕГОДНАЯ МУТАЦИЯ, записанная чтобы её не сделали заново: правка числа в
//   РЕАЛЬНОМ доке. После неё красным станет тот же гейт, но доказано будет лишь
//   то, что файл на диске изменился, — полярность детектора проверяет ФИКСТУРА,
//   у которой свои типы и своё число.
//
// ПОЧЕМУ БЕЛОГО СПИСКА НЕТ ВОВСЕ (в отличие от `doc_refs_test`). Там список
// терпит ссылки, которые НЕЛЬЗЯ проверить; здесь непроверяемое отсеивает сама
// форма: нет литерала — нет суда. А стоячая претензия с числом либо верна, либо
// есть дефект; третьего состояния у неё не бывает, и строка «этой лжи
// разрешено стоять» не нужна ни одна.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "doc_refs.h"

#ifndef TIMAERT_SOURCE_DIR
#error "TIMAERT_SOURCE_DIR must name the repo root - see CMakeLists.txt"
#endif

namespace {

namespace fsx = std::filesystem;

constexpr const char* kRoot = TIMAERT_SOURCE_DIR;
constexpr const char* kFixture = "tests/data/sizeof_doc_fixture.md";

// ПРОБНИКИ ФИКСТУРЫ. Свидетель РОЖДАЕТ своё предусловие (§8 п.11): число, с
// которым сверяется фикстура, закреплено ЗДЕСЬ же, поэтому от правки мира
// протухнуть не может — у этих типов нет ни колонки, ни калибровки. Их три,
// потому что родов вердикта больше одного, и спорить об одном числе им нельзя.
struct SizeofDocProbe {
    std::uint32_t a;
    std::uint32_t b;
};
static_assert(sizeof(SizeofDocProbe) == 8, "пробник фикстуры: ровно 8 Б");

struct SizeofDocProbeWide {
    std::uint64_t a;
    std::uint64_t b;
};
static_assert(sizeof(SizeofDocProbeWide) == 16, "пробник фикстуры: ровно 16 Б");

// Объявлен нарочно БЕЗ ассерта: это род «размер ничем не закреплён», и без
// живого образца его детектор остался бы проверенным руками.
struct SizeofDocProbeLoose {
    std::uint32_t a;
};

// ── РАЗБОР ПРЕТЕНЗИИ ──────────────────────────────────────────────────────
struct Claim {
    std::string type;        // короткое имя: namespace отброшен
    std::string written;     // как написано, с квалификацией
    long long value = 0;
    bool literal = false;    // правая часть — НАЗВАННОЕ целое
    int line = 0;
};

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == ':';
}
bool digit_char(char c) { return c >= '0' && c <= '9'; }
bool space_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '`';
}

std::string short_name(const std::string& written) {
    const std::size_t p = written.rfind("::");
    return p == std::string::npos ? written : written.substr(p + 2);
}

// Код без комментариев И БЕЗ СТРОКОВЫХ ЛИТЕРАЛОВ. Две причины, и вторую купил
// первый же прогон этого прибора:
//   · НЕКРОЛОГ обязан называть умершее своим именем, и строка «было
//     `static_assert(sizeof(PlaceDef) == 120)`» в комментарии не смеет
//     становиться истиной кода: в этом дереве 121 из 506 вхождений
//     `static_assert` — проза в шапках (та же причина, что у `CommentOnly`);
//   · строковый ЛИТЕРАЛ — тоже не код. Свидетель ниже кормит разбор строками
//     вида "static_assert(sizeof(A) == 96)", и без вычистки литералов карта
//     размеров приняла их за закон: тип `A` оказался «закреплён двумя разными
//     числами». То есть прибор, читающий литералы, сам себе сочиняет истину.
std::string strip_cxx_noise(const std::vector<std::string>& lines,
                            std::vector<std::size_t>& lineStart) {
    std::string out;
    bool inBlock = false;
    lineStart.clear();
    for (const std::string& raw : lines) {
        lineStart.push_back(out.size());
        bool inStr = false;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (inBlock) {
                if (raw[i] == '*' && i + 1 < raw.size() && raw[i + 1] == '/') {
                    inBlock = false;
                    ++i;
                }
                continue;
            }
            if (inStr) {
                if (raw[i] == '\\') ++i;
                else if (raw[i] == '"') inStr = false;
                continue;
            }
            if (raw[i] == '"') {
                inStr = true;
                continue;
            }
            if (raw[i] == '/' && i + 1 < raw.size()) {
                if (raw[i + 1] == '/') break;
                if (raw[i + 1] == '*') {
                    inBlock = true;
                    ++i;
                    continue;
                }
            }
            out.push_back(raw[i]);
        }
        out.push_back('\n');
    }
    return out;
}

// Документ без ЗАЧЁРКНУТОГО: `~~…~~` есть авторское «снято», и спорить с ним
// значит требовать переписывать историю, уже помеченную историей. Пара ищется
// внутри строки — markdown через абзац её не носит.
std::string strip_struck(const std::vector<std::string>& lines,
                         std::vector<std::size_t>& lineStart) {
    std::string out;
    lineStart.clear();
    for (const std::string& raw : lines) {
        lineStart.push_back(out.size());
        std::size_t i = 0;
        while (i < raw.size()) {
            const std::size_t open = raw.find("~~", i);
            if (open == std::string::npos) {
                out += raw.substr(i);
                break;
            }
            const std::size_t close = raw.find("~~", open + 2);
            if (close == std::string::npos) {
                out += raw.substr(i);
                break;
            }
            out += raw.substr(i, open - i);
            i = close + 2;
        }
        out.push_back('\n');
    }
    return out;
}

int line_of(const std::vector<std::size_t>& lineStart, std::size_t off) {
    int lo = 0, hi = int(lineStart.size()) - 1, ans = 0;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        if (lineStart[std::size_t(mid)] <= off) {
            ans = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return ans + 1;
}

// `sizeof` ВНУТРИ `static_assert` — вопрос, который задаётся только КОДУ: там
// `sizeof(X) == N` бывает и условием, и инициализатором, а претензией о законе
// является лишь ассерт. Документу такого требования не ставится: седьмая ложь
// дерева (`sizeof(ecs::SquadUpkeep) == 72` в каноне) написана как ПИН, без
// слова `static_assert`, — то есть требование префикса само было бы дырой.
bool inside_static_assert(const std::string& text, std::size_t at) {
    static const std::string kAssert = "static_assert";
    const std::size_t from = at > 400 ? at - 400 : 0;
    const std::size_t p = text.rfind(kAssert, at);
    if (p == std::string::npos || p < from) return false;
    return text.find(';', p) == std::string::npos || text.find(';', p) > at;
}

// Претензия НАЗЫВАЕТ число только когда за цифрами сразу идёт конец аргумента.
// Иначе это ФОРМУЛА (`== 96 * 64 + 4`), и прочесть её первое слагаемое размером
// значило бы соврать точно так, как врут доки — отсюда проверка ХВОСТА.
std::vector<Claim> scan_claims(const std::string& text,
                               const std::vector<std::size_t>& lineStart,
                               bool requireAssert) {
    static const std::string kSizeof = "sizeof";
    std::vector<Claim> out;
    for (std::size_t s = text.find(kSizeof); s != std::string::npos;
         s = text.find(kSizeof, s + 1)) {
        if (requireAssert && !inside_static_assert(text, s)) continue;
        // СЛАГАЕМОЕ — НЕ РАЗМЕР. `static_assert(sizeof(NpcInventory) +
        // sizeof(SquadUpkeep) == 41104)` есть утверждение о СУММЕ, и приписать
        // 41104 второму слагаемому значит выдумать размер: первый прогон так и
        // сделал, объявив `SquadUpkeep` закреплённым и 80, и 41104. Отсюда
        // взгляд НАЗАД: оператор перед `sizeof` снимает претензию целиком.
        {
            std::size_t b = s;
            while (b > 0 && space_char(text[b - 1])) --b;
            const char prev = b > 0 ? text[b - 1] : '(';
            if (prev == '+' || prev == '-' || prev == '*' || prev == '/' ||
                prev == '=' || prev == '<' || prev == '>')
                continue;
        }
        std::size_t i = s + kSizeof.size();
        while (i < text.size() && space_char(text[i])) ++i;
        if (i >= text.size() || text[i] != '(') continue;
        ++i;
        while (i < text.size() && space_char(text[i])) ++i;
        const std::size_t nameBegin = i;
        while (i < text.size() && ident_char(text[i])) ++i;
        if (i == nameBegin) continue;
        const std::string written = text.substr(nameBegin, i - nameBegin);
        while (i < text.size() && space_char(text[i])) ++i;
        if (i >= text.size() || text[i] != ')') continue;
        ++i;

        Claim c;
        c.written = written;
        c.type = short_name(written);
        c.line = line_of(lineStart, s);

        while (i < text.size() && space_char(text[i])) ++i;
        if (i >= text.size() || text[i] != '=') {
            // РАВЕНСТВА НЕТ — ЗНАЧИТ НЕТ И ПРЕТЕНЗИИ, и это не «вне суда», а
            // вовсе не высказывание о размере: так написана ДЕЛЬТА ЗАМЕРА
            // («`sizeof(Chronicle)` 104 → 72»), упоминание двери
            // (`static_assert(sizeof(MacroStore))@src/bus/store.h`) и всякая
            // проза. Считать их «непросуженными» значило бы завысить границу
            // прибора втрое и спрятать настоящий остаток.
            continue;
        }
        ++i;
        if (i < text.size() && text[i] == '=') ++i;
        while (i < text.size() && space_char(text[i])) ++i;
        const std::size_t numBegin = i;
        long long value = 0;
        while (i < text.size() && (digit_char(text[i]) || text[i] == '\'')) {
            if (text[i] != '\'') value = value * 10 + (text[i] - '0');
            ++i;
        }
        if (i == numBegin) {
            out.push_back(c);                 // правая часть выведена
            continue;
        }
        // Суффикс целого (`746553368ull`) — часть числа, а не хвост.
        while (i < text.size() && (text[i] == 'u' || text[i] == 'U' ||
                                   text[i] == 'l' || text[i] == 'L'))
            ++i;
        std::size_t tail = i;
        while (tail < text.size() && (text[tail] == ' ' || text[tail] == '\t'))
            ++tail;
        const char t = tail < text.size() ? text[tail] : '\n';
        const bool closed = t == ',' || t == ')' || t == ';' || t == '`' ||
                            t == '\n';
        if (!closed) {
            out.push_back(c);                 // `== 96 * 64 + 4` и прочие хвосты
            continue;
        }
        c.literal = true;
        c.value = value;
        out.push_back(c);
    }
    return out;
}

// ── ИСТИНА КОДА ───────────────────────────────────────────────────────────
struct CodeTruth {
    std::map<std::string, long long> byType;   // короткое имя → размер
    std::map<std::string, std::string> where;  // и адрес, где он закреплён
    std::set<std::string> declared;            // объявленные в дереве типы
    std::set<std::string> conflicting;         // два разных числа на один тип
    int files = 0;
    int asserts = 0;
    int formulas = 0;
};

void collect_declarations(const std::string& text, std::set<std::string>& out) {
    static const char* kHeads[] = {"struct ", "class ", "union ",
                                   "enum class ", "using "};
    for (const char* head : kHeads) {
        const std::string h(head);
        for (std::size_t p = text.find(h); p != std::string::npos;
             p = text.find(h, p + 1)) {
            std::size_t i = p + h.size();
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
            const std::size_t begin = i;
            while (i < text.size() && (ident_char(text[i]) && text[i] != ':'))
                ++i;
            if (i > begin) out.insert(text.substr(begin, i - begin));
        }
    }
}

CodeTruth read_code(const std::string& root) {
    static const char* kDirs[] = {"src", "tests"};
    CodeTruth t;
    for (const char* dir : kDirs) {
        std::error_code ec;
        const fsx::path base = fsx::path(root) / dir;
        if (!fsx::is_directory(base, ec)) continue;
        fsx::recursive_directory_iterator it(base, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = it->path().extension().string();
            if (ext != ".h" && ext != ".cpp" && ext != ".hpp") continue;
            std::vector<std::string> lines;
            if (!sm::docrefs::read_lines(it->path().string(), lines)) continue;
            ++t.files;
            std::vector<std::size_t> starts;
            const std::string text = strip_cxx_noise(lines, starts);
            collect_declarations(text, t.declared);
            const std::string rel =
                fsx::relative(it->path(), root, ec).generic_string();
            for (const Claim& c : scan_claims(text, starts, true)) {
                if (!c.literal) {
                    ++t.formulas;
                    continue;
                }
                ++t.asserts;
                const auto seen = t.byType.find(c.type);
                if (seen == t.byType.end()) {
                    t.byType[c.type] = c.value;
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "%s:%d", rel.c_str(),
                                  c.line);
                    t.where[c.type] = buf;
                } else if (seen->second != c.value) {
                    t.conflicting.insert(c.type);
                }
            }
        }
    }
    return t;
}

// ── СУД ───────────────────────────────────────────────────────────────────
struct Tally {
    int ok = 0;          // число совпало с компилятором
    int lie = 0;         // КРАСНОЕ: ассерт есть, число другое
    int noAssert = 0;    // тип объявлен, размер не закреплён
    int deadType = 0;    // имени в дереве нет вовсе
    int external = 0;    // `std::…` — свойство библиотеки
    int formula = 0;     // правая часть выведена
};

void judge_doc(const CodeTruth& code, const std::string& doc,
               const std::string& path, Tally& tally, bool loud) {
    std::vector<std::string> lines;
    if (!sm::docrefs::read_lines(path, lines)) return;
    std::vector<std::size_t> starts;
    const std::string text = strip_struck(lines, starts);
    for (const Claim& c : scan_claims(text, starts, false)) {
        if (!c.literal) {
            ++tally.formula;
            continue;
        }
        const auto it = code.byType.find(c.type);
        if (it != code.byType.end() && code.conflicting.count(c.type) == 0) {
            if (it->second == c.value) {
                ++tally.ok;
                continue;
            }
            ++tally.lie;
            if (loud)
                std::fprintf(stderr,
                             "  ЧИСЛО ВРЁТ %s:%d  sizeof(%s): док %lld, "
                             "компилятор %lld (%s) — расхождение %.1f×\n",
                             doc.c_str(), c.line, c.written.c_str(), c.value,
                             it->second, code.where.at(c.type).c_str(),
                             it->second == 0
                                 ? 0.0
                                 : double(c.value) / double(it->second));
            continue;
        }
        if (c.written.rfind("std::", 0) == 0) {
            ++tally.external;
        } else if (code.declared.count(c.type) != 0) {
            ++tally.noAssert;
        } else {
            ++tally.deadType;
            if (loud)
                std::fprintf(stderr,
                             "  ТИП МЁРТВ %s:%d  sizeof(%s) == %lld — "
                             "объявления в дереве нет (дом — доки, не гейт)\n",
                             doc.c_str(), c.line, c.written.c_str(), c.value);
        }
    }
}

Tally test_real_docs(const CodeTruth& code) {
    // Истина кода обязана быть НЕПУСТОЙ: прибор, у которого карта размеров не
    // собралась, объявил бы врущими ВСЕ претензии разом — и это читалось бы
    // как находка (§8 п.2-3).
    CHECK(code.files > 300, "дерево кода прочитано (файлов > 300)");
    CHECK(code.asserts > 50, "закреплённых литералом размеров больше 50");
    CHECK(code.formulas > 10, "ассерты с выведенной правой частью тоже видны");
    CHECK(code.declared.size() > 500, "объявленные типы дерева собраны");
    for (const std::string& c : code.conflicting)
        std::fprintf(stderr, "  ДВА ЧИСЛА НА ОДИН ТИП  %s\n", c.c_str());
    CHECK(code.conflicting.empty(),
          "один тип не закреплён двумя РАЗНЫМИ числами");
    // Якорь на самую карту: `GameState` есть носитель переписи скаляров мира, и
    // если его размер перестал читаться, карта собралась неверно, а не мир
    // изменился (§8 п.6 — у выведенной величины обязан быть якорь).
    CHECK(code.byType.count("GameState") == 1,
          "размер `GameState` в карте есть — иначе сверка пуста по ошибке");

    Tally all;
    int judged = 0;
    std::error_code ec;
    fsx::directory_iterator it(kRoot, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        // Симлинк пропускается ПО ФОРМЕ, а не по имени: `CLAUDE.md` есть
        // `AGENTS.md`, и названный списком он всплыл бы второй раз при каждом
        // переименовании — а претензии его считались бы ДВАЖДЫ.
        if (it->is_symlink(ec)) continue;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".md") continue;
        const std::string doc = it->path().filename().string();
        if (!sm::docrefs::judges_name_liveness(doc)) continue;
        ++judged;
        judge_doc(code, doc, it->path().string(), all, true);
    }

    CHECK(judged >= 3, "судимых документов корня не меньше трёх");
    CHECK(all.ok > 0,
          "совпавшие претензии есть — иначе прибор прошёл по пустому списку "
          "или читает не те файлы");
    CHECK(all.lie == 0,
          "ни один док не называет размер, с которым не согласен компилятор");
    return all;
}

// ── НЕГАТИВНЫЙ КОНТРОЛЬ ───────────────────────────────────────────────────
// Утверждается СОСТАВ вердиктов, а не их число: «шесть отказов» прошло бы и
// тогда, когда шесть строк упали одним родом, а пять детекторов мертвы.
void test_fixture_detects_every_rod(const CodeTruth& code) {
    Tally fx;
    judge_doc(code, "fixture", std::string(kRoot) + "/" + kFixture, fx, false);
    CHECK(fx.ok == 1, "фикстура: ровно одна ЦЕЛАЯ претензия");
    CHECK(fx.lie == 2,
          "фикстура: ДВЕ врущих — с префиксом `static_assert` и без него, "
          "потому что реальная седьмая ложь была написана без него");
    CHECK(fx.noAssert == 1, "фикстура: ровно один тип БЕЗ закреплённого размера");
    CHECK(fx.deadType == 1, "фикстура: ровно один МЁРТВЫЙ тип");
    CHECK(fx.formula == 1, "фикстура: ровно одна выведенная правая часть");
    // И полярность «молчит»: в фикстуре ДВЕ строки-дельты и ОДНА зачёркнутая
    // претензия, и ни одна не стала вердиктом. Без этого детектор, краснеющий
    // на всякое число рядом со словом sizeof, прошёл бы проверки выше.
    CHECK(fx.ok + fx.lie + fx.noAssert + fx.deadType + fx.formula +
                  fx.external == 6,
          "дельта замера и зачёркнутое претензией НЕ считаются: их в фикстуре "
          "три, вердиктов шесть");
}

// Полярность ФОРМЫ, вынесенная из фикстуры в чистый разбор: именно хвост числа
// отличает НАЗВАННЫЙ размер от формулы, и ошибка тут тиха — прибор прочёл бы
// `== 96 * 64 + 4` как «96» и объявил бы верный код врущим.
void test_form_is_what_decides() {
    const auto parse = [](const char* src, bool requireAssert) {
        std::vector<std::string> lines{src};
        std::vector<std::size_t> starts;
        std::string text;
        for (const std::string& l : lines) {
            starts.push_back(text.size());
            text += l;
            text.push_back('\n');
        }
        return scan_claims(text, starts, requireAssert);
    };
    const auto named = parse("static_assert(sizeof(A) == 96, \"x\");", true);
    CHECK_OR_RETURN(named.size() == 1, "одна претензия разобрана");
    CHECK(named[0].literal && named[0].value == 96,
          "названное число читается числом");

    const auto suffixed = parse("static_assert(sizeof(A) == 746553368ull);", true);
    CHECK_OR_RETURN(suffixed.size() == 1, "число с суффиксом разобрано");
    CHECK(suffixed[0].literal && suffixed[0].value == 746553368LL,
          "суффикс `ull` есть часть числа, а не хвост");

    const auto formula =
        parse("static_assert(sizeof(A) == 96 * 64 + 4, \"x\");", true);
    CHECK_OR_RETURN(formula.size() == 1, "формула тоже видна как претензия");
    CHECK(!formula[0].literal,
          "ФОРМУЛА числом не считается — иначе `96 * 64 + 4` читалось бы как 96");

    const auto qualified = parse("`sizeof(ecs::SquadUpkeep) == 72`", false);
    CHECK_OR_RETURN(qualified.size() == 1, "претензия-пин без префикса видна");
    CHECK(qualified[0].literal && qualified[0].type == "SquadUpkeep",
          "namespace отброшен: иначе `ecs::SquadUpkeep` не нашёл бы кода");

    const auto delta = parse("штабель умер, `sizeof(A)` 836736 -> 836704", false);
    CHECK(delta.empty(),
          "ДЕЛЬТА замера претензией не является: равенства в ней нет");

    const auto cond = parse("if (sizeof(A) == 8) return;", true);
    CHECK(cond.empty(),
          "в КОДЕ претензией считается только ассерт: условие и инициализатор "
          "о законе не высказываются");
}

// Политика рода документа — ЧУЖАЯ ДВЕРЬ, и пин стоит затем, чтобы снос второго
// списка не оказался тихим: переведи кто-нибудь реестр в судимые — и прибор
// покраснеет на сорока исторических записях ВЕРНОЙ прозы.
void test_policy_is_borrowed_not_copied() {
    using sm::docrefs::judges_name_liveness;
    CHECK(judges_name_liveness("SKELETON.md") &&
              judges_name_liveness("CANON.md") &&
              judges_name_liveness("AGENTS.md"),
          "доки-утверждения судятся: их претензия есть слово о СЕГОДНЯ");
    CHECK(!judges_name_liveness("macro-registry.md"),
          "реестр нарядов НЕ судится: его строка — снимок момента правки");
}

}  // namespace

int main() {
    const CodeTruth code = read_code(kRoot);
    const Tally real = test_real_docs(code);
    test_fixture_detects_every_rod(code);
    test_form_is_what_decides();
    test_policy_is_borrowed_not_copied();

    std::printf(
        "sizeof_doc: файлов кода %d · размеров закреплено %zu · формул %d\n"
        "  претензии судимых доков: цела %d · ВРЁТ %d · тип мёртв %d\n"
        "  вне суда: размер не закреплён %d · внешний тип %d · формула %d\n",
        code.files, code.byType.size(), code.formulas, real.ok, real.lie,
        real.deadType, real.noAssert, real.external, real.formula);

    char scope[220];
    std::snprintf(scope, sizeof(scope),
                  "размеров в коде %zu · претензий судимо %d · вне суда %d · "
                  "дельты замера и зачёркнутое НЕ судит",
                  code.byType.size(), real.ok + real.lie,
                  real.deadType + real.noAssert + real.external + real.formula);
    return sm::test::report("sizeof_doc_test", scope);
}
