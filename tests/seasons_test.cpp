// Locks the DATA CONTRACT of the seasons system (tables/seasons.h).
//
// Seasons are a pure derivation of the absolute world day — no serialized state.
// This test proves the derivation is total and stable:
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

#include "check.h"
#include "tables/seasons.h"

#include <initializer_list>

namespace {

// ЛЕСТНИЦА ВРЕМЕНИ (core/time.h): 32 дня сезон, четыре сезона год, 128 дней.
// Оба факта известны КОМПИЛЯТОРУ — значит их нарушение обязано быть
// несобираемым, а не пойманным прогоном (§8 п.6: лучший свидетель тот,
// который нельзя обойти запуском).
static_assert(int(sm::Season::Count) == 4,
              "the year is exactly four seasons — the top rung of the time "
              "ladder, not a table that happens to have four rows");
static_assert(sm::kDaysPerYear == sm::kDaysPerSeason * 4,
              "a year IS four equal seasons and nothing besides them");

void test_a_new_world_wakes_in_spring() {
    using namespace sm;
    // state.cpp seeds worldTime.day = 1.
    CHECK(season_at(1) == Season::Spring, "day 1 of a new world is Spring");
}

// Each season spans exactly kDaysPerSeason contiguous days, in order, and the
// sequence repeats every year. Walk two full years day-by-day: a loop that
// stops at the first bad day would hide how far the drift runs (§8 п.3).
void test_seasons_are_four_contiguous_equal_blocks() {
    using namespace sm;
    const Season order[4] = { Season::Spring, Season::Summer,
                              Season::Autumn, Season::Winter };
    int days = 0, disagreed = 0;
    for (int day = 1; day <= 2 * kDaysPerYear; ++day) {
        const int d0 = (day - 1) % kDaysPerYear;      // 0-based day of year
        const Season expect = order[d0 / kDaysPerSeason];
        ++days;
        if (season_at(day) != expect) ++disagreed;
    }
    CHECK(days == 2 * kDaysPerYear,
          "the walk really covered two whole years — an empty walk proves "
          "nothing");
    CHECK(disagreed == 0,
          "every day of two years lands in its own contiguous season block");
}

void test_the_year_repeats_and_the_day_decides_alone() {
    using namespace sm;
    int sampled = 0, notPeriodic = 0, notPure = 0;
    for (int day : {1, 15, 30, 31, 77, 119, 120, 121}) {
        ++sampled;
        if (season_at(day) != season_at(day + kDaysPerYear)) ++notPeriodic;
        if (season_at(day) != season_at(day)) ++notPure;
    }
    CHECK(sampled == 8, "all eight probe days were actually asked");
    CHECK(notPeriodic == 0,
          "a day and the same day a year later share a season — the cycle "
          "closes");
    CHECK(notPure == 0,
          "the day alone decides the season: asking twice answers twice the "
          "same");
}

// Callers never guard the argument, so the derivation must be total.
void test_days_before_the_first_day_still_resolve() {
    using namespace sm;
    CHECK(season_at(0) == Season::Winter,
          "day 0 is the day before day 1 — the last day of the previous year, "
          "Winter");
    CHECK(season_at(-1) == Season::Winter, "day -1 stays inside the year");
    CHECK(season_at(1 - kDaysPerYear) == Season::Spring,
          "a whole year before day 1 is Spring again — the cycle runs "
          "backwards too");
}

// The property foliage selection depends on: summer is the warmest nudge,
// winter the coldest, spring neutral. Each claim breaks alone (§8 п.4).
void test_temperature_offsets_are_ordered_the_way_the_world_reads_them() {
    using namespace sm;
    const float spring = season_temp_offset(1);                  // Spring
    const float summer = season_temp_offset(1 + kDaysPerSeason); // Summer
    const float autumn = season_temp_offset(1 + 2 * kDaysPerSeason);
    const float winter = season_temp_offset(1 + 3 * kDaysPerSeason);
    CHECK(summer > spring, "summer is the warmest nudge of the year");
    CHECK(spring >= autumn, "autumn is already cooling off spring");
    CHECK(autumn > winter, "winter is colder still — the order never folds");
    CHECK(spring == 0.0f, "spring is the neutral offset (0), the baseline");
    CHECK(winter < 0.0f, "winter cools: its offset is a negative number");
}

void test_every_season_row_round_trips_through_the_table() {
    using namespace sm;
    const Season order[4] = { Season::Spring, Season::Summer,
                              Season::Autumn, Season::Winter };
    int rows = 0, wrongId = 0, nameless = 0;
    for (Season s : order) {
        ++rows;
        if (season_def(s).id != s) ++wrongId;
        if (season_def(s).name == nullptr) ++nameless;
    }
    CHECK(rows == int(Season::Count), "every season id was looked up");
    CHECK(wrongId == 0, "a row answers for its OWN season — the table is in "
                        "enum order, not shuffled");
    CHECK(nameless == 0, "every season row carries a name for the world to "
                         "print");
}

} // namespace

int main() {
    test_a_new_world_wakes_in_spring();
    test_seasons_are_four_contiguous_equal_blocks();
    test_the_year_repeats_and_the_day_decides_alone();
    test_days_before_the_first_day_still_resolve();
    test_temperature_offsets_are_ordered_the_way_the_world_reads_them();
    test_every_season_row_round_trips_through_the_table();
    return sm::test::report("seasons_test");
}
