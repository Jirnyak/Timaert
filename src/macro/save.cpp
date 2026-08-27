#include "macro/save.h"
#include "macro/save_stream.h"
#include "macro/world_fields.h"
#include "macro/deposit_layer.h"
#include "macro/state.h"
#include "macro/macro_snapshot.h"
#include "events/quests/quest_types.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sm {
namespace {

constexpr std::uint32_t kMagic = 0x534D5341u; // 'SMSA'
constexpr std::uint32_t kChecksumSeed = 2166136261u;
constexpr std::uint32_t kChecksumPrime = 16777619u;
constexpr std::uint64_t kMaxPayloadBytes = 64ull * 1024ull * 1024ull;
constexpr std::uint32_t kMaxInventoryStacks = 4096u;
constexpr std::uint32_t kMaxSmallVector = 8192u;
// The event-log ring (state.h push_event_log) must fit under the write guard,
// or a full-but-legal log would fail every save. A mechanism, not a hope.
static_assert(kMaxEventLogEntries <= kMaxSmallVector,
              "event-log ring cap exceeds the save-side vector cap");
constexpr std::uint32_t kMaxSettlements = 4096u;
constexpr std::uint32_t kMaxVillages = 16384u;
constexpr std::uint32_t kMaxMarkers = 16384u;
constexpr std::uint32_t kMaxQuests = 4096u;
// (the field caps live with the rows: macro/world_fields.cpp)
constexpr std::uint32_t kMaxQuestParts = 4096u;
constexpr std::uint32_t kMaxSoldiers = 8192u;
// The macro-ECS snapshot (v23): one record per living macro NPC. The cap is
// the owner's macro-squad ceiling — the same golden 2^14 the subworld uses.
constexpr std::uint32_t kMaxMacroNpcs = 16384u;
constexpr std::uint32_t kHeaderBytes = 4u + 4u + 8u + 4u;

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



void write_bool(Writer& w, bool v) {
    const std::uint8_t b = v ? 1u : 0u;
    w.pod(b);
}

bool read_bool(Reader& r, bool& v) {
    std::uint8_t b = 0;
    r.pod(b);
    if (!r.ok || b > 1u) {
        r.ok = false;
        return false;
    }
    v = b != 0;
    return true;
}

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

std::string save_timestamp_for(const GameState& s) {
    std::string stamp = current_timestamp_utc();
    if (!stamp.empty()) return stamp;
    return s.savedAt;
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

void write_string_vector(Writer& w, const std::vector<std::string>& v,
                         std::uint32_t cap = kMaxSmallVector) {
    if (!w.count(v.size(), cap)) return;
    for (const auto& s : v) w.str(s);
}

void read_string_vector(Reader& r, std::vector<std::string>& v,
                        std::uint32_t cap = kMaxSmallVector) {
    std::uint32_t n = 0;
    if (!read_count(r, n, cap)) return;
    v.clear();
    v.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::string s;
        r.str(s);
        v.push_back(std::move(s));
    }
}

void write_string_int_map(Writer& w,
                          const std::unordered_map<std::string, int>& m,
                          std::uint32_t cap = kMaxSmallVector) {
    if (!w.count(m.size(), cap)) return;
    std::vector<std::pair<std::string, int>> rows;
    rows.reserve(m.size());
    for (const auto& [k, v] : m) rows.emplace_back(k, v);
    std::sort(rows.begin(), rows.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [k, v] : rows) {
        w.str(k);
        w.pod(v);
    }
}

void read_string_int_map(Reader& r, std::unordered_map<std::string, int>& m,
                         std::uint32_t cap = kMaxSmallVector) {
    std::uint32_t n = 0;
    if (!read_count(r, n, cap)) return;
    m.clear();
    m.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::string k;
        int v = 0;
        r.str(k);
        r.pod(v);
        m.emplace(std::move(k), v);
    }
}

