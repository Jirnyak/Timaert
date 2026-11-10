#include "macro/save.h"
#include "macro/save_stream.h"
#include "macro/world_fields.h"
#include "bus/world_row.h"   // область существ единого контейнера (M-71)
#include "macro/deposit_layer.h"
#include "macro/state.h"
#include "macro/stacks.h"       // капы штабелей — один источник (перепись)
#include "macro/macro_snapshot.h"
#include "macro/characters.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sm {
namespace {

constexpr std::uint32_t kMagic = 0x534D5341u; // 'SMSA'
constexpr std::uint32_t kChecksumSeed = 2166136261u;
constexpr std::uint32_t kChecksumPrime = 16777619u;
constexpr std::uint32_t kMaxInventoryStacks =
    std::uint32_t(kMaxInventorySlots);
// (Кап меток жил здесь литералом 16384 и уехал к самому списку —
//  `kMaxMarkers@src/macro/markers.h`: кап у списка один, и провод читает его
//  оттуда же, откуда дверь постановки.)
// (Капы квестов kMaxQuests/kMaxQuestParts 4096×4096 УМЕРЛИ 2026-10-23,
//  ломоть 3 M-72 вариант А: блок квестов — 1.45 ГиБ потолка мимо шести
//  родов (§10) — снесён вместе с машиной; цель игрока едет родом 2.)
// (the field caps live with the rows: macro/world_fields.cpp)
// (kMaxSoldiers 8192 died with v97; с v110 существа едут внутри инвентаря —
// один кап слотов, один дом: kMaxInventoryStacks.)
// The macro-ECS snapshot (v23): one record per living macro NPC. v127: кап
// записей = КАП ХРАНИЛИЩА (тогда `kMacroEntityCap`, 2^15) — прежний
// `kWorldSquads` (16384) отказывал файлу на мире, который store законно
// держал (названное расхождение «сейв 16384 против хранилища 32768»). v129:
// хранилище и сейв стоят на ЕДИНОМ капе 16384 (`kUnifiedCap@src/core/caps.h`,
// вердикт 2026-10-06), равенство «кап записей = кап хранилища» держится
// ИМЕНЕМ, второго числа нет.
constexpr std::uint32_t kMaxMacroNpcs = std::uint32_t(kUnifiedCap);
constexpr std::uint32_t kHeaderBytes = 4u + 4u + 8u + 4u;

// ── ПОТОЛОК PAYLOAD — ФАКТ О РАСКЛАДКЕ, А НЕ ЧИСЛО ───────────────────────
//
// Было: литерал 64 МиБ с потолка. Он МЕНЬШЕ суммы капов блоков, которые сейв
// обязан вместить, — легальный мир на капе `save_game` отвергал молча, а
// форма данных под него уже подгонялась (следы писались разреженно «чтобы не
// пробить», write_scent_channel ниже). Вердикт владельца 2026-09-25:
// «вывести kMaxPayloadBytes из суммы капов блоков на сборке, а не литералом».
//
// СЛАГАЕМОЕ = КАП БЛОКА × БАЙТЫ ЕГО ЗАПИСИ, по тем же константам, которые
// читает write_payload, — второго списка капов не заводится (DOD п.6). Байты
// записи считаются по ПРИМИТИВАМ, которыми блок пишется, а не по `sizeof`
// структуры: на провод едут поля, а паддинг состоянием не является.
// Разреженный блок входит своим ПЛОТНЫМ капом: сторож меряет худший
// ЛЕГАЛЬНЫЙ мир, а не типичный, и разреженность остаётся решением ФОРМАТА
// ФАЙЛА (DOD п.7), а не следствием потолка.
//
// ЧЕГО В СУММЕ НЕТ, И ЭТО НАЗВАНО ВСЛУХ: ТЕЛО ОДНОЙ СТРОКИ. У строки нет
// «байтов записи» — она переменной длины, и единственный её потолок это
// `savefmt::kMaxStringBytes` (1 МиБ на штуку, сам взят с потолка). Строковый
// слот на проводе остался ОДИН — метка времени слота, и состоянием мира она
// не является (ПЕРЕСЧЁТ 2026-11-10, v159: имя игрока стало плоской ячейкой и
// вошло в сумму телом; v158: слотов было два, до него 32 770 и ≈32.0 ГиБ — то
// есть сторож, который не сторож). СТРОК СОСТОЯНИЯ МИРА НА ПРОВОДЕ НОЛЬ.
// Сумма ограничивает ПЛОСКУЮ раскладку (её пробить нельзя по построению), а
// тело метки времени ловит тот же сторож в рантайме, в `save_game`.
constexpr std::uint64_t kCountBytes = sizeof(std::uint32_t);   // Writer::count
constexpr std::uint64_t kStrBytes = sizeof(std::uint32_t);     // только префикс

// Стак единого контейнера: адрес слота + строка ItemRef (M-71 — существа
// едут теми же слотами, поэтому кап один).
constexpr std::uint64_t kItemRefBytes =
    sizeof(ItemRef::def) + sizeof(ItemRef::material) + sizeof(ItemRef::level)
    + sizeof(ItemRef::count) + sizeof(ItemRef::seed)
    + sizeof(ItemRef::entityId) + sizeof(ItemRef::affixRow)
    + sizeof(ItemRef::affixValue);
constexpr std::uint64_t kInventoryBytes =            // write_inventory
    kCountBytes + std::uint64_t(kMaxInventoryStacks)
    * (sizeof(std::uint16_t) + kItemRefBytes);
constexpr std::uint64_t kEquipmentBytes =            // write_equipment
    sizeof(Gear::has) + kCountBytes
    + std::uint64_t(kEquipCells)
          * (sizeof(std::uint16_t) + sizeof(std::uint16_t));

// Скаляры мира — один экземпляр, названное исключение переписи штабелей.
constexpr std::uint64_t kPrefixBytes =
    sizeof(GameState::worldSeed) + sizeof(GameState::mapW)
    + sizeof(GameState::mapH) + sizeof(GameState::mapParams)
    + sizeof(GameState::cityCountTarget) + sizeof(GameState::worldTime)
    + sizeof(GameState::lastWorldRebakeDay)
    + sizeof(std::uint32_t)   // nextSpawnId стора — эмитент едет с миром (M-220)
    + kStrBytes;              // метка времени слота (имя сейва умерло в v157)
    // (Скаляр v116 «ординал носителя флажка» умер в v117 — флаг едет
    // колонкой playerFlag записи снапшота, kMacroNpcBytes считает его там.)

constexpr std::uint64_t kPlayerBytes =               // write_player
    sizeof(PlayerState::name) + sizeof(PlayerState::ageDays)
    + sizeof(PlayerState::codexUnlockedBits)
    + kCountBytes
      + std::uint64_t(PlayerState::kJournalFactsCap) * sizeof(WorldFact)
    + sizeof(PlayerState::journalSeenSeq) + sizeof(PlayerState::journalFull);

// (Блок строк мест УМЕР в v128, ломтик F: место целиком — запись его ТЕЛА в
// блоке макро-сквадов, второго штабеля сущностей в сейве больше нет.)

// Метка — плоская запись без дыр (`static_assert` у `Marker`), поэтому байты
// записи на проводе и есть её `sizeof`: блок едет счётом и записями целиком.
constexpr std::uint64_t kMarkersBlockBytes =         // write_markers
    kCountBytes + std::uint64_t(kMaxMarkers) * sizeof(Marker);

// РОД 6 целиком одним pod: строки фракций + пул феодальных рёбер + счёт
// (v121; рантайм-имена хвоста уничтожены вердиктом — строк в блоке ноль).
constexpr std::uint64_t kFactionsBlockBytes =        // write_factions
    sizeof(FactionState);

// (Слагаемое `kSubStateBytes` умерло в v157 вместе с блоком: режим оболочки
// не есть мир и в сейв не едет — M-190.)

// Сквады — гладкий массив макро-энтити (ЗАКОН ГЛАДКОЙ ПАМЯТИ). Кап — тот же
// единый `kUnifiedCap`, что и у колонок мира.
constexpr std::uint64_t kSpellBookBytes =            // write_spell_book
    sizeof(std::int32_t)                             // конверт ёмкости
    + sizeof(MacroNpcRecord::book.learned)
    + sizeof(MacroNpcRecord::book.sustained)
    + sizeof(MacroNpcRecord::book.activeSpell)
    + sizeof(MacroNpcRecord::book.sustainedDrainCarry);
constexpr std::uint64_t kSheetBytes =                // лист опционален (v90)
    sizeof(MacroNpcRecord::sheet.attributes)
    + sizeof(MacroNpcRecord::sheet.skills)
    + sizeof(MacroNpcRecord::sheet.levelData);   // маска перков снесена (M-252)
constexpr std::uint64_t kMacroNpcBytes =             // write_macro_npc
    sizeof(MacroNpcRecord::spawnId) + sizeof(MacroNpcRecord::cell)
    + sizeof(MacroNpcRecord::kind)   // поле `sub` умерло 2026-11-02: доля в runtime
    + sizeof(MacroNpcRecord::pools)   // поле `level` умерло: M-246а
    + sizeof(MacroNpcRecord::runtime)
    + sizeof(MacroNpcRecord::goal)    // v145: цель сквада — колонка (M-1)
    + sizeof(MacroNpcRecord::traits)
    + sizeof(MacroNpcRecord::character) + sizeof(MacroNpcRecord::name)
    + sizeof(MacroNpcRecord::orders)
    + kSpellBookBytes
    + kSheetBytes   // флаг `hasSheet` умер: M-246а
    + sizeof(MacroNpcRecord::hasOrders) + sizeof(MacroNpcRecord::dead)
    + sizeof(MacroNpcRecord::playerFlag)   // 5б: флажок игрока — колонкой
    + sizeof(MacroNpcRecord::designOrdinal)
    + kInventoryBytes + kEquipmentBytes
    + sizeof(MacroNpcRecord::upkeepNeedDebt)
    + sizeof(MacroNpcRecord::upkeepWageDebt)
    + sizeof(MacroNpcRecord::wellbeing)    // v127: колонка анкеты (M-90)
    + sizeof(MacroNpcRecord::interests);   // v127: связи любых сквадов
constexpr std::uint64_t kMacroNpcsBlockBytes =
    kCountBytes + std::uint64_t(kMaxMacroNpcs) * kMacroNpcBytes;

constexpr std::uint64_t kRhythmBytes =               // v24, поле за полем
    sizeof(WorldTickRuntime::pendingDailyTicks)
    + sizeof(WorldTickRuntime::nextDailyTickDay)
    + sizeof(WorldTickRuntime::subworldStepRemainder)
    + sizeof(WorldTickRuntime::jitter.state)
    + sizeof(MacroAiRhythm::jitter.state)
    + sizeof(GameState::playerRoll.state);     // v153 (M-378)

constexpr std::uint64_t kFactBytes =                 // write_fact
    sizeof(WorldFact::day) + sizeof(WorldFact::seq) + sizeof(WorldFact::kind)
    + sizeof(WorldFact::subjectKind) + sizeof(WorldFact::objectKind)
    + sizeof(WorldFact::subject) + sizeof(WorldFact::object)
    + sizeof(WorldFact::x) + sizeof(WorldFact::y) + sizeof(WorldFact::amount);
constexpr std::uint64_t kChronicleBlockBytes =       // write_chronicle
    sizeof(Chronicle::nextSeq) + sizeof(Chronicle::countingDay)
    + sizeof(Chronicle::factsToday) + sizeof(Chronicle::factsPeak)
    // v138, M-184: ярус АННАЛОВ снесён — ушли и его байт `annalsFull`, и его
    // блок (`kCountBytes + kChronicleAnnals × kFactBytes` = 32 МиБ потолка).
    + kCountBytes + std::uint64_t(kChronicleFacts) * kFactBytes;

// Следы: ПЛАН НА ФРАКЦИЮ (scent_field.h — `[f × кап клеток + клетка]`), два
// канала, клетка на проводе = {индекс, значение}. Плотный кап планов строк
// реестра, хотя пишется разреженно: см. шапку выше про формат файла.
constexpr std::uint64_t kScentBlockBytes =           // write_scent
    sizeof(ScentField::w) + sizeof(ScentField::h)
    + 2ull * (kCountBytes
              + std::uint64_t(kFactionCount) * std::uint64_t(kWorldCells)
                * (sizeof(std::uint32_t) + sizeof(std::uint16_t)));

// (Слагаемое `kLogicNodesBlockBytes` умерло в v157 вместе с обоими списками
// logic-узлов — ломоть 1 сейв-эпика.)

// (Слагаемые kObjectiveBytes/kRewardBytes/kEventBytes/kQuestBytes/
// kQuestsBlockBytes умерли с блоком квестов — ломоть 3 M-72, вариант А.)
//
// `kInventoryBytes` УШЛА ИЗ СУММЫ 2026-11-09 (M-439), ПЕРЕЖИВ СВОЙ БЛОК, И ЭТО
// БЫЛО СЛАГАЕМОЕ-СИРОТА. Её блоком был пул дезертиров `GameState::deserterPool`,
// уничтоженный в v131; блока в `write_payload` у неё не осталось НИ ОДНОГО, то
// есть 43 012 Б сторожили то, чего писатель не пишет. Опасна была не переплата,
// а ЛОЖЬ: слагаемое без блока делало неверной прямую претензию абзаца ниже
// («порядок слагаемых — порядок блоков»), а она и есть единственная опора
// глазного контроля. Сама константа ЖИВА и нужна `kMacroNpcBytes` выше —
// инвентарь едет ВНУТРИ записи сквада, и это его законный кап.

// ПОРЯДОК СЛАГАЕМЫХ — ПОРЯДОК БЛОКОВ write_payload. Он держится глазами, а
// не компилятором, и это сказано вслух: пропущенное слагаемое занизит
// потолок, а не сломает сборку. Прибор на это — свидетель в
// save_roundtrip_test: реальный сейв обязан быть строго НИЖЕ суммы.
constexpr std::uint64_t kMaxPayloadBytes =
      kPrefixBytes
    + kPlayerBytes
    + kMarkersBlockBytes
    + kFactionsBlockBytes
    + kWorldFieldsMaxBytes       // macro/world_fields.h, столбец таблицы рядов
    + kMacroNpcsBlockBytes
    + kRhythmBytes
    + kChronicleBlockBytes
    + kScentBlockBytes;

// Вывод не смеет опустить порог ниже прежнего литерала — иначе правка была
// бы регрессом, а не выводом.
static_assert(kMaxPayloadBytes >= 64ull * 1024ull * 1024ull,
              "выведенный потолок payload ниже прежних 64 МиБ");
// Заворот u64 на сложении сделал бы сумму МЕНЬШЕ своего крупнейшего
// слагаемого. `SaveHeader::payloadSize` — u64, так что расти потолку есть
// куда: сегодняшняя сумма стоит ~3048 МиБ ниже границы u32 (ПЕРЕСЧЁТ
// 2026-11-10, v159: ~1047.60 МиБ — запись метки 104 → 68 Б, имя игрока вошло
// ячейкой; v158: ~1048.16 МиБ — блок меток вошёл телами записей, +1.36
// МиБ; v157: ~1046.80 МиБ после сноса ломтя 1; до него ~1078.88, а в эпоху
// мёртвых штабелей квестов и мест — 4072.27 МиБ и зазор «~25 МБ»).
// Запас больше не на один поднятый кап, но вывод цел: граница u32 здесь не
// закон, потому что поле длины шире её.
static_assert(kMaxPayloadBytes > kMacroNpcsBlockBytes,
              "сумма капов блоков завернулась в uint64");

struct SaveHeader {
    std::uint32_t magic = 0;
    std::int32_t version = 0;
    std::uint64_t payloadSize = 0;
    std::uint32_t checksum = 0;
};

// The stream primitives live in macro/save_stream.h so the world-field
// registry rows can serialize themselves; this file keeps the ORDER.
using savefmt::Writer;
using savefmt::Reader;
using savefmt::read_count;

std::uint32_t checksum32(const std::uint8_t* data, std::size_t n) {
    std::uint32_t h = kChecksumSeed;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= kChecksumPrime;
    }
    return h;
}



