// Binary save/load. The terrain/politik layers are regenerated from
// worldSeed + map parameters, then mutable runtime records are overlaid.
// Subworld snapshots are deliberately session-only in v7; see
// sub/map_factory.h for that cache boundary.
//
// Save format is binary, version-gated by kSaveVersion. Per AGENTS.md
// rule #2, we do NOT keep backward compatibility — bump kSaveVersion
// for any layout change and old saves are silently rejected.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

struct GameState;
struct Quest;

enum class SaveInspectStatus : std::uint8_t {
    Missing,
    Unreadable,
    VersionMismatch,
    Ready,
};

struct SaveSummary {
    SaveInspectStatus status = SaveInspectStatus::Missing;
    std::string path;
    std::string saveName;
    std::string savedAt;
    std::int32_t version = 0;
    std::uint32_t worldSeed = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
};

// Returns true on success. Failures return false; no exceptions are used.
bool save_game(const GameState& s, const std::vector<Quest>& activeQuests,
               const std::string& path);
bool load_game(GameState& s, std::vector<Quest>& activeQuests,
               const std::string& path);
SaveSummary inspect_save(const std::string& path);

} // namespace sm