// The spellbook's cooldowns are STEPS now (core/time.h), so the map they ride
// in is integer. Kept beside its float twin rather than templated: two callers,
// two plain functions, and a reader can see exactly what lands on disk.
void write_string_u32_map(Writer& w,
                          const std::unordered_map<std::string, std::uint32_t>& m,
                          std::uint32_t cap = kMaxSmallVector) {
    if (!w.count(m.size(), cap)) return;
    std::vector<std::pair<std::string, std::uint32_t>> rows;
    rows.reserve(m.size());
    for (const auto& [k, v] : m) rows.emplace_back(k, v);
    std::sort(rows.begin(), rows.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [k, v] : rows) {
        w.str(k);
        w.pod(v);
    }
}

void read_string_u32_map(Reader& r,
                         std::unordered_map<std::string, std::uint32_t>& m,
                         std::uint32_t cap = kMaxSmallVector) {
    std::uint32_t n = 0;
    if (!read_count(r, n, cap)) return;
    m.clear();
    m.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        std::string k;
        std::uint32_t v = 0;
        r.str(k);
        r.pod(v);
        m.emplace(std::move(k), v);
    }
}

void write_inventory(Writer& w, const Inventory& inv) {
    if (!w.count(inv.stacks.size(), kMaxInventoryStacks)) return;
    for (const auto& s : inv.stacks) {
        w.str(s.id);
        w.pod(s.count);
    }
}

void read_inventory(Reader& r, Inventory& inv) {
    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxInventoryStacks)) return;
    inv.stacks.clear();
    inv.stacks.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        ItemStack s;
        r.str(s.id);
        r.pod(s.count);
        inv.stacks.push_back(std::move(s));
    }
}

// The soldier-row loop, shared by every roster the save carries: the player's
// army, the deserter pool, garrisons, and the macro snapshot's squad rosters.
// The roster on DISK stays count-prefixed and only as long as the squad
// actually is: the in-memory form is a flat 1024-slot array (macro/army.h), and
// writing its empty tail would put megabytes of zeroes in every save for
// nothing.
void write_squad(Writer& w, const SoldierSquad& squad) {
    if (!w.count(std::size_t(squad.size()), kMaxSoldiers)) return;
    for (const auto& s : squad) {
        if (!valid_npc_kind(s.kind)) {
            w.ok = false;
            return;
        }
        const SoldierRecord normalized = make_soldier(s.kind, s.level, s.entityId);
        w.pod(normalized.entityId);
        w.pod(normalized.kind);
        w.pod(normalized.level);
    }
}

void read_squad(Reader& r, SoldierSquad& squad) {
    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxSoldiers)) return;
    squad.clear();
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        SoldierRecord s{};
        r.pod(s.entityId);
        r.pod(s.kind);
        r.pod(s.level);
        if (!r.ok) break;
        if (!valid_npc_kind(s.kind) || s.level <= 0) {
            r.ok = false;
            return;
        }
        // A save that names more men than a squad can hold is a save from a
        // different game: refuse it loudly rather than keep the first 1024.
        if (!squad.push(make_soldier(s.kind, s.level, s.entityId))) {
            r.ok = false;
            return;
        }
    }
}

// One macro NPC of the ECS snapshot (v23, macro/macro_snapshot.h). The POD
// components ride verbatim — any layout change to them is a save-format
// change and pays a kSaveVersion bump, the same discipline Skills already
// lives under.
void write_macro_npc(Writer& w, const MacroNpcRecord& m) {
    // A macro entity may be a man or a beast: the leader of a wolf pack is a
    // squad leader like any lord (CANON.md S4/S16). What is refused is a kind
    // that names NO row at all — that is a corrupt entity, not a monster.
    if (!valid_npc_kind(m.kind.type)) {
        w.ok = false;
        return;
    }
    w.pod(m.spawnId);
    w.pod(m.pos);
    w.pod(m.visual);
    w.pod(m.kind);
    w.pod(m.health);
    w.pod(m.level);
    w.pod(m.runtime);
    w.pod(m.traits);
    w.pod(m.character);
    w.pod(m.orders);
    w.pod(m.memory);   // v28: the leader's memory — padding-free by static_assert
    w.pod(m.hasOrders);
    w.pod(m.dead);
    write_inventory(w, m.inventory);
    write_squad(w, m.roster);
}

