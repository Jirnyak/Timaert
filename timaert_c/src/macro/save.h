// Minimal save/load. Mirrors map-factory.ts pattern:
// world is regenerated from `worldSeed` on load (deterministic), then
// player progress fields are overlaid. This keeps the file tiny and
// avoids serialising massive procedurally-derived arrays.
//
// Save format is binary, version-gated by kSaveVersion. Per AGENTS.md
// rule #2, we do NOT keep backward compatibility — bump kSaveVersion
// for any layout change and old saves are silently rejected.
#pragma once
#include <string>

namespace sm {

struct GameState;

// Returns true on success. Failures are silent (no exceptions).
bool save_game(const GameState& s, const std::string& path);
bool load_game(GameState& s, const std::string& path);

} // namespace sm
