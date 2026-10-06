// ПЕРЕПИСЬ НОСИТЕЛЕЙ СОСТОЯНИЯ: КАЖДЫЙ ДОЛГОЖИВУЩИЙ БАЙТ МИРА НАЗВАН СТРОКОЙ
// С РОДОМ ФРЕЙМА (M-189; вердикт владельца 2026-09-29, дословно: «пакеты —
// тоже надо зафиксировать что это все данные нашей игры как системы и ничего
// кроме них не может постоянно храниться сразу не словами а
// компиляторно/памяти фиксировалось нарушение»).
//
// ЗАКОН: шесть родов канонического фрейма исчерпывающи — седьмого рода данных
// в игре нет (AGENTS, ЗАКОН КАНОНИЧЕСКОГО ФРЕЙМА п.1). Всё, что живёт дольше
// кадра, обязано быть либо одним из шести родов, либо ПРОИЗВОДНЫМ от них
// (кэш), либо сессией/представлением, либо внутренностью модуля субмира
// (ФРЕЙМ п.5). Носитель вне этих классов — поимённое РАСХОЖДЕНИЕ с нарядом.
//
// ЧТО СУДИТ ПРИБОР. Полную компиляторную стену «ничего вне шести родов» может
// поставить только сам фрейм (фаза 4: M-171/M-172 — разные пути включения).
// До неё — эта перепись: поля четырёх больших носителей (GameState,
// PlayerState, MacroStore, App) сверяются с поимённой таблицей
// {поле → род/класс}. Новое поле без строки = красный тест в тот же день;
// строка без поля = тоже красный (список честный в обе стороны — мёртвая
// строка переписи есть ложь дока, §5 п.1).
//
// ЧЕГО ПРИБОР НЕ СУДИТ, СКАЗАНО ВСЛУХ:
//   · ФОРМУ поля (вектор против плоского массива) судят static_assert(sizeof)
//     и перепись штабелей (core/stacks.h) — второй ответ не заводится;
//   · вложенные структуры БЕЗ фанатизма (вердикт владельца 2026-09-30: «у нас
//     DOD инкапсуляция и чистые БОЛЬШИЕ системы»): строку рода несёт ЧЛЕН-
//     владелец, а не каждый его байт. Landmark переписи полей не получает —
//     он уничтожается целиком (владелец: «ландмарк же будет уничтожена
//     полностью — у нас же сквады единые теперь»), карту колонок снимет M-90;
//   · SubworldEngine — одна строка МОДУЛЬ: нарезку его памяти делает M-150.
//
// МЕХАНИЗМ — текстовая перепись R1-образца (arch_guard_test): парсер полей
// структуры по глубине скобок, для колонок MacroStore — строки X-macro
// SM_MACRO_STORE_COLUMNS. Парсер проверен на фикстуре с негативным контролем
// (§8 п.6): поле, отсутствующее в таблице, ОБЯЗАНО быть замечено.
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

// ── СЛОВАРЬ РОДОВ И КЛАССОВ ───────────────────────────────────────────────
// Первые шесть — рода канонического фрейма (ЗАКОН КАНОНИЧЕСКОГО ФРЕЙМА п.1).
// Остальные — классы, которые фреймом не являются и являться не обязаны:
// производное пересобирается из родов, сессия умирает с моментом, модуль —
// вещь в себе за пакетом. РАСХОЖДЕНИЕ — единственный класс, требующий наряда.
enum class Rod : std::uint8_t {
    Cells,       // род 1: клетки связного тора
    Squads,      // род 2: сквады-анкеты (место — неподвижный сквад)
    Fields,      // род 3: поля — плоский массив над тором
    WorldVars,   // род 4: мировые переменные (актор — сам мир)
    Facts,       // род 5: события и факты (актор — клетка или сквад)
    Factions,    // род 6: фракции
    Cache,       // производное от родов; пересобирается, в сейв не едет
    Session,     // представление/прибор/оболочка — не данные мира
    Module,      // внутренность модуля субмира (ФРЕЙМ п.5)
    Nested,      // вложенный носитель — переписан своей таблицей
    Divergence,  // ВНЕ родов: поимённое расхождение, note обязан звать наряд
};

constexpr const char* rod_name(Rod r) {
    switch (r) {
        case Rod::Cells:      return "род 1 клетки";
        case Rod::Squads:     return "род 2 сквады";
        case Rod::Fields:     return "род 3 поля";
        case Rod::WorldVars:  return "род 4 мировые переменные";
        case Rod::Facts:      return "род 5 события-факты";
        case Rod::Factions:   return "род 6 фракции";
        case Rod::Cache:      return "КЭШ";
        case Rod::Session:    return "СЕССИЯ";
        case Rod::Module:     return "МОДУЛЬ";
        case Rod::Nested:     return "НОСИТЕЛЬ";
        case Rod::Divergence: return "РАСХОЖДЕНИЕ";
    }
    return "?";
}

