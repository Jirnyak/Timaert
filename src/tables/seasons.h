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
// no if-chain (matches the squad_type.h / biomes.h table idiom). Each row
// carries the modifiers its consumers read; a consumer that wants a new seasonal
// effect adds a column here and one multiply at its own site. The first live
// consumer is subworld foliage: season_temp_offset() nudges the temperature that
// drives tree-species selection (sub/tree_atlas.cpp), so forests shift toward
// autumn/evergreen in the cold half of the year with no per-tree code.
#pragma once

#include <cstddef>
#include "core/table_guard.h"
#include <cstdint>

namespace sm {

enum class Season : std::uint8_t { Spring = 0, Summer, Autumn, Winter, Count };

struct SeasonDef {
    Season        id;
    const char*   name;
    // Added to the 0..1 temperature fed to temperature-driven classifiers
    // (foliage today; biome tint / spawn containers are natural future readers).
    // Signed; consumers clamp the sum back into [0,1].
    float         tempOffset;
    // (КОЛОНКА `yieldMul` УНИЧТОЖЕНА 2026-11-08, M-430. Читателей по всему
    //  дереву — НОЛЬ, и её собственная шапка это признавала: «for a future
    //  economy consumer… declared now so the harvest cycle is one multiply
    //  away». Колонка без названного читателя не живёт (DOD п.9) — она не
    //  лежит тихо, её видит следующий и принимает за закон. Урожай придёт
    //  СТРОКОЙ, когда придёт система, а не схемой, ждущей её годами.)
    std::uint32_t tintRGB;   // seasonal tint (0xRRGGBB): consumed by the subworld
                             // day sky (sub/sky.h seasonTint); map tint is a
                             // natural future reader
};

// The ONE tunable that sets year length: 4 seasons * kDaysPerSeason = a year.
// A row count other than 4 still works (season_at wraps on Season::Count).
//
// 32, not a calendar month's 30: it is the top rung of the time ladder
// (core/time.h), and it keeps the whole thing powers of two — 32 days a season,
// 128 a year, and a year comes to exactly 2^20 ticks.
inline constexpr int kDaysPerSeason = 32;

inline constexpr SeasonDef kSeasons[std::size_t(Season::Count)] = {
    // id              name       tempOffset  tintRGB
    { Season::Spring, "Spring",     +0.00f,  0x8FBF6Bu },
    { Season::Summer, "Summer",     +0.15f,  0xE0C060u },
    { Season::Autumn, "Autumn",     -0.05f,  0xC87A3Au },
    { Season::Winter, "Winter",     -0.20f,  0xBFD0E0u },
};
static_assert(rows_in_enum_order(kSeasons, &SeasonDef::id),
              "kSeasons row order must mirror Season");

inline constexpr int kDaysPerYear = kDaysPerSeason * int(Season::Count);

// Pure derivation of the absolute world day. `day` is 1-based (new game starts
// at day 1 -> Spring); negatives are handled so callers never need to guard.
inline Season season_at(int day) {
    int d = (day - 1) % kDaysPerYear;
    if (d < 0) d += kDaysPerYear;
    return Season(d / kDaysPerSeason);
}

// МОНОТОННЫЙ ОРДИНАЛ СЕЗОНА МИРА — сколько полных сезонов прошло до `day`
// (ряд M-495). `season_at` выше отвечает «какой сезон года» и через год
// повторяет номер; этот ответ не повторяется никогда, поэтому «тот же ли
// сезон» им различимо и через годы. Граница та же, что у `season_boundary`
// ниже: день 1 открывает сезон 0. До первого дня мира сезонов не прошло —
// ноль, а не отрицательный номер.
inline constexpr int season_ordinal(int day) {
    return day > 0 ? (day - 1) / kDaysPerSeason : 0;
}

inline constexpr const SeasonDef& season_def(Season s) {
    return kSeasons[std::size_t(s)];
}

// The season BOUNDARY — the one day the world settles its balances and
// rebakes its slow truths (CANON S19.2: единое окно мира — food and pay for
// every container a season ahead, the 1/8 arrears cut, the
// places' squad court, the expensive field rebakes). Day 1 is the first
// boundary: a new world pays for its first season the morning it is born,
// off the mid-life stores it was seeded with. Total over any day, like
// season_at.
inline bool season_boundary(int day) {
    int d = (day - 1) % kDaysPerSeason;
    if (d < 0) d += kDaysPerSeason;
    return d == 0;
}

// Convenience for the foliage consumer: the temperature nudge for a given day.
inline float season_temp_offset(int day) {
    return season_def(season_at(day)).tempOffset;
}

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла, и это не небрежность:
// заголовок пользуется контуром только в блоке ниже, а вставленная
// наверху строка сдвинула бы ВСЁ содержимое на единицу и сгноила бы
// каждую ссылку документов в этот файл (замер: одна такая вставка в 17
// заголовков сломала 10 ссылок в SKELETON.md и промтах). Номер строки —
// производное (§13 п.5), и дешевле не двигать его вовсе.
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА ────────────────────────────────────────────────
// Судит КОМПИЛЯТОР, а не ревью: строка мира, в которую заложили вектор, строку,
// карту, умный указатель, функтор или виртуальный метод, отсюда НЕ СОБЕРЁТСЯ, и
// сообщение назовёт тип поимённо (`core/row_law.h`, наряд M-169).
// Новая структура в этом файле обязана появиться и в этом списке — за полнотой
// списка следит `arch_guard_test`, иначе стену обходили бы молча, новым типом.
TIMAERT_ROW(sm::SeasonDef);
