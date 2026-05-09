#include "macro/save.h"
#include "macro/state.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace sm {
namespace {

constexpr std::uint32_t kMagic = 0x534D5341u; // 'SMSA'

struct Writer {
    std::FILE* f;
    template <class T> void pod(const T& v) { std::fwrite(&v, sizeof(T), 1, f); }
    void str(const std::string& s) {
        std::uint32_t n = std::uint32_t(s.size());
        pod(n);
        if (n) std::fwrite(s.data(), 1, n, f);
    }
};
struct Reader {
    std::FILE* f;
    bool ok = true;
    template <class T> void pod(T& v) {
        if (!ok) return;
        ok = std::fread(&v, sizeof(T), 1, f) == 1;
    }
    void str(std::string& s) {
        std::uint32_t n = 0;
        pod(n);
        if (!ok || n > (1u << 20)) { ok = false; return; }
        s.resize(n);
        if (n) ok = std::fread(s.data(), 1, n, f) == n;
    }
};

} // namespace

bool save_game(const GameState& s, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    Writer w{f};
    w.pod(kMagic);
    std::int32_t ver = kSaveVersion; w.pod(ver);
    w.pod(s.worldSeed);
    w.pod(s.worldTime);
    w.str(s.saveName);
    // Player core
    w.str(s.player.name);
    w.pod(s.player.ageDays);
    w.pod(s.player.x);
    w.pod(s.player.y);
    w.pod(s.player.gold);
    w.pod(s.player.attributes);
    w.pod(s.player.combatStats);
    w.pod(s.player.levelData);
    // Spell book
    std::uint32_t sn = std::uint32_t(s.player.spellBookSpellIds.size());
    w.pod(sn);
    for (const auto& id : s.player.spellBookSpellIds) w.str(id);
    // Completed quests
    std::uint32_t qn = std::uint32_t(s.player.completedQuestIds.size());
    w.pod(qn);
    for (int q : s.player.completedQuestIds) w.pod(q);
    // Reputation
    std::uint32_t rn = std::uint32_t(s.player.reputation.size());
    w.pod(rn);
    for (const auto& [k, v] : s.player.reputation) { w.str(const_cast<std::string&>(k)); w.pod(v); }
    std::fclose(f);
    return true;
}

bool load_game(GameState& s, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    Reader r{f};
    std::uint32_t magic = 0; r.pod(magic);
    std::int32_t ver = 0;    r.pod(ver);
    if (!r.ok || magic != kMagic || ver != kSaveVersion) { std::fclose(f); return false; }
    r.pod(s.worldSeed);
    r.pod(s.worldTime);
    r.str(s.saveName);
    r.str(s.player.name);
    r.pod(s.player.ageDays);
    r.pod(s.player.x);
    r.pod(s.player.y);
    r.pod(s.player.gold);
    r.pod(s.player.attributes);
    r.pod(s.player.combatStats);
    r.pod(s.player.levelData);
    std::uint32_t sn = 0; r.pod(sn);
    s.player.spellBookSpellIds.clear();
    for (std::uint32_t i = 0; i < sn && r.ok; ++i) {
        std::string id; r.str(id);
        s.player.spellBookSpellIds.push_back(std::move(id));
    }
    std::uint32_t qn = 0; r.pod(qn);
    s.player.completedQuestIds.clear();
    for (std::uint32_t i = 0; i < qn && r.ok; ++i) { int q = 0; r.pod(q); s.player.completedQuestIds.push_back(q); }
    std::uint32_t rn = 0; r.pod(rn);
    s.player.reputation.clear();
    for (std::uint32_t i = 0; i < rn && r.ok; ++i) {
        std::string k; int v = 0; r.str(k); r.pod(v);
        s.player.reputation.emplace(std::move(k), v);
    }
    std::fclose(f);
    return r.ok;
}

} // namespace sm
