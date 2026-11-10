#include "macro/save.h"

#include "bus/store.h"
#include "core/row_law.h"
#include "macro/state.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <type_traits>
#include <vector>

namespace sm {
namespace {

constexpr std::uint32_t kMagic = 0x534D5341u; // 'SMSA'

// ── ЗАГОЛОВОК ФАЙЛА — ЕДИНСТВЕННОЕ, ЧТО В ФАЙЛЕ НЕ ЕСТЬ ПАМЯТЬ МИРА ─────────
// Плоская запись без дыр, пишется и читается своими байтами — та же форма,
// что у двух кусков за ней. Размеры кусков названы ЯВНО, хотя оба суть
// `sizeof` типов этой сборки: файл другой раскладки отказывается сравнением
// двух чисел, раньше чем в мир ляжет хоть один его байт.
struct SaveHeader {
    std::uint32_t magic = 0;
    std::int32_t  version = 0;
    std::uint64_t stateBytes = 0;   // sizeof(GameState) записавшей сборки
    std::uint64_t storeBytes = 0;   // sizeof(MacroStore) записавшей сборки
    std::uint64_t checksum = 0;     // SaveChecksum по обоим кускам подряд
    // Метка времени записи (UTC, ISO-8601 с миллисекундами, 24 знака и ноль).
    // Состоянием мира не является — её читает экран загрузки. Ячейка шире
    // метки с запасом и кратна 8, чтобы заголовок не нёс хвостовой дыры.
    char          savedAt[32] = {};
};
static_assert(sizeof(SaveHeader) == 64,
              "заголовок сейва: 4 + 4 + 8 + 8 + 8 + 32 = 64 Б");
// У заголовка нет ни одного float, поэтому «без дыр» утверждается прямо
// стандартным свойством: каждый бит представления значим.
static_assert(std::has_unique_object_representations_v<SaveHeader>,
              "в заголовке сейва дыра выравнивания — его байты пишутся как "
              "есть и сверяются побайтно на самопроверке");

// ── СТЕНЫ ДАМПА ─────────────────────────────────────────────────────────────
// Всё, на чём стоит «файл есть память»: оба куска переживают memcpy, плоски,
// не несут ни одного байта вне названного члена и кратны слову суммы.
static_assert(std::is_trivially_copyable_v<GameState>
                  && std::is_standard_layout_v<GameState>
                  && std::is_trivially_copyable_v<MacroStore>
                  && std::is_standard_layout_v<MacroStore>,
              "дамп снимает оба куска байтами: они обязаны быть тривиально "
              "копируемы и плоски");
static_assert(row_packed_v<GameState> && row_packed_v<MacroStore>,
              "оба куска дампа обязаны стоять под стеной «без дыр» "
              "(TIMAERT_PACKED): байт вне члена дал бы разные файлы у "
              "равных миров");
static_assert(sizeof(GameState) % sizeof(std::uint64_t) == 0
                  && sizeof(MacroStore) % sizeof(std::uint64_t) == 0,
              "сумма считается словами по 8 Б: оба куска обязаны быть "
              "кратны слову");
// Длина файла спрашивается `ftell`, а его `long` на Windows — 32 бита.
// Слот обязан помещаться в знаковые 32 бита, иначе осмотр и загрузка там
// соврали бы о длине; поднимется кап выше — эта строка назовёт день.
static_assert(sizeof(SaveHeader) + sizeof(GameState) + sizeof(MacroStore)
                  < (std::uint64_t(1) << 31),
              "слот сейва перерос 2 ГиБ: длину файла больше нельзя "
              "спрашивать `ftell` на платформе с 32-битным long");

constexpr std::uint64_t kFileBytes =
    sizeof(SaveHeader) + sizeof(GameState) + sizeof(MacroStore);

// Свёртка FNV-1a, 64-битная редакция: базис и простое — её собственные
// опубликованные константы (Fowler–Noll–Vo), не подобранные числа.
constexpr std::uint64_t kSumBasis = 0xCBF29CE484222325ull;
constexpr std::uint64_t kSumPrime = 0x00000100000001B3ull;

inline std::uint64_t load_word(const unsigned char* p) noexcept {
    std::uint64_t w;
    std::memcpy(&w, p, sizeof(w));
    return w;
}

// Порция, которой самопроверка читает файл обратно: 1 МиБ — кратно слову
// суммы и странице, а держать в памяти второй гигабайт ради сверки незачем.
constexpr std::size_t kVerifyChunkBytes = std::size_t(1) << 20;
static_assert(kVerifyChunkBytes % sizeof(std::uint64_t) == 0,
              "порция самопроверки кратна слову суммы");

bool stamp_utc_now(char (&out)[32]) {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    if (seconds == std::time_t(-1)) return false;

    std::tm tm{};
#if defined(_WIN32)
    if (gmtime_s(&tm, &seconds) != 0) return false;
#else
    if (gmtime_r(&seconds, &tm) == nullptr) return false;
#endif

    std::memset(out, 0, sizeof(out));
    const int n = std::snprintf(out, sizeof(out),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, int(ms.count()));
    return n > 0 && n < int(sizeof(out));
}

bool file_exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// Длина открытого файла; курсор остаётся в начале.
bool file_length(std::FILE* f, std::uint64_t& out) {
    if (std::fseek(f, 0, SEEK_END) != 0) return false;
    const long len = std::ftell(f);
    if (len < 0 || std::fseek(f, 0, SEEK_SET) != 0) return false;
    out = std::uint64_t(len);
    return true;
}

// Заголовок этой сборки? Магия и версия судятся звонящим врозь (у осмотра на
// них разные ответы); здесь — раскладка и целость метки.
bool header_shape_ok(const SaveHeader& h) {
    return h.stateBytes == sizeof(GameState)
        && h.storeBytes == sizeof(MacroStore)
        && h.savedAt[sizeof(h.savedAt) - 1u] == '\0';
}

bool write_file(const std::string& path, const SaveHeader& h,
                const GameState& gs, const MacroStore& st) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(&h, sizeof(h), 1, f) == 1;
    ok = ok && std::fwrite(&gs, sizeof(gs), 1, f) == 1;
    ok = ok && std::fwrite(&st, sizeof(st), 1, f) == 1;
    ok = ok && std::fflush(f) == 0;
    ok = ok && std::ferror(f) == 0;
    const bool closeOk = std::fclose(f) == 0;
    return ok && closeOk;
}

// САМОПРОВЕРКА ЗАПИСАННОГО: файл перечитывается порциями, и сверяется ВСЁ —
// заголовок побайтно, длина, сумма. Слот подменяется только после неё.
bool verify_file(const std::string& path, const SaveHeader& want) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::uint64_t len = 0;
    SaveHeader got{};
    bool ok = file_length(f, len) && len == kFileBytes
        && std::fread(&got, sizeof(got), 1, f) == 1
        && std::memcmp(&got, &want, sizeof(got)) == 0;
    if (ok) {
        SaveChecksum sum;
        std::vector<unsigned char> chunk(kVerifyChunkBytes);
        const std::uint64_t total = sizeof(GameState) + sizeof(MacroStore);
        for (std::uint64_t done = 0; ok && done < total;) {
            const std::uint64_t rest = total - done;
            const std::size_t take = rest < chunk.size()
                ? std::size_t(rest) : chunk.size();
            ok = std::fread(chunk.data(), 1, take, f) == take;
            if (ok) sum.feed(chunk.data(), take);
            done += take;
        }
        ok = ok && sum.finish() == want.checksum;
    }
    std::fclose(f);
    return ok;
}

} // namespace

