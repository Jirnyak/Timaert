// Locks THE time ladder (core/time.h).
//
// The whole system rests on one claim: the minute and the hour do not have to
// be stored, because on a 8192-tick day they can be DERIVED exactly with
// integer arithmetic. This test refuses to take that on faith — it walks all
// 8192 ticks of a day and checks the derivation is:
//   * in range (minute 0..1439, hour 0..23) for every tick that exists;
//   * monotone and gap-free (a clock that skips 09:13 is a broken clock);
//   * onto (every one of the 1440 minutes is actually shown to the player);
//   * self-consistent — hour_of_day == minute_of_day / 60 for every tick, the
//     property that makes two derivations of the same instant impossible to
//     disagree;
//   * invertible — tick_at_time_of_day lands on the FIRST tick reading that
//     time, so naming a time of day and reading it back is a round trip.
// Then durations: whole days must convert with zero residue, or "rest a day"
// would drift a little further from the sun every time it was used.
//
// If any of this regresses the clock still compiles and still ticks — it just
// stops telling the truth.

#include "check.h"
#include "core/time.h"
#include "macro/seasons.h"

#include <cstdio>
#include <initializer_list>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/time_ladder_test.cpp", 0);
    return 1;
}

} // namespace

int main() {
    using namespace sm;

    // ── Ladder geometry ──────────────────────────────────────────────────
    if (kTicksPerDay != 8192)        return fail("day is not 8192 ticks");
    if (kTicksPerRealSecond != 64)   return fail("step rate is not 64/s");
    // ÷64 is THE VISUAL-PACE RUNG (owner, 2026-09-03): with the cell↔cell
    // parity anchor, the divisor sets how bodies read on screen — 24 tiles/s
    // on the road. Still po2; a retuned rung updates this pin AND the derived
    // kSubworldWalkTilesPerSecond assert (movement_cost.h) together.
    if (kSubworldTickDivisor != 64)  return fail("subworld divisor is not 64");
    if (kRealSecondsPerDay != 128.0f) return fail("day is not 128 real seconds");
    // The top rung: 32 days a season, 128 a year, and a year is exactly 2^20
    // ticks. If the calendar and the ladder ever part company, this catches it.
    if (kDaysPerSeason != 32) return fail("a season is not 32 days");
    if (kDaysPerYear != 128)  return fail("a year is not 128 days");
    if (kTicksPerDay * std::uint64_t(kDaysPerYear) != (1u << 20))
        return fail("a year is not 2^20 ticks");

    // ── The derivation, over every tick of a day ─────────────────────────
    int prevMinute = -1, prevHour = -1;
    bool minuteSeen[1440] = {};
    for (std::uint64_t t = 0; t < kTicksPerDay; ++t) {
        const int m = minute_of_day(t);
        const int h = hour_of_day(t);

        if (m < 0 || m >= 1440) return fail("minute_of_day out of range");
        if (h < 0 || h >= 24)   return fail("hour_of_day out of range");

        // The two derivations are the same instant, always.
        if (h != m / 60) return fail("hour_of_day != minute_of_day / 60");
        if (minute_of_hour(t) != m % 60) return fail("minute_of_hour disagrees");

        // Monotone, and never skipping a minute: each tick either holds the
        // clock or advances it by exactly one.
        if (m < prevMinute) return fail("minute went backwards");
        if (m > prevMinute + 1) return fail("clock skipped a minute");
        if (h < prevHour)   return fail("hour went backwards");
        if (h > prevHour + 1) return fail("clock skipped an hour");
        prevMinute = m;
        prevHour = h;

        minuteSeen[m] = true;
    }
    // Ends where a day should end, having shown every minute it owns.
    if (prevMinute != 1439) return fail("last tick of the day is not 23:59");
    if (prevHour != 23)     return fail("last tick of the day is not hour 23");
    for (int m = 0; m < 1440; ++m)
        if (!minuteSeen[m]) return fail("a minute of the day is never displayed");

    // ── Inversion: name a time, get the first tick that reads it ─────────
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            const std::uint64_t t = tick_at_time_of_day(h, m);
            if (t >= kTicksPerDay) return fail("tick_at_time_of_day left the day");
            if (hour_of_day(t) != h || minute_of_hour(t) != m)
                return fail("tick_at_time_of_day does not read back");
            // FIRST such tick: the one before it must read something earlier.
            if (t > 0 && minute_of_day(t - 1) == minute_of_day(t))
                return fail("tick_at_time_of_day is not the first matching tick");
        }
    }
    if (tick_at_time_of_day(0, 0) != 0) return fail("midnight is not tick 0");

    // ── Day boundaries ───────────────────────────────────────────────────
    if (day_of(kTicksPerDay - 1) != 0) return fail("day rolled early");
    if (day_of(kTicksPerDay) != 1)     return fail("day did not roll");
    if (tick_of_day(kTicksPerDay) != 0) return fail("tick_of_day did not wrap");
    if (minute_of_day(kTicksPerDay) != 0) return fail("new day did not start at 00:00");
    // A thousand days later the calendar is still exactly where it should be:
    // no accumulation, because nothing accumulates — day is a division.
    if (day_of(kTicksPerDay * 1000 + 7) != 1000) return fail("day drifted over 1000 days");
    if (hour_of_day(kTicksPerDay * 1000 + tick_at_time_of_day(13, 37)) != 13)
        return fail("time of day drifted over 1000 days");

    // ── Absolute counters agree with the wrapped calendar ────────────────
    // The clock reports how much time an advance covered by subtracting these,
    // so if they ever disagreed with what the player is shown, a journey would
    // be billed for hours the sun never moved through.
    for (std::uint64_t t = 0; t < kTicksPerDay * 3; t += 7) {
        if (absolute_minute(t) / 1440 != std::uint64_t(day_of(t)))
            return fail("absolute_minute disagrees with the day");
        if (absolute_hour(t) / 24 != std::uint64_t(day_of(t)))
            return fail("absolute_hour disagrees with the day");
        if (absolute_minute(t) % 1440 != std::uint64_t(minute_of_day(t)))
            return fail("absolute_minute disagrees with the minute of day");
        if (absolute_hour(t) % 24 != std::uint64_t(hour_of_day(t)))
            return fail("absolute_hour disagrees with the hour of day");
    }
    // Absolute counters are monotone and never jump — the same gap-free
    // property as the display, which is what makes subtraction a safe way to
    // count elapsed minutes.
    for (std::uint64_t t = 1; t < kTicksPerDay * 2; ++t) {
        const std::uint64_t d = absolute_minute(t) - absolute_minute(t - 1);
        if (d > 1) return fail("absolute_minute skipped");
    }

    // ── WorldTime reads off the one field ────────────────────────────────
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; m += 7) {
            const WorldTime wt = world_time_at(3, h, m);
            if (wt.day() != 3 || wt.hour() != h || wt.minute() != m)
                return fail("world_time_at does not read back");
        }
    }
    if (WorldTime{}.tick != 0) return fail("default WorldTime is not tick 0");
    if (world_time_at(0, 0, 0).tick != 0) return fail("day 0 midnight is not tick 0");
    if (world_time_at(1, 0, 0).tick != kTicksPerDay)
        return fail("day 1 midnight is not one day of ticks");

    // ── Advancing by an exact number of minutes, from ANY phase ──────────
    // The property that matters: ask for six minutes and the clock reads six
    // minutes later, whether it stood on a minute boundary or three ticks into
    // one. A rounded duration cannot promise this, which is why the primitive
    // aims at a target minute instead.
    if (ticks_to_advance_minutes(0, 0) != 0)  return fail("zero minutes is not zero ticks");
    if (ticks_to_advance_minutes(0, -5) != 0) return fail("negative advance must clamp");
    for (std::uint64_t from = 0; from < 400; ++from) {
        for (std::int64_t want : {1, 5, 6, 59, 60, 137, 1440}) {
            const std::uint64_t ticks = ticks_to_advance_minutes(from, want);
            const std::int64_t got = std::int64_t(absolute_minute(from + ticks))
                                   - std::int64_t(absolute_minute(from));
            if (got != want) return fail("advance did not land on the minute asked for");
            // And it is the CHEAPEST such advance: one tick less falls short.
            if (ticks > 0) {
                const std::int64_t less =
                    std::int64_t(absolute_minute(from + ticks - 1))
                    - std::int64_t(absolute_minute(from));
                if (less >= want) return fail("advance overshoots by a whole tick");
            }
        }
    }
    // A whole day asked for from a day boundary is exactly a day of ticks —
    // what keeps "rest a day" from walking away from the sun.
    for (int n = 1; n <= 128; ++n) {
        if (ticks_to_advance_minutes(0, 1440ll * n) != kTicksPerDay * std::uint64_t(n))
            return fail("whole-day advances accumulate a residue");
    }
    if (ticks_to_advance_hours(0, 24) != kTicksPerDay)
        return fail("24 hours is not a day of ticks");
    // tick_at_absolute_minute is the inverse of absolute_minute, everywhere.
    for (std::uint64_t m = 0; m < 4000; ++m) {
        const std::uint64_t t = tick_at_absolute_minute(m);
        if (absolute_minute(t) != m) return fail("tick_at_absolute_minute misses");
        if (t > 0 && absolute_minute(t - 1) == m)
            return fail("tick_at_absolute_minute is not the first such tick");
    }

    std::printf("OK time_ladder_test: %llu-tick day (%d real s), minute+hour derived "
                "exactly over all %llu ticks, gap-free and onto 1440 minutes, "
                "invertible at every hh:mm, whole days convert with zero residue, "
                "a year is 2^20 ticks\n",
                (unsigned long long)kTicksPerDay, int(kRealSecondsPerDay),
                (unsigned long long)kTicksPerDay);
    CHECK(true, "every gate above held");
    return sm::test::report("time_ladder_test");
}
