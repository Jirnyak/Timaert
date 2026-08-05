// THE time ladder — one authority for every clock in the game.
//
// Before this header the world ran on four unrelated rhythms: the frame's dt,
// a float minute accumulator, a 0.5-second AI cadence and a per-frame daily
// queue. Four rhythms means four places to drift, and a save that could not
// state what time it was to better than a fraction of a minute.
//
// Now there is one quantum — the tick — and everything above it is a whole
// number of ticks:
//
//     1 tick                                the world's quantum
//     64 ticks    = 1 real second   (2^6)   the fixed simulation step
//     8192 ticks  = 1 game day      (2^13)  = 128 real seconds
//     32 days     = 1 season        (2^5)   macro/seasons.h
//     128 days    = 1 year          (2^7)   = 2^20 ticks exactly
//
// REAL SECONDS APPEAR IN EXACTLY ONE CONSTANT ON THIS PAGE and nowhere else in
// the game. Every rate the world has — how far a party marches, how fast a bar
// refills, how often a lord rethinks his errand — is denominated in game time.
// That is what lets the day be lengthened or shortened as a matter of FEEL
// without moving a single number in the economy.
#pragma once
#include <cstdint>

namespace sm {

// ── The two knobs ─────────────────────────────────────────────────────────
// Everything else on this page is derived from these.
inline constexpr std::uint64_t kTicksPerRealSecond = 64;    // 2^6
inline constexpr std::uint64_t kTicksPerDay        = 8192;  // 2^13

// In the subworld the WORLD CLOCK advances one tick per this many simulation
// steps. The simulation itself does not slow down: your body still moves at
// the full step rate, it is the day that stretches, so an hour spent under a
// hill costs 85 real seconds instead of 5. One integer divisor replaces the
// old float kSubworldTimeScale, and because the whole macro world reads the
// same clock, the lords outside slow down with it — nobody crosses the
// continent while you clear one room.
inline constexpr std::uint64_t kSubworldTickDivisor = 16;   // 2^4

// ── Real time, for the two places that are allowed to know about it ───────
// The fixed simulation step, in seconds: what every dt-taking system is fed.
inline constexpr float kStepSeconds = 1.0f / float(kTicksPerRealSecond);
// How long a day lasts on the wall clock, on the macro layer. Pure feel; no
// economy hangs off it.
inline constexpr float kRealSecondsPerDay =
    float(kTicksPerDay) / float(kTicksPerRealSecond);
// The bridge for float rates that are quoted per game hour (march speed, bar
// recovery): multiply by this to get the amount owed for one tick.
inline constexpr float kGameHoursPerTick = 24.0f / float(kTicksPerDay);

// ── The calendar, derived ─────────────────────────────────────────────────
// A day holds 1440 minutes, and 1440 is not a power of two. It does not have
// to be: the minute is never STORED, only derived, and on this ladder the
// derivation is exact integer arithmetic —
//
//     1440 / 8192 = 45 / 256    =>  minute = (t * 45) >> 8
//       24 / 8192 =  3 / 1024   =>  hour   = (t *  3) >> 10
//
// and because floor(floor(a/b)/c) == floor(a/(b*c)), the hour derived here and
// the hour implied by minute/60 are the same number for every tick that exists
// — they cannot disagree. time_ladder_test walks all 8192 and proves it.
//
// The price, stated plainly: an hour is 341.33 ticks, so hour boundaries fall
// BETWEEN ticks and a minute lasts 5 ticks or 6. That is 16 ms of real time,
// and it never accumulates — each tick maps to exactly one (day, hour, minute),
// so there is no drift to accumulate.
// The two derivations in their ABSOLUTE form: minutes and hours elapsed since
// tick 0, unwrapped. The clock counts in these when it has to report how much
// time an advance covered — a subtraction instead of the old loop that stepped
// the world forward one minute at a time — and the wrapped calendar below is
// derived from them, so there is exactly one place where the arithmetic lives.
inline constexpr std::uint64_t absolute_minute(std::uint64_t tick) {
    return (tick * 45) >> 8;
}
inline constexpr std::uint64_t absolute_hour(std::uint64_t tick) {
    return (tick * 3) >> 10;
}

inline constexpr int tick_of_day(std::uint64_t tick) {
    return int(tick % kTicksPerDay);
}
// Day 0 is the first day the ladder can express. A new game seeds day 1 so
// that macro/seasons.h reads day 1 as the first day of Spring.
inline constexpr int day_of(std::uint64_t tick) {
    return int(tick / kTicksPerDay);
}
inline constexpr int minute_of_day(std::uint64_t tick) {
    return int(absolute_minute(tick) % 1440);   // 0..1439
}
inline constexpr int hour_of_day(std::uint64_t tick) {
    return int(absolute_hour(tick) % 24);       // 0..23
}
inline constexpr int minute_of_hour(std::uint64_t tick) {
    return minute_of_day(tick) % 60;            // 0..59
}

// ── Going the other way ───────────────────────────────────────────────────
// The FIRST tick that reads a given absolute minute — the inverse of
// absolute_minute, and the only place the reverse conversion is written.
inline constexpr std::uint64_t tick_at_absolute_minute(std::uint64_t minute) {
    return (minute * 256 + 44) / 45;                  // ceil(minute * 256 / 45)
}
// The first tick of a day that reads hour:minute — the earliest t with
// hour_of_day(t) == hour and minute_of_hour(t) == minute. Used by anything that
// names a time of day: the console clock, a smoke script pinning the sun.
inline constexpr std::uint64_t tick_at_time_of_day(int hour, int minute) {
    return tick_at_absolute_minute(std::uint64_t(hour * 60 + minute));
}

// How many ticks move the clock forward by EXACTLY this many game minutes.
//
// Not a duration lookup — a target. An hour is 341.33 ticks, so "six minutes
// from here" depends on where in the current minute `from` sits: ask for a
// rounded duration and a clock standing at 08:00 and a quarter can advance five
// minutes when six were paid for. Aiming at the minute we want to READ instead
// makes the answer exact from any phase, which is what "rest six hours" and
// every smoke assertion about elapsed time actually mean.
inline constexpr std::uint64_t ticks_to_advance_minutes(std::uint64_t from,
                                                        std::int64_t minutes) {
    if (minutes <= 0) return 0;
    const std::uint64_t target =
        absolute_minute(from) + std::uint64_t(minutes);
    return tick_at_absolute_minute(target) - from;
}
inline constexpr std::uint64_t ticks_to_advance_hours(std::uint64_t from,
                                                      std::int64_t hours) {
    return ticks_to_advance_minutes(from, hours * 60);
}

// ── The frame → tick converter ────────────────────────────────────────────
// The ONE place real time enters the game, and the answer to "does a stutter
// break the clock?".
//
// Ticks are anchored to the OS performance counter, NOT to frames: 64 ticks is
// a real second whether the machine draws 30 frames in it or 240. A frame earns
// whole steps and CARRIES the remainder, so jitter costs nothing — a run of
// wildly uneven frames produces exactly the same number of steps as a run of
// even ones covering the same wall time. Everything is integer, so a machine
// running for a week has lost nothing to rounding.
//
// The one exception is deliberate and is the anti-spiral guard: a frame may run
// at most `maxSteps`, and time beyond that is DISCARDED, not owed. Without it a
// long stall would demand a catch-up frame so expensive that it stalls again,
// each one owing more than the last. With it, a freeze simply costs the world
// the time it was frozen for — at kMaxSimStepsPerFrame = 8 that is anything
// past 125 ms. A machine too slow to sustain the step rate therefore runs the
// world in slow motion rather than falling over.
struct FrameSteps {
    int           steps = 0;   // fixed steps this frame earned
    std::uint64_t carry = 0;   // counter units carried to the next frame
};

inline constexpr FrameSteps steps_for_elapsed(std::uint64_t carry,
                                              std::uint64_t elapsed,
                                              std::uint64_t countsPerStep,
                                              int maxSteps) {
    FrameSteps out{};
    if (countsPerStep == 0 || maxSteps <= 0) return out;
    std::uint64_t accum = carry + elapsed;
    const std::uint64_t cap = countsPerStep * std::uint64_t(maxSteps);
    if (accum > cap) accum = cap;              // a stall is skipped, not owed
    out.steps = int(accum / countsPerStep);
    out.carry = accum - std::uint64_t(out.steps) * countsPerStep;
    return out;
}

// ── The world's clock ─────────────────────────────────────────────────────
// ONE field. Day, hour and minute are not stored beside it — they are read off
// it, so two views of the same instant cannot drift apart, a save states the
// time exactly to the tick, and no code path can set the hour and forget the
// minute. This is the same discipline sub/height.h and macro/faction.h keep:
// one authority, everything else derived.
struct WorldTime {
    std::uint64_t tick = 0;

