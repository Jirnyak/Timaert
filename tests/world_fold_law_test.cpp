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
// ЧТО ИМЕННО ОН СУДИТ, И ПОЧЕМУ ИМЕННО ТАК. Перепись M-124 расклассифицировала
// ВСЕ `wrapi` дерева по ВТОРОМУ АРГУМЕНТУ — по тому, какой период ему передают:
//   · `b.cols`/`b.rows`, `c.cols`/`c.rows` — bucket-сетки: 18 вызовов, ЗАКОННО
//     (сторона сетки есть `сторона мира / размер клетки` и степенью двойки быть
//     не обязана);
//   · `py`, `p` — период шума: 6 вызовов, ЗАКОННО, AGENTS называет этот случай
//     единственным законным применением `wrapi` поимённо;
//   · `src/sub/` — 12 вызовов, ВНЕ ЗАКОНА по построению: ЗАКОН АДРЕСА п.5,
//     «у субмира своё адресное пространство… сводить их с макро-адресом
//     ЗАПРЕЩЕНО». Поэтому каталог не сканируется вовсе, а не прощается;
//   · СТОРОНА МИРА — 23 вызова, все снесены M-124. Ноль — это и есть стена.
//
// ЧЕГО ЭТОТ ПРИБОР НЕ УМЕЕТ, СКАЗАНО ВСЛУХ. Отличить «сторона мира» от «период
// сетки» можно ТОЛЬКО по имени аргумента: типа у них одного — `int`. Значит
// прибор слепнет от переименования `mapW`/`mapH`. Это названо, а не спрятано, и
// у слепоты есть срок: владелец 2026-09-28 вынес вердикт «МИР КВАДРАТНЫЙ
// ИЗОТРОПНЫЙ ВСЕГДА» — пара `mapW`/`mapH` подлежит сносу в пользу одной стороны
// (наряд в реестре). Тот наряд обязан обновить список ниже; до него имена живы,
// и прибор на них честен. Настоящая стена — ТИП стороны мира, а не имя, и она
// станет возможна ровно тогда.
//
// Стрижка комментариев ниже может только ОСЛЕПИТЬ прибор (убрать текст), но не
// оболгать: ложного срабатывания из неё не выходит по построению.
#include "check.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Каталоги, где живёт МАКРОМИР и всё, что адресует его клетки. `src/sub/`
// отсутствует намеренно — см. шапку.
constexpr std::string_view kScanDirs[] = {
    "src/macro", "src/app", "src/content", "src/ecs", "src/ui", "src/events",
};

// Имена, которыми в этом дереве зовётся СТОРОНА МИРА. Список ведётся вручную
// потому, что различия в типе нет (см. шапку); он может только расти вместе с
// новыми спеллингами и целиком умереть вместе с `mapH`.
constexpr std::string_view kWorldSideNames[] = {
    "mapW", "mapH", "mapSide", "terrain.width", "terrain.height",
    "td.width", "td.height",
};

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Комментарии вон: `wrapi(` в прозе — не вызов. Строковые литералы не
// разбираются, и это осознанно: пропуск даёт ложное МОЛЧАНИЕ, не ложный крик.
std::string strip_comments(std::string_view src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
        } else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            i = i + 2 < src.size() ? i + 2 : src.size();
        } else {
            out.push_back(src[i]);
            ++i;
        }
    }
    return out;
}

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}

// Второй аргумент каждого вызова `wrapi(...)` в тексте. Скобки считаются, так
// что вложенный вызов и тернарник внутри первого аргумента запятую не крадут;
// перенос строки роли не играет — разбирается весь файл целиком.
std::vector<std::string> wrapi_second_args(const std::string& src) {
    std::vector<std::string> args;
    constexpr std::string_view kCall = "wrapi(";
    for (std::size_t at = src.find(kCall); at != std::string::npos;
         at = src.find(kCall, at + 1)) {
        // `word_wrapi(` вызовом `wrapi` не является; `sm::wrapi(` — является.
        if (at > 0 && ident_char(src[at - 1])) continue;
        std::size_t i = at + kCall.size();
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
        args.emplace_back(src.substr(commaAt + 1, (i - 1) - (commaAt + 1)));
    }
    return args;
}

bool names_world_side(std::string_view arg) {
    for (std::string_view n : kWorldSideNames) {
        if (arg.find(n) != std::string_view::npos) return true;
    }
    return false;
}

