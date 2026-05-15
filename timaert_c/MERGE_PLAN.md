# timaert_c — Merge Plan

`timaert_c/` is the **single canonical** native build going forward.
It is born from a three-way merge:

| Source               | Role                                                   |
|----------------------|--------------------------------------------------------|
| legacy OpenGL native prototype | **DELETED** (folded into `timaert_c/`). Provided OpenGL macro renderer + GLSL biome shaders, EnTT ECS, sub3D, faithful TS gameplay ports (attributes, army, items, economy, language, flag-generator, movement-cost, state factories). |
| `proto_c/`           | Source of the **playable UX shell**: state machine (menu/play/pause/load/settings/stat/map/event), top status bar, bottom command toolbar, settlement/proximity panels, save/load patterns, random-event content (~1 485 LOC catalogue). Re-implemented in ImGui because proto_c uses SDL_Renderer. **Note:** there is no `battle` state in the design — combat is unified subworld play (see ARCHITECTURE.md §Combat System). |
| `../src/` (TS)       | **Gameplay source of truth.** All constants, formulas, AI, content tables, save schema. Every C++ port matches TS 1:1. |

`proto_c/` will be deleted once `timaert_c/` covers its visible UX
features and content (random-event catalogue). `../src/` (TS / Vite)
remains as the reference build and is **never** deleted.

## Principles (data-orientated, performance-first)

1. **One folder = one canonical native build.** No more parallel ports.
2. **TS is gameplay truth, not code truth.** Constants, balance, and
   content come from `../src/`. Algorithms and data layouts in C++ may be
   redesigned where they end up cleaner, faster, or more idiomatic
   (SoA, EnTT views, branch-free dispatch, POD tables).
3. **proto_c is UX truth.** Layout, state transitions, panel arrangement
   come from `proto_c/src/states/` + `proto_c/src/rendering/hud.h`.
4. **GLOB_RECURSE.** Drop a `.cpp` in a configured source module
   (`src/app`, `src/core`, `src/gl`, `src/ecs`, `src/macro`, `src/sub`,
   `src/events`, `src/content`, `src/ui`, or `src/assets`) and it is compiled.
5. **No save compatibility.** Bump `kSaveVersion` for any breaking change.
6. **No legacy code.** Delete deprecated paths immediately.
7. **Performance first.** Better algorithms / data layouts / SoA before
   micro-optimisation.
8. **Never weaker than TS.** A native module must never be noticeably
   *worse* in player-visible behaviour than its TS counterpart — fewer
   events, dumber AI, missing biome variety, etc. Better is fine; weaker
   is a bug.

## Round 4 Correction (2026-05-11)

Commit `0866bb4` must be read as an integration diff, not a gameplay-progress
claim. Windows/MSVC build success is verification evidence only. TS/Svelte
under `C:\Timaert\src` remains the behavior authority.

Diff audit packet: compare `5b16b69..0866bb4`, then inspect
`src/macro/spawners.cpp`, `src/macro/save.cpp`, `src/app/main.cpp`,
`src/ui/overlays.cpp`, `src/events/*`, and `tests/` first. Classify each
change as `KEEP`, `KEEP WITH FIX`, `REVERT`, or `UNKNOWN`.

Committed test facts: `quest_lifecycle_test` and `save_roundtrip_test` are
present in CMake and pass locally. These prove their native fixtures only;
they do not prove full TS parity.

## Layered architecture

```
L4 content/ → L3 events/ → L2 sub/ → L1 macro/ → core/, gl/, ecs/
ui/ sits above; never owns gameplay.
```

## First milestone (delivered)

- ✅ `timaert_c/` is the **sole canonical native build**;
  legacy native prototype deleted; only `proto_c/` (read-only reference) and
  `src/` (TS truth) remain alongside.
- ✅ **Full TS biome shader system ported** to `macro_renderer.cpp` —
  10 distinct procedural `bt_<biome>` (tundra, taiga, snow, valley,
  meadow, swamp, desert, steppe, tropics, water), 8-neighbour
  signed-distance shore band (water side crisp 4.5 px, land side
  noise-broken 12 px fade), land↔land biome blending (5 px),
  `bt_climateOverlay` with snow patches on cold land and drift ice
  with crack pattern on cold water, zoom strength fade.
- ✅ Top status bar — full-width strip showing
  `Day | HH:MM | HP/MP/SP bars | Gold | Items | Pos | Name + Lv + EXP`
  (proto_c style, ImGui).
- ✅ Bottom command toolbar — full-width strip with semantic intents:
  `II  >  >>  Z | Inv Map Bld Qst Par Eq | Cdx Dip In/Out | – +`
  Wired: pause / diplomacy / settlement / quests / codex / map / zoom /
  enter-or-leave subworld. Other intents emit `ToolbarResult` flags
  ready for handlers.
