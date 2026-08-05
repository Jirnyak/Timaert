// Seasons — a data-driven climate cycle derived purely from world time.
//
// The world clock (macro/state.h WorldTime) counts an ABSOLUTE `day` that never
// wraps into a year. A season is the day-scale analogue of the hour-scale
// day/night fraction the renderer already derives from `hour`: a pure function
// of the already-persisted `day`, holding NO state of its own. Because nothing
// new is serialized, old saves keep loading and kSaveVersion does not move — the
// same "derive, don't store" discipline as the possessed-macro identity remap.
//
// Adding a season, or retuning one, is ONE row in kSeasons — no engine change,
// no if-chain (matches the landmark_registry.h / biomes.h table idiom). Each row
// carries the modifiers its consumers read; a consumer that wants a new seasonal
// effect adds a column here and one multiply at its own site. The first live
// consumer is subworld foliage: season_temp_offset() nudges the temperature that
// drives tree-species selection (sub/tree_atlas.cpp), so forests shift toward
// autumn/evergreen in the cold half of the year with no per-tree code.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sm {

enum class Season : std::uint8_t { Spring = 0, Summer, Autumn, Winter, Count };

struct SeasonDef {
    Season        id;
    const char*   name;
    // Added to the 0..1 temperature fed to temperature-driven classifiers
    // (foliage today; biome tint / spawn rosters are natural future readers).
    // Signed; consumers clamp the sum back into [0,1].
    float         tempOffset;
    // Scales agricultural yield for a future economy consumer (see seasons.md).
    // Declared now so the harvest cycle is one multiply away, not a schema change.
    float         yieldMul;
    std::uint32_t tintRGB;   // optional seasonal map tint (0xRRGGBB), reserved
};

// The ONE tunable that sets year length: 4 seasons * kDaysPerSeason = a year.
// A row count other than 4 still works (season_at wraps on Season::Count).
//
// 32, not a calendar month's 30: it is the top rung of the time ladder
// (core/time.h), and it keeps the whole thing powers of two — 32 days a season,
// 128 a year, and a year comes to exactly 2^20 ticks.
inline constexpr int kDaysPerSeason = 32;

inline constexpr SeasonDef kSeasons[std::size_t(Season::Count)] = {
    // id              name       tempOffset  yieldMul  tintRGB
    { Season::Spring, "Spring",     +0.00f,     1.10f,  0x8FBF6Bu },
    { Season::Summer, "Summer",     +0.15f,     1.25f,  0xE0C060u },
    { Season::Autumn, "Autumn",     -0.05f,     0.90f,  0xC87A3Au },
    { Season::Winter, "Winter",     -0.20f,     0.60f,  0xBFD0E0u },
};

inline constexpr int kDaysPerYear = kDaysPerSeason * int(Season::Count);

// Pure derivation of the absolute world day. `day` is 1-based (new game starts
// at day 1 -> Spring); negatives are handled so callers never need to guard.
inline Season season_at(int day) {
    int d = (day - 1) % kDaysPerYear;
    if (d < 0) d += kDaysPerYear;
    return Season(d / kDaysPerSeason);
}

inline constexpr const SeasonDef& season_def(Season s) {
    return kSeasons[std::size_t(s)];
}

// Convenience for the foliage consumer: the temperature nudge for a given day.
inline float season_temp_offset(int day) {
    return season_def(season_at(day)).tempOffset;
}

} // namespace sm