    constexpr int day()    const { return day_of(tick); }
    constexpr int hour()   const { return hour_of_day(tick); }
    constexpr int minute() const { return minute_of_hour(tick); }
};

// Build a clock reading — for seeding a new game, for the console setting the
// hour, for a smoke script pinning the sun. Everything that WRITES the time
// goes through here rather than assigning parts.
inline constexpr WorldTime world_time_at(int day, int hour, int minute) {
    return WorldTime{ std::uint64_t(day) * kTicksPerDay
                      + tick_at_time_of_day(hour, minute) };
}

// ── The ladder must hold, at compile time ─────────────────────────────────
static_assert((kTicksPerDay & (kTicksPerDay - 1)) == 0,
              "a day must be a power of two ticks");
static_assert((kTicksPerRealSecond & (kTicksPerRealSecond - 1)) == 0,
              "the step rate must be a power of two");
static_assert((kSubworldTickDivisor & (kSubworldTickDivisor - 1)) == 0,
              "the subworld divisor must be a power of two");
static_assert(kTicksPerDay % kTicksPerRealSecond == 0,
              "a day must be a whole number of real seconds");
static_assert(kTicksPerDay % kSubworldTickDivisor == 0,
              "the subworld divisor must divide the day");
// The two derivations are exact rationals, not approximations. If either of
// these fails the shifts above are lies and the clock quietly bends.
static_assert(kTicksPerDay * 45 == 1440 * 256, "minute = (t*45)>>8 is not exact");
static_assert(kTicksPerDay *  3 ==   24 * 1024, "hour = (t*3)>>10 is not exact");

} // namespace sm