struct Row {
    std::string_view field;
    Rod rod;
    std::string_view note;   // у Divergence ОБЯЗАН звать наряд
};

// ── ПАРСЕР ПОЛЕЙ ──────────────────────────────────────────────────────────
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

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// Вызов МАКРОСА КАПСОМ в голове statement (расклад X-macro в теле структуры,
// без ';' — он склеивается со следующим полем): полем не является, срезается;
// колонки самого X-macro считает parse_xmacro.
std::string strip_macro_calls(std::string t) {
    for (;;) {
        t = trim(t);
        std::size_t i = 0;
        bool caps = !t.empty() && (t[0] >= 'A' && t[0] <= 'Z');
        while (i < t.size() && ident_char(t[i])) {
            if (t[i] >= 'a' && t[i] <= 'z') caps = false;
            ++i;
        }
        if (!caps || i == 0 || i >= t.size() || t[i] != '(') return t;
        int nest = 0;
        std::size_t k = i;
        for (; k < t.size(); ++k) {
            if (t[k] == '(') ++nest;
            else if (t[k] == ')') {
                --nest;
                if (nest == 0) { ++k; break; }
            }
        }
        if (nest != 0) return t;
        t = t.substr(k);
    }
}

// Имена деклараторов одного statement: срез инициализаторов по первому '='
// верхнего уровня, разрез по запятым верхнего уровня (<>()[] — вложенность),
// у каждого фрагмента имя — последний идентификатор до конца или до '['.
void declarator_names(const std::string& stmt, std::vector<std::string>& out) {
    // Разрез по верхнеуровневым запятым; '=' гасит хвост своего фрагмента.
    std::vector<std::string> frags;
    std::string cur;
    int nest = 0;
    bool inInit = false;
    for (char c : stmt) {
        if (c == '<' || c == '(' || c == '[') ++nest;
        else if (c == '>' || c == ')' || c == ']') --nest;
        else if (c == '=' && nest == 0) { inInit = true; continue; }
        else if (c == ',' && nest == 0) {
            frags.push_back(cur);
            cur.clear();
            inInit = false;
            continue;
        }
        if (!inInit) cur.push_back(c);
    }
    frags.push_back(cur);
    for (std::string& f : frags) {
        // Массивный хвост отрезается: имя стоит ДО '[' верхнего уровня.
        std::string head;
        int n2 = 0;
        for (char c : f) {
            if (c == '[' && n2 == 0) break;
            if (c == '<' || c == '(') ++n2;
            else if (c == '>' || c == ')') --n2;
            head.push_back(c);
        }
        // Последний идентификатор.
        std::size_t e = head.size();
        while (e > 0 && !ident_char(head[e - 1])) --e;
        std::size_t b = e;
        while (b > 0 && ident_char(head[b - 1])) --b;
        if (e > b) out.push_back(head.substr(b, e - b));
    }
}

// Statement — поле, если первая '(' не раньше первых '=', '[', '{' (иначе это
// декларация/тело функции) и он не начинается служебным словом.
bool statement_is_field(const std::string& t) {
    if (t.empty()) return false;
    for (std::string_view kw :
         {"using ", "typedef ", "static ", "friend ", "enum ", "struct ",
          "class ", "public:", "private:", "protected:"})
        if (t.rfind(kw, 0) == 0) return false;
    const std::size_t paren = t.find('(');
    if (paren == std::string::npos) return true;
    const std::size_t eq = t.find('=');
    const std::size_t br = t.find('[');
    if (eq != std::string::npos && eq < paren) return true;
    if (br != std::string::npos && br < paren) return true;
    return false;
}