void read_macro_npc(Reader& r, MacroNpcRecord& m) {
    r.pod(m.spawnId);
    r.pod(m.pos);
    r.pod(m.visual);
    r.pod(m.kind);
    r.pod(m.health);
    r.pod(m.level);
    r.pod(m.runtime);
    r.pod(m.traits);
    r.pod(m.character);
    r.pod(m.orders);
    r.pod(m.memory);   // v28
    r.pod(m.hasOrders);
    r.pod(m.dead);
    if (!r.ok) return;
    if (m.kind.type >= std::uint16_t(NPCType::Count)
        || m.hasOrders > 1 || m.dead > 1) {
        r.ok = false;
        return;
    }
    read_inventory(r, m.inventory);
    read_squad(r, m.roster);
}

void write_history(Writer& w, const SettlementHistory& h) {
    const std::size_t n = std::min(h.days.size(), h.population.size());
    if (!w.count(n, kMaxSmallVector)) return;
    for (std::size_t i = 0; i < n; ++i) {
        w.pod(h.days[i]);
        w.pod(h.population[i]);
    }
}

void read_history(Reader& r, SettlementHistory& h) {
    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxSmallVector)) return;
    h.days.clear();
    h.population.clear();
    h.days.reserve(n);
    h.population.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        int day = 0;
        int pop = 0;
        r.pod(day);
        r.pod(pop);
        h.days.push_back(day);
        h.population.push_back(pop);
    }
}

void write_log_entry(Writer& w, const LogEntry& e) {
    write_enum8(w, e.type);
    w.str(e.message);
    w.pod(e.day);
}

void read_log_entry(Reader& r, LogEntry& e) {
    read_enum8(r, e.type, static_cast<std::uint8_t>(LogType::World));
    r.str(e.message);
    r.pod(e.day);
}

void write_perks(Writer& w, const Perks& perks) {
    if (!w.count(perks.ids.size(), kMaxSmallVector)) return;
    for (PerkID id : perks.ids) write_enum8(w, id);
}

void read_perks(Reader& r, Perks& perks) {
    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxSmallVector)) return;
    perks.ids.clear();
    perks.ids.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        PerkID id = PerkID::Immortal;
        if (read_enum8(r, id, static_cast<std::uint8_t>(PerkID::KingPesant))) {
            perks.ids.push_back(id);
        }
    }
}

void write_spell_book(Writer& w, const SpellBook& spellBook) {
    write_string_vector(w, spellBook.learned);
    w.str(spellBook.activeSpellId);
    write_string_u32_map(w, spellBook.cooldowns);
    write_string_vector(w, spellBook.sustainedActive);
}

void read_spell_book(Reader& r, SpellBook& spellBook) {
    read_string_vector(r, spellBook.learned);
    r.str(spellBook.activeSpellId);
    read_string_u32_map(r, spellBook.cooldowns);
    read_string_vector(r, spellBook.sustainedActive);
}

void write_player(Writer& w, const PlayerState& p) {
    w.str(p.name);
    w.pod(p.ageDays);
    w.pod(p.x);
    w.pod(p.y);
    w.pod(p.memory);   // v32: the player's head (debt facts et al.)
    w.pod(p.sheet.attributes);
    w.pod(p.combatStats);
    w.pod(p.sheet.levelData);
    w.pod(p.sheet.skills);
    write_perks(w, p.sheet.perks);
    write_inventory(w, p.inventory);
    // No reputation map: the player's standing is his row in gs.factions, which
    // the faction matrix below already persists (kSaveVersion 16).
    write_string_vector(w, p.codexUnlocked);

    if (w.count(p.eventLog.size(), kMaxSmallVector)) {
        for (const auto& e : p.eventLog) write_log_entry(w, e);
    }
    write_spell_book(w, p.spellBook);
    w.pod(p.factionPeaceUntilDay);
    write_string_vector(w, p.completedQuestIds);
    write_string_vector(w, p.failedQuestIds);
    w.pod(p.possessedMacroSpawnId);   // Inc 5e-2 (kSaveVersion 10)
    w.pod(p.entryDir);                // entry-side context (kSaveVersion 15)
    w.pod(p.entryTicks);
}

