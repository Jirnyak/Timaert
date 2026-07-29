// Locks the DATA CONTRACT of the seasons system (macro/seasons.h).
//
// Seasons are a pure derivation of the absolute world day — no serialized state,
// no kSaveVersion bump. This test proves the derivation is total and stable:
//   * a new game (day 1) starts in Spring;
//   * the year is four equal seasons of kDaysPerSeason and then repeats;
//   * season_at is a PURE function of day (same day -> same season) and wraps
//     cleanly for arbitrarily large days AND for non-positive days (so no caller
//     ever has to guard the argument);
//   * the temperature offsets are ordered the way the world reads them
//     (summer warmest, winter coldest, spring neutral) — the property the
//     foliage consumer in engine.cpp relies on.
// If any of these regress, seasonal foliage silently drifts even though it
// still compiles.

#include "macro/seasons.h"

#include <cstdio>
#include <initializer_list>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL seasons_test: %s\n", msg);
    return 1;
}

} // namespace

int main() {
    using namespace sm;

    // Year geometry: four seasons, equal length, kDaysPerYear total.
    if (int(Season::Count) != 4) return fail("expected exactly 4 seasons");
    if (kDaysPerYear != kDaysPerSeason * 4) return fail("kDaysPerYear != 4 seasons");

    // New game starts at day 1 -> Spring (state.cpp seeds worldTime.day = 1).
    if (season_at(1) != Season::Spring) return fail("day 1 must be Spring");

    // Each season spans exactly kDaysPerSeason contiguous days, in order, and
    // the sequence repeats every year. Walk two full years day-by-day.
    const Season order[4] = { Season::Spring, Season::Summer,
                              Season::Autumn, Season::Winter };
    for (int day = 1; day <= 2 * kDaysPerYear; ++day) {
        const int d0 = (day - 1) % kDaysPerYear;      // 0-based day of year
        const Season expect = order[d0 / kDaysPerSeason];
        if (season_at(day) != expect)
            return fail("season_at disagrees with contiguous-block layout");
    }

    // Wrap: day N and day N + kDaysPerYear are the same season (periodicity),
    // and season_at is pure (idempotent for a fixed day).
    for (int day : {1, 15, 30, 31, 77, 119, 120, 121}) {
        if (season_at(day) != season_at(day + kDaysPerYear))
            return fail("season not periodic over a year");
        if (season_at(day) != season_at(day)) return fail("season_at not pure");
    }

    // Non-positive days must still resolve (callers never guard). Day 0 is the
    // day before day 1, i.e. the last day of the previous year -> Winter.
    if (season_at(0) != Season::Winter) return fail("day 0 must wrap to Winter");
    if (season_at(-1) != Season::Winter) return fail("day -1 must stay in range");
    if (season_at(1 - kDaysPerYear) != Season::Spring)
        return fail("a whole year before day 1 must be Spring again");

    // Temperature-offset ordering — the property foliage selection depends on:
    // summer is the warmest nudge, winter the coldest, spring neutral (0).
    const float spring = season_temp_offset(1);                 // Spring
    const float summer = season_temp_offset(1 + kDaysPerSeason); // Summer
    const float autumn = season_temp_offset(1 + 2 * kDaysPerSeason);
    const float winter = season_temp_offset(1 + 3 * kDaysPerSeason);
    if (!(summer > spring && spring >= autumn && autumn > winter))
        return fail("temp offsets not ordered summer>spring>=autumn>winter");
    if (spring != 0.0f) return fail("spring offset must be neutral (0)");
    if (!(winter < 0.0f)) return fail("winter must be a cooling offset");

    // season_def round-trips the table row for each season id.
    for (Season s : order) {
        if (season_def(s).id != s) return fail("season_def id mismatch");
        if (season_def(s).name == nullptr) return fail("season def missing name");
    }

    std::printf("OK seasons_test: 4x%d-day year from day 1=Spring, pure+periodic, "
                "wraps non-positive days, offsets summer>spring(0)>=autumn>winter "
                "(year=%d days)\n", kDaysPerSeason, kDaysPerYear);
    return 0;
}