// (write_bool/read_bool умерли с сериализаторами квестов — ломоть 3 M-72:
//  других читателей булевого провода не было.)

std::string current_timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    if (seconds == std::time_t(-1)) return {};

    std::tm tm{};
#if defined(_WIN32)
    if (gmtime_s(&tm, &seconds) != 0) return {};
#else
    if (gmtime_r(&seconds, &tm) == nullptr) return {};
#endif

    char buf[25];
    const int n = std::snprintf(buf, sizeof(buf),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms.count()));
    if (n <= 0 || n >= int(sizeof(buf))) return {};
    return std::string(buf, static_cast<std::size_t>(n));
}


template <class Enum>
void write_enum8(Writer& w, Enum v) {
    const auto raw = static_cast<std::uint8_t>(v);
    w.pod(raw);
}

template <class Enum>
bool read_enum8(Reader& r, Enum& out, std::uint8_t maxValue) {
    std::uint8_t raw = 0;
    r.pod(raw);
    if (!r.ok || raw > maxValue) {
        r.ok = false;
        return false;
    }
    out = static_cast<Enum>(raw);
    return true;
}

bool file_exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long len = std::ftell(f);
    if (len < 0 || static_cast<std::uint64_t>(len) > kHeaderBytes + kMaxPayloadBytes) {
        std::fclose(f);
        return false;
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(len));
    const bool ok = out.empty()
        || std::fread(out.data(), 1, out.size(), f) == out.size();
    const bool closeOk = std::fclose(f) == 0;
    return ok && closeOk;
}