// Поля структуры `structName` из текста `src`. Глубина скобок: поле живёт на
// глубине 1; тела функций и вложенные структуры — глубже и пропускаются;
// `} name;` вложенной структуры отдаёт имя. Комментарии и препроцессор
// вырезаны построчно.
std::vector<std::string> parse_fields(const std::string& src,
                                      std::string_view structName) {
    std::vector<std::string> out;
    const std::string opener = "struct " + std::string(structName) + " {";
    std::size_t at = src.find("\n" + opener);
    if (at == std::string::npos) {
        if (src.rfind(opener, 0) == 0) at = 0;
        else return out;
    } else {
        at += 1;
    }
    std::size_t i = at + opener.size();
    int depth = 1;
    std::string buf;                 // текущий statement на глубине 1
    bool lineComment = false, preproc = false, lineStart = true;
    auto flush_semicolon = [&]() {
        const std::string t = strip_macro_calls(trim(buf));
        buf.clear();
        if (t.empty()) return;
        if (t[0] == '}') {           // `} name;` вложенной структуры
            std::vector<std::string> names;
            declarator_names(t.substr(1), names);
            for (auto& n : names) out.push_back(n);
            return;
        }
        if (!statement_is_field(t)) return;
        std::vector<std::string> names;
        declarator_names(t, names);
        for (auto& n : names) out.push_back(n);
    };
    for (; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '\n') {
            lineComment = false;
            preproc = false;
            lineStart = true;
            if (depth == 1 && !buf.empty() && buf.back() != ' ')
                buf.push_back(' ');
            continue;
        }
        if (lineComment || preproc) continue;
        if (lineStart && (c == ' ' || c == '\t')) continue;
        if (lineStart && c == '#') { preproc = true; continue; }
        lineStart = false;
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            lineComment = true;
            continue;
        }
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}') {
            --depth;
            if (depth == 0) { flush_semicolon(); break; }
            if (depth == 1) {
                // Вернулись из вложенного блока: тело функции — мусор,
                // вложенная структура ждёт декларатора, брейс-иниц. — хвост
                // поля. Сигнатура с '(' выдаёт функцию.
                const std::string t = strip_macro_calls(trim(buf));
                if (t.find('(') != std::string::npos
                    && !statement_is_field(t)) {
                    buf.clear();
                } else if (t.rfind("struct ", 0) == 0
                           || t.rfind("class ", 0) == 0) {
                    buf = "}";
                }
                // иначе — брейс-инициализатор поля: buf стоит как есть.
            }
            continue;
        }
        if (depth != 1) continue;
        if (c == ';') { flush_semicolon(); continue; }
        buf.push_back(c);
    }
    return out;
}

// Колонки X-macro: строки `X(name, Type)` внутри блока
// `#define <macroName>(X)` (пока строки продолжаются `\`).
std::vector<std::string> parse_xmacro(const std::string& src,
                                      std::string_view macroName) {
    std::vector<std::string> out;
    const std::string opener = "#define " + std::string(macroName) + "(X)";
    std::size_t at = src.find(opener);
    if (at == std::string::npos) return out;
    std::size_t lineEnd = src.find('\n', at);
    while (lineEnd != std::string::npos) {
        // Продолжается ли блок: предыдущая строка кончается '\'.
        std::size_t e = lineEnd;
        while (e > at && (src[e - 1] == ' ' || src[e - 1] == '\r')) --e;
        if (e == at || src[e - 1] != '\\') break;
        const std::size_t next = src.find('\n', lineEnd + 1);
        std::string line = src.substr(lineEnd + 1,
                                      (next == std::string::npos
                                           ? src.size()
                                           : next) - lineEnd - 1);
        const std::size_t x = line.find("X(");
        if (x != std::string::npos) {
            std::size_t b = x + 2, i2 = b;
            while (i2 < line.size() && ident_char(line[i2])) ++i2;
            if (i2 > b) out.push_back(line.substr(b, i2 - b));
        }
        at = lineEnd + 1;
        lineEnd = next;
    }
    return out;
}

// ── СВЕРКА ПОЛЕЙ С ТАБЛИЦЕЙ ───────────────────────────────────────────────
struct Tally {
    int fields = 0;
    int missingRod = 0;    // поле есть, строки с родом нет
    int deadRow = 0;       // строка есть, поля нет
    int byRod[16] = {};
};

Tally compare(const char* carrier, const std::vector<std::string>& parsed,
              const Row* rows, std::size_t rowCount, bool loud = true) {
    Tally t;
    std::set<std::string> seen;
    for (const std::string& f : parsed) {
        ++t.fields;
        if (!seen.insert(f).second && loud)
            std::fprintf(stderr, "  ДУБЛЬ ИМЕНИ ПОЛЯ: %s::%s\n", carrier,
                         f.c_str());
        bool found = false;
        for (std::size_t r = 0; r < rowCount; ++r) {
            if (rows[r].field == f) {
                ++t.byRod[int(rows[r].rod)];
                found = true;
                break;
            }
        }
        if (!found) {
            ++t.missingRod;
            if (loud)
                std::fprintf(stderr,
                             "  ПОЛЕ БЕЗ РОДА: %s::%s — впиши строку с родом "
                             "фрейма в state_census_test (шесть родов "
                             "исчерпывающи, ФРЕЙМ п.1)\n",
                             carrier, f.c_str());
        }
    }
    for (std::size_t r = 0; r < rowCount; ++r) {
        bool found = false;
        for (const std::string& f : parsed)
            if (rows[r].field == f) { found = true; break; }
        if (!found) {
            ++t.deadRow;
            if (loud)
                std::fprintf(stderr,
                             "  СТРОКА БЕЗ ПОЛЯ: %s::%.*s — поле умерло, "
                             "строка переписи лжёт (§5 п.1)\n",
                             carrier, int(rows[r].field.size()),
                             rows[r].field.data());
        }
    }
    return t;
}