- ✅ Cell-grid GLSL overlay in `macro_renderer.cpp` — torus cell
  structure visible at zoom ≥ 8, full strength at zoom ≥ 16.
- ✅ World 1024² (matches TS GameScreen).
- ✅ Phase A complete: TS-faithful attributes, army, items, economy,
  language, flag-generator, movement-cost.
- ✅ B1: `default_player` + `default_game_state` factories, faction
  band matrix with PAIR_OVERRIDES + lineage logic, `EconomyState eco`
  embedded on Settlement+Village.
- ✅ **World-gen TS fidelity restored** — `map_generator.cpp` fragment
  shader rewritten as TS-faithful port (additive multi-octave continent
  bias, ridge via `pow(1-abs(noise), 3)`, latitude-driven temperature
  with noise blend, domain warp). Defaults synced to TS
  (`seaLevel=0.40, continentScale=0.50, continentIntensity=0.40,
  ridgeIntensity=0.15, domainWarp=0.30, temperatureVariation=0.30`).
  `snap_cities_to_land` post-pass nudges every city onto land via
  bounded spiral BFS. `build_feature_layer` skips water cells so roads
  / trees / mountains / dirt no longer appear in oceans.
- ✅ **B5 world-tick** — `world_tick.cpp` fully ports `world-tick.ts`:
  fractional-minute accumulator, day-rollover settlement & village
  daily simulation (economy → mood → garrison → 30-day rolling history),
  trade route settlement + dispatch (villages → city, cities → cities &
  villages), player upkeep + ageing. New `GameState::activeTradeRoutes`
  + `cityLastTradeDay` make trade deterministic across save/load. Subworld
  time advancement is runtime-proven by the `subworld_time` smoke path on
  seed 42, including the combined battle-start handoff smoke.
- ✅ **B2 npc registry** — `npc.h` now carries the full TS
  `NPC_TYPE_DEFS` data: per-type label, portrait, baseHp, baseLevel,
  AI-behaviour selector, `CombatTemplate`, name pools (up to 16), and
  random talk lines (up to 6). All POD / constexpr — zero runtime
  allocation. `settlement_faction()` ported as a free helper.
- ✅ **`ARCHITECTURE.md`** — full C++ / OpenGL / EnTT / ImGui translation
  of the outer 827-line TS architecture doc; now the canonical source
  of truth for `timaert_c/`.
