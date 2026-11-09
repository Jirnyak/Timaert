// ── ПРИБОР НА ПРЕТЕНЗИЮ-ЧИСЛО И НА ДУБЛЬ РЯДА ──────────────────────────────
//
// ЗАЧЕМ ОН, И ЭТО ВЕРДИКТ ВЛАДЕЛЬЦА 2026-11-08. Перепись пачки I скелета
// (M-437) дала вывод, опрокинувший собственную посылку наряда: из ЧЕТЫРНАДЦАТИ
// ложных рядов **СЕМЬ несли ЦЕЛЫЙ `symbol@path`**, два — целый отпечаток, один
// не имел адреса вовсе, и лишь четыре гнили голой формой `путь:N`. Врал в них
// не адрес, а СЧЁТ: читателей, колонок, рядов, вхождений. Ни `docs_sync`, ни
// `doc_refs_test` этого не видят ПО ПОСТРОЕНИЮ — первый правит номер строки,
// второй спрашивает, разрешается ли адрес, и шапка `doc_refs.h` объявляет эту
// границу вслух: «проверяет, что ссылка ЦЕЛА, и НИЧЕГО не говорит о том, правда
// ли утверждение рядом с ней». Значит нужен ВТОРОЙ прибор, а не расширение
// первого (иначе у одного вопроса стало бы два ответа, DOD п.6).
//
// ЧТО ОН СУДИТ, И ТОЛЬКО ЭТО. Как `sizeof_doc_test` взял ОДИН род чисел —
// размер типа, у которого источник есть КОМПИЛЯТОР, — так этот берёт два рода,
// у которых источник ОБЪЕКТИВЕН и разбора смысла не требует:
//
//   1) ЗНАЧЕНИЕ ИМЕНОВАННОЙ КОНСТАНТЫ. Док написал «`kScarRows@src/macro/
//      stacks.h` = 2», а в коде стоит `constexpr std::size_t kScarRows = 1u`.
//      Это СТОЯЧАЯ ПРЕТЕНЗИЯ о настоящем, и у неё есть единственный источник —
//      сама строка объявления. Обобщение `sizeof_doc` с размера на константу.
//   2) ДУБЛЬ РЯДА. Содержательный ряд таблицы, который есть БАЙТОВЫЙ ПРЕФИКС
//      другого ряда того же дока (частный случай — побайтово тот же ряд).
//      Такой ряд рождается редактированием: прежнюю редакцию не снесли, а
//      дописали рядом, и в файле стало ДВА ответа на один вопрос, причём
//      младший — усечённый, то есть он молчит о том, что старший уже знает.
//
// ЧТО ОН НЕ СУДИТ, И ЭТО ЗАМЕРЕНО, А НЕ ВЫБРАНО ПО ВКУСУ. Первой формой
// задумывалось «два ряда цитируют ОДИН `symbol@path` и дают РАЗНЫЕ числа» —
// ровно так выглядели оба сегодняшних противоречия (333 против 347, 487 против
// 486). Перепись отвергла форму: таких пар в судимых доках **634**, и
// подавляющая часть законна (один символ обсуждается в разных разделах про
// разные свои стороны). Прибор, который обвиняет зря, заставляет подгонять доки
// под себя (ЗАКОН НУЛЕВОЙ п.4), поэтому форма отклонена ЧИСЛОМ.
// Так же отклонено наивное «у двух рядов одинаковая первая клетка»: **34**
// попадания, и почти все — ЗАГОЛОВКИ таблиц (`| что |`, `| ось |`).
//
// ЧТО ОН НАШЁЛ НА ПЕРВОМ ПРОГОНЕ, И КАК ЖИВОЕ ДЕРЕВО ПОЙМАЛО ЕГО САМОГО.
// Находок было три, и настоящей оказалась ОДНА:
//   · `SKELETON.md` держал ДВА ПОБАЙТОВО ИДЕНТИЧНЫХ ряда про `StructureIndex`
//     (317 из 319 байт префикса) — НАСТОЯЩИЙ дефект, ряд снесён;
//   · две «лжи» о `kSaveVersion` (док 132, код 155) оказались ДЕЛЬТАМИ ЗАМЕРА
//     («132 → 133»), то есть ВЕРНОЙ прозой о прошлом. Исключение дельты не
//     выдумано здесь заново, а ЗАНЯТО у `sizeof_doc_test`, который этот род
//     исключает по построению с рождения, — иначе у одного правила стало бы
//     два написания (DOD п.6). Урок прибора о себе: детектор, проверенный
//     только на фикстуре, не знает, на чём он шумит; шум назвало ДЕРЕВО.
//
// ПОЛИТИКА РОДА ДОКУМЕНТА НЕ ПИШЕТСЯ ЗАНОВО: берётся ЕДИНСТВЕННАЯ дверь
// `judges_name_liveness@tests/doc_refs.cpp` — та же, что у `doc_refs_test` и
// `sizeof_doc_test`. Второй список судимых доков был бы вторым ответом на один
// вопрос, и разъехались бы они молча.
//
// НОРМАЛИЗАЦИЯ ЧИСЛА — ЧАСТЬ ЗАКОНА, А НЕ УДОБСТВО. Сравнение идёт ЗНАЧЕНИЕМ,
// не текстом: `15'000` против `15000` и `16384.0` против `16384` суть ОДНО
// число, и прибор, краснеющий на разделитель разрядов или на хвост `.0`,
// обвиняет форму записи вместо утверждения. Разведка формы дала ровно эти три
// ложных срабатывания (`kHaulBackG`, `kHeightScaleM`, `kHumanHeightM`), и сняты
// они НОРМАЛИЗАЦИЕЙ, а не белым списком: список пришлось бы пополнять каждой
// новой константой, а нормализация закрывает род.
//
// ── ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ (ПРОГНАНА 2026-11-08) ───────
// Мутируется ФИКСТУРА, а не живые доки: прибор обязан падать на своём
// материале. Проверки фикстуры утверждают СОСТАВ вердиктов, поэтому красит и
// «ложь стала правдой», и «правда стала ложью» — детектор не умеет «просто не
// заметить» починку.
//   КРАСНЫЕ — детектор обязан увидеть, и увидел:
//     · ложь о константе ПОЧИНЕНА (777 → 16384) ...... КРАСНЫЙ, 1 из 11
//       (lie 1 → 0: состав перестал сходиться);
//     · точный дубль разведён правкой В СЕРЕДИНЕ ряда  КРАСНЫЙ, 1 из 11
//       (dup 2 → 1);
//     · УСЕЧЁННЫЙ дубль разведён ..................... КРАСНЫЙ, 1 из 11
//       (dup 2 → 1) — ловится именно та форма, что жила в скелете, а не
//       только точная копия;
//     · снято зачёркивание (`~~…~~`) ................. КРАСНЫЙ, 2 из 11:
//       претензия вышла из-под исключения И стала судимой ложью разом;
//     · у дельты снята стрелка (`32768 → 16384` на `=`) КРАСНЫЙ, 2 из 11,
//       по той же причине.
//   МОЛЧАЩИЕ — детектор обязан пройти мимо, иначе он шум, а не детектор:
//     · ЗАЧЁРКНУТАЯ претензия и ДЕЛЬТА ЗАМЕРА в живых доках . ЗЕЛЁНЫЙ
//       (2 дельты в дереве сегодня — обе про `kSaveVersion`);
//     · претензия с разделителем разрядов (`15'000` против `15000`) ЗЕЛЁНЫЙ;
//     · претензия о float-константе хвостом `.0` (16384 против 16384.0) ЗЕЛЁНЫЙ;
//     · короткий ряд-ЗАГОЛОВОК, повторённый в каждой таблице . ЗЕЛЁНЫЙ
//       (порог первой клетки 24 байта и порог ряда 200).
//   НЕГОДНАЯ МУТАЦИЯ, ЗАПИСАНА ЧТОБЫ ЕЁ НЕ СДЕЛАЛИ ЗАНОВО:
//     · «развести точный дубль, ДОПИСАВ хвост второму ряду» — ЗЕЛЁНЫЙ 11/11,
//       и это НЕ дыра: дописка превращает точный дубль в УСЕЧЁННЫЙ, который
//       детектор ловит тем же правилом. Мутация не судила ничего, потому что
//       не разрывала отношение «префикс»; разрывает его правка В СЕРЕДИНЕ.
#include "check.h"
#include "doc_refs.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fsx = std::filesystem;

