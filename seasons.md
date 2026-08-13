# Seasons

A data-driven climate cycle derived **purely from world time**. Seasons hold no
state of their own — a season is a total function of the absolute `worldTime.day`
the clock already counts, the day-scale analogue of the hour-scale day/night
fraction the renderer derives from `worldTime.hour`. Nothing new is serialized,
so old saves keep loading and `kSaveVersion` does not move (same "derive, don't
store" discipline as the possessed-macro identity remap).

## The model — one table, one pure lookup

`src/macro/seasons.h` (header-only, zero dependencies):

- `enum class Season { Spring, Summer, Autumn, Winter, Count }`.
- `struct SeasonDef { id; name; tempOffset; yieldMul; tintRGB; }` — one row per
  season in `kSeasons[]`, matching the `landmark_registry.h` / `biomes.h` table
  idiom (a `constexpr` array indexed by the enum + an `inline constexpr`
  accessor). **Adding or retuning a season is one row — no engine change, no
  if-chain.**
- `kDaysPerSeason = 32` is the single tunable that sets year length
  (`kDaysPerYear = 4 * 32 = 128`). 32 rather than a calendar month's 30 because
  the season is the top rung of the time ladder ([time.md](time.md)): a year
  comes to exactly 2^20 ticks.
- `season_at(int day)` — pure derivation; 1-based (`day 1 → Spring`), and total:
  negative / zero days wrap cleanly so no caller ever guards the argument.
- `season_def(Season)` / `season_temp_offset(int day)` — accessors.

| Season | tempOffset | yieldMul | tint     |
|--------|-----------:|---------:|----------|
| Spring |      +0.00 |     1.10 | `8FBF6B` |
| Summer |      +0.15 |     1.25 | `E0C060` |
| Autumn |      −0.05 |     0.90 | `C87A3A` |
| Winter |      −0.20 |     0.60 | `BFD0E0` |

The offsets are ordered `summer > spring(0) ≥ autumn > winter` — the property
the foliage consumer relies on; `seasons_test` locks it.

## Live consumer — subworld foliage

Season nudges the temperature that drives **tree-species selection**. It is
applied at a single source — `SubworldEngine::resolve_context`
(`src/sub/engine.cpp`), where the raw macro-cell temperature becomes
`c.macroTemperature`:

```cpp
const float seasonOffset = gs_ ? season_temp_offset(gs_->worldTime.day) : 0.0f;
c.macroTemperature = std::clamp(t + seasonOffset, 0.0f, 1.0f);
```

Because both the CPU tree dispatch (`main.cpp` → `tree_type_for_temperature`)
and the renderer's atlas bake (`vk_renderer_3d.cpp`, graphics-owned) read
`macroTemperature`, applying the offset **at the source** gives seasonal foliage
to both paths for free — no renderer edit, the same fold-at-source discipline as
the directional-lighting night-glow fix. In the cold half of the year forests
shift toward evergreen/autumn species; in summer toward warm-band species.

**Biome classification is deliberately left on the raw climate `t`** (the
`biome_from_climate(t, m)` call in the same function keeps the unmodified
temperature): a forest must not reclassify to tundra in winter — only its
*trees* change, not the land type.

## Extending it

The columns already on `SeasonDef` are the seams for the next consumers, each a
one-multiply hook at its own site (no schema change):

- **`yieldMul`** — agricultural / economy output. The daily economy tick
  (`world_tick.cpp` → `tick_economy_(gs, day)`) already carries the `day`;
  a future harvest-cycle reader multiplies village yield by
  `season_def(season_at(day)).yieldMul`. (Left unwired for now: the current
  `economy.cpp` gather/produce functions take no `day` and gather all resources
  uniformly, so a clean hook needs a small signature/scope change — deferred as
  its own increment rather than bundled here.)
- **`tintRGB`** — the seasonal tint. CONSUMED by the subworld day sky (Sky
  Inc E): `sub/sky.h build_sky_context` pre-blends it toward white at
  `kSkySeasonTintStrength` and `sky.frag` multiplies the day gradient +
  clouds by it (scaled by dayF — the night sky stays untinted), so winter
  pales the sky, summer gilds it, autumn rusts it. A macro-overlay reader
  blending it into the world-map palette remains a natural future consumer.
- **spawn rosters / biome tint** — natural future readers of `season_at`.

## Tests

`tests/seasons_test.cpp` (CTest-registered) locks the data contract independent
of the renderer: 4 equal seasons starting at Spring on day 1, contiguous
`kDaysPerSeason` blocks that repeat every `kDaysPerYear`, `season_at` pure and
periodic and total over non-positive days, and the offset ordering. Latest run:
`OK seasons_test: 4x32-day year from day 1=Spring, pure+periodic, wraps
non-positive days, offsets summer>spring(0)>=autumn>winter (year=128 days)`.

## Seasons & Weather — the system to be (owner's vision, 2026-08-13)

> Recorded ahead of its own track: «сезонность — это не ресурсы, а отдельная
> большая система: и снег, и времена года, и снег в макромире, и дожди, и
> засухи — расширяемо». What stands above is the CALENDAR half, already
> live; what follows is the WEATHER half and the ruling that shapes it.

**The ruling — weather is a FIELD.** The engine is data-oriented and already
speaks fields fluently ([resources.md](resources.md)): per-cell quantities
over the macro map behind one read door. Weather joins that family — one
uniform design, not a second dialect: a WEATHER FIELD over macro cells
(precipitation / cloud / wind / drought state per cell), the macro truth the
owner's «макро = истина, микро рендерит» law always intended. The subworld
sky already carries the receiving lanes (SkyContext precip01/cloudiness01/
wind — deliberately laid in Sky Inc E for exactly this): `build_sky_context`
stops deriving weather from the bare calendar (`weather_at` — today a
GLOBAL function, the same rain over the whole planet) and starts reading
the field at the player's cell. Whether the field's content is derived
fresh each day (climate × calendar × seed, nothing serialized) or carries
simulated state is the track's own first decision — the FIELD shape is
decided, the filling of it is not.

**Consumers the track wires (each a data hook, никогда ветка):**

| Consumer | Hook |
| --- | --- |
| Growth law | a season/weather multiplier in `growthAt` (resources.md) — no growth in winter, droughts starve the fields; deliberately deferred INTO this track so growth is balanced once, not twice |
| Fields & farmer | seasonal crops (golden in autumn, bare in winter — the Session 24 остаток) and the reaping gate |
| Subworld sky | already renders precip/clouds/lightning — switches source from calendar to field |
| Macro map | SNOW in the macroworld: the map palette reads the same field (winter whitens the cold latitudes), the seasonal `tintRGB` finally gets its macro consumer |
| Travel | snow/mud as movement-cost context (the same terrain-weight table, a data column — Session 21's one law of movement) |
| Fauna / economy | breeding seasons, `yieldMul` finally wired, drought → famine pressure through the honest econ day |

**Extensibility promise:** a weather KIND (hail, storm, drought, blizzard)
is a row; a consumer is a hook reading the field. Nothing here may grow an
if-chain per season — the `SeasonDef`/`kSeasonPrecip` table idiom is the
template.

The track's prompt lives in
[proposals/session-prompts.md](proposals/session-prompts.md) § «Сессия —
сезоны и погода».
