// Landmark registry — TS-faithful runtime aggregator (mirrors
// `landmark-registry.ts`). Walks `GameState` and yields a unified list of
// every macroworld point-of-interest with its kind, name, position, and a
// short detail line. Consumed by debug overlays / map tooltips. Pure
// read-only — does not allocate beyond the returned vector.
//
// To add a new landmark kind: extend `collect_landmarks` with one extra
// pass over the new GameState collection. No header changes required by
// callers — the entry list is uniform.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

struct GameState;

struct LandmarkEntry {
    std::string  kind;    // "City" | "Village" | "Spire" | "Marker"
    std::string  id;      // unique within kind
    std::string  name;
    std::string  detail;  // optional secondary line
    int          x;
    int          y;
    std::uint32_t color;  // ARGB
};

std::vector<LandmarkEntry> collect_landmarks(const GameState& gs);

} // namespace sm
// Landmark registry — every macroworld point-of-interest type the game can
// place lives in this single table. Adding a landmark = one entry. Mirrors
// landmark-registry.ts. Pure data.
#pragma once
#include <cstdint>
#include <string_view>

namespace sm {

enum class LandmarkType : std::uint8_t {
    None = 0,
    City,
    Village,
    Spire,
    Ruin,
    Lair,
    Shrine,
    Mine,
    Tower,
    Count,
};

struct LandmarkDef {
    std::string_view id;
    std::string_view label;
    std::uint8_t     minZone;     // minimum difficulty zone to spawn
    std::uint8_t     maxZone;
    std::uint8_t     glyph;       // ASCII glyph for fallback rendering
    std::uint32_t    color;       // ARGB tint
    bool             walkable;    // can the player enter it
    std::uint32_t    lightColor;  // ARGB night-emission tint (0 = does not glow)
    float            lightPop;    // synthetic emitter strength for fixed POIs;
                                  // 0 => inhabited type, scale by live population
};

// Night-light columns (lightColor / lightPop) drive the universal macro
// lighting system (macro_lighting.cpp). Inhabited types (city, village) emit
// the TS warm hearth tint scaled by live population (lightPop = 0). Fixed POIs
// emit their own tint at a synthetic strength. Set lightColor = 0 to opt a type
// out of night glow entirely — the single data-driven gate.
inline constexpr LandmarkDef kLandmarks[std::size_t(LandmarkType::Count)] = {
    {"none",    "",         0, 9, ' ', 0x00000000u, true, 0x00000000u,   0.0f },
    {"city",    "City",     0, 2, '#', 0xFFE7D27Au, true, 0xFFFFC76Bu,   0.0f },
    {"village", "Village",  0, 3, 'v', 0xFFCCB068u, true, 0xFFFFC76Bu,   0.0f },
    {"spire",   "Spire",    5, 9, 'I', 0xFFA86CFFu, true, 0xFFA86CFFu, 200.0f },
    {"ruin",    "Ruin",     2, 8, 'r', 0xFF8E8576u, true, 0xFF8E8576u,  40.0f },
    {"lair",    "Lair",     4, 9, 'L', 0xFF883A3Au, true, 0xFF883A3Au,  70.0f },
    {"shrine",  "Shrine",   1, 6, '+', 0xFFE2E2E2u, true, 0xFFE2E2E2u,  90.0f },
    {"mine",    "Mine",     2, 7, 'M', 0xFF8B6332u, true, 0xFF8B6332u,  60.0f },
    {"tower",   "Tower",    3, 7, 'T', 0xFF6E6E89u, true, 0xFF6E6E89u,  80.0f },
};

inline constexpr const LandmarkDef& landmark_def(LandmarkType t) {
    return kLandmarks[std::size_t(t)];
}

} // namespace sm