void read_player(Reader& r, PlayerState& p) {
    r.str(p.name);
    r.pod(p.ageDays);
    r.pod(p.x);
    r.pod(p.y);
    r.pod(p.memory);   // v32
    r.pod(p.sheet.attributes);
    r.pod(p.combatStats);
    r.pod(p.sheet.levelData);
    r.pod(p.sheet.skills);
    read_perks(r, p.sheet.perks);
    read_inventory(r, p.inventory);
    read_string_vector(r, p.codexUnlocked);

    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxSmallVector)) return;
    p.eventLog.clear();
    p.eventLog.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        LogEntry e{};
        read_log_entry(r, e);
        p.eventLog.push_back(std::move(e));
    }
    read_spell_book(r, p.spellBook);
    r.pod(p.factionPeaceUntilDay);
    read_string_vector(r, p.completedQuestIds);
    read_string_vector(r, p.failedQuestIds);
    r.pod(p.possessedMacroSpawnId);   // Inc 5e-2 (kSaveVersion 10)
    r.pod(p.entryDir);                // entry-side context (kSaveVersion 15)
    r.pod(p.entryTicks);
}

void write_settlement(Writer& w, const Settlement& s) {
    w.pod(s.id);
    w.str(s.name);
    w.pod(s.x);
    w.pod(s.y);
    w.pod(s.population);
    write_enum8(w, s.mood);
    write_inventory(w, s.inventory);
    write_history(w, s.history);
    write_squad(w, s.garrison);
    w.pod(s.kingdomIdx);
    w.pod(s.starvedYesterday);   // v29: the honest day's readouts
    w.pod(s.unmetYesterday);
    w.pod(s.famineActive);
    w.pod(s.popGrowthCarry);
}

void read_settlement(Reader& r, Settlement& s) {
    r.pod(s.id);
    r.str(s.name);
    r.pod(s.x);
    r.pod(s.y);
    r.pod(s.population);
    read_enum8(r, s.mood, static_cast<std::uint8_t>(SettlementMood::Revolt));
    read_inventory(r, s.inventory);
    read_history(r, s.history);
    read_squad(r, s.garrison);
    r.pod(s.kingdomIdx);
    r.pod(s.starvedYesterday);   // v29
    r.pod(s.unmetYesterday);
    r.pod(s.famineActive);
    r.pod(s.popGrowthCarry);
}

void write_village(Writer& w, const Village& v) {
    w.pod(v.id);
    w.str(v.name);
    w.pod(v.x);
    w.pod(v.y);
    w.pod(v.population);
    write_enum8(w, v.mood);
    write_inventory(w, v.inventory);
    w.pod(v.nearestCityId);
    w.pod(v.kingdomIdx);
    write_history(w, v.history);
    w.pod(v.starvedYesterday);   // v29
    w.pod(v.unmetYesterday);
    w.pod(v.famineActive);
    w.pod(v.popGrowthCarry);
}

void read_village(Reader& r, Village& v) {
    r.pod(v.id);
    r.str(v.name);
    r.pod(v.x);
    r.pod(v.y);
    r.pod(v.population);
    read_enum8(r, v.mood, static_cast<std::uint8_t>(SettlementMood::Revolt));
    read_inventory(r, v.inventory);
    r.pod(v.nearestCityId);
    r.pod(v.kingdomIdx);
    read_history(r, v.history);
    r.pod(v.starvedYesterday);   // v29
    r.pod(v.unmetYesterday);
    r.pod(v.famineActive);
    r.pod(v.popGrowthCarry);
}

