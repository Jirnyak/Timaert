// Binary save/load. The terrain/politik layers are regenerated from
// worldSeed + map parameters, then mutable runtime records are overlaid.
// Subworld snapshots are deliberately session-only; see sub/map_factory.h
// for that cache boundary.
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
struct MacroNpcRecord;
struct DepositLayer;

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
// `macroNpcs` is the flattened macro-ECS (macro/macro_snapshot.h) — a
// REQUIRED parameter on purpose: a call site that could omit it would write
// a world with no lords in it and nobody would notice until a load.
// `treeCounts` (the living tree grid, v36) and `deposits` (the living
// deposit cells, v37) are required for the same reason: omit either and a
// felled forest or a drained vein silently regrows on load.
bool save_game(const GameState& s, const std::vector<Quest>& activeQuests,
               const std::vector<MacroNpcRecord>& macroNpcs,
               const std::vector<std::uint16_t>& treeCounts,
               const DepositLayer& deposits,
               const std::string& path);
bool load_game(GameState& s, std::vector<Quest>& activeQuests,
               std::vector<MacroNpcRecord>& macroNpcs,
               std::vector<std::uint16_t>& treeCounts,
               DepositLayer& deposits,
               const std::string& path);
SaveSummary inspect_save(const std::string& path);

// ── THE witness on the load's fold ────────────────────────────────────────
//
// What a world WOULD weigh on disk: the same write_payload every save runs,
// with the timestamp held fixed (it is the one field that differs between two
// saves of one state), checksummed. Not a save — nothing is written anywhere.
//
// It exists because the load applies a file to a LIVING world, and that apply
// was a hand-written list of assignments. A hand-written fold drops fields
// silently: five of them rode the file and died on the way in (SAVE-1 —
// resource scars, the loot pool, the ships, the scent fields, the landmark
// issuer), and nothing could see it, because the round-trip test proves
// file→GameState, which is the half that worked.
//
// So the door proves ITSELF instead: fingerprint the state parsed from the
// file, fingerprint the living world right after the fold, and demand they
// agree. The ENUMERATION of fields is the writer's, not a human's — a field
// added to write_payload is watched from the day it is added, and a field
// dropped on the way in is a mismatch the same second.
//
// Two conditions this rests on, both now true: the byte stream is
// deterministic for one state (every sparse block sorts — deposits, scars,
// and since SAVE-3 the ships), and the macro snapshot rides sorted by
// ordinal (macro_snapshot.cpp:47).
std::uint32_t save_payload_fingerprint(
    const GameState& s, const std::vector<Quest>& activeQuests,
    const std::vector<MacroNpcRecord>& macroNpcs,
    const std::vector<std::uint16_t>& treeCounts,
    const DepositLayer& deposits);

} // namespace sm