// ── ПЕРЕПИСЬ: ТАБЛИЦЫ РОДОВ ───────────────────────────────────────────────
// Снята прибором 2026-09-30 (числа из прогона, не из глаз). Правило чтения:
// строка отвечает «КАКОМУ роду фрейма принадлежит поле» — о ФОРМЕ поля она
// не судит; расхождение формы зовёт наряд в note.

// GameState@src/macro/state.h — сам агрегат мира: пять родов + оболочка.
constexpr Row kGameStateRows[] = {
    {"version", Rod::Session,
     "метаданные провода сейва, как saveName/savedAt (вердикт 2026-09-30); "
     "свой формат сейва запрещён (§10) — умирает с фреймом M-171"},
    {"saveName", Rod::Session, "метаданные слота для UI; std::string — M-180"},
    {"savedAt", Rod::Session, "метаданные слота для UI; std::string — M-180"},
    {"worldSeed", Rod::WorldVars, "сид мира"},
    {"mapW", Rod::WorldVars, "форма мира"},
    {"mapH", Rod::WorldVars, "форма мира"},
    {"mapParams", Rod::WorldVars, "параметры генерации (префикс сейва)"},
    {"cityCountTarget", Rod::WorldVars, "параметр генерации"},
    {"markers", Rod::Session,
     "UX-рендер карты (вердикт 2026-09-30); замысел — верхняя система "
     "ФАКТ→ПРЕДИКАТ→ЦЕЛЬ, не достроена; сегодня едет в сейве; строки — "
     "M-180"},
    {"knowledge", Rod::Fields, "байт знания игрока на клетку"},
    {"chronicle", Rod::Facts, "летопись: кольцо + анналы"},
    {"scent", Rod::Fields, "поля следов фракций"},
    {"sessionFeed", Rod::Session, "HUD-лента, умирает с моментом"},
    {"factions", Rod::Factions,
     "строки фракций (macro/factions.h, v121): id, цвет, отношения колонкой "
     "строки, феодальные рёбра дани отрезками общего пула"},
    {"cellOwner", Rod::Fields,
     "поле владения землёй: байт строки фракции на клетку тора, 0xff = дикие "
     "земли. Обёртка `Politik` снесена (M-90): список городов оказался планом "
     "ГЕНЕРАТОРА и уехал локальным буфером генезиса, копия размера карты умерла "
     "как второй ответ на сторону мира"},
    {"player", Rod::Nested, "переписан своей таблицей ниже"},
    {"playerSquadBits", Rod::Cache, "кэш записи spawnId == kPlayerSquadOrdinal"},
    {"playerFlagBits", Rod::Cache, "кэш носителя колонки playerFlag"},
    {"worldTime", Rod::WorldVars, "время — частный случай мировой переменной"},
    {"lastWorldRebakeDay", Rod::WorldVars, "фаза перепёка/автосейва"},
    {"nextMacroSpawnOrdinal", Rod::WorldVars,
     "ЕДИНЫЙ эмитент ординалов субъектов — сквады и места (M-37); выдача с "
     "1, 0 = «никто» (закон нуля-ординала)"},
    {"navEpoch", Rod::Cache, "счётчик события состава; в сейв не едет"},
    {"nextQuestOrdinal", Rod::WorldVars, "эмитент ординалов квестов"},
    {"worldTickRt", Rod::WorldVars, "очередь дневного тика + RNG"},
    {"macroAiRhythm", Rod::WorldVars,
     "ритм макро-ИИ + RNG; сейв-образ живой половины App::npcAi"},
    {"logicNodesRegistered", Rod::Facts,
     "прогресс сюжета; форма vector<string> — M-180"},
    {"logicNodesActive", Rod::Facts,
     "прогресс сюжета; форма vector<string> — M-180"},
    {"subState", Rod::Session,
     "сцена оболочки (Trading/ViewingMap/PreBattle + settlementId); сегодня "
     "едет в сейве; чистая судьба не решена — вопрос у владельца (2026-09-30)"},
    {"deserterPool", Rod::WorldVars,
     "пул мира-актора; потребитель — РЕЖИССЁР ЛУТА (над-система: бандиты/"
     "монстры/сложность/лут — вердикт 2026-09-30, не достроена)"},
    {"lootPoolValue", Rod::WorldVars, "казна пула — актор сам мир"},
    {"resourceScarCells", Rod::Fields, "шрамы природных строк по реестру"},
    {"worked", Rod::Fields, "слой разработки: число под фичей клетки"},
    {"builtFeatures", Rod::Facts,
     "акты работы сквадов над слоем фич; часть замысла «у каждого типа "
     "сквада своё поведение» (АИ по типу — контент, связь таблиц сквадов и "
     "полей; система не достроена — вердикт 2026-09-30)"},
};

