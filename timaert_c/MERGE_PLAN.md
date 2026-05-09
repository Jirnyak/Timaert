# timaert_c — Merge Plan

`timaert_c/` is the **single canonical** native build going forward.
It is born from a three-way merge:

| Source               | Role                                                   |
|----------------------|--------------------------------------------------------|
| `samosbor_nolod/`    | **DELETED** (folded into `timaert_c/`). Provided OpenGL macro renderer + GLSL biome shaders, EnTT ECS, sub3D, faithful TS gameplay ports (attributes, army, items, economy, language, flag-generator, movement-cost, state factories). |
| `proto_c/`           | Source of the **playable UX shell**: state machine (menu/play/pause/load/settings/stat/map/battle/event), top status bar, bottom command toolbar, settlement/proximity panels, save/load patterns, random-event content (~1 485 LOC catalogue). Re-implemented in ImGui because proto_c uses SDL_Renderer. |
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
4. **GLOB_RECURSE.** Drop a `.cpp` in `src/<layer>/`, it is compiled.
5. **No save compatibility.** Bump `kSaveVersion` for any breaking change.
6. **No legacy code.** Delete deprecated paths immediately.
7. **Performance first.** Better algorithms / data layouts / SoA before
   micro-optimisation.
8. **Never weaker than TS.** A native module must never be noticeably
   *worse* in player-visible behaviour than its TS counterpart — fewer
   events, dumber AI, missing biome variety, etc. Better is fine; weaker
   is a bug.

## Layered architecture

```
L4 content/ → L3 events/ → L2 sub/ → L1 macro/ → core/, gl/, ecs/
ui/ sits above; never owns gameplay.
```

## First milestone (delivered)

- ✅ `timaert_c/` is the **sole canonical native build**;
  `samosbor_nolod/` deleted; only `proto_c/` (read-only reference) and
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
  + `cityLastTradeDay` make trade deterministic across save/load.
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
- ✅ **Politik MST + bridges + finalize** — `politik.cpp` rewritten:
  proper Prim's MST per kingdom seeded at the capital, +1 extra nearest
  non-connected edge per city for redundancy (cap 4), inter-kingdom
  bridge roads between every kingdom pair whose closest city pair is
  within `0.35 ×` half-diagonal. New `finalize_politik` does lake-snap
  for capitals flagged `capital_requires_lake` + multi-source 4-
  neighbour BFS Voronoi over land cells (territories bounded by
  coastlines, never jumping sea).
- ✅ **Natural road network (A* + phantom-edge pruning)** — `trace_roads`
  rewritten to route over a road-aware cost grid (water 50× → impassable,
  mountain 5×, land 1×, existing road 0.3× → branches share corridors)
  using the existing `find_path` A*. Connections whose A* either fails
  or had to cross water are pruned from `politik.cities[*].connections`,
  killing dangling coastal road stubs and ensuring NPC AI / trade never
  see phantom edges. Pipeline reordered: roads are now the **last**
  connectivity step before feature compositing, exactly per user spec
  ("terrain → biomes → cities → features → roads, natural, elegant,
  universal, modular, expandable, minimal").

## Next milestones (priority order)

1. **State machine parity with proto_c** — port `play_state` / `menu_state`
   / `pause_state` / `load_state` / `settings_state` / `stat_state` /
   `event_state` from `proto_c/src/states/*.h` into `src/states/*` as
   ImGui-driven panels. Each state a thin orchestrator over
   `ui::draw_*` + L1 game logic.
2. **Settlement panel** — proto_c layout: name banner, lineage,
   population, garrison, inventory, trade tab, quests tab, build tab.
   Replace current `draw_settlement` placeholder.
3. **Proximity NPC panel** — right-edge dock listing NPCs in
   `DETECTION_RADIUS`. Talk / trade / attack buttons per row.
4. **Random events catalogue** — port `proto_c/src/systems/random_events.cpp`
   (~1 485 LOC) into `src/content/plot/random_events.cpp` as a
   data-driven table consumed by the L3 event bus. **Style only**;
   gameplay values come from TS (`event-types.ts`, `effect-applicator.ts`).
5. **Settlement panel proto_c parity** — port `proto_c/src/states/play_state.h`
   settlement panel (name banner, lineage, population, garrison,
   inventory tabs, trade tab, quests tab, build tab) into
   `ui::draw_settlement_overlay`.
6. **Random events catalogue** — port `proto_c/src/systems/random_events.cpp`
   (~1 485 LOC) into `src/content/plot/random_events.cpp` as a
   data-driven L4 table consumed by the L3 event bus. Style only from
   proto_c; gameplay values from TS.
7. **NPC inventory + character data** — extend `make_npc` in
   `npc_spawn.cpp` to also generate per-NPC inventory (TS
   `generateNpcInventory`) and visual character data
   (`generateNpcCharacter`).

## Cleanup plan

- After milestones 1–3 land, delete `samosbor_nolod/` (its visible
  features now live in `timaert_c/`).
- `proto_c/` is deleted when (4) lands and the random-event catalogue
  is fully consumed by the L3 bus. Until then it is read-only reference.

## Build

```bash
cd timaert_c
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/timaert
```
