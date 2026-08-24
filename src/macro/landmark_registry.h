// Landmark registry — every macroworld point-of-interest type the game can
// place lives in this single table. Adding a landmark = one entry (CANON S16).
//
// This is also THE landmark vocabulary (2026-08-24): the subworld's
// "CellLandmarkKind" and the fauna router's "LandmarkKind" were two more
// five-value copies of this enum, joined by a hand-written bridge — and the
// four kinds the copies could not name (Lair, Shrine, Mine, Tower) physically
// could not reach the microworld (canon-audit C1). One enum, everywhere.
// (The header's former first half — collect_landmarks and its string-typed
// LandmarkEntry — had no callers and is gone with landmark_registry.cpp.)
#pragma once
#include <cstdint>
#include "core/table_guard.h"
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
    // MUST equal the row's index in kLandmarks (guard below the table).
    LandmarkType     type;
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
    {LandmarkType::None,    "none",    "",        0, 9, ' ', 0x00000000u, true, 0x00000000u,   0.0f },
    {LandmarkType::City,    "city",    "City",    0, 2, '#', 0xFFE7D27Au, true, 0xFFFFC76Bu,   0.0f },
    {LandmarkType::Village, "village", "Village", 0, 3, 'v', 0xFFCCB068u, true, 0xFFFFC76Bu,   0.0f },
    {LandmarkType::Spire,   "spire",   "Spire",   5, 9, 'I', 0xFFA86CFFu, true, 0xFFA86CFFu, 200.0f },
    {LandmarkType::Ruin,    "ruin",    "Ruin",    2, 8, 'r', 0xFF8E8576u, true, 0xFF8E8576u,  40.0f },
    {LandmarkType::Lair,    "lair",    "Lair",    4, 9, 'L', 0xFF883A3Au, true, 0xFF883A3Au,  70.0f },
    {LandmarkType::Shrine,  "shrine",  "Shrine",  1, 6, '+', 0xFFE2E2E2u, true, 0xFFE2E2E2u,  90.0f },
    {LandmarkType::Mine,    "mine",    "Mine",    2, 7, 'M', 0xFF8B6332u, true, 0xFF8B6332u,  60.0f },
    {LandmarkType::Tower,   "tower",   "Tower",   3, 7, 'T', 0xFF6E6E89u, true, 0xFF6E6E89u,  80.0f },
};
static_assert(rows_in_enum_order(kLandmarks, &LandmarkDef::type),
              "kLandmarks row order must mirror LandmarkType");

inline constexpr const LandmarkDef& landmark_def(LandmarkType t) {
    return kLandmarks[std::size_t(t)];
}

} // namespace sm