// PlayerState@src/macro/state.h — остаток блока игрока: знание и оболочка
// анкеты (сама анкета давно на его скваде, v85-v91).
constexpr Row kPlayerStateRows[] = {
    {"name", Rod::Squads,
     "имя анкеты; форма std::string умирает в char name[32] колонкой анкеты "
     "КАЖДОГО сквада (вердикт 3, M-90)"},
    {"sexIdx", Rod::Squads, "выбор создания — анкета"},
    {"ageDays", Rod::Squads, "анкета"},
    {"codexUnlockedBits", Rod::Squads, "знание игрока — колонка его анкеты"},
    {"journal", Rod::Facts, "знание игрока о фактах — копии записей летописи"},
    {"journalSeenSeq", Rod::Facts, "курсор той же системы"},
    {"journalFull", Rod::Facts, "громкий кап той же системы"},
    {"factionPeaceUntilDay", Rod::Factions, "перемирия по слотам фракций"},
    {"settledQuestOffers", Rod::Facts, "дедуп предложений дня"},
    {"completedQuestCount", Rod::Facts, "счётчик жизни"},
    {"failedQuestCount", Rod::Facts, "счётчик жизни"},
};

// MacroStore@src/macro/store.h — гладкая память рода 2 целиком: колонки
// X-macro + обвязка слотов той же системы.
constexpr Row kMacroStoreRows[] = {
    {"spawnId", Rod::Squads, ""},
    {"cell", Rod::Squads, "адрес — одно число (ЗАКОН АДРЕСА)"},
    {"kind", Rod::Squads, ""},
    {"visual", Rod::Squads, ""},
    {"character", Rod::Squads, ""},
    {"name", Rod::Squads,
     "имя анкеты char[32], мутируемо (вердикт 3, 2026-09-30); Landmark::name "
     "умирает в неё при M-90; nameIdx — дефолт генерации"},
    {"level", Rod::Squads, ""},
    {"traits", Rod::Squads, ""},
    {"pools", Rod::Squads, "HP/MP/SP — ресурсы обоих миров"},
    {"runtime", Rod::Squads, ""},
    {"spellBook", Rod::Squads, ""},
    {"memory", Rod::Squads, ""},
    {"upkeep", Rod::Squads, ""},
    {"wellbeing", Rod::Squads, "благополучие анкеты (M-90 флип: было плечом места)"},
    {"interests", Rod::Squads, "связи любых сквадов (M-90 флип: было плечом места)"},
    {"inventory", Rod::Squads, "единый контейнер: предметы + существа"},
    {"sheet", Rod::Squads, ""},
    {"orders", Rod::Squads, ""},
    {"gear", Rod::Squads, ""},
    {"designTag", Rod::Squads, ""},
    {"playerFlag", Rod::Squads, "истина «кто игрок» (5б)"},
    {"dead", Rod::Squads, ""},
    {"generation", Rod::Squads, "обвязка слотов той же системы"},
    {"alive", Rod::Squads, "обвязка слотов"},
    {"freeSlots", Rod::Squads, "обвязка слотов"},
    {"freeCount", Rod::Squads, "обвязка слотов"},
    {"aliveCount", Rod::Squads, "обвязка слотов"},
};

