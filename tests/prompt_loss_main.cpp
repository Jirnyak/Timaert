// ПРИБОР «ЗНАНИЕ БЕЗ ДОМА»: что в сегодняшнем промте умрёт при перезаписи.
//
// ЗАЧЕМ. Наряд M-264 замерил цену дефекта: `NEXT_SESSION.md` переписывается
// каждую сессию по своему закону, и всё, что записано ТОЛЬКО там, умирает
// молча. История это подтвердила — 323 версии, 14 059 уникальных удалённых
// строк, ≈114 потерь, из них живой случай (перепись шва трёх жопусов) стёрт
// через сутки после написания. Правило-проза «в тот же день уезжает в реестр»
// уже записано, но у проекта есть шрам ровно на прозаические контракты: ЗАКОН
// СЛОВАРЯ куплен тем, что шапка `commodity.h` САМА просила кэшировать ординал,
// и её проигнорировали, «потому что контракт был ПРОЗОЙ».
//
// ПОЧЕМУ ОТЧЁТ, А НЕ ГЕЙТ, И ЭТО ЗАМЕР, А НЕ ВКУС. Гейт «каждое число промта
// обязано иметь адресата» был посчитан на трёх исторических обвалах: он выдал
// бы 235 жалоб (2026-10-15), 194 (2026-09-28) и 55 (2026-09-18) уже ПОСЛЕ
// отбрасывания шума, то есть встал бы стеной в худший момент и стал налогом на
// каждую правку промта — прямо против §8 п.7 («тесты НИКОГДА не должны мешать
// развитию»). Поэтому прибор ПЕЧАТАЕТ ведомость и НЕ несёт вердикта: зовётся
// руками перед перезаписью промта (`--target prompt_loss`), как `docs_sync`.
//
// ЧТО СЧИТАЕТСЯ ДОМОМ. Только ПОСТОЯННЫЕ доки. `NEXT_SESSION.md` и
// `jopus_prompts.md` в корпус НЕ входят ПО ПОСТРОЕНИЮ: это сами
// перезаписываемые файлы, и считать их домом знания значит судить по кругу.
// Этот дефект был пойман вживую на первой редакции детектора — он МОЛЧАЛ о
// заведомо потерянной переписи шва, потому что находил её в том самом промте,
// который стёрли следующим коммитом.
//
// ЧТО СЧИТАЕТСЯ ЗНАНИЕМ. Пара ЧИСЛО+СУБЪЕКТ («44 включения») и адрес
// `symbol@path`. Голое число домом не проверяется: двузначные числа переписей
// совпадают внутри других чисел («44» живёт внутри «4436»), и первая редакция
// детектора именно так молчала о настоящей потере. Поэтому число ищется РЯДОМ
// со своим словом, а цифровой прогон анкерится по краям.
//
// ЧТО ОТБРАСЫВАЕТСЯ КАК ЭФЕМЕРНОЕ. Статус прогона и календарь: `check 129/129`,
// `смоуки 66`, `сейв v139`, даты `20NN-NN-NN`, номера нарядов `M-NNN`. Они
// протухают по построению, их дом — сам прогон, а не док.

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

#ifndef TIMAERT_SOURCE_DIR
#define TIMAERT_SOURCE_DIR "."
#endif

// ПОСТОЯННЫЕ доки. Волатильных здесь нет и быть не может (см. шапку).
const char* kPermanent[] = {
    "AGENTS.md", "CANON.md", "SKELETON.md", "macro-registry.md",
    "bugs.md",   "problems.md", "release.md", "README.md", "lore.md",
};

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) return {};
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

bool is_digit_byte(char c) { return c >= '0' && c <= '9'; }

// Байт — часть слова, если он не пробел, не цифра и не пунктуация разметки.
bool is_word_byte(unsigned char c) {
    if (c >= 0x80) return true;                 // UTF-8: кириллица
    if (std::isalpha(c) || c == '_') return true;
    return false;
}