bool parse_header(const std::vector<std::uint8_t>& file, SaveHeader& h) {
    if (file.size() < kHeaderBytes) return false;
    Reader r{file.data(), file.size()};
    r.pod(h.magic);
    r.pod(h.version);
    r.pod(h.payloadSize);
    r.pod(h.checksum);
    if (!r.ok) return false;
    if (h.payloadSize > kMaxPayloadBytes) return false;
    if (h.payloadSize != static_cast<std::uint64_t>(file.size() - kHeaderBytes)) return false;
    return true;
}

bool checksum_matches(const std::vector<std::uint8_t>& file, const SaveHeader& h) {
    const auto* payload = file.data() + kHeaderBytes;
    const auto n = static_cast<std::size_t>(h.payloadSize);
    return checksum32(payload, n) == h.checksum;
}

bool write_file(const std::string& path, const SaveHeader& h,
                const std::vector<std::uint8_t>& payload) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    bool ok = true;
    ok = ok && std::fwrite(&h.magic, sizeof(h.magic), 1, f) == 1;
    ok = ok && std::fwrite(&h.version, sizeof(h.version), 1, f) == 1;
    ok = ok && std::fwrite(&h.payloadSize, sizeof(h.payloadSize), 1, f) == 1;
    ok = ok && std::fwrite(&h.checksum, sizeof(h.checksum), 1, f) == 1;
    if (!payload.empty()) {
        ok = ok && std::fwrite(payload.data(), 1, payload.size(), f) == payload.size();
    }
    ok = ok && std::fflush(f) == 0;
    ok = ok && std::ferror(f) == 0;
    const bool closeOk = std::fclose(f) == 0;
    return ok && closeOk;
}

bool verify_file(const std::string& path, std::uint64_t payloadSize,
                 std::uint32_t checksum) {
    std::vector<std::uint8_t> file;
    if (!read_file(path, file)) return false;
    SaveHeader h{};
    if (!parse_header(file, h)) return false;
    return h.magic == kMagic
        && h.version == kSaveVersion
        && h.payloadSize == payloadSize
        && h.checksum == checksum
        && checksum_matches(file, h);
}

bool atomic_replace(const std::string& path, const SaveHeader& h,
                    const std::vector<std::uint8_t>& payload) {
    const std::string tmp = path + ".tmp";
    const std::string bak = path + ".bak";
    std::remove(tmp.c_str());

    if (!write_file(tmp, h, payload)) {
        std::remove(tmp.c_str());
        return false;
    }
    if (!verify_file(tmp, h.payloadSize, h.checksum)) {
        std::remove(tmp.c_str());
        return false;
    }

    const bool hadFinal = file_exists(path);
    if (hadFinal) {
        if (file_exists(bak) && std::remove(bak.c_str()) != 0) {
            std::remove(tmp.c_str());
            return false;
        }
        if (std::rename(path.c_str(), bak.c_str()) != 0) {
            std::remove(tmp.c_str());
            return false;
        }
    }

    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        if (hadFinal) std::rename(bak.c_str(), path.c_str());
        return false;
    }
    return true;
}