void test_no_world_side_wrapi() {
    const fs::path root{TIMAERT_SOURCE_DIR};
    int filesScanned = 0, callsSeen = 0, violations = 0;

    for (std::string_view dir : kScanDirs) {
        const fs::path d = root / fs::path(dir);
        if (!fs::is_directory(d)) continue;
        for (const auto& e : fs::recursive_directory_iterator(d)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".cpp" && ext != ".h") continue;
            ++filesScanned;
            const std::string src = strip_comments(read_file(e.path()));
            for (const std::string& arg : wrapi_second_args(src)) {
                ++callsSeen;
                if (!names_world_side(arg)) continue;
                ++violations;
                std::fprintf(stderr,
                             "  wrapi по СТОРОНЕ МИРА: %s  ->  второй аргумент"
                             " «%s»\n",
                             e.path().string().c_str(), arg.c_str());
            }
        }
    }

    // Цикл, который меряет, обязан утверждать, что ПОМЕРИЛ (§8 п.3): иначе
    // пустой обход отчитается зелёным ровно тогда, когда прибор сломан.
    CHECK(filesScanned > 100, "прибор действительно обошёл дерево");
    CHECK(callsSeen > 0,
          "в сканируемых каталогах ЕСТЬ вызовы wrapi — иначе судить нечего "
          "и зелёный ничего не значит");
    CHECK(violations == 0,
          "клетка мира не сворачивается делением: wrapi по стороне мира = 0 "
          "(AGENTS §3 ЗАКОН АДРЕСА п.4 — свёртка есть МАСКА, cell_of/cell_step)");
}

// НЕГАТИВНЫЙ КОНТРОЛЬ: детектор обязан РЕАЛЬНО ловить нарушение, иначе
// `violations == 0` выше есть вера, а не измерение (§8 п.6). Контроль гоняет
// ту же самую пару функций, что и обход дерева, — второй копии разбора нет.
void test_detector_actually_sees() {
    const std::string guilty =
        "int a = sm::wrapi(x + 1, app.gs.mapW);\n"
        "int b = wrapi(int(std::floor(v)),\n                  mapH);\n";
    const auto guiltyArgs = wrapi_second_args(strip_comments(guilty));
    CHECK(guiltyArgs.size() == 2, "разбор нашёл оба вызова, включая переносный");
    int caught = 0;
    for (const auto& a : guiltyArgs) caught += names_world_side(a) ? 1 : 0;
    CHECK(caught == 2, "детектор ловит свёртку по стороне мира — оба спеллинга");

    // Законные периоды обязаны молчать, иначе прибор запретит то, что AGENTS
    // разрешает поимённо, и следующий агент начнёт подгонять мир под прибор.
    const std::string innocent =
        "int g = wrapi(cx0 + ox, b.cols);\n"
        "int h = wrapi(v, width / period);\n"
        "int k = wrapi(iy, p);\n";
    const auto innocentArgs = wrapi_second_args(strip_comments(innocent));
    CHECK(innocentArgs.size() == 3, "разбор нашёл все три законных вызова");
    int falseAlarms = 0;
    for (const auto& a : innocentArgs) falseAlarms += names_world_side(a) ? 1 : 0;
    CHECK(falseAlarms == 0,
          "bucket-сетка и период шума прибор НЕ трогает (AGENTS §3 п.4 — "
          "единственное законное применение wrapi)");

    // Скобки не должны красть запятую: тернарник и вложенный вызов в ПЕРВОМ
    // аргументе — ровно та форма, что стояла в smoke.cpp:2084 до M-124.
    const auto nested = wrapi_second_args(strip_comments(
        "wrapi(baseY + (probe % 31 == 30 ? 41 : 0), b.rows);"));
    CHECK(nested.size() == 1 && !names_world_side(nested[0]),
          "вложенные скобки не сбивают разбор второго аргумента");

    // Прозе — молчание: закон цитируют в комментариях, и цитата не вызов.
    CHECK(wrapi_second_args(strip_comments(
              "// свернуть через wrapi(x, mapW) запрещено\n")).empty(),
          "wrapi в комментарии вызовом не считается");
}

} // namespace

int main() {
    test_no_world_side_wrapi();
    test_detector_actually_sees();
    return sm::test::report("world_fold_law_test");
}