// Узкие и неразрывные пробелы разрядов сводятся к обычному, иначе «69 624»
// обычным пробелом не ищется — грабля, на которую независимо наступили три
// жопуса переписи.
std::string normalize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        const unsigned char a = static_cast<unsigned char>(in[i]);
        if (a == 0xC2 && i + 1 < in.size()
            && static_cast<unsigned char>(in[i + 1]) == 0xA0) {
            out.push_back(' '); i += 2; continue;        // NBSP
        }
        if (a == 0xE2 && i + 2 < in.size()
            && static_cast<unsigned char>(in[i + 1]) == 0x80
            && (static_cast<unsigned char>(in[i + 2]) == 0xAF
                || static_cast<unsigned char>(in[i + 2]) == 0x89)) {
            out.push_back(' '); i += 3; continue;        // narrow/thin space
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

struct Claim {
    std::string token;    // число или адрес, как в промте
    std::string subject;  // ближайшее слово справа; пусто у адреса
    std::string subject2; // и слева — пара ищется по ЛЮБОМУ из двух
    int         line = 0;
    std::string text;     // строка промта целиком — для печати
};

bool ephemeral_line(const std::string& s) {
    if (s.find("check ") != std::string::npos) return true;
    if (s.find("смоук") != std::string::npos) return true;
    if (s.find("сейв v") != std::string::npos) return true;
    return false;
}

// Число РЯДОМ со своим словом: ищем число в корпусе и смотрим, стоит ли
// префикс субъекта в окне. Окно байтовое, потому что кириллица двухбайтовая.
bool claim_has_home(const Claim& c, const std::string& corpus) {
    const std::size_t kWindow = 90;
    if (c.subject.empty() && c.subject2.empty())
        return corpus.find(c.token) != std::string::npos;
    // Субъект берётся с ДВУХ сторон, и дом есть, если спарился ХОТЬ ОДИН: выбор
    // «только слово справа» давал ложные жалобы, когда справа стояла закрывающая
    // скобка или начало новой фразы («256 ~ Сборка» вместо кванта 256).
    std::string keys[2];
    keys[0] = c.subject.substr(0, c.subject.size() > 10 ? 10 : c.subject.size());
    keys[1] = c.subject2.substr(0, c.subject2.size() > 10 ? 10 : c.subject2.size());
    std::size_t at = 0;
    while ((at = corpus.find(c.token, at)) != std::string::npos) {
        // цифровой анкер: число не внутри другого числа
        const bool leftOk = at == 0 || !is_digit_byte(corpus[at - 1]);
        const std::size_t end = at + c.token.size();
        const bool rightOk = end >= corpus.size() || !is_digit_byte(corpus[end]);
        if (leftOk && rightOk) {
            const std::size_t from = at > kWindow ? at - kWindow : 0;
            const std::size_t to = end + kWindow < corpus.size() ? end + kWindow : corpus.size();
            const std::string win = corpus.substr(from, to - from);
            for (const std::string& key : keys)
                if (!key.empty() && win.find(key) != std::string::npos) return true;
        }
        at = end;
    }
    return false;
}

} // namespace