// App@src/app/app_state.h — агрегат программы: оболочка, представление и
// приборы вокруг мира; слои мира ЖИВУТ здесь его членами (род 3), их долг
// снимает фрейм (M-171), переездом их не трогать (NEXT_SESSION «чего не
// делать»).
constexpr Row kAppRows[] = {
    {"window", Rod::Session, ""},
    {"device", Rod::Session, ""},
    {"renderer", Rod::Session, ""},
    {"gpuTimer", Rod::Session, ""},
    {"showFpsHud", Rod::Session, ""},
    {"imguiPool", Rod::Session, ""},
    {"width", Rod::Session, ""},
    {"height", Rod::Session, ""},
    {"running", Rod::Session, ""},
    {"savePath", Rod::Session, ""},
    {"autosavePath", Rod::Session, ""},
    {"prefsPath", Rod::Session, ""},
    {"keymapPath", Rod::Session, ""},
    {"state", Rod::Session, "экран оболочки"},
    {"loadReturnState", Rod::Session, ""},
    {"worldLoaded", Rod::Session, ""},
    {"subworldWasActive", Rod::Session, "ребро перехода макро↔микро"},
    {"zoneFieldDirty", Rod::Session, "GPU-флаш"},
    {"gs", Rod::Nested, "агрегат мира — переписан своей таблицей"},
    {"terrain", Rod::Fields, "слой клеток: высота/биом"},
    {"features", Rod::Fields, "слой фич"},
    {"zones", Rod::Fields, "слой опасности"},
    {"treeLayer", Rod::Fields, "живое поле леса (едет в сейве целиком)"},
    {"uploadedTreeRev", Rod::Session, "ревизия GPU-текстуры"},
    {"uploadedKnowledgeRev", Rod::Session, "ревизия GPU-текстуры"},
    {"mapScreen", Rod::Session, ""},
    {"deposits", Rod::Fields, "слой жил Clay/Iron/Stone"},
    {"sightRt", Rod::Session, "сессионная половина знания"},
    {"revealMapOn", Rod::Session, "прибор консоли"},
    {"optical", Rod::Cache, "float-виды мира для света и взгляда"},
    {"macro", Rod::Session, "рендерер макромира"},
    {"macroLightsDirty", Rod::Session, ""},
    {"lastSpellFlight", Rod::Session, ""},
    {"lastJumpHeld", Rod::Session, ""},
    {"lastStandingBonuses", Rod::Cache, "что стояло на теле прошлым шагом"},
    {"ecs", Rod::Divergence,
     "entt-мир сцены — легаси под снос: тела M-150, стена M-188"},
    {"macroStore", Rod::Squads, "гладкая память рода 2"},
    {"sceneObjects", Rod::Session,
     "единый массив объектов сцены (M-150) — транзиент окна, в сейв не едет"},
    {"bus", Rod::Session, "шина событий кадра"},
    {"logic", Rod::Facts,
     "живая половина прогресса узлов; сейв-образ gs.logicNodes*"},
    {"quests", Rod::Facts, "живая половина квестов"},
    {"activeQuests", Rod::Facts, ""},
    {"questMarkerSig", Rod::Cache, ""},
    {"availableSettlementQuests", Rod::Cache, "перегенерируется от сида дня"},
    {"availableQuestSettlementId", Rod::Cache, ""},
    {"availableQuestDay", Rod::Cache, ""},
    {"appliedEventCount", Rod::Session, ""},
    {"appliedStoryResultCount", Rod::Session, ""},
    {"appliedSpawnEventCount", Rod::Session, ""},
    {"npcAi", Rod::WorldVars,
     "живая половина ритма макро-ИИ; сейв-образ gs.macroAiRhythm; индекс "
     "сквадов — кэш на драйв"},
    {"subworld", Rod::Module,
     "модуль субмира (ФРЕЙМ п.5); нарезка его памяти — M-150"},
    {"audio", Rod::Session, ""},
    {"audioDesired", Rod::Session, ""},
    {"audioFailed", Rod::Session, ""},
    {"subworldLastPlayerHp", Rod::Session, ""},
    {"subworldHitFlashTimer", Rod::Session, ""},
    {"restRegenSuppressed", Rod::Session, "прибор смоука"},
    {"trees", Rod::Cache, "точки деревьев от treeLayer"},
    {"treeGrid", Rod::Cache, "bucket-сетка точек"},
    {"camX", Rod::Session, ""},
    {"camY", Rod::Session, ""},
    {"camTargetX", Rod::Session, ""},
    {"camTargetY", Rod::Session, ""},
    {"camPanX", Rod::Session, ""},
    {"camPanY", Rod::Session, ""},
    {"zoom", Rod::Session, ""},
    {"panning", Rod::Session, ""},
    {"panLastMouseX", Rod::Session, ""},
    {"panLastMouseY", Rod::Session, ""},
    {"relativeMouseActive", Rod::Session, ""},
    {"playerPaused", Rod::Session, "единственный хранимый бит паузы"},
    {"turnBasedMode", Rod::Session, ""},
    {"showDebug", Rod::Session, ""},
    {"showDialogOpen", Rod::Session, ""},
    {"showDialogEvent", Rod::Session, ""},
    {"showDialogUi", Rod::Session, ""},
    {"showDialogCapturedTick", Rod::Session, ""},
    {"storyOverlay", Rod::Session, ""},
    {"showStoryCapturedTick", Rod::Session, ""},
    {"sceneHoldsMap", Rod::Session, ""},
    {"pendingPresentationEvents", Rod::Session, ""},
    {"pendingPresentationCount", Rod::Session, ""},
    {"pendingPresentationTick", Rod::Session, ""},
    {"pendingPresentationSeen", Rod::Session, ""},
    {"ui", Rod::Session, ""},
    {"uiSettings", Rod::Session, ""},
    {"keymap", Rod::Session, ""},
    {"uiPrefsDirty", Rod::Session, ""},
    {"cursor", Rod::Session, ""},
    {"saveSummary", Rod::Session, ""},
    {"autosaveSummary", Rod::Session, ""},
    {"pathCost", Rod::Cache, "запечённая цена пути"},
    {"pathScratch", Rod::Session, "рабочая память поиска"},
    {"navWorld", Rod::Cache, "запечённая навигация: округи + порталы + граф"},
    {"customParams", Rod::Session, ""},
    {"creation", Rod::Session, ""},
    {"creationCustom", Rod::Session, ""},
    {"introSlides", Rod::Session, ""},
    {"splash", Rod::Session, ""},
    {"customPreviewTex", Rod::Session, ""},
    {"customPreviewSide", Rod::Session, ""},
    {"customWorldReady", Rod::Session, ""},
    {"pendingWorldShell", Rod::Session, ""},
    {"smoke", Rod::Session, "прибор смоука"},
    {"console", Rod::Session, "прибор консоли"},
    {"preBattleNpc", Rod::Session, "хэндл сессии; протухание мертвит поколение"},
    {"encounterGraceNpc", Rod::Session, ""},
    {"subjectSquad", Rod::Session, ""},
    {"encounterTalkLine", Rod::Session, ""},
    {"simSpeed", Rod::Session, ""},
    {"simStepCarry", Rod::Session, ""},
    {"restUntilTick", Rod::Session, ""},
    {"tickRateCounter", Rod::Session, ""},
    {"tickRateMark", Rod::Session, ""},
    {"measuredTicksPerSec", Rod::Session, ""},
    {"panels", Rod::Session, ""},
};

