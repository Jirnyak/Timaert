# Time

One quantum, one ladder, one place where real seconds exist.

Everything the world does — a march, a harvest, a lord's errand, the sun — is
measured in **ticks**. The tick is an integer, the clock is a single `uint64`,
and every unit above it is a whole number of the one below.

```
1 tick                                the world's quantum
64 ticks     = 1 real second   (2^6)  the fixed simulation step
8192 ticks   = 1 game day      (2^13) = 128 real seconds
32 days      = 1 season        (2^5)
128 days     = 1 year          (2^7)  = 2^20 ticks exactly
```

`core/time.h` owns all of it. `macro/seasons.h` owns the top two rungs.

## Why an integer

The world used to run on four unrelated rhythms: the frame's `dt`, a float
minute accumulator, a 0.5-second AI cadence, and a daily queue budgeted per
frame. Four rhythms is four places to drift, a save that could not state the
time to better than a fraction of a minute, and a simulation whose outcome
depended on the frame rate of the machine that ran it.

Now the frame earns whole steps and nothing else moves the world. The clock
cannot drift because it never accumulates: it is a counter, and the calendar is
read off it.

## The minute is not stored

A day holds 1440 minutes and 1440 is not a power of two. It does not need to
be, because the minute is derived, and on an 8192-tick day the derivation is
exact integer arithmetic:

```
1440 / 8192 = 45 / 256    =>  minute = (t * 45) >> 8      // 0..1439
  24 / 8192 =  3 / 1024   =>  hour   = (t *  3) >> 10     // 0..23
```

Since `floor(floor(a/b)/c) == floor(a/(b*c))`, the hour derived directly and the
hour implied by `minute / 60` are the same number for every tick that exists —
two readings of one instant cannot disagree. `time_ladder_test` walks all 8192
ticks of a day and proves it, along with: the display is gap-free (no minute is
ever skipped) and onto (every one of the 1440 is shown), and naming an `hh:mm`
round-trips back to the first tick that reads it.

The price, stated plainly: an hour is 341.33 ticks, so hour boundaries fall
*between* ticks and a minute lasts 5 ticks or 6. That is 16 ms of real time, and
it never accumulates.

**Advancing by a duration aims at a target, not at a length.** "Six minutes from
here" depends on where in the current minute you stand, so
`ticks_to_advance_minutes(from, n)` computes the first tick that *reads* n
minutes later. Ask for a rounded length instead and a clock standing a quarter
of the way into 08:00 can advance five minutes when six were paid for.

## The frame is not the world

`main.cpp` accumulates raw performance-counter units — integers straight from
the OS — and spends them on whole simulation steps. Everything below that call
is presentation and runs once per frame, whatever the world did:

```
frame:  poll input
        N = whole steps the wall clock has earned   (0 on a fast machine,
        for each: one fixed step of sm::kStepSeconds  several after a stall)
        draw once
```

A stall is **skipped, not owed**: at most `kMaxSimStepsPerFrame` steps are run
in one frame, so a hitch costs an eighth of a second of world time instead of
starting a spiral where each frame owes more than the last.

The developer `simspeed` multiplier scales the step count and carries its
fractional part between frames. At the normal `1.0` the carry is exactly zero,
so ordinary play is drift-free and only a deliberate fast-forward rounds.

## Underground the day stretches

In the subworld, `kSubworldTickDivisor = 16` simulation steps buy one tick of
world time: a game hour costs 85 real seconds instead of 5. The simulation does
not slow down — your body still moves at the full step rate, it is the *day*
that stretches. The leftover steps are kept as a whole number in the tick
runtime, so pausing, saving or walking out mid-divisor loses nothing.

Because the whole macro world reads the same clock, the lords outside slow down
with it. Nobody crosses the continent while you clear one room.

## Every rate is a game-time rate

Real seconds appear in exactly one constant, `kTicksPerRealSecond`. Everything
else is denominated in game time:

| Rate | Unit | Where |
|---|---|---|
| macro march | 32 cells per game **hour** | `macro/movement_cost.h` |
| bar recovery | 10 points per game **hour** | `macro/player_recovery.cpp` |
| travel stamina | per **cell**, not per hour | `macro/movement_cost.h` |
| subworld walk | 96 tiles per **real** second | `app/main.cpp` |

That last row is the deliberate exception. Down in the subworld you are a body
doing a thing in real time; up on the map you are an abstraction of a journey.
Two kinds of motion, two denominators, and the difference is written down rather
than stumbled into.

The point of the rest of the table: **the length of a day is a matter of feel,
not of balance.** Lengthen it and the world simply takes longer to live through
— the marching economy does not move a point, because the march was never
quoted in real seconds.

## Consequences worth knowing

- **The save states the instant exactly**, to 1/64 of a real second, in one
  `uint64` (`kSaveVersion 18`).
- **A month of resting and a single frame cost the same three lines.** Minutes,
  hours and days are linear in the tick, so what an advance covered is a
  subtraction however large the jump — `world_tick.cpp` no longer walks the
  clock forward a minute at a time.
- **0 HP means dead, reliably.** The coarse pre-tick frame used to run recovery
  and the death check in the same call, so a player at exactly zero could round
  his way back to 1 before anything noticed. On a fixed step the rule bites the
  same way every time.

## Tests

- `time_ladder_test` — the ladder itself: derivation exact over a whole day,
  gap-free, onto, invertible, whole-day advances at zero residue, and the year
  at exactly 2^20 ticks.
- `world_tick_parity_test` — **no drift**: ten thousand one-tick advances and
  one ten-thousand-tick advance land on the same instant, report the same
  elapsed time and queue the same daily work. Plus the subworld divisor keeping
  its remainder across a split.
- `save_roundtrip_test` — the clock survives a save exactly, mid-minute.
- `macro_travel_parity_test` — the travel economy in cells per game hour.