// ── СУММА ───────────────────────────────────────────────────────────────────

SaveChecksum::SaveChecksum() {
    for (std::uint64_t& l : lane) l = kSumBasis;
}

void SaveChecksum::feed(const void* data, std::size_t bytes) {
    const auto* p = static_cast<const unsigned char*>(data);
    const std::size_t n = bytes / sizeof(std::uint64_t);
    std::size_t i = 0;
    // До границы четвёрки — по слову: порция могла начаться посреди неё.
    for (; i < n && (words & 3u) != 0u; ++i, ++words) {
        std::uint64_t& l = lane[words & 3u];
        l = (l ^ load_word(p + i * 8u)) * kSumPrime;
    }
    // Четыре дорожки разом: четыре независимые цепочки умножений процессор
    // ведёт одновременно, и ради этого дорожек именно четыре.
    std::uint64_t a = lane[0], b = lane[1], c = lane[2], d = lane[3];
    for (; i + 4u <= n; i += 4u) {
        const unsigned char* q = p + i * 8u;
        a = (a ^ load_word(q)) * kSumPrime;
        b = (b ^ load_word(q + 8)) * kSumPrime;
        c = (c ^ load_word(q + 16)) * kSumPrime;
        d = (d ^ load_word(q + 24)) * kSumPrime;
        words += 4u;
    }
    lane[0] = a; lane[1] = b; lane[2] = c; lane[3] = d;
    for (; i < n; ++i, ++words) {
        std::uint64_t& l = lane[words & 3u];
        l = (l ^ load_word(p + i * 8u)) * kSumPrime;
    }
    // Хвост короче слова бывает только у порции, нарушившей договор о
    // кратности. Он не выбрасывается: дополняется нулями до слова, чтобы
    // сумма осталась функцией ВСЕХ поданных байтов.
    const std::size_t rest = bytes - n * sizeof(std::uint64_t);
    if (rest != 0u) {
        std::uint64_t w = 0;
        std::memcpy(&w, p + n * 8u, rest);
        std::uint64_t& l = lane[words & 3u];
        l = (l ^ w) * kSumPrime;
        ++words;
    }
}