// ── САМОПРОВЕРКА ПАРСЕРА + НЕГАТИВНЫЙ КОНТРОЛЬ (§8 п.6) ──────────────────
constexpr std::string_view kFixture =
    "struct Sample {\n"
    "    int plain = 1, second = 2;\n"
    "    std::array<std::uint16_t, kCap> arr;\n"
    "    SurveyRow rows[std::size_t(Kind::Count)];\n"
    "    WorldTime t = world_time_at(0, 6, 0);\n"
    "    Rng jitter{0xC0FFEEu};\n"
    "    bool valid(Handle h) const {\n"
    "        return alive[h.slot] != 0; // ; в теле\n"
    "    }\n"
    "    static constexpr int kConst = 4;\n"
    "    using Alias = int;\n"
    "#define SM_X(name, T) T name;\n"
    "    SM_COLUMNS(SM_X)\n"
    "#undef SM_X\n"
    "    struct Inner { int a = 0; };\n"
    "    Inner inner;\n"
    "    struct Panels {\n"
    "        bool a = false;\n"
    "    } panels;\n"
    "    std::vector<float> heights, treeDensity;\n"
    "    ImTextureID tex = ImTextureID();\n"
    "};\n";

void test_parser_on_fixture() {
    const std::vector<std::string> got =
        parse_fields(std::string(kFixture), "Sample");
    const std::vector<std::string> want = {
        "plain", "second", "arr", "rows", "t", "jitter",
        "inner", "panels", "heights", "treeDensity", "tex",
    };
    CHECK(got == want, "парсер снимает с фикстуры ровно ожидаемые поля — "
                       "мульти-деклараторы, массивы, шаблоны, брейс-иниц., "
                       "вложенные структуры; функции и константы мимо");
    if (got != want) {
        std::fprintf(stderr, "  парсер снял (%zu):", got.size());
        for (auto& g : got) std::fprintf(stderr, " %s", g.c_str());
        std::fprintf(stderr, "\n");
    }
    // Негативный контроль: таблица без строки `panels` обязана дать ровно
    // одно «поле без рода» и ноль мёртвых строк — детектор ВИДИТ дефект.
    constexpr Row kShort[] = {
        {"plain", Rod::Session, ""},  {"second", Rod::Session, ""},
        {"arr", Rod::Session, ""},    {"rows", Rod::Session, ""},
        {"t", Rod::Session, ""},      {"jitter", Rod::Session, ""},
        {"inner", Rod::Session, ""},  {"heights", Rod::Session, ""},
        {"treeDensity", Rod::Session, ""}, {"tex", Rod::Session, ""},
    };
    const Tally neg = compare("Sample", got, kShort, std::size(kShort),
                              /*loud=*/false);
    CHECK(neg.missingRod == 1 && neg.deadRow == 0,
          "негативный контроль: поле без строки переписи ЗАМЕЧЕНО");
    // И обратная сторона: мёртвая строка переписи тоже краснит.
    constexpr Row kDead[] = {{"ghost", Rod::Session, ""}};
    const Tally dead = compare("Sample", got, kDead, std::size(kDead),
                               /*loud=*/false);
    CHECK(dead.deadRow == 1, "негативный контроль: строка без поля ЗАМЕЧЕНА");
}

void test_xmacro_parser() {
    const std::string fx =
        "#define SM_COLS(X) \\\n"
        "    X(alpha, TypeA) \\\n"
        "    X(beta,  TypeB) \\\n"
        "    X(gamma, std::uint8_t)\n"
        "\n";
    const std::vector<std::string> got = parse_xmacro(fx, "SM_COLS");
    const std::vector<std::string> want = {"alpha", "beta", "gamma"};
    CHECK(got == want, "парсер X-macro снимает колонки по порядку");
}

// ── ПЕРЕПИСЬ ЖИВЫХ НОСИТЕЛЕЙ ──────────────────────────────────────────────
struct Carrier {
    const char* name;
    const char* header;
    const Row* rows;
    std::size_t rowCount;
};

