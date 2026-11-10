// СЕЙВ ЕСТЬ ПРЯМОЙ ДАМП ПАКЕТНОЙ ФОРМЫ — СВИДЕТЕЛЬ (M-439, CRAFT §10).
//
// Файл — заголовок и ДВА КУСКА ПАМЯТИ байт в байт: состояние макромира
// (`GameState`) и гладкая память сквадов (`MacroStore`). Сериализатора нет,
// поэтому и свидетель судит не «каждое поле доехало» (списка полей у сейва не
// существует), а ЗАКОНЫ, на которых дамп стоит:
//
//   1. КРУГ: записанный и прочитанный мир равен исходному ПОБАЙТНО — оба куска
//      целиком, включая последний байт последнего массива каждого.
//   2. ОДИН МИР — ОДНИ БАЙТЫ: два мира, рождённых одними дверями в разной
//      памяти, равны побайтно и дают одну сумму. На этом стоит всё остальное:
//      байт вне названного члена (дыра выравнивания) или член без рождения
//      дали бы шум. Стену держит компилятор (`TIMAERT_PACKED`), свидетель
//      судит её следствие в рантайме.
//   3. ЖИВАЯ КАРТА ПЕРЕЖИВАЕТ СЕЙВ ДВЕРЯМИ, А НЕ ТОЛЬКО БАЙТАМИ: убитый лорд
//      остаётся мёртвым, ординал мертвеца не переиздаётся, флажок игрока
//      возвращается на того же лорда, справочник ординалов отвечает, руина
//      остаётся руиной (M-456). Эти законы стояли у снимка сквадов, умершего
//      вместе со вторым сериализатором; носитель сменился, законы — нет.
//   4. ОТКАЗ В ТОЧКЕ ВХОДА: чужая магия, версия, раскладка, обрезанный файл и
//      порча ЛЮБОГО байта мира — начала, середины, самого конца — не
//      загружаются.
//   5. НЕУДАВШАЯСЯ ЗАПИСЬ НЕ ТРОГАЕТ ПРЕЖНИЙ СЛОТ; удавшаяся оставляет его в
//      `.bak`.
//   6. СУММА (вердикт владельца В4): слова по 8 Б, четыре дорожки — не зависит
//      от нарезки на порции и видит перестановку слов, порчу бита и лишнее
//      слово.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ (AGENTS §8 п.6; мутации ставились
// в `src/macro/save.cpp`, файл после каждой возвращался `cp` и сверялся
// `cmp`, прогон 2026-11-10):
//   БАЗА (целый код) ........................................ OK, 45 проверок
//   M1  запись стора короче на слово ........................ FAIL (запись
//       отказала самопроверкой — длина файла не та)
//   M2  загрузка не сверяет сумму ........................... FAIL (порча
//       байта мира загрузилась)
//   M3  сумма не кормится стором (запись, сверка, чтение) ... FAIL ×2 (порча
//       первого и последнего байта стора загрузилась; сумма заголовка не
//       равна сумме мира)
//   M4  загрузка не судит версию заголовка .................. FAIL
//       · ДО правки свидетеля эта мутация проходила ЗЕЛЁНОЙ: чужую версию
//         нёс только обрезанный файл, а он отказывается уже длиной. Дыра
//         закрыта подделкой ПОЛНОЙ длины — ровно один байт настоящего слота.
//   M4б загрузка не судит магию ............................. FAIL
//   M4в загрузка не судит размеры кусков .................... FAIL
//   M5  порция самопроверки 4 МиБ вместо 1 (обязана МОЛЧАТЬ)  OK — детектор
//       не краснеет на правку, не меняющую закона.
#include "check.h"

#include "bus/session.h"
#include "bus/store.h"
#include "bus/world_row.h"
#include "macro/birth_squad.h"
#include "macro/chronicle.h"
#include "macro/features.h"
#include "macro/knowledge.h"
#include "macro/labour.h"
#include "macro/markers.h"
#include "macro/npc_spawn.h"
#include "macro/player_entity.h"
#include "macro/save.h"
#include "macro/scent_field.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "macro/tree_layer.h"
#include "tables/codex.h"
#include "tables/faction.h"
#include "tables/npc.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sm;