// The spellbook's cooldowns are STEPS now (core/time.h), so the map they ride
// in is integer. Kept beside its float twin rather than templated: two callers,
// two plain functions, and a reader can see exactly what lands on disk.
// v110 (слияние M-71): стак едет С АДРЕСОМ СЛОТА. Позиция — носитель закона:
// область существ растёт сверху вниз, и её порядок ЕСТЬ закон ходока «свежие
// уходят первыми» — восстановление через add_ref перевернуло бы очередь
// дезертирства молча. Файл несёт занятые слоты с их индексами, читатель
// кладёт байты РОВНО туда же.
void write_inventory(Writer& w, const Inventory& inv) {
    if (!w.count(std::size_t(inv.used_slots()), kMaxInventoryStacks)) return;
    for (int i = 0; i < kMaxInventorySlots; ++i) {
        const ItemRef& s = inv.slots[std::size_t(i)];
        if (s.empty()) continue;
        // Fail-closed НА ЗАПИСИ (закон бывшего write_squad): строка, которой
        // нет ни в предметном каталоге, ни в каталоге существ, — порча, и
        // файл отказывается, а не увозит призрака.
        // ВОПРОС ЗДЕСЬ АДРЕСНЫЙ, И ОН УЖЕ ИМЕЕТ СВОЮ ДВЕРЬ (упрощение M-295).
        // Здесь стояла развилка по половине пространства с переспросом
        // `item_def_at(...) != nullptr` в предметной ветке — а `world_row_is_item`
        // И ЕСТЬ «ординал ниже размера каталога», то есть ветка проверяла ровно
        // то, что уже проверила сама дверь. Один вопрос — одна дверь.
        const bool knownRow = world_row_exists(s.def);
        if (!knownRow) {
            w.ok = false;
            return;
        }
        w.pod(std::uint16_t(i));
        w.pod(s.def);
        w.pod(s.material);
        w.pod(s.level);
        w.pod(s.count);
        w.pod(s.seed);
        w.pod(s.entityId);
        w.pod(s.affixRow);
        w.pod(s.affixValue);
    }
}

// The worn cells, by CELL INDEX — a flat array with holes, exactly like the
// bag, so the file carries what is there and not 480 cells of nothing.
// M-183: ячейка везёт ИНДЕКС слота инвентаря (истина вещи — сам инвентарь,
// он уже уехал своим блоком выше); маска тела едет целиком — она и есть
// экземплярный «редактор тел».
void write_equipment(Writer& w, const Gear& g) {
    w.pod(g.has);
    if (!w.count(std::size_t(worn_cells(g)), std::uint32_t(kEquipCells))) {
        return;
    }
    for (int i = 0; i < kEquipCells; ++i) {
        const std::uint16_t v = g.worn[std::size_t(i)];
        if (v >= kWornBlocked) continue;   // пусто/блокер: блоки производные
        const std::uint16_t cell = std::uint16_t(i);
        w.pod(cell);
        w.pod(v);
    }
}

// Блоки двуручников НЕ читаются — их пересчитывает читатель записи
// (remark_gear_blocks) от инвентаря, прочитанного тем же блоком записи.
void read_equipment(Reader& r, Gear& g) {
    g = Gear{};
    r.pod(g.has);
    std::uint32_t n = 0;
    if (!read_count(r, n, std::uint32_t(kEquipCells))) return;
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint16_t cell = 0;
        std::uint16_t v = 0;
        r.pod(cell);
        r.pod(v);
        if (cell >= std::uint16_t(kEquipCells)
            || v >= std::uint16_t(kMaxInventorySlots)) {
            r.ok = false;
            return;
        }
        g.worn[std::size_t(cell)] = v;
    }
}

void read_inventory(Reader& r, Inventory& inv) {
    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxInventoryStacks)) return;
    inv.clear();
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::uint16_t slot = 0;
        r.pod(slot);
        ItemRef s{};
        r.pod(s.def);
        r.pod(s.material);
        r.pod(s.level);
        r.pod(s.count);
        r.pod(s.seed);
        r.pod(s.entityId);
        r.pod(s.affixRow);
        r.pod(s.affixValue);
        if (!r.ok) break;
        // A row the world does not know, a stack of nothing, an address
        // outside the container or a doubly-written slot is a save from a
        // different game — refuse it out loud rather than carry a phantom.
        // Строка существа легальна (M-71): она живёт за предметным
        // каталогом, но внутри world_row_count().
        // ВОПРОС ЗДЕСЬ АДРЕСНЫЙ, И ОН УЖЕ ИМЕЕТ СВОЮ ДВЕРЬ (упрощение M-295).
        // Здесь стояла развилка по половине пространства с переспросом
        // `item_def_at(...) != nullptr` в предметной ветке — а `world_row_is_item`
        // И ЕСТЬ «ординал ниже размера каталога», то есть ветка проверяла ровно
        // то, что уже проверила сама дверь. Один вопрос — одна дверь.
        const bool knownRow = world_row_exists(s.def);
        if (!knownRow || s.count <= 0
            || slot >= std::uint16_t(kMaxInventorySlots)) {
            r.ok = false;
            return;
        }
        // Инварианты области существ (бывший read_squad): уровень живой,
        // душа с историей — ровно одна.
        if (world_row_is_creature(s.def)
            && (s.level == 0 || (s.entityId != 0 && s.count != 1))) {
            r.ok = false;
            return;
        }
        // Адресная дверь загрузки (M-296): она же и проверяет «дважды
        // записанный слот» — условие, которое здесь стояло руками выше, —
        // и она же сводит контейнер, поэтому отдельного пересчёта после
        // чтения не нужно. Запись слота снаружи больше не собирается.
        if (!inv.put_at(int(slot), s)) {
            r.ok = false;
            return;
        }
    }
}
// (write_squad/read_squad умерли слиянием M-71: существа едут внутри
// write_inventory — область существ со своими адресами слотов.)

void write_spell_book(Writer& w, const SpellBook& spellBook) {
    // v89: the ENVELOPE is written first (the owner's 256), so a book saved
    // against a different capacity is refused loudly instead of sliding bit
    // planes; ordinals inside the planes are append-only registry rows.
    // v83: no per-spell cooldown cell — recovery is the BODY's one gate
    // (session-scene state, never saved), so the book carries only knowledge.
    w.pod(std::int32_t(kSpellBookCapacity));
    for (const std::uint64_t word : spellBook.learned) w.pod(word);
    for (const std::uint64_t word : spellBook.sustained) w.pod(word);
    w.pod(spellBook.activeSpell);
    w.pod(spellBook.sustainedDrainCarry);
}

void read_spell_book(Reader& r, SpellBook& spellBook) {
    std::int32_t cap = 0;
    r.pod(cap);
    if (!r.ok || cap != kSpellBookCapacity) { r.ok = false; return; }
    for (std::uint64_t& word : spellBook.learned) r.pod(word);
    for (std::uint64_t& word : spellBook.sustained) r.pod(word);
    r.pod(spellBook.activeSpell);
    r.pod(spellBook.sustainedDrainCarry);
}