int main() {
    const std::string root = std::string(TIMAERT_SOURCE_DIR) + "/";

    const std::string prompt = normalize(slurp(root + "NEXT_SESSION.md"));
    if (prompt.empty()) {
        std::printf("prompt_loss: NEXT_SESSION.md не прочитан — нечего судить\n");
        return 0;
    }
    std::string corpus;
    int docs = 0;
    for (const char* name : kPermanent) {
        const std::string body = slurp(root + name);
        if (body.empty()) continue;
        ++docs;
        corpus += normalize(body);
        corpus.push_back('\n');
    }

    // Разбор промта на строки и сбор заявок.
    std::vector<Claim> claims;
    std::istringstream in(prompt);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (ephemeral_line(line)) continue;

        // адреса `symbol@path`
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] != '@') continue;
            std::size_t a = i;
            while (a > 0 && is_word_byte(static_cast<unsigned char>(line[a - 1]))) --a;
            std::size_t b = i + 1;
            while (b < line.size()
                   && (is_word_byte(static_cast<unsigned char>(line[b]))
                       || line[b] == '/' || line[b] == '.' || line[b] == '-'))
                ++b;
            if (b - a < 6 || a == i) continue;
            Claim c;
            c.token = line.substr(a, b - a);
            c.line = lineNo;
            c.text = line;
            claims.push_back(c);
        }

        // числа со своим словом
        for (std::size_t i = 0; i < line.size();) {
            if (!is_digit_byte(line[i])) { ++i; continue; }
            const std::size_t numStart = i;
            std::size_t j = i;
            while (j < line.size()
                   && (is_digit_byte(line[j])
                       || ((line[j] == ' ' || line[j] == '.' || line[j] == ',')
                           && j + 1 < line.size() && is_digit_byte(line[j + 1]))))
                ++j;
            std::string num = line.substr(i, j - i);
            while (!num.empty() && (num.back() == '.' || num.back() == ',')) num.pop_back();
            i = j;

            // ЧИСЛО ПОСЛЕ ДЕФИСА ЗАМЕРОМ НЕ ЯВЛЯЕТСЯ — одно правило снимает три
            // рода шума, и каждый был пойман на себе: номер наряда `M-72`
            // (ординал, его дом сам реестр), куски даты `2026-10-18` → «10»,
            // «18» (календарь), флаг команды `--oneline -10`. Вторая редакция
            // прибора ставила этот страж ПОСЛЕ сдвига курсора за число, то есть
            // смотрела на конец числа вместо начала, и не отсекала ничего:
            // поэтому индекс начала берётся явно (`numStart`).
            if (numStart > 0 && line[numStart - 1] == '-') continue;

            std::size_t digits = 0;
            for (char c : num) if (is_digit_byte(c)) ++digits;
            if (digits < 2) continue;                 // одиночная цифра не замер
            if (digits == 4 && num.find(' ') == std::string::npos
                && num.find('.') == std::string::npos
                && num.rfind("20", 0) == 0) continue; // год — календарь, не замер

            // ближайшее слово справа
            std::size_t k = j;
            while (k < line.size() && !is_word_byte(static_cast<unsigned char>(line[k]))) ++k;
            std::size_t e = k;
            while (e < line.size() && is_word_byte(static_cast<unsigned char>(line[e]))) ++e;
            // и слово СЛЕВА от числа
            std::size_t lb = i;
            while (lb > 0 && !is_word_byte(static_cast<unsigned char>(line[lb - 1]))) --lb;
            std::size_t la = lb;
            while (la > 0 && is_word_byte(static_cast<unsigned char>(line[la - 1]))) --la;

            const bool rightOkWord = (e - k) >= 6;
            const bool leftOkWord = (lb - la) >= 6;
            if (!rightOkWord && !leftOkWord) continue;  // слова рядом нет — не заявка

            Claim c;
            c.token = num;
            if (rightOkWord) c.subject = line.substr(k, e - k);
            if (leftOkWord) c.subject2 = line.substr(la, lb - la);
            c.line = lineNo;
            c.text = line;
            claims.push_back(c);
        }
    }

    int homeless = 0;
    std::printf("ПРИБОР «ЗНАНИЕ БЕЗ ДОМА» (M-264). Отчёт, НЕ вердикт: "
                "волатильные файлы домом не считаются.\n");
    for (const Claim& c : claims) {
        if (claim_has_home(c, corpus)) continue;
        ++homeless;
        if (homeless <= 40) {
            std::printf("  БЕЗ ДОМА  %s%s%s  (NEXT_SESSION:%d)\n",
                        c.token.c_str(), c.subject.empty() ? "" : " ~ ",
                        c.subject.c_str(), c.line);
        }
    }
    std::printf("prompt_loss: постоянных доков %d · заявок в промте %zu · "
                "БЕЗ ДОМА %d\n", docs, claims.size(), homeless);
    if (homeless > 0)
        std::printf("  Это умрёт при перезаписи промта. Перенести в "
                    "macro-registry.md (или SKELETON/CANON) ДО перезаписи.\n");
    return 0;
}