std::string temp_path(const char* name) {
    const char* dir = std::getenv("TEMP");
    if (!dir || dir[0] == '\0') dir = std::getenv("TMP");
    if (!dir || dir[0] == '\0') dir = ".";
    std::string p(dir);
    const char last = p.empty() ? '\0' : p[p.size() - 1u];
    if (last != '/' && last != '\\') p += '/';
    return p + name;
}

void remove_slot(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
    std::remove((path + ".tmp").c_str());
}

long file_bytes(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fclose(f);
    return n;
}

// Переписать один байт файла на месте; вернуть прежний (или -1 при отказе).
int poke_file_byte(const std::string& path, long offset, unsigned char value) {
    std::FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) return -1;
    int old = -1;
    if (std::fseek(f, offset, SEEK_SET) == 0) {
        old = std::fgetc(f);
        if (old >= 0 && std::fseek(f, offset, SEEK_SET) == 0)
            std::fputc(value, f);
    }
    std::fclose(f);
    return old;
}

// Первые `n` байт файла — заголовок и начало состояния, для малых подделок.
std::vector<unsigned char> file_head(const std::string& path, std::size_t n) {
    std::vector<unsigned char> out(n);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    const std::size_t got = std::fread(out.data(), 1, n, f);
    std::fclose(f);
    out.resize(got);
    return out;
}

bool write_bytes(const std::string& path, const std::vector<unsigned char>& b) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return std::fclose(f) == 0 && ok;
}

// ── МИР ФИКСТУРЫ — РОЖДЁН ДВЕРЯМИ ИГРЫ, И ТРОГАЕТ КРАЯ ──────────────────────
// Сторона мира — КАП (1024): ловушка ломтя 4 сейв-эпика говорит, что у
// штабеля под капом свидетель обязан трогать КРАЙ, а не начало. Здесь тронут
// последний байт последнего члена КАЖДОГО из двух кусков: последняя клетка
// поля света (хвост состояния) и названное хвостовое слово стора рядом с
// последним занятым слотом (хвост хранилища) — обрезанный на слово круг их
// бы потерял.
constexpr int kSide = int(kWorldSideCap);
constexpr int kLast = kSide - 1;

struct World {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    std::unique_ptr<MacroStore> stOwn = make_macro_store();
    MacroStore& st = *stOwn;
    Session ses{};
    std::uint32_t lordOrd = 0;     // живой лорд под флажком игрока
    std::uint32_t deadOrd = 0;     // убитый отряд — обязан остаться мёртвым
    std::uint32_t cityOrd = 0;     // осёдлый: город с душами
    std::uint32_t ruinCell = 0;    // клетка города, ставшего руиной (M-456)
    std::uint16_t lastSlot = 0;    // последний слот хранилища
};

TerrainData dry_land() {
    TerrainData td;
    td.width = kSide;
    td.height = kSide;
    td.rgba.assign(std::size_t(kSide) * std::size_t(kSide) * 4u,
                   field_word_of(0.6f));
    td.seaLevel16 = field_word_of(kDefaultSeaLevel);
    return td;
}