// One macro NPC of the ECS snapshot (v23, macro/macro_snapshot.h). The POD
// components ride verbatim — any layout change to them is a save-format
// change and pays a kSaveVersion bump, the same discipline Skills already
// lives under.
void write_macro_npc(Writer& w, const MacroNpcRecord& m) {
    // A macro entity may be a man or a beast: the avatar of a wolf pack is a
    // squad avatar like any lord (CANON.md S4/S16). What is refused is a kind
    // that names NO row at all — that is a corrupt entity, not a monster.
    if (!valid_npc_kind(m.kind.type)) {
        w.ok = false;
        return;
    }
    w.pod(m.spawnId);
    w.pod(m.cell);   // v86: ОДНО число — клетка сквада (MacroCell)
    w.pod(m.kind);
    w.pod(m.pools);
    w.pod(m.runtime);
    w.pod(m.goal);   // v145: цель сквада — колонка рода 2 (M-1)
    w.pod(m.traits);
    w.pod(m.character);
    w.pod(m.name);   // v119: имя анкеты — колонка (вердикт 3)
    w.pod(m.orders);
    write_spell_book(w, m.book);   // v89: knowledge is the body's (§41 root 3)
    // АНКЕТА ЕДЕТ БЕЗУСЛОВНО (M-246а): опт-ин `hasSheet` умер. Он был
    // мёртвым флагом с флипа колонок — снимок ставил его 1 ВСЕГДА
    // (macro_snapshot.cpp), потому что колонка листа есть у каждого слота,
    // и ветка «листа нет» была недостижима. Держать уровень и опыт внутри
    // опционального блока после их переезда в анкету значило бы сделать
    // ОБЯЗАТЕЛЬНУЮ величину мира условной.
    w.pod(m.sheet.attributes);
    w.pod(m.sheet.skills);
    w.pod(m.sheet.levelData);   // v100: grew perkPoints (third K, S14)
    w.pod(m.hasOrders);
    w.pod(m.dead);
    w.pod(m.playerFlag);   // 5б: «этот сквад — игрок», колонка анкеты
    w.pod(m.designOrdinal);   // v92: строка стола анкет, −1 у обычных
    write_inventory(w, m.inventory);   // v110: существа едут здесь (M-71)
    write_equipment(w, m.gear);
    w.pod(m.upkeepNeedDebt);   // v105: счёт содержания контейнера
    w.pod(m.upkeepWageDebt);
    w.pod(m.wellbeing);   // v127: благополучие — колонка анкеты (M-90 флип)
    w.pod(m.interests);   // v127: связи любых сквадов (M-90 флип)
}

void read_macro_npc(Reader& r, MacroNpcRecord& m) {
    r.pod(m.spawnId);
    r.pod(m.cell);   // v86
    r.pod(m.kind);
    r.pod(m.pools);
    r.pod(m.runtime);
    r.pod(m.goal);   // v145
    r.pod(m.traits);
    r.pod(m.character);
    r.pod(m.name);   // v119
    r.pod(m.orders);
    read_spell_book(r, m.book);    // v89
    if (!r.ok) return;
    r.pod(m.sheet.attributes);     // M-246а: безусловно, `hasSheet` умер
    r.pod(m.sheet.skills);
    r.pod(m.sheet.levelData);      // v100
    r.pod(m.hasOrders);
    r.pod(m.dead);
    r.pod(m.playerFlag);   // 5б
    r.pod(m.designOrdinal);   // v92
    if (!r.ok) return;
    if (m.kind.type >= std::uint16_t(NPCType::Count)
        || m.hasOrders > 1 || m.dead > 1 || m.playerFlag > 1
        // v92: ординал стола анкет обязан называть живую строку или −1 —
        // каталожный закон (только аппенд) делает иное порчей файла.
        || m.designOrdinal < -1 || m.designOrdinal >= kDesignCharacterCount) {
        r.ok = false;
        return;
    }
    // The READ order is the WRITE order, field for field. It has to be said
    // out loud because these two functions are a hundred lines apart and a
    // block inserted into one of them reads the next block's bytes.
    read_inventory(r, m.inventory);   // v110: существа едут здесь (M-71)
    read_equipment(r, m.gear);
    remark_gear_blocks(m.gear, m.inventory);
    r.pod(m.upkeepNeedDebt);   // v105
    r.pod(m.upkeepWageDebt);
    r.pod(m.wellbeing);   // v127
    r.pod(m.interests);   // v127
}

// Written OLDEST FIRST, so the file carries a past and not a ring's seam: a
// reader never has to know where the head was when the game was saved.
void write_fact(Writer& w, const WorldFact& f) {
    w.pod(f.day);
    w.pod(f.seq);
    w.pod(f.kind);
    w.pod(f.subjectKind);
    w.pod(f.objectKind);
    w.pod(f.subject);
    w.pod(f.object);
    w.pod(f.x);
    w.pod(f.y);
    w.pod(f.amount);
    // `nextInCell` is deliberately absent: it is derived, and the load
    // rebuilds every chain from the facts themselves.
}

void read_fact(Reader& r, WorldFact& f) {
    f = WorldFact{};
    r.pod(f.day);
    r.pod(f.seq);
    r.pod(f.kind);
    r.pod(f.subjectKind);
    r.pod(f.objectKind);
    r.pod(f.subject);
    r.pod(f.object);
    r.pod(f.x);
    r.pod(f.y);
    r.pod(f.amount);
}

void write_chronicle(Writer& w, const Chronicle& c) {
    w.pod(c.nextSeq);
    w.pod(c.countingDay);
    w.pod(c.factsToday);
    w.pod(c.factsPeak);          // v135: пик дня — величина формулы кольца

    // The ring's LIVE facts, oldest first, so the load can replay them in the
    // order they were written.
    std::uint32_t live = 0;
    if (c.ready()) {
        for (std::uint32_t seq = c.oldest_seq(); seq < c.nextSeq; ++seq) {
            if (c.ring[(seq - 1u) % kChronicleFacts].seq == seq) ++live;
        }
    }
    if (w.count(std::size_t(live), kChronicleFacts)) {
        for (std::uint32_t seq = c.oldest_seq(); seq < c.nextSeq; ++seq) {
            const WorldFact& f = c.ring[(seq - 1u) % kChronicleFacts];
            if (f.seq == seq) write_fact(w, f);
        }
    }
}

// v75: поля следов фракций (macro/scent_field.h, CANON S10 «хищник-жертва»).
// Едут в сейве целиком (вердикт владельца: движение не фактируется, реплеить
// след не из чего). РАЗРЕЖЕННО — потому что планы почти пусты (след живёт на
// дорогах), и нули не стоят мегабайтов: пишутся только ненулевые клетки
// парами {индекс, значение}. Это решение ФОРМАТА ФАЙЛА и только оно (DOD
// п.7 — разреженное законно для ПРОВОДА сейва, живой слой остаётся полем).
// Прежнее обоснование — «плотные планы пробили бы kMaxPayloadBytes» — было
// ложным и перевёрнутым: литерал-потолок диктовал форму данных, чего закон
// не разрешает. Потолок теперь ВЫВЕДЕН и берёт этот блок ПЛОТНЫМ капом, так
// что пробить его разреженная запись не может по построению.
// Канал обходится по планам строк реестра и по клеткам МИРА: хвост за миром
// и планы выше реестра нулевые по построению поля. Индекс на проводе — адрес
// клетки внутри канала (`scent_idx`), тот же, что в памяти.
void write_scent_channel(Writer& w, const ScentField& sf,
                         const std::uint16_t* cells) {
    const std::size_t plane = world_cells_within_cap(sf.w, sf.h);
    const std::uint32_t cap =
        std::uint32_t(std::size_t(kFactionCount) * plane);
    std::size_t live = 0;
    for (int f = 0; f < kFactionCount; ++f) {
        const std::uint16_t* p = cells + std::size_t(f) * kScentPlaneCells;
        for (std::size_t i = 0; i < plane; ++i)
            if (p[i] != 0u) ++live;
    }
    if (!w.count(live, cap)) return;
    for (int f = 0; f < kFactionCount; ++f) {
        const std::size_t base = std::size_t(f) * kScentPlaneCells;
        for (std::size_t i = 0; i < plane; ++i) {
            if (cells[base + i] == 0u) continue;
            w.pod(std::uint32_t(base + i));
            w.pod(cells[base + i]);
        }
    }
}

