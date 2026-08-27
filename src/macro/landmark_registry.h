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

    // Faction forced onto every creature this place rolls (spawn law,
    // fauna.h): a ruin's wolves ARE demons. nullptr = each row keeps its own.
    const char*      spawnFaction = nullptr;

    // How RICH this place is — the world's modulation of the coin a body
    // carries (owner ruling 2026-08-27: «кошелёк по строке × богатство
    // места», the S10 law «база из таблицы, мир — модуляция», applied through
    // the one context door). The row's purse (macro/npc.h kNpcPurse) says
    // what a merchant IS worth; this says whether he trades in a capital or
    // scrapes a living by a ruin. 1.0 = open land, the silent default.
    // When the honest-loan track lands (CANON S5: a body's belongings are a
    // LOAN from its home landmark's stock), this coefficient is what the
    // actual stockpile query replaces — the socket is deliberate.
    float            wealthMul = 1.0f;
};

// Night-light columns (lightColor / lightPop) drive the universal macro
// lighting system (macro_lighting.cpp). Inhabited types (city, village) emit
// the TS warm hearth tint scaled by live population (lightPop = 0). Fixed POIs
// emit their own tint at a synthetic strength. Set lightColor = 0 to opt a type
// out of night glow entirely — the single data-driven gate.
// minZone/maxZone are DANGER BYTES on the 0..255 continuum (owner,
// 2026-08-24). The old 0..9 rows translate as band edges: min = band*256/10,
// max = (band+1)*256/10 - 1 — City "0..2" became 0..76, Spire "5..9" became
// 128..255, unchanged in meaning, finer in resolution.
inline constexpr LandmarkDef kLandmarks[std::size_t(LandmarkType::Count)] = {
    {LandmarkType::None,    "none",    "",          0, 255, ' ', 0x00000000u, true, 0x00000000u,   0.0f },
    {LandmarkType::City,    "city",    "City",      0,  76, '#', 0xFFE7D27Au, true, 0xFFFFC76Bu,   0.0f, nullptr, /*wealth*/1.5f },
    {LandmarkType::Village, "village", "Village",   0, 101, 'v', 0xFFCCB068u, true, 0xFFFFC76Bu,   0.0f, nullptr, /*wealth*/1.0f },
    {LandmarkType::Spire,   "spire",   "Spire",   128, 255, 'I', 0xFFA86CFFu, true, 0xFFA86CFFu, 200.0f, "demons", /*wealth*/1.25f },
    {LandmarkType::Ruin,    "ruin",    "Ruin",     51, 229, 'r', 0xFF8E8576u, true, 0xFF8E8576u,  40.0f, "demons", /*wealth*/0.5f },
    {LandmarkType::Lair,    "lair",    "Lair",    102, 255, 'L', 0xFF883A3Au, true, 0xFF883A3Au,  70.0f, nullptr, /*wealth*/1.25f },
    {LandmarkType::Shrine,  "shrine",  "Shrine",   25, 178, '+', 0xFFE2E2E2u, true, 0xFFE2E2E2u,  90.0f },
    {LandmarkType::Mine,    "mine",    "Mine",     51, 203, 'M', 0xFF8B6332u, true, 0xFF8B6332u,  60.0f, nullptr, /*wealth*/1.25f },
    {LandmarkType::Tower,   "tower",   "Tower",    76, 203, 'T', 0xFF6E6E89u, true, 0xFF6E6E89u,  80.0f },
};
static_assert(rows_in_enum_order(kLandmarks, &LandmarkDef::type),
              "kLandmarks row order must mirror LandmarkType");

inline constexpr const LandmarkDef& landmark_def(LandmarkType t) {
    return kLandmarks[std::size_t(t)];
}

} // namespace sm