void build_world(World& w, const TerrainData& land) {
    GameState& gs = w.gs;
    MacroStore& st = w.st;
    gs.worldSeed = 0x12345678u;
    gs.mapW = kSide;
    gs.mapH = kSide;
    gs.mapParams.seed = gs.worldSeed % 100000u;
    gs.mapParams.seaLevel = 0.55f;
    gs.cityCountTarget = 77;
    gs.worldTime = world_time_at(12, 13, 14);
    gs.worldTime.tick += 3;   // не на границе минуты: миг назван до тика
    gs.lastWorldRebakeDay = 9;
    gs.worldTickRt.pendingDailyTicks = 3;
    gs.worldTickRt.nextDailyTickDay = 13;
    gs.worldTickRt.subworldStepRemainder = 11;
    gs.worldTickRt.jitter.state = 0xDEADBEEFu;
    gs.macroAiRhythm.jitter.state = 0xFEEDF00Du;
    gs.playerRoll.state = 0x5A1700D5u;
    // Строки рода 6 — дверью реестра (сама `create_factions` живёт в
    // `state.cpp` и тянет за собой генезис; свидетелю нужны строки, а не он).
    claim_registry_rows(gs.factions);
    set_relation(gs.factions, faction_index("timaert"),
                 faction_index("lake_duchy"), -5);

    // Игрок: кодекс и журнал — два выученных факта.
    gs.player.ageDays = 1234;
    gs.player.codexUnlockedBits = codex_bit(CodexArticleId::Witches)
                                | codex_bit(CodexArticleId::Market);

    // Память мира: два факта в кольце и головы цепочек по клетке.
    chronicle_init(gs.chronicle, kSide, kSide);
    {
        WorldFact f{};
        f.day = 11;
        f.kind = std::uint16_t(FactKind::Killed);
        f.subjectKind = std::uint8_t(FactSubject::Cell);
        f.x = 40; f.y = 40; f.amount = 3;
        chronicle_record(gs.chronicle, f);
        f.day = 12;
        f.kind = std::uint16_t(FactKind::Taxed);
        f.x = std::int16_t(kLast); f.y = std::int16_t(kLast);
        chronicle_record(gs.chronicle, f);
    }

    // Поля над тором — каждое своей дверью, и каждое в ПОСЛЕДНЕЙ клетке мира.
    knowledge_reset(gs.knowledge, kSide, kSide);
    gs.knowledge.data[0] = kKnowledgeExplored;
    gs.knowledge.data[kWorldCellCap - 1u] = kKnowledgeExplored;
    tree_layer_reset(gs.treeLayer, kSide, kSide);
    set_tree_count(gs.treeLayer, 17, 0, 12000);
    set_tree_count(gs.treeLayer, kLast, kLast, 600);
    gs.features.resize(kSide, kSide);
    feature_put(gs.features, land, kLast, kLast, FT_Road);
    feature_number_write(gs, 9, 4, 2);
    fauna_field_write(gs, kLast, 0, 31);
    // Поле света — ПОСЛЕДНИЙ член состояния, и это его последняя клетка: байт,
    // которым кончается первый кусок файла. Прежний провод поле света не вёз
    // вовсе; дамп везёт все поля состояния единообразно.
    light_emission_write(gs, kLast, kLast, 777);
    gs.cellOwner.data[kWorldCellCap - 1u] = 3u;
    scent_ensure(gs.scent, kSide, kSide);
    scent_deposit(gs.scent, 1, 100, 100, 400u, 4000u);
    scent_deposit(gs.scent, kFactionCount - 1, kLast, kLast, 800u, 80u);
    add_marker(gs.markers, MarkerStyle::Danger, cell_of(15, 16, kSide),
               "Round Danger");
    add_marker(gs.markers, MarkerStyle::Waypoint, cell_of(3, 4, kSide), "gone");
    remove_marker(gs.markers, MarkerStyle::Waypoint, cell_of(3, 4, kSide));

    // ── Род 2: осёдлый город с душами, деревня-вассал, ходячие отряды ────────
    const int tim = faction_index("timaert");
    const MacroHandle city = birth_squad(gs, &w.ses, st, SquadType::City, 40,
                                         80, std::int16_t(tim), "Round City",
                                         &land);
    w.cityOrd = city.id;
    settle_souls(gs, st, city.slot, 777);
    st.inventory[city.slot].inv.add("wood", 19);
    st.wellbeing[city.slot].popGrowthCarry = 0.375f;
    const MacroHandle village = birth_squad(gs, &w.ses, st, SquadType::Village,
                                            45, 85, std::int16_t(tim),
                                            "Round Hamlet", &land);
    settle_souls(gs, st, village.slot, 111);
    set_relation(st, int(village.id), int(city.id), Stance::Suzerain, 80, 0);
    tithe_of(st, village.slot)->owedValue = 123;

    // Второй город УМЕР И ОСТАВИЛ РУИНУ (M-456): та же дверь поля фич, которой
    // пишет строка смерти города. Рождение положило на клетку фичу города,
    // живой мир переписал её — и ровно это обязан привезти сейв.
    const MacroHandle fallen = birth_squad(gs, &w.ses, st, SquadType::City,
                                           200, 300, std::int16_t(tim),
                                           "Fallen", &land);
    w.ruinCell = st.cell[fallen.slot].idx;
    feature_put(gs.features, land, w.ruinCell, FT_Ruin);

    TerrainData absent{};
    SquadSpec lord{};
    lord.avatarType = NPCType::Bandit;
    lord.avatarLevel = 5;
    lord.x = 20; lord.y = 20;
    lord.factionIndex = faction_index("bandits");
    lord.members.push(make_soldier(std::uint8_t(NPCType::Guard), 3, 1001u));
    lord.members.push(make_soldier(std::uint8_t(NPCType::Guard), 2, 1002u));
    lord.waypointCount = 2;
    lord.waypoints[0] = 24; lord.waypoints[1] = 20;
    lord.waypoints[2] = 24; lord.waypoints[3] = 24;
    const MacroHandle a = spawn_squad(gs, st, absent, lord);
    w.lordOrd = a.id;
    st.sheet[a.slot].levelData.exp = exp_at_level(5) + 777u;
    st.sheet[a.slot].attributes[AttributeId::End] = 13;
    st.pools[a.slot].sp = -15;
    st.cell[a.slot].idx = ecs::cell_index(25, 21, kSide);
    ecs::set_squad_name(st.name[a.slot], "Chornyy Voron");
    // Игрок ВСЕЛЁН в лорда: истина — колонка флажка, кэш — сессия.
    transfer_player_flag(st, w.ses.playerFlagRef, a);

    SquadSpec crew{};
    crew.avatarType = NPCType::Peasant;
    crew.avatarLevel = 2;
    crew.x = 40; crew.y = 40;
    crew.factionIndex = tim;
    const MacroHandle b = spawn_squad(gs, st, absent, crew);
    w.deadOrd = b.id;
    st.pools[b.slot].hp = 0;
    macro_mark_dead(st, b);

    // ── Край хранилища: занять слоты ДО ПОСЛЕДНЕГО, потом проредить ─────────
    // Рождение выдаёт слоты по возрастанию; последний слот капа занимается
    // только заполнением хранилища. Половина заполнителей затем умирает —
    // стопка свободных слотов и справочник ординалов получают живое
    // содержимое, а не девственный порядок рождения.
    std::vector<MacroHandle> filler;
    while (st.freeCount > 0) filler.push_back(store_birth(st));
    w.lastSlot = filler.back().slot;
    ecs::set_squad_name(st.name[w.lastSlot], "Last Slot");
    st.inventory[w.lastSlot].inv.add("wood", 1);
    for (std::size_t i = 0; i + 1u < filler.size(); i += 2u)
        store_death(st, filler[i]);

    // Журнал игрока — копии записей, которые он узнал.
    gs.player.journal[0] = gs.chronicle.ring[0];
    gs.player.journal[1] = gs.chronicle.ring[1];
    gs.player.journalCount = 2u;
    gs.player.journalSeenSeq = 2u;
}