bool read_scent_channel(Reader& r, const ScentField& sf,
                        std::uint16_t* cells) {
    const std::size_t plane = world_cells_within_cap(sf.w, sf.h);
    const std::uint32_t cap =
        std::uint32_t(std::size_t(kFactionCount) * plane);
    std::uint32_t live = 0;
    if (!read_count(r, live, cap)) return false;
    for (std::uint32_t i = 0; i < live && r.ok; ++i) {
        std::uint32_t idx = 0;
        std::uint16_t v = 0;
        r.pod(idx);
        r.pod(v);
        // Адрес обязан называть клетку МИРА в плане строки РЕЕСТРА — иначе
        // файл положил бы след туда, где его не видит ни одна дверь поля.
        if (!r.ok || std::size_t(idx) / kScentPlaneCells
                         >= std::size_t(kFactionCount)
            || std::size_t(idx) % kScentPlaneCells >= plane) {
            r.ok = false;
            return false;
        }
        cells[idx] = v;
    }
    return r.ok;
}

void write_scent(Writer& w, const ScentField& sf) {
    w.pod(sf.w);
    w.pod(sf.h);
    write_scent_channel(w, sf, sf.strength);
    write_scent_channel(w, sf, sf.wealth);
}

void read_scent(Reader& r, ScentField& sf, int mapW, int mapH) {
    std::int32_t w = 0, h = 0;
    r.pod(w);
    r.pod(h);
    // Мир, сохранённый с ХОЛОДНЫМ полем (фикстура, не знавшая генезиса), —
    // легальный ноль (S6): `scent_ensure` скроит его на первом тике.
    if (r.ok && w == 0 && h == 0) {
        scent_reset(sf, 0, 0);
        std::uint32_t none = 0;
        if (read_count(r, none, 0u)) read_count(r, none, 0u);
        return;
    }
    // Версия гейтит формат — несовпадение измерений здесь значит битый файл,
    // не миграцию.
    if (!r.ok || w != mapW || h != mapH) {
        r.ok = false;
        return;
    }
    scent_reset(sf, mapW, mapH);
    if (!sf.sized_for(mapW, mapH)) { r.ok = false; return; }
    if (!read_scent_channel(r, sf, sf.strength)) return;
    read_scent_channel(r, sf, sf.wealth);
}

void read_chronicle(Reader& r, Chronicle& c) {
    r.pod(c.nextSeq);
    r.pod(c.countingDay);
    r.pod(c.factsToday);
    r.pod(c.factsPeak);          // v135

    std::uint32_t n = 0;
    if (!read_count(r, n, kChronicleFacts)) return;
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        WorldFact f{};
        read_fact(r, f);
        if (f.seq == 0u || f.seq >= c.nextSeq) { r.ok = false; return; }
        if (c.ready()) c.ring[(f.seq - 1u) % kChronicleFacts] = f;
    }
    // The chains are DERIVED, so they are rebuilt rather than restored.
    chronicle_rebuild_links(c);
    // (ЗДЕСЬ ЧИТАЛСЯ БЛОК АННАЛОВ — ярус снесён, M-184. Формат сейва
    //  ограничением не является: версия бампается молча, миграций нет.)
}

void write_player(Writer& w, const PlayerState& p) {
    w.pod(p.name);   // v159: плоская ячейка имени, целиком
    w.pod(p.ageDays);
    // (No position block since v88: WHERE he stands is his squad's MacroCell
    // inside its MacroNpcRecord — подпосадка 4 killed the scalar double,
    // exactly as v85 killed the bars' one.)
    // (No sheet block since v91: his sheet is the owned CharacterSheet
    // component on his squad entity, riding its MacroNpcRecord opt-in like
    // every named character's — посадка Б. Second copy on disk = the same
    // defect the bars' block was.)
    // (No combatStats block since v85: the player's bars ride the macro-ECS
    // snapshot inside his squad's MacroNpcRecord like every lord's — a second
    // copy of the same three bars on disk was the defect landing 4 removed.)
    // (No perk block HERE: since v100 the perk bytes — points in LevelData,
    // the 256-bit mask beside it — ride the owned CharacterSheet inside his
    // squad's MacroNpcRecord, the same door every named character uses.)
    // (No inventory block: his bag is an ordinary NpcInventory on his squad
    // entity, and rides the macro snapshot with every other squad's.)
    // No reputation map: the player's standing is his row in gs.factions, which
    // the faction matrix below already persists (kSaveVersion 16).
    w.pod(p.codexUnlockedBits);   // v63: a bit per article ordinal
    // (No event log block since v58: session messages die with the session,
    // the player's past rides below as his journal of chronicle records.)
    // (No spellbook block since v89: his book is the SpellBook component on
    // his squad entity, riding its MacroNpcRecord like every body's.)
    // (Офферы и счётчики квестов умерли 2026-10-23 — ломоть 3 M-72.)
    // (No possessedMacroSpawnId since v87: "who is controlled" is the honest
    // playerFlag byte on the possessed record itself — one store, the world's.)
    // (No entry-side bytes since v88: the entry context lives in his
    // squad's MacroNpcRuntime, which rides the record verbatim.)
    // v57: the player's journal — copies of the chronicle records he learned
    // (his whole game, never forgotten), plus the reader's own cursor.
    // Хвост журнала за счётом не едет: он нулевой по построению двери поимки.
    if (w.count(p.journalCount, PlayerState::kJournalFactsCap)) {
        for (std::uint32_t i = 0; i < p.journalCount; ++i) w.pod(p.journal[i]);
    }
    w.pod(p.journalSeenSeq);
    w.pod(p.journalFull);
}

// Пишет в состояние, РОЖДЁННОЕ НУЛЁМ (черновик загрузки): хвост журнала за
// прочитанным счётом уже нулевой, чистить его здесь нечем и незачем.
void read_player(Reader& r, PlayerState& p) {
    r.pod(p.name);
    // Ячейка имени обязана кончаться нулём внутри себя — её читают строкой.
    if (!r.ok || p.name.text[sizeof(p.name.text) - 1u] != '\0') {
        r.ok = false;
        return;
    }
    r.pod(p.ageDays);
    r.pod(p.codexUnlockedBits);   // v63
    std::uint32_t jn = 0;             // v57: the journal rides whole
    if (!read_count(r, jn, PlayerState::kJournalFactsCap)) return;
    p.journalCount = 0;
    for (std::uint32_t i = 0; i < jn && r.ok; ++i) {
        r.pod(p.journal[i]);
        if (r.ok) p.journalCount = i + 1u;
    }
    r.pod(p.journalSeenSeq);
    r.pod(p.journalFull);
}