void write_spire(Writer& w, const Spire& s) {
    w.pod(s.id);
    w.pod(s.x);
    w.pod(s.y);
    w.pod(s.spellId);
    write_bool(w, s.depleted);
}

void read_spire(Reader& r, Spire& s) {
    r.pod(s.id);
    r.pod(s.x);
    r.pod(s.y);
    r.pod(s.spellId);
    read_bool(r, s.depleted);
}

void write_marker(Writer& w, const Marker& m) {
    w.str(m.id);
    write_enum8(w, m.style);
    w.pod(m.x);
    w.pod(m.y);
    w.str(m.label);
}

void read_marker(Reader& r, Marker& m) {
    r.str(m.id);
    read_enum8(r, m.style, static_cast<std::uint8_t>(MarkerStyle::Waypoint));
    r.pod(m.x);
    r.pod(m.y);
    r.str(m.label);
}

// THE relation matrix, byte for byte (macro/relations.h). It used to be a
// string-keyed map of string-keyed maps: every faction wrote its id, name,
// description and colour — four verbatim copies of the registry row that
// already declares them — and each PAIR was written twice, once per direction.
// Now the block is the flat matrix plus the names of whatever runtime slots
// were claimed, which is the only part a registry cannot answer.
void write_relations(Writer& w, const RelationMatrix& m) {
    w.pod(m.rel);
    w.pod(m.used);
    for (int i = kFactionCount; i < kMaxWorldFactions; ++i) {
        w.str(std::string(m.used[i] ? m.runtimeIds[i] : ""));
    }
}

void read_relations(Reader& r, RelationMatrix& m) {
    m = RelationMatrix{};
    r.pod(m.rel);
    r.pod(m.used);
    for (int i = kFactionCount; i < kMaxWorldFactions && r.ok; ++i) {
        std::string id;
        r.str(id);
        std::snprintf(m.runtimeIds[i], RelationMatrix::kMaxIdLen, "%s",
                      id.c_str());
        // A tail slot with no name was never claimed, whatever the flag said.
        if (id.empty()) m.used[i] = false;
    }
    // The registry's own slots are always claimed — a save cannot un-declare a
    // faction the game is compiled with.
    claim_registry_slots(m);
}
void write_sub_state(Writer& w, const GameSubState& s) {
    write_enum8(w, s.kind);
    w.pod(s.settlementId);
    w.str(s.eventId);
    w.str(s.enemyId);
    w.pod(s.pendingEncounterIdx);
}

void read_sub_state(Reader& r, GameSubState& s) {
    std::uint8_t raw = 0;
    r.pod(raw);
    constexpr std::uint8_t kMaxLiveSubState =
        static_cast<std::uint8_t>(GameSubStateKind::Event);
    constexpr std::uint8_t kLegacyBattleSubState = 5u;
    if (!r.ok) return;
    if (raw <= kMaxLiveSubState) {
        s.kind = static_cast<GameSubStateKind>(raw);
    } else if (raw == kLegacyBattleSubState) {
        s.kind = GameSubStateKind::Exploring;
    } else {
        r.ok = false;
        return;
    }
    r.pod(s.settlementId);
    r.str(s.eventId);
    r.str(s.enemyId);
    r.pod(s.pendingEncounterIdx);
}

void write_event(Writer& w, const GameEvent& ev) {
    const auto tag = static_cast<std::uint16_t>(ev.tag);
    w.pod(tag);
    w.pod(ev.a);
    w.pod(ev.b);
    w.pod(ev.fx);
    w.pod(ev.fy);
    w.pod(ev.ix);
    w.pod(ev.iy);
    w.str(ev.s1);
    w.str(ev.s2);
}