std::uint64_t world_sum(const World& w) {
    SaveChecksum s;
    s.feed(&w.gs, sizeof(GameState));
    s.feed(&w.st, sizeof(MacroStore));
    return s.finish();
}

std::uint64_t header_checksum(const std::string& path) {
    // Заголовок: магия 4, версия 4, размеры 8 + 8, сумма 8 — сумма на 24-м Б.
    const std::vector<unsigned char> h = file_head(path, 32);
    std::uint64_t sum = 0;
    if (h.size() == 32) std::memcpy(&sum, h.data() + 24, sizeof(sum));
    return sum;
}

// ── 6. СУММА ────────────────────────────────────────────────────────────────
void test_checksum_laws() {
    std::vector<std::uint64_t> words(1024);
    std::uint64_t x = 0x9E3779B97F4A7C15ull;
    for (std::uint64_t& w : words) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;   // xorshift64 — заполнитель
        w = x;
    }
    const std::size_t bytes = words.size() * sizeof(std::uint64_t);
    const auto whole = [&](const std::vector<std::uint64_t>& v) {
        SaveChecksum s;
        s.feed(v.data(), v.size() * sizeof(std::uint64_t));
        return s.finish();
    };
    const std::uint64_t base = whole(words);

    // Нарезка на порции любой кратной слову длины итога не меняет — в том
    // числе порции, рвущие четвёрку дорожек посередине.
    int cuts = 0, cutMismatch = 0;
    for (const std::size_t step : {std::size_t(8), std::size_t(24),
                                   std::size_t(40), std::size_t(4096),
                                   std::size_t(1000 * 8)}) {
        SaveChecksum s;
        const auto* p = reinterpret_cast<const unsigned char*>(words.data());
        for (std::size_t at = 0; at < bytes; at += step)
            s.feed(p + at, at + step <= bytes ? step : bytes - at);
        ++cuts;
        if (s.finish() != base) ++cutMismatch;
    }
    CHECK(cuts == 5 && cutMismatch == 0,
          "сумма зависит от последовательности слов, а не от нарезки на "
          "порции");

    // НЕГАТИВНЫЕ КОНТРОЛИ: каждая из порч обязана сдвинуть сумму.
    int probes = 0, blind = 0;
    for (const std::size_t at : {std::size_t(0), std::size_t(1),
                                 std::size_t(2), std::size_t(3),
                                 std::size_t(511), words.size() - 1u}) {
        std::vector<std::uint64_t> v = words;
        v[at] ^= std::uint64_t(1) << (at % 64u);   // один бит
        ++probes;
        if (whole(v) == base) ++blind;
    }
    {
        std::vector<std::uint64_t> v = words;   // соседние слова — разные дорожки
        std::swap(v[8], v[9]);
        ++probes;
        if (whole(v) == base) ++blind;
    }
    {
        std::vector<std::uint64_t> v = words;   // слова ОДНОЙ дорожки
        std::swap(v[8], v[12]);
        ++probes;
        if (whole(v) == base) ++blind;
    }
    {
        std::vector<std::uint64_t> v = words;   // лишнее нулевое слово в хвосте
        v.push_back(0u);
        ++probes;
        if (whole(v) == base) ++blind;
    }
    {
        // Целые дорожки, обменявшиеся содержимым: слияние порядковое.
        std::vector<std::uint64_t> v = words;
        for (std::size_t i = 0; i + 3u < v.size(); i += 4u)
            std::swap(v[i + 1u], v[i + 2u]);
        ++probes;
        if (whole(v) == base) ++blind;
    }
    CHECK(probes == 10 && blind == 0,
          "сумма видит порчу бита в каждой дорожке и в последнем слове, "
          "перестановку слов между дорожками и внутри одной, обмен дорожек и "
          "лишнее слово");
}

