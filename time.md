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
depended on the frame rate of the machine that ran it. **All four are gone** —
each is now a whole number of ticks on the one ladder.

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

## The frame IS the world's clock

**THE TICK IS PRIMARY.** The world's time is the number of ticks that have RUN —
a day is 8192 of them, a year is 2^20, and "128 real seconds" is only what that
comes to on a machine keeping up. Ticks are born from the loop turning, never
from the clock moving.

So one turn of the loop is one tick **and one frame** — the frame rate and the
world's tick rate are the same number. A low frame rate is therefore not a
choppier picture of a world moving at its usual pace, as it was under the old
variable-`dt` loop where a bigger `dt` covered the gap; it is the world itself
living slower, because fewer ticks happened.

The wall clock is consulted for exactly one purpose: **if a turn finished quicker than a tick is worth, wait for the
remainder**, so the world can never run FASTER than nominal. It is never
consulted to decide that ticks are owed. There is no accumulator and no debt.

```
loop turn:  poll input
            ONE fixed step of the world
            draw once
            if the turn was quicker than a tick, wait out the difference
```

The wait lands on the beat, not near it: `SDL_Delay` takes whole milliseconds
and rounds DOWN, so sleeping with it alone undershoots by up to 1 ms a turn —
nearly 7 % at 64 Hz, which would make the world run FASTER than nominal, the one
thing this rule forbids. So the loop sleeps the whole milliseconds and spins out
the sub-millisecond remainder onto the exact deadline.

Everything follows from that without a single special case:

* a slow turn is just a slow turn — one tick, later. Nothing is lost.
* a machine that cannot sustain the rate runs at a lower frame rate and a
  slower world. It does not make the world **live** less.
* a **suspended process** — a closed laptop, an hour on a breakpoint — ran no
  turns and advanced no ticks. On resume it carries on. There is no gap to
  detect and nothing to catch up, because real time was never what produced
  ticks in the first place.

**The present mode is a TIME decision, not a graphics one.** The world advances
one tick per turn, so anything that gates how fast the loop may turn also gates
how fast the world lives. `VK_PRESENT_MODE_FIFO_KHR` blocks until the display's
refresh, which on a 60 Hz screen would hold the loop to 60 turns a second — the
world running 6 % slow because of the *monitor*, not because the game was busy.
That is the real world reaching into the simulation through a side door.

So the swapchain prefers `MAILBOX`: it returns immediately and shows the newest
finished frame, leaving the pace to the loop's own wait — exactly the tick rate
on any display. FIFO remains the fallback because it is the only mode Vulkan
guarantees, and where it is all the surface offers, the world's rate follows the
display. The debug HUD says which one is in force.

Note the difference from a slow machine, which is legitimate: if a TICK takes
longer than its period the frame rate falls and the world slows, and that is the
model working. What must not happen is the world slowing while the game had
time to spare.

The developer `simspeed` multiplier runs several ticks per turn and carries its
fractional part, so 1.0 is exact and only a deliberate fast-forward rounds.

## Underground the day stretches

In the subworld, `kSubworldTickDivisor = 16` simulation steps buy one tick of
world time: a game hour costs 85 real seconds instead of 5. The simulation does
not slow down — your body still moves at the full step rate, it is the *day*
that stretches. The leftover steps are kept as a whole number in the tick
runtime, so pausing, saving or walking out mid-divisor loses nothing.

Because the whole macro world reads the same clock, the lords outside slow down
with it. Nobody crosses the continent while you clear one room.

That is literal, not a figure of speech: macro NPC AI is quoted in world ticks
(`kAiTicks = 32`), so it wakes once per half-hour of game time wherever the
player is standing. Measured on the seed-locked `subworld_time` smoke, a
thousand simulation steps underground: **24186 NPC thinks before, 852 after** —
one sweep instead of thirty-one. Today that saves a fraction of a millisecond,
because a fresh world holds ~850 macro NPCs. It is written for the world that
holds thousands of parties, where it is the difference between a subworld that
runs and one that does not.

## Every rate is a game-time rate

Real seconds appear in exactly one constant, `kTicksPerRealSecond`. Everything
else is denominated in game time:

| Rate | Unit | Where |
|---|---|---|
| macro march | 32 cells per game **hour** | `macro/movement_cost.h` |
| bar recovery | 10 points per game **hour** | `macro/player_recovery.cpp` |
| travel stamina | per **cell**, not per hour | `macro/movement_cost.h` |
| macro NPC thinking | every 32 **ticks** | `macro/npc_ai.h` |
| player time-in-cell | every 32 **ticks** | `app/main.cpp` |
| subworld walk | 96 tiles per **real** second | `app/main.cpp` |

That last row is the deliberate exception. Down in the subworld you are a body
doing a thing in real time; up on the map you are an abstraction of a journey.
Two kinds of motion, two denominators, and the difference is written down rather
than stumbled into.

The point of the rest of the table: **the length of a day is a matter of feel,
not of balance.** Lengthen it and the world simply takes longer to live through
— the marching economy does not move a point, because the march was never
quoted in real seconds.

## Simulated, or merely drawn

The step is for things the world does. The frame is for things the player sees.
`tick_macro_npc_visuals` — the easing of a macro NPC's drawn position toward the
cell its AI put it in — is interpolation for the eye, so it runs once per FRAME
at the rate the frame is actually drawn, not on a 64 Hz step the monitor knows
nothing about. `frame()` takes `frameSeconds` for exactly this class of work.

And it is drawn tighter than that: **nothing that writes game state is ever
handed the real duration of a turn.** Every dt inside a step is the compile-time
constant `kStepSeconds`. Even the easing of a macro NPC's drawn position — pure
interpolation for the eye, but it writes to the ECS — advances by the tick, so a
slow machine cannot smooth it at a different pace than the world moved it.

Real time is read in exactly four places in the whole game, and only one of them
is per-frame:

| where | what for | touches the world |
|---|---|---|
| the loop's wait | decide whether to pause before the next turn | no |
| `macro.record(... SDL_GetTicks())` | shader animation clock (water shimmer) | no, drawing only |
| new-game seed when none is given | *which* world you get | no |
| — | | |

There is no fourth row. That is the point.

## Consequences worth knowing

- **The save states the instant exactly**, in one `uint64` (`kSaveVersion 18`) —
  a tick number, not a duration.
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