namespace {

constexpr const char* kRoot = TIMAERT_SOURCE_DIR;
// Фикстура живёт рядом с прибором: негативный контроль обязан РЕАЛЬНО падать,
// и проверяется он на своём файле, а не правкой живого дока (§8 п.6).
constexpr const char* kFixture = "tests/data/doc_claims_fixture.md";

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ── ЧИСЛО КАК ЗНАЧЕНИЕ, А НЕ КАК ТЕКСТ ───────────────────────────────────
// Разделитель разрядов `'` (C++14) и хвост `u`/`f`/`U`/`F` снимаются, точка
// остаётся: сравнение идёт `double`, поэтому 16384 и 16384.0 равны.
bool parse_number(const std::string& raw, double& out) {
    std::string clean;
    for (const char c : raw) {
        if (c == '\'' || c == ' ' || c == '_') continue;
        if (c == 'u' || c == 'U' || c == 'f' || c == 'F'
            || c == 'l' || c == 'L') break;
        clean.push_back(c);
    }
    if (clean.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(clean.c_str(), &end);
    if (end == clean.c_str()) return false;
    out = v;
    return true;
}

// ── КАРТА ИМЕНОВАННЫХ КОНСТАНТ ДЕРЕВА ────────────────────────────────────
// Берутся только те, у кого ОДНО объявление и ЛИТЕРАЛЬНОЕ значение: константа,
// выведенная формулой, числа не называет, и спорить с ней доку нечем.
struct ConstMap {
    std::map<std::string, double> value;   // имя → значение (единственное)
    std::set<std::string> ambiguous;       // имя объявлено не раз
    std::size_t declared = 0;
};

void harvest_consts(const std::string& text, ConstMap& m) {
    const std::vector<std::string> ls = lines_of(text);
    for (const std::string& l : ls) {
        const std::size_t ce = l.find("constexpr");
        if (ce == std::string::npos) continue;
        // ...и только объявление вида `… kName = <литерал>;`
        const std::size_t eq = l.find('=', ce);
        if (eq == std::string::npos) continue;
        // имя — последний идентификатор перед `=`
        std::size_t e = eq;
        while (e > ce && std::isspace(static_cast<unsigned char>(l[e - 1]))) --e;
        std::size_t b = e;
        while (b > ce
               && (std::isalnum(static_cast<unsigned char>(l[b - 1]))
                   || l[b - 1] == '_')) --b;
        if (b >= e || l[b] != 'k') continue;      // только `kИмя` — дом констант
        const std::string name = l.substr(b, e - b);
        std::size_t v = eq + 1;
        while (v < l.size() && std::isspace(static_cast<unsigned char>(l[v]))) ++v;
        std::size_t ve = v;
        while (ve < l.size()
               && (std::isalnum(static_cast<unsigned char>(l[ve]))
                   || l[ve] == '.' || l[ve] == '\'' || l[ve] == 'x')) ++ve;
        if (ve == v) continue;
        // За литералом обязан идти конец выражения: иначе это ФОРМУЛА
        // (`kA * 24.0f / 2.0f`), и числа она не называет.
        std::size_t t = ve;
        while (t < l.size() && std::isspace(static_cast<unsigned char>(l[t]))) ++t;
        if (t >= l.size() || (l[t] != ';' && l[t] != ',')) continue;
        double val = 0.0;
        if (!parse_number(l.substr(v, ve - v), val)) continue;
        ++m.declared;
        const auto it = m.value.find(name);
        if (it == m.value.end()) m.value.emplace(name, val);
        else if (it->second != val) m.ambiguous.insert(name);
    }
}

void walk_tree(ConstMap& m) {
    std::error_code ec;
    fsx::recursive_directory_iterator it(std::string(kRoot) + "/src", ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = it->path().extension().string();
        if (ext != ".h" && ext != ".cpp") continue;
        harvest_consts(read_file(it->path().string()), m);
    }
}

struct Tally {
    std::size_t judged = 0;    // претензий о константе, взятых под суд
    std::size_t ok = 0;        // согласны с кодом
    std::size_t lie = 0;       // расходятся
    std::size_t skippedStruck = 0;   // зачёркнутые — вне суда
    std::size_t skippedDelta = 0;    // «было → стало»: первое число есть ПРОШЛОЕ
    std::size_t skippedUnknown = 0;  // константы нет в карте / она формула
    std::size_t dup = 0;       // усечённых и точных дублей ряда
};

// Претензия: `` `kName@path` `` и сразу за ней число (возможно в `**…**`).
void judge_claims(const ConstMap& code, const std::string& doc,
                  const std::vector<std::string>& ls, Tally& t, bool loud) {
    for (std::size_t i = 0; i < ls.size(); ++i) {
        const std::string& l = ls[i];
        std::size_t p = 0;
        while ((p = l.find('`', p)) != std::string::npos) {
            const std::size_t close = l.find('`', p + 1);
            if (close == std::string::npos) break;
            const std::string ref = l.substr(p + 1, close - p - 1);
            p = close + 1;
            const std::size_t at = ref.find('@');
            if (at == std::string::npos || at == 0 || ref[0] != 'k') continue;
            const std::string name = ref.substr(0, at);
            // хвост: `= N`, `— N`, `: N`, просто ` N`, с `**` вокруг числа
            std::size_t q = p;
            while (q < l.size()
                   && (l[q] == ' ' || l[q] == '=' || l[q] == ':' || l[q] == '*'
                       || (unsigned char)l[q] >= 0x80)) {
                // многобайтовое тире/= пропускаем как разделитель
                if ((unsigned char)l[q] >= 0x80) { ++q; continue; }
                ++q;
            }
            if (q >= l.size() || !std::isdigit(static_cast<unsigned char>(l[q])))
                continue;
            std::size_t qe = q;
            while (qe < l.size()
                   && (std::isdigit(static_cast<unsigned char>(l[qe]))
                       || l[qe] == '.' || l[qe] == '\'' || l[qe] == ' '))
                ++qe;
            double claim = 0.0;
            if (!parse_number(l.substr(q, qe - q), claim)) continue;
            // ЗАЧЁРКНУТОЕ ВНЕ СУДА: автор объявил строку снятой.
            if (l.find("~~") != std::string::npos) { ++t.skippedStruck; continue; }
            // ДЕЛЬТА ЗАМЕРА ВНЕ СУДА, И ЭТО ИСКЛЮЧЕНИЕ ЗАНЯТО У
            // `sizeof_doc_test`, А НЕ ВЫДУМАНО ВТОРОЕ: в записи «`kSaveVersion`
            // 132 → 155» первое число есть ПРОШЛОЕ, и красить её значит
            // требовать переписать ВЕРНУЮ прозу (§8 п.7). Поймало меня на этом
            // живое дерево: обе «лжи» первого прогона оказались дельтами.
            {
                std::size_t a = qe;
                while (a < l.size() && l[a] == ' ') ++a;
                const bool arrow =
                    (a + 2 < l.size() && (unsigned char)l[a] == 0xE2
                     && (unsigned char)l[a + 1] == 0x86
                     && (unsigned char)l[a + 2] == 0x92)        // U+2192 «→»
                    || (a + 1 < l.size() && l[a] == '-' && l[a + 1] == '>');
                if (arrow || a >= l.size()) { ++t.skippedDelta; continue; }
            }
            const auto it = code.value.find(name);
            if (it == code.value.end() || code.ambiguous.count(name)) {
                ++t.skippedUnknown;
                continue;
            }
            ++t.judged;
            if (it->second == claim) { ++t.ok; continue; }
            ++t.lie;
            if (loud)
                std::fprintf(stderr,
                             "  ВРЁТ О КОНСТАНТЕ  %s:%zu  %s — док %.10g, "
                             "код %.10g\n",
                             doc.c_str(), i + 1, name.c_str(), claim,
                             it->second);
        }
    }
}

// Содержательный ряд: не меньше трёх клеток и первая клетка длиннее порога —
// так заголовки таблиц (`| что | как в коде | статус |`) выпадают сами.
bool content_row(const std::string& l, std::size_t minClaim) {
    if (l.size() < 4 || l[0] != '|' || l[1] != ' ') return false;
    std::size_t bars = 0;
    for (const char c : l) if (c == '|') ++bars;
    if (bars < 3) return false;
    const std::size_t a = l.find('|');
    const std::size_t b = l.find('|', a + 1);
    if (b == std::string::npos) return false;
    return (b - a - 1) > minClaim;
}

void judge_dups(const std::string& doc, const std::vector<std::string>& ls,
                Tally& t, bool loud) {
    constexpr std::size_t kMinClaim = 24;   // порог первой клетки, байт
    constexpr std::size_t kMinRow   = 200;  // ряд короче не судим: шум таблиц
    std::vector<std::pair<std::size_t, std::string>> rows;
    for (std::size_t i = 0; i < ls.size(); ++i)
        if (content_row(ls[i], kMinClaim)) rows.emplace_back(i + 1, ls[i]);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        // «Сердцевина» — ряд без закрывающей ` |`: усечённый дубль кончается
        // ею, а старший на том же байте продолжает фразу.
        std::string core = rows[i].second;
        if (core.size() > 2 && core.compare(core.size() - 2, 2, " |") == 0)
            core.resize(core.size() - 2);
        if (core.size() < kMinRow) continue;
        for (std::size_t j = 0; j < rows.size(); ++j) {
            if (i == j) continue;
            const std::string& other = rows[j].second;
            if (other.size() < core.size()) continue;
            if (other.compare(0, core.size(), core) != 0) continue;
            // при точном равенстве считаем ОДИН раз — младшую строку
            if (other.size() == rows[i].second.size() && j < i) continue;
            ++t.dup;
            if (loud)
                std::fprintf(stderr,
                             "  ДУБЛЬ РЯДА  %s:%zu есть префикс %s:%zu "
                             "(%zu из %zu байт)\n",
                             doc.c_str(), rows[i].first, doc.c_str(),
                             rows[j].first, core.size(), other.size());
            break;
        }
    }
}

} // namespace