void read_event(Reader& r, GameEvent& ev) {
    std::uint16_t tag = 0;
    r.pod(tag);
    if (!r.ok || tag > static_cast<std::uint16_t>(EventTag::LastSerializable)) {
        r.ok = false;
        return;
    }
    ev.tag = static_cast<EventTag>(tag);
    r.pod(ev.a);
    r.pod(ev.b);
    r.pod(ev.fx);
    r.pod(ev.fy);
    r.pod(ev.ix);
    r.pod(ev.iy);
    r.str(ev.s1);
    r.str(ev.s2);
}

void write_objective(Writer& w, const Objective& o) {
    write_enum8(w, o.kind);
    write_bool(w, o.completed);
    w.pod(o.ix);
    w.pod(o.iy);
    w.pod(o.cellX);
    w.pod(o.cellY);
    w.pod(o.subX);
    w.pod(o.subY);
    w.pod(o.radius);
    w.str(o.itemId);
    w.pod(o.quantity);
    w.pod(o.targetSettlementId);
    w.pod(o.npcType);
    w.pod(o.count);
    w.pod(o.killed);
    w.pod(o.zoneRadius);
    w.pod(o.hoursRequired);
    w.pod(o.hoursWaited);
    w.str(o.action);
}

void read_objective(Reader& r, Objective& o) {
    read_enum8(r, o.kind, static_cast<std::uint8_t>(ObjectiveKind::InteractCell));
    read_bool(r, o.completed);
    r.pod(o.ix);
    r.pod(o.iy);
    r.pod(o.cellX);
    r.pod(o.cellY);
    r.pod(o.subX);
    r.pod(o.subY);
    r.pod(o.radius);
    r.str(o.itemId);
    r.pod(o.quantity);
    r.pod(o.targetSettlementId);
    r.pod(o.npcType);
    r.pod(o.count);
    r.pod(o.killed);
    r.pod(o.zoneRadius);
    r.pod(o.hoursRequired);
    r.pod(o.hoursWaited);
    r.str(o.action);
}

void write_reward(Writer& w, const Reward& reward) {
    write_enum8(w, reward.kind);
    w.pod(reward.amount);
    w.str(reward.itemId);
    w.str(reward.faction);
    w.pod(reward.delta);
    write_event(w, reward.event);
}

void read_reward(Reader& r, Reward& reward) {
    read_enum8(r, reward.kind, static_cast<std::uint8_t>(RewardKind::Event));
    r.pod(reward.amount);
    r.str(reward.itemId);
    r.str(reward.faction);
    r.pod(reward.delta);
    read_event(r, reward.event);
}

void write_quest(Writer& w, const Quest& q) {
    w.str(q.id);
    w.str(q.title);
    w.str(q.description);
    write_enum8(w, q.category);
    w.pod(q.giverSettlementId);

    if (w.count(q.objectives.size(), kMaxQuestParts)) {
        for (const auto& o : q.objectives) write_objective(w, o);
    }
    if (w.count(q.rewards.size(), kMaxQuestParts)) {
        for (const auto& reward : q.rewards) write_reward(w, reward);
    }
    if (w.count(q.onAccept.size(), kMaxQuestParts)) {
        for (const auto& ev : q.onAccept) write_event(w, ev);
    }
    w.pod(q.expireDay);
    w.pod(q.difficulty);
}

void read_quest(Reader& r, Quest& q) {
    r.str(q.id);
    r.str(q.title);
    r.str(q.description);
    read_enum8(r, q.category, static_cast<std::uint8_t>(QuestCategory::Procedural));
    r.pod(q.giverSettlementId);

    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxQuestParts)) return;
    q.objectives.clear();
    q.objectives.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Objective o{};
        read_objective(r, o);
        q.objectives.push_back(std::move(o));
    }

    if (!read_count(r, n, kMaxQuestParts)) return;
    q.rewards.clear();
    q.rewards.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Reward reward{};
        read_reward(r, reward);
        q.rewards.push_back(std::move(reward));
    }

    if (!read_count(r, n, kMaxQuestParts)) return;
    q.onAccept.clear();
    q.onAccept.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        GameEvent ev{};
        read_event(r, ev);
        q.onAccept.push_back(std::move(ev));
    }

    r.pod(q.expireDay);
    r.pod(q.difficulty);
}