// МЕТКИ КАРТЫ — СЧЁТ И ЗАПИСИ ЦЕЛИКОМ (v158; с v159 запись — клетка, стиль,
// подпись: строкового ключа и float-координат в ней больше нет). Запись плоская и без дыр, так
// что на провод идут её байты как есть — та же форма, что у рода 6 ниже.
// Хвост списка за счётом не едет: он нулевой по построению дверей.
// Писатель судит клетку ТЕМ ЖЕ условием, что читатель: метка вне мира — порча
// состояния, и файл отказывается сейчас, а не кладёт слот, который игра сама
// же не прочтёт.
void write_markers(Writer& w, const MarkerList& list, std::size_t worldCells) {
    if (!w.count(list.count, std::uint32_t(kMaxMarkers))) return;
    for (const Marker& m : list.live()) {
        if (std::size_t(m.cell) >= worldCells) { w.ok = false; return; }
        w.pod(m);
    }
}

// Чтение судит каждую запись: стиль обязан быть строкой перечисления, клетка —
// клеткой ЭТОГО мира, подпись — кончаться нулём внутри своей ячейки, а пара
// (стиль, клетка) — встречаться один раз (её судит сама дверь постановки).
// Запись, не прошедшая суд, — порча файла: загрузка отказывает целиком, а не
// чинит её.
void read_markers(Reader& r, MarkerList& list, std::size_t worldCells) {
    std::uint32_t n = 0;
    if (!read_count(r, n, std::uint32_t(kMaxMarkers))) return;
    // На месте, не временным значением: список весит 1.6 МиБ.
    list.count = 0;
    std::memset(list.rows, 0, sizeof(list.rows));
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Marker m{};
        r.pod(m);
        if (!r.ok) break;
        if (std::uint8_t(m.style) > std::uint8_t(MarkerStyle::Waypoint)
            || std::size_t(m.cell) >= worldCells
            || m.label[kMarkerLabelBytes - 1u] != '\0'
            || !add_marker(list, m.style, m.cell, marker_label(m))) {
            r.ok = false;
            break;
        }
    }
}

// РОД 6 — строки фракций, byte for byte (macro/factions.h). It used to be a
// string-keyed map of string-keyed maps: every faction wrote its id, name,
// description and colour — four verbatim copies of the registry row that
// already declares them — and each PAIR was written twice, once per direction.
// v121: РОД 6 едет ЦЕЛИКОМ одним pod — FactionState тривиально копируем и
// знает свой sizeof (ЗАКОН ФРЕЙМА п.7): строки, пул феодальных рёбер, счёт.
// Второго сериализатора у рода нет.
void write_factions(Writer& w, const FactionState& f) {
    w.pod(f);
}

void read_factions(Reader& r, FactionState& f) {
    f = FactionState{};
    r.pod(f);
    // Скомпилированный реестр — истина id/цвета своих строк: сейв не может
    // «разобъявить» фракцию, с которой игра собрана.
    claim_registry_rows(f);
}
// (write/read_sub_state умерли в v157 с блоком режима оболочки — M-190.)

// (write/read_event, write/read_objective, write/read_reward,
// write/read_quest умерли с блоком квестов — ломоть 3 M-72, вариант А.)

void write_payload(Writer& w, const GameState& s,
                   const std::string& savedAt,
                   std::uint32_t storeNextSpawnId,
                   const std::vector<MacroNpcRecord>& macroNpcs) {
    w.pod(s.worldSeed);
    w.pod(s.mapW);
    w.pod(s.mapH);
    w.pod(s.mapParams);
    w.pod(s.cityCountTarget);
    w.pod(s.worldTime);
    w.pod(s.lastWorldRebakeDay);   // v22: autosave/re-bake phase survives a load
    w.pod(storeNextSpawnId);        // v118/M-220: эмитент ординалов — у стора
    w.str(savedAt);                 // метка слота — читает inspect_save
    // v67: the world's loot pool — the victorless dead's worth, ONE number
    // (CANON S5, the deserter pool's sibling for things).
    // («Кем я на карте» с 5б едет КОЛОНКОЙ playerFlag записи снапшота —
    // свой скаляр v116 в скалярах мира умер, провод получает флаг даром.)
    // (v74's own ship-counter block died in v96: hulls moored are the FEATURE-NUMBER
    // layer's number under the harbour's feature, and that layer rides as a
    // world-field row — so the sort this block needed died with the hash it
    // was sorting.)
    write_player(w, s.player);

    write_markers(w, s.markers, world_cells_within_cap(s.mapW, s.mapH));

    write_factions(w, s.factions);


    // The saved world FIELDS ride as registry rows (macro/world_fields.h):
    // trees, knowledge, deposits, scars — each row writes its own bytes, in
    // row order, byte-identical to the four hand blocks that lived here.
    // Adding a per-cell world truth is one row THERE, no code here.
    write_world_fields(w, s);

    // v23: the macro-ECS snapshot — the lords, squads, bandits and beasts of
    // the living map, one record each (macro/macro_snapshot.h).
    if (w.count(macroNpcs.size(), kMaxMacroNpcs)) {
        for (const auto& m : macroNpcs) write_macro_npc(w, m);
    }

    // v24: the world's runtime rhythms — field by field (never the structs
    // whole: padding bytes are not state).
    w.pod(s.worldTickRt.pendingDailyTicks);
    w.pod(s.worldTickRt.nextDailyTickDay);
    w.pod(s.worldTickRt.subworldStepRemainder);
    w.pod(s.worldTickRt.jitter.state);
    w.pod(s.macroAiRhythm.jitter.state);
    w.pod(s.playerRoll.state);                 // v153 — поток бросков игрока

    // The world's own memory (macro/chronicle.h, CANON S20.1) — ОДИН ярус:
    // ярус анналов и его блок снесены (M-184, v138).
    //
    // The RING writes only its LIVE facts, not its capacity: a fresh world
    // costs four bytes here, not two megabytes of zeroes. The per-cell CHAINS
    // are not written at all — a link is derived from the facts, and a stored
    // derivative is a second truth waiting to disagree with the first.
    write_chronicle(w, s.chronicle);

    // v75: поля следов фракций — рядом с летописью, обе — память мира.
    write_scent(w, s.scent);
    // (Блок прогресса сюжета v25 — два списка logic-узлов — умер в v157.)
    // (Блок activeQuests умер 2026-10-23: цель игрока — род 2, ломоть 3.)
}

