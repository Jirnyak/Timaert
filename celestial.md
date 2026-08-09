# Celestial — moons & constellations

A data-driven night sky, derived **purely from world time** and authored data.
Like [seasons](seasons.md), a moon holds no state of its own: its phase is a
total function of the absolute `worldTime.day()` the clock already counts
([time.md](time.md) — the day is derived from the one tick counter). Nothing
new is serialized, so old saves keep loading and `kSaveVersion` does not move
(the same "derive, don't store" discipline).

`src/macro/celestial.h` (header-only, zero deps beyond `<cmath>`/`<cstdint>`).

## Moons — procedural phases

Timaert has more than one moon; each advances on its own cycle so they are
rarely full together, giving a varied sky with no authored keyframes.

- `enum class MoonId : std::uint8_t { Pale, Crimson, Count }`.
- `struct MoonDef { id; name; baseSize; colorRGB; cyclePeriodDays;
  phaseOffsetDays; orbitTiltDeg; }` — one row per moon in `kMoons[]`, matching
  the `seasons.h` / `landmark_registry.h` idiom (a `constexpr` array indexed by
  the enum + inline accessors). **Adding a third moon is one row.**
- `moon_phase01f(id, dayf)` — the **master formula**: phase in `[0,1)` of a
  *fractional* day (`0` = new, `0.5` = full), so the sky glides through
  midnight (a per-integer-day phase would pop the moon ≈13° at every day flip).
  `moon_phase01(id, day)` delegates to it (`float(day)`), so the two can never
  drift. Pure, periodic over the moon's own `cyclePeriodDays`, and total
  (non-positive days wrap cleanly), exactly like `season_at`.
- `moon_illumination01f / moon_illumination01` — the lit fraction
  `(1 − cos 2π·phase)/2`, `0` at new and `1` at full. This is what the renderer
  scales the lit disc / moonlight by, **replacing today's hardcoded always-full
  moon**.
- `moon_is_waxing(id, day)` — growing vs. shrinking (crescent orientation).
- `moon_color_rgb(id, rgb[3])` — the authored `0xRRGGBB` unpacked to `[0,1]`
  floats (the form push constants / light colours want).

| Moon | period | offset | baseSize | colour | tilt |
|------|-------:|-------:|---------:|--------|-----:|
| Selûne (Pale)   | 28 d | 0 | 1.00 | `E8ECF5` |   9° |
| Vharûn (Crimson)| 11 d | 3 | 0.55 | `D98A6A` | −16° |

Coprime-ish periods mean full moons rarely coincide (~every 308 days).

## Sky position — procedural orbits (no `-sunDir` decree)

A moon's *place* derives from the **same phase** that lights it: it rides the
sun's daily arc but lags the sun by its phase,

    moonAngle = sunAngle − phase01 · 2π

so the classic facts *emerge* instead of being pinned by contract:

- full moon (phase 0.5) → exactly anti-solar: rises at sunset, highest at
  midnight;
- new moon → travels with the sun, lost in its glare (and unlit anyway);
- waxing quarter → trails the sun by 90°, standing highest at sunset (the
  test that pins the lag *sign* — at full/new, ±π land on the same bearing,
  so only a quarter phase can catch a flipped sign; found by a negative
  control that the anti-solar check alone survived).

Each orbit tilts off the sun's X-Y plane by its own `orbitTiltDeg`, swinging
the arc into Z so two moons never stack on one line.

- `sun_dir(tod)` — the sun's arc (`tod` fraction of day, 0.25 = sunrise). THE
  formula `sub/lighting.h` and `sky.frag` each hardcoded; both consuming it
  from here (Inc B) makes "one celestial direction" a mechanism, not a comment.
- `moon_dir(id, day, tod)` — unit `SkyDir` on the dome, phase sampled at the
  fractional day `day + tod`.

## Night light — contextual, not hardcoded

`night_light(day, tod)` → `NightLight { dir; rgb[3]; strength01; moonIndex }`:
the dominant moon is whichever is both **lit and up** (illumination × the same
smoothstep horizon fade `lighting.h` applies to the sun). On a night when every
moon is new or down, `strength01 == 0` and the world honestly goes dark (the
ambient floor in `lighting.h` prevents pure black) — an owner-approved feature:
dark new-moon nights are context. Directional night light, the water's
moon-path and the sky's bloom all read this one answer, so they can never
disagree.

## Constellations — star-graphs

Stars sit at **fixed** dome positions, so a constellation is a static graph:

- `struct StarDef { name; az; el; brightness; }` — azimuth `[0,360)`° and
  elevation `[0,90]`° on the dome, brightness `[0,1]`.
- `struct StarEdge { a, b; }` — indices into the constellation's own star array.
- `struct ConstellationDef { name; stars; starCount; edges; edgeCount; }`, with
  a `constexpr` star + edge array per figure and a top-level `kConstellations[]`
  pointing at them.

Three authored figures ship: **The Wain** (a 7-star dipper), **The Hunter** (a
belt-and-shoulders figure), **The Serpent** (a short chain). Add a figure by
declaring its two arrays and one `kConstellations` row.

### Counts are derived, never hand-written

`kConstellations` computes each `starCount`/`edgeCount` with `arr_count(arr)`
(`sizeof`-based), so a declared count can never drift past its array. This is
not cosmetic: a hand-written over-count (the first draft said the Wain had 8
edges when the array held 7) is an **out-of-bounds read = UB**. It passed at
`-O0` but *hung* at the shipping `-O3 -flto` — the optimizer assumes UB cannot
happen and miscompiled the bounded loop into an infinite one. Deriving the
counts removes the whole class of bug.

A `constexpr celestial_edges_all_valid()` + `static_assert` in the header then
proves at **build time** that every edge index is in range (and no self-loops),
so a bad hand-authored edge like `{2,9}` in a 7-star figure fails the build for
every consumer — verified with a negative control.

## The star-size seam ("stars too big" — WIRED)

- `kSkyStarSizeScale = 0.60` — uniform shrink factor the shader's star disc
  radius is multiplied by;
- `kSkyStarSizeMin` — a floor so the dimmest star stays ≥ a pixel (applied
  CPU-side in `build_sky_context`, so the shader trusts the one scale it gets).

`sub/sky.h` carries the scale through the `SkyPush` push-constant and
`sky.frag` applies it to all three star layers. This layer owns the *value and
the seam*, the renderer owns the *plumbing*.

## Seam for the renderer (LIVE since Sky Inc B)

The header depends only on the standard library, so consumers include it with
zero coupling to the rest of gameplay. Live consumers today:

- `sub/sky.h` (`build_sky_context`) — moons' `moon_dir` / `moon_illumination01f`
  / tints / sizes + `sun_dir` + the star-size scale, into the sky pass;
- `sub/lighting.h` — `sun_dir` for the day arc, `night_light` for which moon
  lights the night (and the water's moon-path, which reflects that same slot);
- `tests/gpu_smoke3d.cpp` — mirrors the same context into the harness's
  hand-built `SkyPush` (shared-shader contract).

`kConstellations` (star plots) is the remaining unconsumed table — Sky Inc C.

## Tests

`tests/celestial_test.cpp` (CTest-registered) locks the data contract
independent of the renderer: moon phase pure + periodic + total over
non-positive days, illumination in `[0,1]` with new-moon dark / full-moon
bright, the two moons actually desync, every constellation edge references a
real star (no self-loops), and the star-size seam stays sane. It also serves as
the runtime companion to the header's compile-time edge-validity `static_assert`.