// ── 1–5. КРУГ, ЖИВАЯ КАРТА, ОТКАЗЫ ──────────────────────────────────────────
void test_dump_roundtrip() {
    const TerrainData land = dry_land();
    const std::string path = temp_path("timaert_save_dump.bin");
    const std::string forged = temp_path("timaert_save_dump_forged.bin");
    remove_slot(path);
    remove_slot(forged);

    World w;
    build_world(w, land);

    // ── 2. ОДИН МИР — ОДНИ БАЙТЫ ────────────────────────────────────────────
    {
        World twin;
        build_world(twin, land);
        CHECK(std::memcmp(&w.gs, &twin.gs, sizeof(GameState)) == 0,
              "два состояния, рождённых одними дверями в разной памяти, "
              "равны побайтно — ни дыр выравнивания, ни членов без рождения");
        CHECK(std::memcmp(&w.st, &twin.st, sizeof(MacroStore)) == 0,
              "две гладкие памяти сквадов, рождённые одними дверями, равны "
              "побайтно");
        CHECK(world_sum(w) == world_sum(twin),
              "равные миры дают одну сумму");
        // Негативный контроль: детектор обязан видеть дельту в одном байте
        // ПОСЛЕДНЕГО члена — иначе равенство выше не значило бы ничего.
        twin.st.pad ^= 1u;
        CHECK(std::memcmp(&w.st, &twin.st, sizeof(MacroStore)) != 0
                  && world_sum(w) != world_sum(twin),
              "побайтное сравнение и сумма видят один бит в хвостовом слове "
              "хранилища (негативный контроль)");
    }

    // ── 1. КРУГ ─────────────────────────────────────────────────────────────
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t sumBefore = world_sum(w);
    const auto t1 = std::chrono::steady_clock::now();
    CHECK_OR_RETURN(save_game(w.gs, w.st, path), "мир записан");
    const auto t2 = std::chrono::steady_clock::now();
    CHECK(std::uint64_t(file_bytes(path)) == save_file_bytes(),
          "слот весит ровно заголовок и два куска — константа сборки");
    CHECK(world_sum(w) == sumBefore,
          "запись не тронула мир: сумма живой памяти та же");
    CHECK(header_checksum(path) == sumBefore,
          "в заголовке стоит сумма живой памяти");

    World back;
    const auto t3 = std::chrono::steady_clock::now();
    CHECK_OR_RETURN(load_game(back.gs, back.st, path), "мир прочитан");
    const auto t4 = std::chrono::steady_clock::now();
    CHECK(std::memcmp(&w.gs, &back.gs, sizeof(GameState)) == 0,
          "состояние макромира вернулось побайтно");
    CHECK(std::memcmp(&w.st, &back.st, sizeof(MacroStore)) == 0,
          "гладкая память сквадов вернулась побайтно");
    const auto ms = [](auto a, auto b) {
        return double(std::chrono::duration_cast<std::chrono::microseconds>(
                          b - a).count()) / 1000.0;
    };
    std::printf("save_dump: слот %.2f МиБ · сумма %.1f мс · запись с "
                "самопроверкой %.1f мс · чтение с суммой %.1f мс\n",
                double(save_file_bytes()) / (1024.0 * 1024.0), ms(t0, t1),
                ms(t1, t2), ms(t3, t4));

    // ── Осмотр слота ────────────────────────────────────────────────────────
    {
        const SaveSummary s = inspect_save(path);
        CHECK(s.status == SaveInspectStatus::Ready
                  && s.version == kSaveVersion
                  && s.worldSeed == w.gs.worldSeed
                  && s.day == w.gs.worldTime.day()
                  && s.hour == w.gs.worldTime.hour()
                  && s.minute == w.gs.worldTime.minute()
                  && s.savedAt.size() == 24u && s.savedAt.back() == 'Z',
              "осмотр читает сид и время ИЗ КУСКА состояния по их смещению, "
              "метку — из заголовка");
        CHECK(inspect_save(forged).status == SaveInspectStatus::Missing,
              "слота нет — осмотр так и говорит");
    }

    // ── 3. ЖИВАЯ КАРТА — ДВЕРЯМИ ЗАГРУЖЕННОГО МИРА ──────────────────────────
    MacroStore& st2 = back.st;
    GameState& gs2 = back.gs;
    resolve_player_handles_after_load(back.ses, st2);
    const MacroHandle lord = macro_handle_by_spawn_id(st2, w.lordOrd);
    CHECK_OR_RETURN(st2.valid(lord),
                    "справочник ординалов загруженного стора отвечает: лорд "
                    "найден по своему ординалу");
    CHECK(player_flag_handle(back.ses) == lord,
          "флажок игрока после загрузки стоит на ТОМ ЖЕ лорде — кэш сессии "
          "пересобран из колонки");
    {
        int flagged = 0;
        for (std::size_t s = 0; s < kUnifiedCap; ++s)
            if (st2.alive[s] != 0 && st2.playerFlag[s].on != 0) ++flagged;
        CHECK(flagged == 1, "носитель флажка ровно один — не каждый слот");
    }
    CHECK(exp_in_level_of(st2.sheet[lord.slot].levelData) == 777
              && squad_level(st2, lord.slot) == 5
              && st2.sheet[lord.slot].attributes.of(AttributeId::End) == 13,
          "кампании лорда — опыт, ступень, расхождение листа с рождением");
    CHECK(st2.pools[lord.slot].sp == -15
              && ecs::cell_x(st2.cell[lord.slot], kSide) == 25
              && ecs::cell_y(st2.cell[lord.slot], kSide) == 21,
          "долг усталости и клетка марша — те, что были на записи");
    CHECK(std::strcmp(st2.name[lord.slot].text, "Chornyy Voron") == 0
              && creature_count(st2.inventory[lord.slot].inv) == 2
              && st2.orders[lord.slot].waypointCount == 2,
          "имя анкеты, головы ростера и маршрут приказа");
    const MacroHandle dead = macro_handle_by_spawn_id(st2, w.deadOrd);
    CHECK(st2.valid(dead) && macro_dead(st2, dead)
              && st2.pools[dead.slot].hp == 0
              && st2.deadRowCount == w.st.deadRowCount
              && st2.deadRowCount > 0,
          "УБИТЫЙ остаётся мёртвым, и ведомость павших его помнит — свип "
          "загруженного мира не слеп к трупам из файла");
    const MacroHandle city = macro_handle_by_spawn_id(st2, w.cityOrd);
    CHECK_OR_RETURN(st2.valid(city), "город найден по своему ординалу");
    {
        // Ожидание выводится из ИСХОДНОГО мира, а не из литерала (§8 п.4):
        // что двери поселили до записи, то и стоит после чтения.
        const int cx = ecs::cell_x(st2.cell[city.slot], kSide);
        const int cy = ecs::cell_y(st2.cell[city.slot], kSide);
        const int flock0 = feature_number_read(w.gs, cx, cy);
        const int home0 = souls_home(w.st, city.slot);
        CHECK(flock0 > 0 && home0 > 0
                  && feature_number_read(gs2, cx, cy) == flock0
                  && souls_home(st2, city.slot) == home0
                  && st2.inventory[city.slot].inv.count("wood") == 19
                  && gs2.features.at(st2.cell[city.slot].idx) == FT_City,
              "осёдлый город: число фичи под ним, души дома, склад и ФИЧА "
              "города на клетке");
    }
    CHECK(gs2.features.at(w.ruinCell) == FT_Ruin,
          "РУИНА ОСТАЁТСЯ РУИНОЙ (M-456): поле фич приехало из файла, а не "
          "перевыведено из сида");
    CHECK(gs2.features.at(kLast, kLast) == FT_Road
              && gs2.treeLayer.at(kLast, kLast) == 600
              && gs2.knowledge.data[kWorldCellCap - 1u] == kKnowledgeExplored
              && gs2.cellOwner.data[kWorldCellCap - 1u] == 3u
              && gs2.lightEmission.at(kLast, kLast) == 777
              // След квантуется самим полем — ожидание берётся у исходного
              // мира, а не пересказывается литералом (§8 п.4).
              && scent_strength_at(gs2.scent, kFactionCount - 1, kLast, kLast)
                     == scent_strength_at(w.gs.scent, kFactionCount - 1,
                                          kLast, kLast)
              && scent_strength_at(gs2.scent, kFactionCount - 1, kLast, kLast)
                     > 0u,
          "последняя клетка каждого поля над тором доехала — включая поле "
          "света и владение землёй, которых прежний провод не вёз");
    CHECK(std::strcmp(st2.name[w.lastSlot].text, "Last Slot") == 0
              && st2.inventory[w.lastSlot].inv.count("wood") == 1
              && st2.freeCount == w.st.freeCount && st2.freeCount > 0,
          "последний слот хранилища и стопка свободных слотов доехали");
    {
        // ОРДИНАЛ МЕРТВЕЦА НЕ ПЕРЕИЗДАЁТСЯ (19.24): снести носителя старшего
        // из двух ординалов и родить нового — эмитент приехал с хранилищем.
        const std::uint32_t issuer = st2.nextSpawnId;
        CHECK(issuer == w.st.nextSpawnId && issuer > w.lordOrd,
              "эмитент ординалов приехал своим значением");
        store_death(st2, dead);
        const MacroHandle born = store_birth(st2);
        CHECK(st2.valid(born) && born.id >= issuer && born.id > w.deadOrd,
              "рождение после загрузки не берёт ординал мертвеца");
    }

    // ── 4. ОТКАЗЫ ───────────────────────────────────────────────────────────
    const std::vector<unsigned char> head = file_head(path, 4096);
    CHECK_OR_RETURN(head.size() == 4096, "голова слота прочитана");
    World scratch;   // черновики: при отказе их содержимое не определено
    int refusals = 0, wronglyLoaded = 0;
    const auto refuse = [&](std::vector<unsigned char> bytes,
                            SaveInspectStatus want, const char* why) {
        write_bytes(forged, bytes);
        ++refusals;
        if (load_game(scratch.gs, scratch.st, forged)) ++wronglyLoaded;
        CHECK(inspect_save(forged).status == want, why);
    };
    {
        std::vector<unsigned char> b = head;
        b[0] ^= 0xFFu;
        refuse(b, SaveInspectStatus::Unreadable, "чужая магия — не наш файл");
    }
    {
        std::vector<unsigned char> b = head;
        b[4] = static_cast<unsigned char>(b[4] + 1u);
        refuse(b, SaveInspectStatus::VersionMismatch,
               "чужая версия названа вслух, а не слита с порчей");
    }
    refuse(head, SaveInspectStatus::Unreadable,
           "обрезанный файл с целым заголовком — нечитаем");
    refuse(std::vector<unsigned char>(head.begin(), head.begin() + 6),
           SaveInspectStatus::Unreadable, "файл короче заголовка — нечитаем");
    CHECK(refusals == 4 && wronglyLoaded == 0,
          "ни одна подделка заголовка не загрузилась");
    CHECK(!load_game(scratch.gs, scratch.st, temp_path("timaert_no_such.bin")),
          "слота нет — загрузка отказывает");

    // ЦЕЛЫЙ ПО ДЛИНЕ ФАЙЛ С ЧУЖОЙ МАГИЕЙ И С ЧУЖОЙ ВЕРСИЕЙ. Малые подделки
    // выше отказываются уже длиной — суд самих полей заголовка они не судят
    // (мутация «загрузка не смотрит версию» проходила мимо них зелёной).
    // Здесь меняется ровно один байт настоящего слота, а длина и мир целы.
    {
        const int oldMagic = poke_file_byte(path, 0, 0x00);
        CHECK(oldMagic > 0 && !load_game(scratch.gs, scratch.st, path)
                  && inspect_save(path).status == SaveInspectStatus::Unreadable,
              "слот полной длины с чужой магией не загружается");
        poke_file_byte(path, 0, static_cast<unsigned char>(oldMagic));
        const int oldVer = poke_file_byte(path, 4, 0x00);
        poke_file_byte(path, 4, static_cast<unsigned char>(oldVer + 1));
        CHECK(oldVer >= 0 && !load_game(scratch.gs, scratch.st, path)
                  && inspect_save(path).status
                         == SaveInspectStatus::VersionMismatch,
              "слот полной длины с чужой версией не загружается, и осмотр "
              "называет причину");
        poke_file_byte(path, 4, static_cast<unsigned char>(oldVer));
    }
    // Раскладка другой сборки: размер куска в заголовке не тот.
    {
        const int old = poke_file_byte(path, 8, 0x55);
        CHECK(old >= 0 && !load_game(scratch.gs, scratch.st, path)
                  && inspect_save(path).status == SaveInspectStatus::Unreadable,
              "заголовок с чужим размером состояния отказывается до первого "
              "байта мира");
        poke_file_byte(path, 8, static_cast<unsigned char>(old));
    }
    // ПОРЧА ОДНОГО БАЙТА МИРА — В НАЧАЛЕ, В СЕРЕДИНЕ И В САМОМ КОНЦЕ ФАЙЛА.
    {
        const long total = long(save_file_bytes());
        const long spots[] = {64, 64 + long(sizeof(GameState)) / 2,
                              64 + long(sizeof(GameState)),   // 1-й Б стора
                              total - 1};
        int corrupted = 0, slipped = 0;
        for (const long at : spots) {
            const int old = poke_file_byte(path, at, 0);
            const unsigned char bad =
                static_cast<unsigned char>(old == 0xA5 ? 0x5A : 0xA5);
            poke_file_byte(path, at, bad);
            ++corrupted;
            if (load_game(scratch.gs, scratch.st, path)) ++slipped;
            poke_file_byte(path, at, static_cast<unsigned char>(old));
        }
        CHECK(corrupted == 4 && slipped == 0,
              "порча одного байта в любом месте мира — начало состояния, его "
              "середина, первый и последний байт стора — не загружается");
        CHECK(load_game(scratch.gs, scratch.st, path)
                  && std::memcmp(&w.gs, &scratch.gs, sizeof(GameState)) == 0
                  && std::memcmp(&w.st, &scratch.st, sizeof(MacroStore)) == 0,
              "возвращённые байты снова дают тот же мир — отказывала порча, "
              "а не сама дверь (негативный контроль отказов)");
    }

    // ── 5. НЕУДАВШАЯСЯ ЗАПИСЬ И `.bak` ──────────────────────────────────────
    {
        const std::string nowhere =
            temp_path("timaert_no_such_dir/deeper/slot.bin");
        CHECK(!save_game(w.gs, w.st, nowhere),
              "запись в несуществующий каталог отказывает вслух");
        const std::uint64_t first = header_checksum(path);
        w.gs.worldTime.tick += 64;   // мир шагнул — второй слот другой
        CHECK(save_game(w.gs, w.st, path), "вторая запись в тот же слот");
        CHECK(header_checksum(path) != first
                  && header_checksum(path + ".bak") == first
                  && std::uint64_t(file_bytes(path + ".bak"))
                         == save_file_bytes(),
              "прежний слот уехал в `.bak` целым, новый несёт новый мир");
        CHECK(file_bytes(path + ".tmp") < 0,
              "временного файла после удавшейся записи не осталось");
    }

    remove_slot(path);
    remove_slot(forged);
}

} // namespace

int main() {
    test_checksum_laws();
    test_dump_roundtrip();
    return sm::test::report("save_roundtrip_test");
}