void write_payload(Writer& w, const GameState& s,
                   const std::string& savedAt,
                   const std::vector<Quest>& activeQuests,
                   const std::vector<MacroNpcRecord>& macroNpcs,
                   const std::vector<std::uint16_t>& treeCounts,
                   const DepositLayer& deposits) {
    w.pod(s.worldSeed);
    w.pod(s.mapW);
    w.pod(s.mapH);
    w.pod(s.mapParams);
    w.pod(s.cityCountTarget);
    w.pod(s.worldTime);
    w.pod(s.lastWorldRebakeDay);   // v22: autosave/re-bake phase survives a load
    w.pod(s.nextMacroSpawnOrdinal); // v23: the ONE MacroSpawnId issuer
    w.str(s.saveName);
    w.str(savedAt);
    write_player(w, s.player);

    if (w.count(s.settlements.size(), kMaxSettlements)) {
        for (const auto& settlement : s.settlements) write_settlement(w, settlement);
    }
    if (w.count(s.villages.size(), kMaxVillages)) {
        for (const auto& village : s.villages) write_village(w, village);
    }
    if (w.count(s.spires.size(), kMaxSmallVector)) {
        for (const auto& spire : s.spires) write_spire(w, spire);
    }
    if (w.count(s.markers.size(), kMaxMarkers)) {
        for (const auto& marker : s.markers) write_marker(w, marker);
    }

    write_relations(w, s.relations);

    write_sub_state(w, s.subState);
    write_squad(w, s.deserterPool);


    // The saved world FIELDS ride as registry rows (macro/world_fields.h):
    // trees, knowledge, deposits, scars — each row writes its own bytes, in
    // row order, byte-identical to the four hand blocks that lived here.
    // Adding a per-cell world truth is one row THERE, no code here.
    write_world_fields(w, WorldFieldStores{&s, &treeCounts, &deposits});

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
    w.pod(s.macroAiRhythm.sweepAccum);
    w.pod(s.macroAiRhythm.pendingSweeps);
    w.pod(s.macroAiRhythm.sweepCursor);

    // v25: story progress — which logic nodes still exist / are active.
    write_string_vector(w, s.logicNodesRegistered);
    write_string_vector(w, s.logicNodesActive);

    if (w.count(activeQuests.size(), kMaxQuests)) {
        for (const auto& q : activeQuests) write_quest(w, q);
    }
}

void read_payload(Reader& r, GameState& s, std::vector<Quest>& activeQuests,
                  std::vector<MacroNpcRecord>& macroNpcs,
                  std::vector<std::uint16_t>& treeCounts,
                  DepositLayer& deposits) {
    s.version = kSaveVersion;
    r.pod(s.worldSeed);
    r.pod(s.mapW);
    r.pod(s.mapH);
    r.pod(s.mapParams);
    r.pod(s.cityCountTarget);
    r.pod(s.worldTime);
    r.pod(s.lastWorldRebakeDay);   // v22
    r.pod(s.nextMacroSpawnOrdinal); // v23
    r.str(s.saveName);
    r.str(s.savedAt);
    read_player(r, s.player);

    std::uint32_t n = 0;
    if (!read_count(r, n, kMaxSettlements)) return;
    s.settlements.clear();
    s.settlements.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Settlement settlement{};
        read_settlement(r, settlement);
        s.settlements.push_back(std::move(settlement));
    }

    if (!read_count(r, n, kMaxVillages)) return;
    s.villages.clear();
    s.villages.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Village village{};
        read_village(r, village);
        s.villages.push_back(std::move(village));
    }

    if (!read_count(r, n, kMaxSmallVector)) return;
    s.spires.clear();
    s.spires.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Spire spire{};
        read_spire(r, spire);
        s.spires.push_back(spire);
    }

    if (!read_count(r, n, kMaxMarkers)) return;
    s.markers.clear();
    s.markers.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Marker marker{};
        read_marker(r, marker);
        s.markers.push_back(std::move(marker));
    }

    read_relations(r, s.relations);

    read_sub_state(r, s.subState);
    read_squad(r, s.deserterPool);

    // The saved world FIELDS, by registry row (macro/world_fields.h) — the
    // mirror of the write side; the caller validates trees against the
    // loaded map dims (restore_tree_counts).
    if (!read_world_fields(r, WorldFieldStoresMut{&s, &treeCounts,
                                                  &deposits})) {
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
    r.pod(s.macroAiRhythm.sweepAccum);
    r.pod(s.macroAiRhythm.pendingSweeps);
    r.pod(s.macroAiRhythm.sweepCursor);

    read_string_vector(r, s.logicNodesRegistered);   // v25
    read_string_vector(r, s.logicNodesActive);

    if (!read_count(r, n, kMaxQuests)) return;
    activeQuests.clear();
    activeQuests.reserve(n);
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Quest q{};
        read_quest(r, q);
        activeQuests.push_back(std::move(q));
    }
}