std::uint64_t SaveChecksum::finish() const {
    // Слияние ПОРЯДКОВОЕ: дорожки, обменявшиеся содержимым, дают другой
    // итог. Счёт слов входит последним — два куска разной длины с общим
    // началом не сойдутся.
    std::uint64_t h = kSumBasis;
    for (const std::uint64_t l : lane) h = (h ^ l) * kSumPrime;
    return (h ^ words) * kSumPrime;
}

// ── ЗАПИСЬ ──────────────────────────────────────────────────────────────────

bool save_game(const GameState& gs, const MacroStore& st,
               const std::string& path) {
    SaveHeader h{};
    h.magic = kMagic;
    h.version = kSaveVersion;
    h.stateBytes = sizeof(GameState);
    h.storeBytes = sizeof(MacroStore);
    if (!stamp_utc_now(h.savedAt)) return false;
    {
        SaveChecksum sum;
        sum.feed(&gs, sizeof(gs));
        sum.feed(&st, sizeof(st));
        h.checksum = sum.finish();
    }

    const std::string tmp = path + ".tmp";
    const std::string bak = path + ".bak";
    std::remove(tmp.c_str());

    if (!write_file(tmp, h, gs, st) || !verify_file(tmp, h)) {
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

// ── ЧТЕНИЕ ──────────────────────────────────────────────────────────────────

bool load_game(GameState& gs, MacroStore& st, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::uint64_t len = 0;
    SaveHeader h{};
    // Заголовок и длина судятся ДО первого байта мира: чужая раскладка,
    // чужая версия и обрезанный файл по адресу не ложатся вовсе.
    bool ok = file_length(f, len) && len == kFileBytes
        && std::fread(&h, sizeof(h), 1, f) == 1
        && h.magic == kMagic && h.version == kSaveVersion
        && header_shape_ok(h);
    ok = ok && std::fread(&gs, sizeof(gs), 1, f) == 1;
    ok = ok && std::fread(&st, sizeof(st), 1, f) == 1;
    std::fclose(f);
    if (!ok) return false;

    SaveChecksum sum;
    sum.feed(&gs, sizeof(gs));
    sum.feed(&st, sizeof(st));
    return sum.finish() == h.checksum;
}

// ── ОСМОТР ──────────────────────────────────────────────────────────────────

SaveSummary inspect_save(const std::string& path) {
    SaveSummary out;
    out.path = path;

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;   // Missing
    out.status = SaveInspectStatus::Unreadable;

    std::uint64_t len = 0;
    SaveHeader h{};
    // Магия и версия — первые восемь байт любого слота, и прежних форматов
    // тоже: чужая версия называется вслух, а не сливается с порчей.
    if (!file_length(f, len) || len < 2u * sizeof(std::uint32_t)
        || std::fread(&h.magic, sizeof(h.magic), 1, f) != 1
        || std::fread(&h.version, sizeof(h.version), 1, f) != 1
        || h.magic != kMagic) {
        std::fclose(f);
        return out;
    }
    out.version = h.version;
    if (h.version != kSaveVersion) {
        out.status = SaveInspectStatus::VersionMismatch;
        std::fclose(f);
        return out;
    }

    // Два числа состояния читаются ПО ИХ СМЕЩЕНИЮ В КУСКЕ — у сейва нет
    // своей копии сида и времени, которая могла бы разойтись с миром.
    WorldTime time{};
    const bool ok = len == kFileBytes
        && std::fseek(f, 0, SEEK_SET) == 0
        && std::fread(&h, sizeof(h), 1, f) == 1
        && header_shape_ok(h)
        && std::fseek(f, long(sizeof(SaveHeader)
                              + offsetof(GameState, worldSeed)),
                      SEEK_SET) == 0
        && std::fread(&out.worldSeed, sizeof(out.worldSeed), 1, f) == 1
        && std::fseek(f, long(sizeof(SaveHeader)
                              + offsetof(GameState, worldTime)),
                      SEEK_SET) == 0
        && std::fread(&time, sizeof(time), 1, f) == 1;
    std::fclose(f);
    if (!ok) return out;

    out.savedAt = h.savedAt;
    out.day = time.day();
    out.hour = time.hour();
    out.minute = time.minute();
    out.status = SaveInspectStatus::Ready;
    return out;
}

std::uint64_t save_file_bytes() { return kFileBytes; }

} // namespace sm