void read_payload(Reader& r, GameState& s, std::uint32_t& storeNextSpawnId,
                  std::vector<MacroNpcRecord>& macroNpcs) {
    s.version = kSaveVersion;
    r.pod(s.worldSeed);
    r.pod(s.mapW);
    r.pod(s.mapH);
    r.pod(s.mapParams);
    r.pod(s.cityCountTarget);
    r.pod(s.worldTime);
    r.pod(s.lastWorldRebakeDay);   // v22
    r.pod(storeNextSpawnId);        // v118/M-220: эмитент — у стора
    // Метка времени слота: её читатель — inspect_save (экран загрузки). Мир
    // её не носит, поэтому здесь она только проходится, чтобы встать на блок
    // игрока.
    std::string savedAtOnWire;
    r.str(savedAtOnWire);
    read_player(r, s.player);

    // (Блок строк мест умер в v128: тела мест едут записями блока
    // макро-сквадов ниже, самолечение эмитента — там же, macro_snapshot.h.)
    read_markers(r, s.markers, world_cells_within_cap(s.mapW, s.mapH));
    if (!r.ok) return;
    std::uint32_t n = 0;

    read_factions(r, s.factions);

    // The saved world FIELDS, by registry row (macro/world_fields.h) — the
    // mirror of the write side. The forest lands in the state like every
    // other row; a non-empty block must cover the loaded map exactly.
    if (!read_world_fields(r, s)) {
        return;
    }

    if (!read_count(r, n, kMaxMacroNpcs)) return;
    macroNpcs.clear();
    macroNpcs.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        MacroNpcRecord m{};
        read_macro_npc(r, m);
        if (r.ok) macroNpcs.push_back(std::move(m));
    }

    r.pod(s.worldTickRt.pendingDailyTicks);   // v24
    r.pod(s.worldTickRt.nextDailyTickDay);
    r.pod(s.worldTickRt.subworldStepRemainder);
    r.pod(s.worldTickRt.jitter.state);
    r.pod(s.macroAiRhythm.jitter.state);
    r.pod(s.playerRoll.state);                 // v153

    // The world's own memory. Read in the SAME order it was written — the ring
    // sizes itself from the map first, because a fact's slot is its sequence
    // modulo the capacity and a chronicle with no world has nowhere to put it.
    chronicle_init(s.chronicle, s.mapW, s.mapH);
    read_chronicle(r, s.chronicle);

    read_scent(r, s.scent, s.mapW, s.mapH);   // v75
}

bool load_payload_from_file(const std::string& path, GameState& s,
                            std::uint32_t& storeNextSpawnId,
                            std::vector<MacroNpcRecord>& macroNpcs) {
    std::vector<std::uint8_t> file;
    if (!read_file(path, file)) return false;

    SaveHeader h{};
    if (!parse_header(file, h)) return false;
    if (h.magic != kMagic || h.version != kSaveVersion) return false;
    if (!checksum_matches(file, h)) return false;

    // Черновик состояния — В КУЧЕ: `GameState` несёт слои мира плоскими
    // массивами под капом клеток (M-180) и на стеке не живёт. Он остаётся
    // черновиком по прежней причине — битый файл не должен тронуть `s`.
    const std::unique_ptr<GameState> loadedOwn = make_game_state();
    GameState& loaded = *loadedOwn;
    std::uint32_t loadedNextSpawnId = 1;
    std::vector<MacroNpcRecord> loadedMacro;
    Reader r{file.data() + kHeaderBytes, static_cast<std::size_t>(h.payloadSize)};
    read_payload(r, loaded, loadedNextSpawnId, loadedMacro);
    if (!r.ok || r.pos != r.size) return false;

    s = std::move(loaded);
    storeNextSpawnId = loadedNextSpawnId;
    macroNpcs = std::move(loadedMacro);
    return true;
}

} // namespace

bool save_game(const GameState& s, std::uint32_t storeNextSpawnId,
               const std::vector<MacroNpcRecord>& macroNpcs,
               const std::string& path) {
    Writer payload;
    payload.bytes.reserve(64u * 1024u);
    const std::string savedAt = current_timestamp_utc();
    if (savedAt.empty()) return false;
    write_payload(payload, s, savedAt, storeNextSpawnId, macroNpcs);
    if (!payload.ok || payload.bytes.size() > kMaxPayloadBytes) return false;

    SaveHeader h;
    h.magic = kMagic;
    h.version = kSaveVersion;
    h.payloadSize = static_cast<std::uint64_t>(payload.bytes.size());
    h.checksum = checksum32(payload.bytes.data(), payload.bytes.size());

    return atomic_replace(path, h, payload.bytes);
}

std::uint64_t save_max_payload_bytes() { return kMaxPayloadBytes; }

std::uint32_t save_payload_fingerprint(
    const GameState& s, std::uint32_t storeNextSpawnId,
    const std::vector<MacroNpcRecord>& macroNpcs) {
    Writer payload;
    payload.bytes.reserve(64u * 1024u);
    // The stamp is held FIXED, and it is the only field that has to be: two
    // honest saves of one state differ in savedAt by construction (a fresh
    // UTC stamp per save, save.cpp:1272), and that is not state.
    write_payload(payload, s, std::string(), storeNextSpawnId, macroNpcs);
    if (!payload.ok) return 0u;   // 0 = "no answer", never a real fingerprint
    return checksum32(payload.bytes.data(), payload.bytes.size());
}

bool load_game(GameState& s, std::uint32_t& storeNextSpawnId,
               std::vector<MacroNpcRecord>& macroNpcs,
               const std::string& path) {
    return load_payload_from_file(path, s, storeNextSpawnId, macroNpcs);
}

SaveSummary inspect_save(const std::string& path) {
    SaveSummary out;
    out.path = path;

    const bool exists = file_exists(path);
    std::vector<std::uint8_t> file;
    if (!read_file(path, file)) {
        out.status = exists ? SaveInspectStatus::Unreadable
                            : SaveInspectStatus::Missing;
        return out;
    }
    if (file.size() < 8u) {
        out.status = SaveInspectStatus::Unreadable;
        return out;
    }

    Reader prefix{file.data(), file.size()};
    std::uint32_t magic = 0;
    prefix.pod(magic);
    prefix.pod(out.version);
    if (!prefix.ok || magic != kMagic) {
        out.status = SaveInspectStatus::Unreadable;
        return out;
    }
    if (out.version != kSaveVersion) {
        out.status = SaveInspectStatus::VersionMismatch;
        return out;
    }

    SaveHeader h{};
    if (!parse_header(file, h) || !checksum_matches(file, h)) {
        out.status = SaveInspectStatus::Unreadable;
        return out;
    }

    Reader r{file.data() + kHeaderBytes, static_cast<std::size_t>(h.payloadSize)};
    r.pod(out.worldSeed);
    int mapW = 0;
    int mapH = 0;
    LayerParameters mapParams{};
    int cityCountTarget = 0;
    r.pod(mapW);
    r.pod(mapH);
    r.pod(mapParams);
    r.pod(cityCountTarget);
    WorldTime time{};
    r.pod(time);
    int lastWorldRebakeDay = 0;   // v22 — present in the prefix, not summarised
    r.pod(lastWorldRebakeDay);
    std::uint32_t nextMacroSpawnOrdinal = 0;   // v118 — same
    r.pod(nextMacroSpawnOrdinal);
    r.str(out.savedAt);
    if (!r.ok) {
        out.status = SaveInspectStatus::Unreadable;
        return out;
    }

    out.day = time.day();
    out.hour = time.hour();
    out.minute = time.minute();
    out.status = SaveInspectStatus::Ready;
    return out;
}

} // namespace sm