bool load_payload_from_file(const std::string& path, GameState& s,
                            std::vector<Quest>& activeQuests,
                            std::vector<MacroNpcRecord>& macroNpcs,
                            std::vector<std::uint16_t>& treeCounts,
                            DepositLayer& deposits) {
    std::vector<std::uint8_t> file;
    if (!read_file(path, file)) return false;

    SaveHeader h{};
    if (!parse_header(file, h)) return false;
    if (h.magic != kMagic || h.version != kSaveVersion) return false;
    if (!checksum_matches(file, h)) return false;

    GameState loaded;
    std::vector<Quest> loadedQuests;
    std::vector<MacroNpcRecord> loadedMacro;
    std::vector<std::uint16_t> loadedTrees;
    DepositLayer loadedDeposits;
    Reader r{file.data() + kHeaderBytes, static_cast<std::size_t>(h.payloadSize)};
    read_payload(r, loaded, loadedQuests, loadedMacro, loadedTrees,
                 loadedDeposits);
    if (!r.ok || r.pos != r.size) return false;

    s = std::move(loaded);
    activeQuests = std::move(loadedQuests);
    macroNpcs = std::move(loadedMacro);
    treeCounts = std::move(loadedTrees);
    deposits = std::move(loadedDeposits);
    return true;
}

} // namespace

bool save_game(const GameState& s, const std::vector<Quest>& activeQuests,
               const std::vector<MacroNpcRecord>& macroNpcs,
               const std::vector<std::uint16_t>& treeCounts,
               const DepositLayer& deposits,
               const std::string& path) {
    Writer payload;
    payload.bytes.reserve(64u * 1024u);
    const std::string savedAt = save_timestamp_for(s);
    if (savedAt.empty()) return false;
    write_payload(payload, s, savedAt, activeQuests, macroNpcs, treeCounts,
                  deposits);
    if (!payload.ok || payload.bytes.size() > kMaxPayloadBytes) return false;

    SaveHeader h;
    h.magic = kMagic;
    h.version = kSaveVersion;
    h.payloadSize = static_cast<std::uint64_t>(payload.bytes.size());
    h.checksum = checksum32(payload.bytes.data(), payload.bytes.size());

    return atomic_replace(path, h, payload.bytes);
}

bool load_game(GameState& s, std::vector<Quest>& activeQuests,
               std::vector<MacroNpcRecord>& macroNpcs,
               std::vector<std::uint16_t>& treeCounts,
               DepositLayer& deposits,
               const std::string& path) {
    return load_payload_from_file(path, s, activeQuests, macroNpcs,
                                  treeCounts, deposits);
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
    std::uint32_t nextMacroSpawnOrdinal = 0;   // v23 — same
    r.pod(nextMacroSpawnOrdinal);
    r.str(out.saveName);
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