// ГРАНИЦА ПРИБОРА, ЧИСЛАМИ. Перепись судит ЧЕТЫРЕ названных носителя, а не
// дерево: закон «ничего вне шести родов» целиком поставит только сам фрейм
// (фаза 4, M-171/M-172 — разные пути включения). Пока стены нет, эти числа
// обязаны ехать в вердикте, иначе зелёный читается как «седьмого рода в игре
// нет», тогда как верно «у четырёх носителей седьмого рода нет».
struct Boundary {
    int carriers = 0;    // носителей под переписью
    int fields = 0;      // их полей названо
    int divergence = 0;  // поимённых расхождений
};

Boundary census() {
    const std::string stateSrc =
        read_file(fs::path(kRoot) / "src/macro/state.h");
    const std::string storeSrc =
        read_file(fs::path(kRoot) / "src/macro/store.h");
    const std::string appSrc =
        read_file(fs::path(kRoot) / "src/app/app_state.h");
    CHECK(!stateSrc.empty() && !storeSrc.empty() && !appSrc.empty(),
          "заголовки носителей читаются");

    int totalFields = 0, totalMissing = 0, totalDead = 0, totalDivergence = 0;
    // Носители СЧИТАЮТСЯ, а не набираются литералом: число в вердикте обязано
    // двигаться само, когда носителя добавят или снимут.
    int carriers = 0;
    int byRod[16] = {};
    auto add = [&](const Tally& t) {
        ++carriers;
        totalFields += t.fields;
        totalMissing += t.missingRod;
        totalDead += t.deadRow;
        for (int r = 0; r < 16; ++r) byRod[r] += t.byRod[r];
    };

    // GameState / PlayerState — src/macro/state.h.
    add(compare("GameState", parse_fields(stateSrc, "GameState"),
                kGameStateRows, std::size(kGameStateRows)));
    add(compare("PlayerState", parse_fields(stateSrc, "PlayerState"),
                kPlayerStateRows, std::size(kPlayerStateRows)));

    // MacroStore — колонки X-macro + служебные поля тела структуры.
    std::vector<std::string> storeFields =
        parse_xmacro(storeSrc, "SM_MACRO_STORE_COLUMNS");
    CHECK(storeFields.size() >= 19u,
          "колонки MacroStore сняты с X-macro (19 на день постройки)");
    for (const std::string& f : parse_fields(storeSrc, "MacroStore"))
        storeFields.push_back(f);
    add(compare("MacroStore", storeFields, kMacroStoreRows,
                std::size(kMacroStoreRows)));

    // App — src/app/app_state.h.
    add(compare("App", parse_fields(appSrc, "App"), kAppRows,
                std::size(kAppRows)));

    // У каждого РАСХОЖДЕНИЯ note обязан звать наряд — молчаливых расхождений
    // не бывает (M-189: «носитель вне родов = поимённое расхождение + наряд»).
    auto checkNotes = [&](const Row* rows, std::size_t n, const char* who) {
        for (std::size_t i = 0; i < n; ++i) {
            if (rows[i].rod != Rod::Divergence) continue;
            ++totalDivergence;
            if (rows[i].note.find("M-") == std::string_view::npos) {
                std::fprintf(stderr,
                             "  РАСХОЖДЕНИЕ БЕЗ НАРЯДА: %s::%.*s\n", who,
                             int(rows[i].field.size()), rows[i].field.data());
                CHECK(false, "расхождение обязано звать наряд M-…");
            }
        }
    };
    checkNotes(kGameStateRows, std::size(kGameStateRows), "GameState");
    checkNotes(kPlayerStateRows, std::size(kPlayerStateRows), "PlayerState");
    checkNotes(kMacroStoreRows, std::size(kMacroStoreRows), "MacroStore");
    checkNotes(kAppRows, std::size(kAppRows), "App");

    CHECK(totalFields > 100,
          "перепись видит носители: у четырёх структур больше сотни полей — "
          "меньше значит парсер ослеп, зелёный ничего бы не значил");
    CHECK(totalMissing == 0,
          "у каждого поля носителя есть строка с родом фрейма (M-189)");
    CHECK(totalDead == 0, "мёртвых строк переписи нет (§5 п.1)");

    std::printf("state_census: полей %d · расхождений %d\n", totalFields,
                totalDivergence);
    std::printf("  ");
    for (int r = 0; r < 16; ++r) {
        if (byRod[r] == 0) continue;
        std::printf("%s=%d · ", rod_name(Rod(r)), byRod[r]);
    }
    std::printf("\n");

    return Boundary{carriers, totalFields, totalDivergence};
}

}  // namespace

int main() {
    test_parser_on_fixture();
    test_xmacro_parser();
    const Boundary b = census();

    char scope[200];
    std::snprintf(scope, sizeof(scope),
                  "носителей %d · полей %d · расхождений %d · стены шести "
                  "родов ещё нет", b.carriers, b.fields, b.divergence);
    return sm::test::report("state_census_test", scope);
}
