#include "tu_time_jump.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

namespace sm::build {
namespace {

// Формат журнала ninja (v5-v7): start_ms \t end_ms \t mtime \t output \t hash.
// Строки с '#' — шапка версии. Ninja ДОПИСЫВАЕТ, значит порядок строк в файле
// хронологический: последняя запись выхода — свежая, всё до неё — прошлое.
constexpr int kNinjaFields = 5;

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() &&
           s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// Судим ТОЛЬКО объектники: линковка (LTO), `ctest` и штампы живут по своим
// законам и плывут на порядок сильнее — их шум глушил бы прибор.
bool is_translation_unit(std::string_view out) {
    return ends_with(out, ".o") || ends_with(out, ".obj");
}

bool parse_int(std::string_view field, long long& out) {
    const char* first = field.data();
    const char* last = field.data() + field.size();
    const auto res = std::from_chars(first, last, out);
    return res.ec == std::errc() && res.ptr == last;
}

// Построчный проход текста; последняя строка без '\n' тоже строка.
template <typename Fn>
void for_each_line(const std::string& text, Fn&& fn) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        if (eol > pos) fn(std::string_view(text.data() + pos, eol - pos));
        pos = eol + 1;
    }
}

} // namespace

TuTimeReport scan_ninja_log(const std::string& logText,
                            const std::vector<TuBest>& baseline) {
    std::unordered_map<std::string, double> known;
    known.reserve(baseline.size() * 2);
    for (const TuBest& b : baseline) known.emplace(b.output, b.bestSec);

    // Истории по выходу; порядок первого появления сохраняем, чтобы отчёт и
    // ведомость были воспроизводимы (хеш-таблица порядка не держит).
    std::unordered_map<std::string, std::vector<double>> history;
    std::vector<std::string> order;

    for_each_line(logText, [&](std::string_view line) {
        if (line.front() == '#') return;

        std::string_view fields[kNinjaFields];
        int count = 0;
        std::size_t at = 0;
        while (count < kNinjaFields) {
            const std::size_t tab = line.find('\t', at);
            if (tab == std::string_view::npos) {
                fields[count++] = line.substr(at);
                break;
            }
            fields[count++] = line.substr(at, tab - at);
            at = tab + 1;
        }
        if (count != kNinjaFields) return;

        long long startMs = 0;
        long long endMs = 0;
        if (!parse_int(fields[0], startMs) || !parse_int(fields[1], endMs)) return;
        if (!is_translation_unit(fields[3])) return;

        const double sec = double(endMs - startMs) / 1000.0;
        std::string key(fields[3]);
        auto it = history.find(key);
        if (it == history.end()) {
            order.push_back(key);
            history.emplace(std::move(key), std::vector<double>{sec});
        } else {
            it->second.push_back(sec);
        }
    });

    TuTimeReport r;
    r.tuCount = int(order.size());
    if (order.empty()) return r;

    std::vector<double> latest;
    latest.reserve(order.size());
    for (const std::string& name : order) {
        const double now = history[name].back();
        latest.push_back(now);
        if (now > r.slowestSec) {
            r.slowestSec = now;
            r.slowest = name;
        }
    }
    std::vector<double> sorted = latest;
    std::sort(sorted.begin(), sorted.end());
    r.medianSec = sorted[sorted.size() / 2];

    r.best.reserve(order.size());
    for (const std::string& name : order) {
        const std::vector<double>& runs = history[name];
        const double now = runs.back();

        // База — ЛУЧШЕЕ известное прошлое: уцелевшая история журнала И своя
        // ведомость. Лучшее, а не последнее: регресс, попавший в базу, иначе
        // объявил бы себя нормой на второй же сборке.
        bool hasPast = false;
        double past = 0.0;
        for (std::size_t i = 0; i + 1 < runs.size(); ++i) {
            past = hasPast ? std::min(past, runs[i]) : runs[i];
            hasPast = true;
        }
        const auto seen = known.find(name);
        if (seen != known.end()) {
            past = hasPast ? std::min(past, seen->second) : seen->second;
            hasPast = true;
        }

        if (!hasPast) {
            ++r.fresh;
        } else {
            ++r.comparable;
            const double base = std::max(past, kLogResolutionSec);
            const double ratio = now / base;
            // Пол шума — медиана прогона, а не выдуманная секунда: TU, который
            // дешевле половины дерева, физически не может держать время
            // сборки, сколько бы раз он ни удвоился.
            if (ratio >= kJumpRatio && now > r.medianSec)
                r.jumps.push_back(TuJump{name, past, now, ratio});
        }

        r.best.push_back(TuBest{name, hasPast ? std::min(past, now) : now});
    }

    std::sort(r.jumps.begin(), r.jumps.end(),
              [](const TuJump& a, const TuJump& b) { return a.ratio > b.ratio; });
    return r;
}

bool read_text_file(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    std::size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

std::vector<TuBest> load_baseline(const char* path) {
    std::vector<TuBest> out;
    std::string text;
    if (!read_text_file(path, text)) return out;
    for_each_line(text, [&](std::string_view line) {
        const std::size_t tab = line.rfind('\t');
        if (tab == std::string_view::npos) return;
        const std::string secText(line.substr(tab + 1));
        const double sec = std::atof(secText.c_str());
        if (sec < 0.0) return;
        out.push_back(TuBest{std::string(line.substr(0, tab)), sec});
    });
    return out;
}

bool save_baseline(const char* path, const std::vector<TuBest>& best) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    for (const TuBest& b : best)
        std::fprintf(f, "%s\t%.3f\n", b.output.c_str(), b.bestSec);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

} // namespace sm::build