int main() {
    ConstMap code;
    walk_tree(code);
    CHECK(code.declared > 300,
          "именованные константы дерева собраны — иначе прибор прошёл по "
          "пустой карте и промолчал бы о любой лжи");
    CHECK(code.value.count("kUnifiedCap") == 1,
          "ЯКОРЬ: кап макромира есть в карте — независимо известный факт "
          "мира, а не пересчёт самой карты");

    Tally all;
    int judgedDocs = 0;
    std::error_code ec;
    fsx::directory_iterator it(kRoot, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        // Симлинк пропускается ПО ФОРМЕ: `CLAUDE.md` есть `AGENTS.md`, и его
        // претензии считались бы ДВАЖДЫ.
        if (it->is_symlink(ec)) continue;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".md") continue;
        const std::string doc = it->path().filename().string();
        if (!sm::docrefs::judges_name_liveness(doc)) continue;
        ++judgedDocs;
        const std::vector<std::string> ls =
            lines_of(read_file(it->path().string()));
        judge_claims(code, doc, ls, all, true);
        judge_dups(doc, ls, all, true);
    }
    CHECK(judgedDocs >= 3, "судимых документов корня не меньше трёх");
    CHECK(all.judged > 0,
          "претензии о константах под судом ЕСТЬ — иначе прибор прошёл мимо "
          "всех и его зелень ничего не значит (§8 п.3)");

    // ── НЕГАТИВНЫЙ КОНТРОЛЬ НА СВОЁМ ФАЙЛЕ, И ОН УТВЕРЖДАЕТСЯ ────────────
    Tally fx;
    const std::vector<std::string> fls =
        lines_of(read_file(std::string(kRoot) + "/" + kFixture));
    CHECK(!fls.empty(), "фикстура читается");
    judge_claims(code, "fixture", fls, fx, false);
    judge_dups("fixture", fls, fx, false);
    CHECK(fx.lie == 1 && fx.ok == 1,
          "детектор константы видит ложь и НЕ видит правду: в фикстуре по "
          "одной штуке каждого");
    CHECK(fx.dup == 2,
          "детектор дубля видит ОБЕ формы — точную копию и УСЕЧЁННУЮ; "
          "усечённая и жила в скелете");
    CHECK(fx.skippedStruck == 1,
          "зачёркнутая претензия вне суда: автор уже объявил строку снятой");
    CHECK(fx.skippedDelta == 1,
          "дельта замера вне суда: «было → стало» есть верная проза о прошлом");

    std::fprintf(stderr,
                 "doc_claims: констант в коде %zu · претензий судимо %zu "
                 "(совпали %zu) · вне суда %zu (зачёркнуто %zu, дельт "
                 "замера %zu) · дублей %zu\n",
                 code.declared, all.judged, all.ok, all.skippedUnknown,
                 all.skippedStruck, all.skippedDelta, all.dup);

    CHECK(all.lie == 0,
          "ни один судимый док не называет значение именованной константы, с "
          "которым не согласен её единственный источник");
    CHECK(all.dup == 0,
          "ни один судимый док не держит ряд, который есть префикс другого: "
          "прежнюю редакцию СНОСЯТ, а не дописывают рядом (§5 п.14)");
    return sm::test::report("doc_claims_test");
}