- ✅ **Toroidal seam fix** — replaced non-periodic value noise in
  `map_generator.cpp` with the canonical Ken-Perlin permutation-table
  periodic noise (period scales with each fbm call's domain) so the
  master heightfield/moisture/temperature texture tiles seamlessly.
  Also wrapped `wp` in `macro_renderer.cpp`'s biome shader so the
  per-pixel bt_* hash/fbm pattern wraps with the world.
- ✅ **B7 npc-ai full dispatch** — `npc_ai.{h,cpp}` ports all 8 TS
  behaviours (HomeWanderer / Woodcutter / Trader / Nomad / Aggressive
  / Patrol / Teleporter / Wanderer) plus `TreeGrid` (hashed spatial
  grid for O(1) nearest-tree), `TickContext`, and the 0.5 s tick
  accumulator. New `ecs::MacroNpcRuntime` POD carries state, timers,
  sp, home/target settlement ids, teleport cooldown, visual speed.
- ✅ **B8 macro NPC spawning** — `npc_spawn.{h,cpp}` ports `spawnNPCs()`
  faithfully: per-settlement peasants/woodcutters/optional merchant/
  guards, plus global pools (caravans 30 %, bandits 30 %+2, witches
  10 %, sorceresses 5 %), plus per-village peasant gatherers +
  optional woodcutter. Land check via terrain alpha mask;
  `find_valid_spawn` mirrors TS retry-up-to-20 fallback.
- ✅ **Macro overlay markers** — `ui/macro_overlay.{h,cpp}` draws
  settlements (yellow rings + name labels), villages (small brown
  dots), NPCs (kind-coloured dots, hidden when zoomed out below
  6 px/cell) and the player crosshair via ImGui's background draw
  list. Torus-aware world→screen wrap so off-screen markers don't
  smear across the map seam. Lightweight debug pass; not the final
  art but lets the live world be observed.
- ✅ **Macro cell interaction restored** — TS-parity hover + click-to-
  travel. `MacroCursor` (in `ui/macro_overlay.h`) tracks the cell
  under the mouse, shows an ImGui tooltip with `(x, y) / biome /
  feature / landmark` (sampled from `TerrainData` + `FeatureLayer` +
  `gs.settlements`/`villages`), and on left-click queues a path via
  `find_path` over the cached `PathCostData` built once per world.
  `step_macro_walk()` advances the player along the path at 6
  cells/sec, draws a cyan polyline + endpoint flag overlay, and the
  walk cancels the moment the player taps WASD/arrows. ImGui mouse
  is unprojected through the same `(uv-0.5)*viewSize/zoom + cam`
  transform the macro fragment shader uses, scaled by
  `DisplayFramebufferScale` for HiDPI.
- ✅ **Macro feature shader sub-cell fidelity** — single-line bug in
  `macro_renderer.cpp` `main()` had `mapUV` snapped to cell-centres
  (`fract((floor(worldPx) + 0.5) / u_mapSize)`) before being passed to
  every feature overlay. Inside each overlay `worldPos = mapUV *
  u_mapSize` therefore returned a constant per cell, collapsing every
  tree / mountain / road / dirt-road / landmark to a single flat
  pixel-art swatch — the long-running "colored squares" complaint.
  Replaced with continuous `vec2 mapUV = fract(worldPx / u_mapSize)`
  so feature shaders see real sub-cell coords. **Lesson:** the top-
  level `worldPx → mapUV` mapping must stay continuous; cell snapping
  belongs *inside* lookup helpers (e.g. `cellUV` for `featureMap`),
  never at the pipeline input.
- ✅ **Macro decoration painter order** — C++ now mirrors TS
  `renderer.ts` decoration pass: road/dirt are ground overlays, then
  trees, mountains, and landmarks are drawn through one 3×3 far-to-near
  painter overlay. This fixes the old global `tree → mountain → landmark`
  pass order where a near tree could not overlap a farther landmark.
- ✅ **Politik MST + bridges + finalize** — `politik.cpp` rewritten:
  proper Prim's MST per kingdom seeded at the capital, +1 extra nearest
  non-connected edge per city for redundancy (cap 4), inter-kingdom
  bridge roads between every kingdom pair whose closest city pair is
  within `0.35 ×` half-diagonal. New `finalize_politik` does lake-snap
  for capitals flagged `capital_requires_lake` + multi-source 4-
  neighbour BFS Voronoi over land cells (territories bounded by
  coastlines, never jumping sea).
- ✅ **Road routing audit + bounded hardening** — current `trace_roads`
  intentionally diverges from TS `road-network.ts` corridor-guided Bresenham:
  it keeps Politik topology, component-prunes cross-island pairs, then uses
  generation-tagged terrain-cost A* with a large-map step cap. Edges that
  cannot be proven within the budget are pruned instead of running unbounded
  full-map A*. No direct-line or water-stamping fallback exists in the current
  source.

## Next milestones (priority order)

Current Windows/MSVC evidence covers build, launch, title menu, New Game
`[boot] done`, macro walking, Load screen, GUI save/load round trip,
settlement trade/quest accept, NPC Talk/Trade/Attack, spell overlay/casting,
subworld time, `ShowDialog`, and `ShowStory`. Save schema is currently
`kSaveVersion = 8`, and `save_roundtrip_test` passes. Items below still
require TS parity review or targeted runtime proof before being called fully
closed.

1. **State machine parity with proto_c** — Load is runtime-evidenced; finish
   settings / stat / event shell parity as ImGui-driven panels over
   `ui::draw_*` + L1 game logic.
2. **Settlement panel proto_c parity** — `draw_settlement` has runtime evidence
   for trade and quest accept. The Build tab is deliberately a non-action
   surface until a real TS/native build-project data contract exists.
3. **Proximity NPC panel action parity** — right-edge nearby-NPC panel, Talk,
   Trade, and Attack are runtime-evidenced. Remaining work is polish/parity
   review, not first wiring.
4. **Event/story overlay parity** — `ShowDialog` and `ShowStory` now have native
   consumers. Remaining gap: nodeId-only dialog choices are disabled unless a
   concrete effect payload or native node binding exists.
5. **Road visual upgrade proof** — road parity audit is complete; any future
   rewrite must provide same-seed A/B screenshots and keep the rejected-water
   pruning invariant covered by `road_river_generation_test`.
6. **Random events catalogue expansion** — expand `content/plot/encounters.cpp`
   toward the proto_c random-event catalogue style. Gameplay values come from
   TS (`event-types.ts`, `effect-applicator.ts`).

## Cleanup plan

- `proto_c/` is deleted when (4) lands and the random-event catalogue
  is fully consumed by the L3 bus. Until then it is read-only reference.

## Build

Windows/MSVC known-good:

```cmd
cd timaert_c
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
.\build-msvc\timaert.exe
```

Portable native, when SDL2 and SDL2_mixer are available from the system package
manager:

```bash
cd timaert_c
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/timaert
```
