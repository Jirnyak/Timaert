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
#include "tables/seasons.h"

#include <cstdio>
#include <initializer_list>

namespace {

} // namespace

int main() {
    using namespace sm;

    // ── Ladder geometry ──────────────────────────────────────────────────
    const bool ok1 = !(kTicksPerDay != 8192);
    CHECK(ok1, "day is not 8192 ticks");
    if (!ok1) return 1;
    const bool ok2 = !(kTicksPerRealSecond != 64);
    CHECK(ok2, "step rate is not 64/s");
    if (!ok2) return 1;
    // ÷64 is THE VISUAL-PACE RUNG (owner, 2026-09-03): with the cell↔cell
    // parity anchor, the divisor sets how bodies read on screen — 24 tiles/s
    // on the road. Still po2; a retuned rung updates this pin AND the derived
    // kSubworldWalkTilesPerSecond assert (movement_cost.h) together.
    const bool ok3 = !(kSubworldTickDivisor != 64);
    CHECK(ok3, "subworld divisor is not 64");
    if (!ok3) return 1;
    const bool ok4 = !(kRealSecondsPerDay != 128.0f);
    CHECK(ok4, "day is not 128 real seconds");
    if (!ok4) return 1;
    // The top rung: 32 days a season, 128 a year, and a year is exactly 2^20
    // ticks. If the calendar and the ladder ever part company, this catches it.
    const bool ok5 = !(kDaysPerSeason != 32);
    CHECK(ok5, "a season is not 32 days");
    if (!ok5) return 1;
    const bool ok6 = !(kDaysPerYear != 128);
    CHECK(ok6, "a year is not 128 days");
    if (!ok6) return 1;
    const bool ok7 =
        !(kTicksPerDay * std::uint64_t(kDaysPerYear) != (1u << 20));
    CHECK(ok7, "a year is not 2^20 ticks");
    if (!ok7) return 1;

    // ── The derivation, over every tick of a day ─────────────────────────
    int prevMinute = -1, prevHour = -1;
    bool minuteSeen[1440] = {};
    for (std::uint64_t t = 0; t < kTicksPerDay; ++t) {
        const int m = minute_of_day(t);
        const int h = hour_of_day(t);

        const bool ok8 = !(m < 0 || m >= 1440);
        CHECK(ok8, "minute_of_day out of range");
        if (!ok8) return 1;
        const bool ok9 = !(h < 0 || h >= 24);
        CHECK(ok9, "hour_of_day out of range");
        if (!ok9) return 1;

        // The two derivations are the same instant, always.
        const bool ok10 = !(h != m / 60);
        CHECK(ok10, "hour_of_day != minute_of_day / 60");
        if (!ok10) return 1;
        const bool ok11 = !(minute_of_hour(t) != m % 60);
        CHECK(ok11, "minute_of_hour disagrees");
        if (!ok11) return 1;

        // Monotone, and never skipping a minute: each tick either holds the
        // clock or advances it by exactly one.
        const bool ok12 = !(m < prevMinute);
        CHECK(ok12, "minute went backwards");
        if (!ok12) return 1;
        const bool ok13 = !(m > prevMinute + 1);
        CHECK(ok13, "clock skipped a minute");
        if (!ok13) return 1;
        const bool ok14 = !(h < prevHour);
        CHECK(ok14, "hour went backwards");
        if (!ok14) return 1;
        const bool ok15 = !(h > prevHour + 1);
        CHECK(ok15, "clock skipped an hour");
        if (!ok15) return 1;
        prevMinute = m;
        prevHour = h;

        minuteSeen[m] = true;
    }
    // Ends where a day should end, having shown every minute it owns.
    const bool ok16 = !(prevMinute != 1439);
    CHECK(ok16, "last tick of the day is not 23:59");
    if (!ok16) return 1;
    const bool ok17 = !(prevHour != 23);
    CHECK(ok17, "last tick of the day is not hour 23");
    if (!ok17) return 1;
    for (int m = 0; m < 1440; ++m) {
        const bool ok18 = !(!minuteSeen[m]);
        CHECK(ok18, "a minute of the day is never displayed");
        if (!ok18) return 1;
    }

    // ── Inversion: name a time, get the first tick that reads it ─────────
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; ++m) {
            const std::uint64_t t = tick_at_time_of_day(h, m);
            const bool ok19 = !(t >= kTicksPerDay);
            CHECK(ok19, "tick_at_time_of_day left the day");
            if (!ok19) return 1;
            const bool ok20 = !(hour_of_day(t) != h || minute_of_hour(t) != m);
            CHECK(ok20, "tick_at_time_of_day does not read back");
            if (!ok20) return 1;
            // FIRST such tick: the one before it must read something earlier.
            const bool ok21 =
                !(t > 0 && minute_of_day(t - 1) == minute_of_day(t));
            CHECK(ok21, "tick_at_time_of_day is not the first matching tick");
            if (!ok21) return 1;
        }
    }
    const bool ok22 = !(tick_at_time_of_day(0, 0) != 0);
    CHECK(ok22, "midnight is not tick 0");
    if (!ok22) return 1;

    // ── Day boundaries ───────────────────────────────────────────────────
    const bool ok23 = !(day_of(kTicksPerDay - 1) != 0);
    CHECK(ok23, "day rolled early");
    if (!ok23) return 1;
    const bool ok24 = !(day_of(kTicksPerDay) != 1);
    CHECK(ok24, "day did not roll");
    if (!ok24) return 1;
    const bool ok25 = !(tick_of_day(kTicksPerDay) != 0);
    CHECK(ok25, "tick_of_day did not wrap");
    if (!ok25) return 1;
    const bool ok26 = !(minute_of_day(kTicksPerDay) != 0);
    CHECK(ok26, "new day did not start at 00:00");
    if (!ok26) return 1;
    // A thousand days later the calendar is still exactly where it should be:
    // no accumulation, because nothing accumulates — day is a division.
    const bool ok27 = !(day_of(kTicksPerDay * 1000 + 7) != 1000);
    CHECK(ok27, "day drifted over 1000 days");
    if (!ok27) return 1;
    const bool ok28 =
        !(hour_of_day(kTicksPerDay * 1000 + tick_at_time_of_day(13, 37)) != 13);
    CHECK(ok28, "time of day drifted over 1000 days");
    if (!ok28) return 1;

    // ── Absolute counters agree with the wrapped calendar ────────────────
    // The clock reports how much time an advance covered by subtracting these,
    // so if they ever disagreed with what the player is shown, a journey would
    // be billed for hours the sun never moved through.
    for (std::uint64_t t = 0; t < kTicksPerDay * 3; t += 7) {
        const bool ok29 =
            !(absolute_minute(t) / 1440 != std::uint64_t(day_of(t)));
        CHECK(ok29, "absolute_minute disagrees with the day");
        if (!ok29) return 1;
        const bool ok30 = !(absolute_hour(t) / 24 != std::uint64_t(day_of(t)));
        CHECK(ok30, "absolute_hour disagrees with the day");
        if (!ok30) return 1;
        const bool ok31 =
            !(absolute_minute(t) % 1440 != std::uint64_t(minute_of_day(t)));
        CHECK(ok31, "absolute_minute disagrees with the minute of day");
        if (!ok31) return 1;
        const bool ok32 =
            !(absolute_hour(t) % 24 != std::uint64_t(hour_of_day(t)));
        CHECK(ok32, "absolute_hour disagrees with the hour of day");
        if (!ok32) return 1;
    }
    // Absolute counters are monotone and never jump — the same gap-free
    // property as the display, which is what makes subtraction a safe way to
    // count elapsed minutes.
    for (std::uint64_t t = 1; t < kTicksPerDay * 2; ++t) {
        const std::uint64_t d = absolute_minute(t) - absolute_minute(t - 1);
        const bool ok33 = !(d > 1);
        CHECK(ok33, "absolute_minute skipped");
        if (!ok33) return 1;
    }

    // ── WorldTime reads off the one field ────────────────────────────────
    for (int h = 0; h < 24; ++h) {
        for (int m = 0; m < 60; m += 7) {
            const WorldTime wt = world_time_at(3, h, m);
            const bool ok34 =
                !(wt.day() != 3 || wt.hour() != h || wt.minute() != m);
            CHECK(ok34, "world_time_at does not read back");
            if (!ok34) return 1;
        }
    }
    const bool ok35 = !(WorldTime{}.tick != 0);
    CHECK(ok35, "default WorldTime is not tick 0");
    if (!ok35) return 1;
    const bool ok36 = !(world_time_at(0, 0, 0).tick != 0);
    CHECK(ok36, "day 0 midnight is not tick 0");
    if (!ok36) return 1;
    const bool ok37 = !(world_time_at(1, 0, 0).tick != kTicksPerDay);
    CHECK(ok37, "day 1 midnight is not one day of ticks");
    if (!ok37) return 1;

    // ── Advancing by an exact number of minutes, from ANY phase ──────────
    // The property that matters: ask for six minutes and the clock reads six
    // minutes later, whether it stood on a minute boundary or three ticks into
    // one. A rounded duration cannot promise this, which is why the primitive
    // aims at a target minute instead.
    const bool ok38 = !(ticks_to_advance_minutes(0, 0) != 0);
    CHECK(ok38, "zero minutes is not zero ticks");
    if (!ok38) return 1;
    const bool ok39 = !(ticks_to_advance_minutes(0, -5) != 0);
    CHECK(ok39, "negative advance must clamp");
    if (!ok39) return 1;
    for (std::uint64_t from = 0; from < 400; ++from) {
        for (std::int64_t want : {1, 5, 6, 59, 60, 137, 1440}) {
            const std::uint64_t ticks = ticks_to_advance_minutes(from, want);
            const std::int64_t got = std::int64_t(absolute_minute(from + ticks))
                                   - std::int64_t(absolute_minute(from));
            const bool ok40 = !(got != want);
            CHECK(ok40, "advance did not land on the minute asked for");
            if (!ok40) return 1;
            // And it is the CHEAPEST such advance: one tick less falls short.
            if (ticks > 0) {
                const std::int64_t less =
                    std::int64_t(absolute_minute(from + ticks - 1))
                    - std::int64_t(absolute_minute(from));
                const bool ok41 = !(less >= want);
                CHECK(ok41, "advance overshoots by a whole tick");
                if (!ok41) return 1;
            }
        }
    }
    // A whole day asked for from a day boundary is exactly a day of ticks —
    // what keeps "rest a day" from walking away from the sun.
    for (int n = 1; n <= 128; ++n) {
        const bool ok42 =
            !(ticks_to_advance_minutes(0, 1440ll * n) != kTicksPerDay * std::uint64_t(n));
        CHECK(ok42, "whole-day advances accumulate a residue");
        if (!ok42) return 1;
    }
    const bool ok43 = !(ticks_to_advance_hours(0, 24) != kTicksPerDay);
    CHECK(ok43, "24 hours is not a day of ticks");
    if (!ok43) return 1;
    // tick_at_absolute_minute is the inverse of absolute_minute, everywhere.
    for (std::uint64_t m = 0; m < 4000; ++m) {
        const std::uint64_t t = tick_at_absolute_minute(m);
        const bool ok44 = !(absolute_minute(t) != m);
        CHECK(ok44, "tick_at_absolute_minute misses");
        if (!ok44) return 1;
        const bool ok45 = !(t > 0 && absolute_minute(t - 1) == m);
        CHECK(ok45, "tick_at_absolute_minute is not the first such tick");
        if (!ok45) return 1;
    }

    std::printf("OK time_ladder_test: %llu-tick day (%d real s), minute+hour derived "
                "exactly over all %llu ticks, gap-free and onto 1440 minutes, "
                "invertible at every hh:mm, whole days convert with zero residue, "
                "a year is 2^20 ticks\n",
                (unsigned long long)kTicksPerDay, int(kRealSecondsPerDay),
                (unsigned long long)kTicksPerDay);
    return sm::test::report("time_ladder_test");
}
