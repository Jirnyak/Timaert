# Translation Plan — TS Vite → C++ / OpenGL / EnTT

## Goal

`timaert_c/` is a **performance rewrite** of the Timaert game. The
TypeScript / Vite / WebGL2 build at `../src/` is too slow on real maps —
the macroworld tick, subworld generation, and renderer all stall the main
thread. The C++ port targets a **standalone, native, single-binary
SDL2 + OpenGL 3.2 Core + EnTT game** with zero browser dependencies and
order-of-magnitude better frame times.

## Sources of Truth

Two reference codebases — used for **different purposes**:

| Reference | Role | When to consult |
|-----------|------|-----------------|
| `../src/` (TS / Vite / WebGL2) | **Gameplay source of truth.** Constants, formulas, AI behaviours, content tables, save schema. The C++ port must match this 1:1. | Every translation. Read the TS file before writing the C++ file. |
| `../proto_c/` (SDL2 + SDL2_image + SDL2_ttf, ~5 471 LOC) | **Playable C++ prototype reference.** Older, raw, missing many features (no subworld, no OpenGL, no ECS) but **runs as a real game** with a working menu, HUD, map, settlement panel, save/load, and toolbar. Useful for: state-machine layout, on-screen UI panel arrangement, save binary I/O patterns, terrain wraparound (`tergen.h`), random-event content style. | When designing user-visible UX (menu/HUD/toolbar/panels) or a low-level pattern that has no analogue in the TS web build. **Never** for gameplay constants — those come from TS. |

Read both before starting a UX/shell task. Read TS only for gameplay tasks.

This document tracks what has been translated *faithfully* (matching TS
behaviour 1:1), what is currently a *stub* (compiles but does not match),
and the order in which faithful translation will proceed.

## Principles

1. **Gameplay parity is the floor, not the ceiling.** The C++ port must
   reproduce the *playable behaviour* of the TS module — same content,
   same balance, same player-visible outcomes — *as a minimum*. Where the
   TS design is clearly suboptimal and a known-better idiom from the
   reference games below would be straight-up superior, **do better**:
   - **Macroworld:** *Mount & Blade* (free overworld camera, party-marker
     travel, click-to-move, encounter triggers on movement) and *Dwarf
     Fortress* (deep simulated settlements, faction history, emergent
     economy and politics) are the references. Better camera, smarter
     world tick, richer settlement/economy emergence than TS — fine,
     do it.
   - **Microworld:** *Might & Magic 6/7/8* and *Daggerfall* (first-person
     ARPG dungeon/wilderness exploration, real-time combat with cool-
     downs, generated interiors, item/loot density) are the references.
     If TS subworld is shallower than these, the C++ port is allowed —
     and encouraged — to be deeper.
2. **Constants and balance numbers come from TS by default.** Damage
   matrices, XP curves, item prices, spawn weights, etc. — copy these
   verbatim *unless* keeping them produces an obviously worse-than-
   reference experience. Then redesign with a short note explaining what
   was changed and why (one line in the row's `Notes`).
3. **Code parity is not required.** Algorithms, data layouts, control
   flow are free to be redesigned for C++ idioms (SoA, contiguous tables,
   EnTT views, branch-free dispatch, POD components, GLSL-side work).
4. **One TS module → one C++ TU pair (`.h` + `.cpp` if needed)** *as a
   default* — but merge or split freely when there is a real
   architectural seam, when a single C++ TU subsumes several tiny TS
   files, or when a single TS file has multiple unrelated concerns.
5. **Data-driven tables are constexpr `inline` arrays** in headers,
   indexed by enum value. No `if`/`switch` on type ids in engine code.
6. **Never silently weaker than TS.** A C++ port must never make the
   game noticeably *worse* than TS — fewer events, dumber AI, missing
   biome variety, missing UX affordances. Weaker is a bug.
7. **Verify by playtest + diff.** After porting, walk every TS exported
   symbol and confirm there is *some* representation in the C++ build
   (even if reorganised, merged, or replaced by a better idiom). Drop a
   symbol only with an explicit note in the "Intentional Skips" section.

## Status Legend

| Tag | Meaning |
|-----|---------|
| ✅  | Faithful — matches TS gameplay; reviewed line-by-line |
| 🟨  | Stub — compiles, structure exists, values/branches drift from TS |
| ⏳  | Planned — not started |
| ⛔  | Intentionally skipped (Web-only / N/A in C++) |

Windows/MSVC smoke evidence exists for `build-msvc`, title launch, New Game
`[boot] done`, macro walking, Load screen, settlement trade/quest accept, NPC
Talk, and character tabs. Rows marked complete describe code translation or
build presence; runtime-only behaviours still need their own smoke proof when
noted.

## Module Inventory

### L1 — Macroworld Core (`src/game/*.ts` → `timaert_c/src/macro/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| ✅ | `attributes.ts` (283) | `macro/attributes.{h,cpp}` | All 9 attrs, perks, skills, level XP, garrison helpers |
| ✅ | `army.ts` (262) | `macro/army.{h,cpp}` | RPS matrix, hire/fire/garrison/desertion |
| ✅ | `state.ts` (989) | `macro/state.{h,cpp}` | `default_player` + `default_game_state` factories, faction band matrix with PAIR_OVERRIDES + lineage logic, `EconomyState eco` embedded on Settlement+Village. Current save schema is `kSaveVersion=4`. New `populate_landmarks_from_politik(gs, terrain, seaLevel)` bridges politik cities → `gs.settlements` (full Settlement init: language-named, biome-derived economy archetype + local resources, default garrison) and scatters 1–3 villages per settlement on land cells in a 4–14 cell ring. Without this bridge npc_spawn / overlay markers / world-tick / macro renderer landmark layer all silently saw empty lists. |
| ✅ | `economy.ts` (516) | `macro/economy.{h,cpp}` | 6 resources, 15 hand-tuned goods, gather/produce/prices/consume, trade routes, player buy/sell, terrain mapping |
| ✅ | `items.ts` (514) | `macro/items.{h,cpp}` | Full catalog (12 ids), 8 NPC loot tables, fauna loot, gold formula, settlement loot, useItem |
| ✅ | `npc.ts` (644) | `macro/npc.h` + `macro/npc_spawn.cpp` | Full `NPC_TYPE_DEFS[8]` (label, portrait, baseHp, baseLevel, AIBehaviour, CombatTemplate, names[16], talkLines[6]); per-NPC `NpcLevel`/`NpcInventory`/`NpcCharacter` ECS components emplaced at spawn. Visual character data redesigned as compact POD seed (visualSeed + bodyShape + tint + nameIdx) per relaxed translation policy — no HTML-canvas atlas mirror needed. |
| ✅ | `politik.ts` (646) | `macro/politik.{h,cpp}` | Kingdom defs + Prim's MST per kingdom + 1 extra nearest edge for redundancy + inter-kingdom bridge roads (dist ≤ 0.35) + `finalize_politik` (lake-snap for capitals with `capital_requires_lake` + multi-source 4-neighbour BFS Voronoi over land cells, terrain-aware territories that never jump the sea) |
| ✅ | `language.ts` (247) | `macro/language.{h,cpp}` | Full phonotactics: 6 vowels, 17 consonants, Zipf weights, 3-5 weighted syllable templates, doubling, unique-name |
| ✅ | `pathfinding.ts` (244) | `macro/pathfinding.{h,cpp}` | TS-faithful: cost-grid A* with octile heuristic + indexed binary min-heap (key=y*W+x dedup), torus wrap, edge cost = costGrid[dest] * stepLen. `build_cost_grid` ports buildCostGrid from movement-cost.ts. |
| ✅ | `world-tick.ts` (240) | `macro/world_tick.{h,cpp}` | Daily settlement & village ticks (economy → mood → garrison → 30-day rolling history), trade-route settle + dispatch (villages → nearest city, cities → cities & villages), player upkeep + ageing. Sub-minute accumulator (`kRealSecondsPerDay = 100`). Village history is included in the current v4 save schema. Hourly `TimeAdvance` event emit is wired through current event processing. Subworld time is instrumented but lacks reliable runtime proof. |
| ✅ | `tree-spawner.ts` (441) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | GLSL renderer + CPU density: domain-warped multi-scale FBM (large×0.40 + med×0.35 + fine×0.25) with smoothstep curve t0=0.35 t1=0.55, biome exclusion via 3×3 climate matrix, shoreline buffer + mountain cap; `ihash01` bit-identical to TS |
| ✅ | `mountain-spawner.ts` (199) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | TS file is GLSL-only (no CPU spawner). GLSL inlined verbatim into kFS: 4 rock/snow palettes, `hParam = (h - threshold) / (1 - threshold)`-driven peak height, 2-peak composition above hParam>0.55, snow line, drop shadow, 2×2 cell footprint with torus-safe local UV. CPU "placement" = pass 1 of `build_feature_layer` (height ≥ threshold → FT_Mountain). |
| 🟨 | `road-network.ts` (180) + `road-spawner.ts` (143) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | Current `trace_roads()` uses budgeted torus A* with reusable scratch and a dry/short Bresenham fallback. It reaches boot; remaining debt is route quality under expansion budgets and pruning rules. |
| ✅ | `dirt-road-spawner.ts` (180) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | GLSL renderer + CPU placement: spiral search up to 60 tiles → torus-aware lerp trace, skips villages already on roads, doesn't overwrite road cells, `landMaskA` filters water/ice |
| ✅ | `features.ts` (89) | `macro/features.h` + `spawners.cpp::build_feature_layer` | Enum + grid + 4-pass writer (Mountain via height threshold → Tree → DirtRoad → Road, last-writer-wins). C++ adds water filter (skip cells with red-channel < seaLevel) so ocean cells never receive features — deliberate divergence from TS for natural-looking maps. |
| ✅ | `zones.ts` (305) | `macro/zones.{h,cpp}` | TS-faithful three-stage compose: civ pull BFS (city 1.10 / village 0.55 / road 0.35 / dirt 0.22, decay 0.06 ortho / 0.085 diag, max-strength wins) − applied to fbm_zone (5-octave value noise wavelength 96, smoothstep, **toroidally wrapped per octave** so no seam at world edges) + mountain-depth BFS only into mountain cells (1 ortho / 1.414 diag) → boost = 0.08 + min(0.45−0.08, depth×0.04). Forest +0.04, water (alpha < 128) +0.05. Field clamp01 + quantise floor(z×10). |
| ✅ | `biomes.ts` (141) | `macro/biomes.h` | 3×3 climate matrix verbatim (Tundra/Taiga/Snow / Valley/Meadow/Swamp / Desert/Steppe/Tropics + Water). `biome_from_climate` now uses round-to-nearest (TS `Math.round(t01 * 2)`) instead of floor — fixes biome boundary drift on borderline climate cells. GPU lookup texture composed inline in `macro_renderer.cpp` (kFS). |
| ✅ | `markers.ts` (64) | `macro/markers.h` | Full TS API (add/remove/has/by-prefix dedup), TS unicode glyphs (★ ◆), TS hex colours |
| ✅ | `flag-generator.ts` (175) | `macro/flag_generator.{h,cpp}` | 128×128 RGBA, sin-based RNG bit-identical to TS, 4 background/body/symbol/detail layers |
| ✅ | `npc-ai.ts` (544) | `macro/npc_ai.{h,cpp}` | All 8 TS behaviours dispatched via `AIBehaviour` enum: HomeWanderer/Woodcutter/Trader/Nomad/Aggressive/Patrol/Teleporter/Wanderer. `TreeGrid` hashed spatial grid for O(1) nearest-tree. 0.5 s tick accumulator. New `ecs::MacroNpcRuntime` POD carries state/timers/sp/home/target/teleport/visualSpeed. |
| ✅ | `movement-cost.ts` (97) | `macro/movement_cost.h` | Biome + feature SP weight tables verified vs TS |
| ✅ | `rng.ts` (15) | `core/rng.h` | bit-exact xorshift32 |
| ✅ | `torus.ts` (78) | `core/torus.h` | wrap+dist+step ported |
| ⏳ | `audio.ts` (174) | `macro/audio.{h,cpp}` | Needs SDL_mixer wrapper |
| ⏳ | `renderer.ts` (985) | `macro/macro_renderer.cpp` | Sprite batching + entity rendering — partial |
| ✅ | `biome-textures.ts` (302) | `macro/macro_renderer.cpp` (GLSL) | Full port: noise primitives, 3×3 climate dispatch, 8-neighbour signed-distance shore band, land↔land biome blend (5px), bt_climateOverlay (snow + drift-ice with cracks), zoom strength fade |
| ✅ | `tundra.ts`, `taiga.ts`, `snow.ts`, `valley.ts`, `meadow.ts`, `swamp.ts`, `desert.ts`, `steppe.ts`, `tropics.ts`, `water-biome.ts` | `macro/macro_renderer.cpp` (GLSL) | All 10 `bt_<biome>` functions inlined verbatim into kFS |
| ✅ | `landmark-registry.ts` | `macro/landmark_registry.{h,cpp}` | TS-faithful runtime aggregator: `collect_landmarks(GameState&)` walks `settlements` / `villages` / `spires` / `markers` and yields a unified `LandmarkEntry` list (kind, id, name, detail, x, y, ARGB). TS Tailwind colours mapped to ARGB (amber-300 / lime-300 / purple-300 / cyan-300). |
| ✅ | `effect-applicator.ts` | `events/effect_applicator.cpp` | TS-faithful: `heal_hp` == `restore_hp` (clamp-add by value, not full restore); `damage_hp` / `restore_mp` / `restore_sp` / `drain_sp` / `grant_xp` mirror TS dispatcher; CodexUnlock dedups via `std::find`; no auto-level-up inside `grant_xp` (handled by PlayerLevelUp event). |

### L1.character — `src/character/` (sprite atlas, animation, palette)

| Status | TS module | C++ target | Notes |
|--------|-----------|-----------|-------|
| ✅ | (TS `public/assets/sprites/*.png`) | `assets/sprite_atlas.{h,cpp}` | Lazy-loaded GL textures for the real PNG sprite set (city, village, spireA/D, player, peasant, corovan, witch, cultistka, imp_golem, coins). stb_image via FetchContent; CMake symlinks `build/assets → ../public/assets`. Macro overlay now draws cities/villages/spires/NPCs/player as actual sprites instead of pixel-art placeholders. |
| ⏳ | `character/atlas-loader.ts` | `character/atlas_loader.{h,cpp}` | Full per-frame paper-doll atlas (parts JSON + animation frames) — separate from the simple PNG atlas above |
| ⏳ | `character/animation.ts` | `character/animation.{h,cpp}` | Frame timing, state machine |
| ⏳ | `character/palette.ts` | `character/palette.{h,cpp}` | Palette swap shader |
| ⏳ | `character/character-generator.ts` | `character/character_generator.{h,cpp}` | Random character composition |
| ⏳ | `character/renderer.ts` | `character/renderer.{h,cpp}` | Composes atlas+palette → quad batch |

### L2 — Subworld (`src/game/subworld/*.ts` → `timaert_c/src/sub/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| 🟨 | `subworld/engine.ts` (1413) | `sub/engine.{h,cpp}` | Game loop + input present. Combat constants verified TS-verbatim in `sub/ai.h`: `kHostileThreshold=-50`, `kHitRepPenalty=-1`, `kCrowdPenalty=40`, `kDetectionRadius=200`. Visual water plane = `WATER_LEVEL = 0.40` (TS-faithful: `renderer-3d.ts:728` default `0.3` is dead code; `SubworldScreen.svelte` always overrides it via `setWaterLevel(seamless.compositeWaterLevel())` which returns `mapData.waterLevel = WATER_LEVEL = 0.40`). Aligning the visual plane with the heightmap sea-level anchor is also the only self-consistent setting at the C++ port's `kHeightScale = 1500` (any gap blows up to a 150 m cliff and lifts bilinear-blended water cells above the waterline). Remaining stub work: spell casting wiring, full event-bus subscriptions. |
| 🟨 | `subworld/seamless-manager.ts` (1060) | `sub/seamless_manager.{h,cpp}` | 9-cell grid + edge re-center; pre-gen needs verification |
| ✅ | `subworld/base-generator.ts` (1712) | `sub/base_generator.{h,cpp}` | TS-faithful per-cell remap with land baseline lifted by `kLandMargin = 0.02` (single source of truth in `base_generator.h`: `WATER_LEVEL = kMacroSeaLevel = 0.40`, `kLandMargin = 0.02` → `kLandFloor = 0.42`, `landScale = 0.967`). Bilinear blend across the 3×3 grid stays above the visual water plane at land/water seams — no per-pixel clamps. TS hides this with single-cell render; C++ exposes it via the full 3×3 mesh and the `kLandMargin` shift is the principled fix. Plus TS-faithful desert dunes, swamp pre-flatten, boggy lowland dips, **scatter_universal_trees** (global-coord aligned grid stitch, smooth_noise_ts cluster FBM, terrain_noise_ts placement hash, biome density / step / size from BiomeConfig, urban clearRadius), and **apply_mountain_ridges** (TS-verbatim domain-warped 4-octave ridged multifractal at freqs 0.004/0.009/0.02/0.045, 90px warp, valleyFloor=max(WATER+0.08, macroH*0.5), peak=min(1, macroH+0.08), blend by rw=clamp((amp-1)*0.5)). 2D-pipeline-only members (`layGrassBase`, `markOrganicMainRoad`) intentionally skipped — see Intentional Skips. |
| ✅ | `subworld/map-data.ts` (674) | `sub/map_data.h` | Types + `Dir` / `DIR_OFFSETS` / `angularDistance` / `findTileNear` / `oppositeDir` helpers ported. `EdgeAnchor`, `blendedMacroHeightmap`, `NeighborGrid` intentionally skipped — see Intentional Skips (2D-pipeline helpers; 3D port already has cross-cell continuity via `nbHeights[9]` + bilinear blend in `generate_heightmap`). |
| ⛔ | `subworld/renderer.ts` (361) | `sub/renderer_2d.{h,cpp}` | 2D top-down subworld view intentionally skipped — see Intentional Skips. The C++ port is pure 3D; future top-down view will be a baked minimap texture, not a tile-id grid render. The existing `sub/renderer_2d.{h,cpp}` is dead code pending removal. |
| 🟨 | `subworld/renderer-3d.ts` (2040) | `sub/renderer_3d.{h,cpp}` | Compact 192² mesh + 4-band quantised NdotL + **TS-faithful per-biome atlas sampling** (TileAtlas binds gen_tundra/.../gen_water 64×64 procedural textures, fragment bilinearly samples 4 nearest cells across the 3×3 grid, repeat-wrap, uTileScale=9 atlas tiles per world). Slope-driven rock/snow overlay sits on top — flat ground stays pure biome, only steep mountainsides expose rock/snow. **World physical scale = TS (kTileMeters = 1.0 → 1024 m per cell; full 3×3 grid spans 3072 m).** **kHeightScale = 1500** = TS `HEIGHT_SCALE = 500` × `kFullSize/kCellSize = 3` — TS only renders one 1024 m cell, C++ renders the full 3×3 for far-view fog; scaling proportionally preserves TS vertical:horizontal proportions exactly. **Distance fog (kFogStart=800 / kFogEnd=2800, fog colour follows ambient → blends seamlessly with sky tint at any TOD).** **Water plane upgraded to TS-faithful Blinn-Phong sun specular (perturbed wave normal, 48-power highlight), Fresnel-style alpha, and matching distance fog.** Missing: billboard shadows. |
| ✅ | `subworld/sky.ts` (271) | `sub/sky.{h,cpp}` | TS-faithful celestial sphere: per-pixel view ray (yaw/pitch/fov/aspect); gradient + sun(disc/glow/scatter) + procedural moons (1-3 per world) + animated FBM clouds with sun-lit edges. **Stars rewritten as a 3D Fibonacci-lattice catalog** — 220 quasi-uniform sphere points + per-star jitter / brightness / twinkle phase from a 1-D hash; rendered like the sun via `dot(rd, starDir)` with no 2D projection (no polar singularity, no grid pattern). |
| ✅ | `subworld/lighting.ts` (75) | `sub/lighting.h` | TS-faithful: `sunDir = (cos(ang), sin(ang), 0)` toward sun; `sunIntensity = smoothstep(-0.05, 0.30, elevation)`; sun colour `(1-w·0.10, 1-w·0.30, 1-w·0.55)` with `w = 1-smoothstep(0,0.4,elev)`; ambient `(0.12+d·0.28, 0.12+d·0.28, 0.18+d·0.22)` with `d = clamp01(smoothstep(0.22,0.35,tod) - smoothstep(0.65,0.78,tod))`. Adds `PointLight` POD + `kMaxPointLights = 8`. Renderer_3d negates `sunDir` on upload (shader expects from-sun-toward-world). |
| ✅ | `subworld/camera.ts` (127) | `sub/camera.h` | TS-faithful constants (`kEyeHeight=2`, `kFovRad=1.309 ≈ 75°`, `kMaxPitchRad=π/3`) plus helpers `sample_height` (bilinear, clamp-to-edge), `rotate_camera` (sensitivity 0.002, pitch clamp), `move_vector` / `move_vector_3d`. Renderer keeps its own `kHeightScale` since the C++ port uses a smaller world scale than TS. |
| ✅ | `subworld/math3d.ts` (149) | `core/math.h` | TS-faithful: `mat4_perspective` / `mat4_lookAt` / `mat4_mul` verified column-by-column vs TS (z-axis sign, cross order, translation column). Added missing TS post-multiply helpers `mat4_translate`, `mat4_scale`, `mat4_rotate_y` (M' = M · T/S/Ry). vec3 helpers (`normalize`, `cross`, `operator-`) already 1:1. |
| ✅ | `subworld/textures.ts` (1123) | `sub/textures.{h,cpp}` | 64×64 atlas, all 10 biomes (Tundra…Water) bit-faithful with TS texNoise hash; fixes prior Steppe/Desert atlas swap |
| ✅ | `subworld/fauna.ts` (386) | `sub/fauna.{h,cpp}` | All 18 critter PODs + 14 tables 1:1 with TS (hp/damage/speed/range/cooldown/colour/radius); landmark > feature > biome routing; weighted random `roll_fauna`; ruin/spire faction-override |
| ✅ | `subworld/spawn.ts` (545) | `sub/spawn.{h,cpp}` | Landmark-aware: `(biome, feature, landmark)` signature; `get_fauna_table`+`roll_fauna`; 20-attempt water-tile retry; full `deriveContextScale` ported (√(pop/100) level bonus + zones >2 → +(z-2) levels and 1+(z-2)·0.18 hp/damage multipliers); zone level looked up from `ZoneLayer::at` at `enter()`. |
| ✅ | `subworld/ai.ts` (191) | `sub/ai.{h,cpp}` | TS-port: `SubworldAi` component dispatched as Wander / Flee / Combat. Wander = random-walk with bounce-off-bounds; Flee = sprint away from player when within 60u, else wander; Combat = chase + attack-on-cooldown. Macro NPCs without SubworldAi keep legacy chase behaviour. |
| 🟨 | `subworld/{city-generator,village,forest,grassland,ruin,mountain,swamp,water,road-generator,spire}.ts` | `sub/gens/dispatch.cpp` | All 10 collapsed; gen_open / gen_forest / gen_swamp / gen_village / gen_city now use TS-faithful **cross-cell stitched tree scatter** (`scatter_universal_trees`: global-coord aligned grid + smooth_noise_ts FBM cluster gate + terrain_noise_ts placement hash → adjacent cells produce identical tree distributions, no seams). City/village apply clearRadius around centre. Per-mode dedicated generators (city walls, village street nodes, ruin decay, spire interior) still pending.
| ✅ | `subworld/map-factory.ts` (319) | `sub/map_factory.{h,cpp}` | In-memory snapshot cache keyed by (cellSeed, mode); `snapshot_subworld` quantises heightmap to u16, `restore_into` merges per `Structure::Kind` (matching prefix kept, surplus saved structures marked decayed via negative height sentinel, fresh extras appended). Wired into `SeamlessSubworldManager`: every cell consults `find_saved_subworld` after `dispatch_generate`, all 9 cells are snapshotted on 3×3 re-centre and on `SubworldEngine::leave()`. |
| ⛔ | `subworld/map-renderer.ts` (354) | (skipped) | 2D top-down sub renderer companion (renders citizen sprite sheets, projectile sprites, fauna icons via Canvas2D `drawImage`). C++ port is pure 3D; future minimap will be a baked texture, not a sprite-grid blit. |
| ✅ | `subworld/spatial-hash.ts` (87) | `sub/spatial_hash.h` | TS-faithful uniform grid (`CELL=64` world units). Header-only POD; data-orientated layout — single contiguous `entries` vector with per-bucket `{begin,end}` ranges (two-pass build: count → prefix-sum → scatter; zero per-bucket heap allocs). `build_spatial_hash(reg, w, h)` walks `view<Position, Health>(exclude<Dead>)` and includes any entity with `SubworldTag` or `PlayerTag`. `for_each_in_radius` matches TS `forEachInRadius` semantics, also passing exact d² to the visitor. |
| ⛔ | `subworld/citizen-sprites.ts` (220) | (skipped) | Pre-renders citizen / player paper-doll walk strips into a Canvas2D sheet for the 2D sub renderer (`drawImage`). Depends on the full `character/*` atlas + animation + palette + renderer chain. C++ port is pure 3D — NPC visuals will use the 3D sprite billboarding path, not a 2D walk-strip atlas. |
| ⛔ | `subworld/gen-worker.ts` (140) | — | Web Worker; in C++ use `std::thread` if needed (deferred) |

### L3 — Event System (`src/game/*.ts` → `timaert_c/src/events/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| ✅ | `event-bus.ts` (180) | `events/event_bus.{h,cpp}` | TS-faithful: tick buffer + lastTick + history, per-tag listeners, `emit`/`emit_all`/`on`/`unsubscribe`/`flush`. Adds the full TS query surface — `has_tag`, `find`, `find_all`, `query_history` (newest-first scan with limit), `trim_history`, `reset`, `tick()`. |
| 🟨 | `event-types.ts` (322) | `events/event_types.h` | C++ `EventTag` is a parallel design (26 tags vs TS 29). Coverage gaps that matter when consumers exist: TS-only tags awaiting consumers — `NpcHpChange`, `SettlementMoodChange`, `PlayerStatChange`, `PlayerEnter/LeaveSettlement` (C++ collapses both into `SettlementVisit`), `BattleEnd`, `MagicSurge`, `FactionRelationChange`, `DialogStart`, `ShowDialog`, `ShowStory`, `SpawnEntity`, `CameraMove`. C++-only useful tags: `PlayerDeath`, `NpcGreeted`, `SpellCast`, `SpellLearned`, `Trade`. Quest tags renamed (`QuestAccepted/Completed/Failed/Abandoned` vs TS `QuestStart/Complete/Fail`) — semantically equivalent. Add TS tags as their consumers (dialog UI, story overlay, faction panel) land. |
| 🟨 | `logic-nodes.ts` (230) | `events/logic_nodes.{h,cpp}` | Engine present |
| 🟨 | `effect-applicator.ts` | `events/effect_applicator.{h,cpp}` | Effect verbs reduced |
| 🟨 | `node-registry.ts` (106) | `events/node_registry.{h,cpp}` | TS `createBuiltinNodes()` returns 3 LogicNodes (`enc_random`, `sys_level_up`, `sys_settlement`) that emit `ShowDialog` events to a UI overlay. C++ has no dialog overlay yet, so `register_builtin_nodes` instead wires three back-end side-effects directly via `bus.on`: PlayerLevelUp → XP-overflow level rollover; SettlementVisit / NpcGreeted → first-visit codex unlocks; QuestCompleted → +5 reputation with quest-giver faction. The encounter-roll node is duplicated by the existing modal encounter pipeline (`content/plot/encounters.cpp` ✅) which also fires per-tick on PlayerMove. Promote to ✅ once dialog UI lands and the 3 TS LogicNodes are registered through `LogicNodeEngine` for parity. |
| ✅ | `quests/quest-engine.ts` (295) | `events/quests/quest_engine.{h,cpp}` | Six universal verbs (`VisitCell`, `FindLocation`, `DeliverItems`, `DestroyNpc`, `WaitAt`, `InteractCell`) with TS-faithful per-tick checker dispatch; reward application for all 5 kinds (`Gold`, `Xp`, `Item`, `Reputation`, `Event`); accept/abandon lifecycle emits matching events; deadline expiry on `time.day > expireDay`. Tick consumes `bus.last_tick_events()` for kill / interact / cell-change matching. |
| ✅ | `quests/quest-types.ts` (102) | `events/quests/quest_types.h` | Six `ObjectiveKind` verbs and five `RewardKind` kinds match TS 1:1; `Quest` POD carries id/title/description/category/giver/objectives/rewards/onAccept/expireDay/difficulty. Naming uses C++ idiom (`kind` for discriminant, capitalised enums) but data layout is bit-equivalent. |

### L4 — Plot Content (`src/game/plot/`, `src/game/spells/`, `src/game/quests/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| 🟨 | `quests/quest-generators.ts` (424) | `content/quests/procedural.{h,cpp}` | Templates compacted |
| ✅ | `plot/encounters.ts` (406) | `content/plot/encounters.{h,cpp}` | 15 encounters with full choice/effect data; modal UI + per-tick trigger wired |
| ⏳ | `plot/intro.ts` (85) | `content/plot/intro.{h,cpp}` | 9-slide intro |
| ⏳ | `plot/chapter-1.ts` (18) | `content/plot/chapter_1.h` | Placeholder |
| 🟨 | `spells/spell-types.ts` (227) + `spell-casting.ts` (198) | `content/spells/spell_types.h` + `spell_book.{h,cpp}` + `registry.cpp` | Cooldowns, mana, cast ctx |
| 🟨 | `spells/{fireball,ice-shard,lightning-chain,energy-beam,magic-bolt,armageddon,flight,haste}.ts` (~840) | `content/spells/registry.cpp` per-spell `spawn_*` | All 8 spells registered with TS-faithful constants (mana, cooldown, base damage, projectile speed, blast radius, friendly-fire) and distinct visual tints. Lightning-chain treated as fast small-AoE projectile until subworld chain logic lands; energy-beam as fast narrow projectile until beam volumetric damage lands; haste/flight registered without spawn fn (sustained self-buff hook pending). |
| ⛔ | `spells/spell-renderer.ts` (96) | (skipped) | Canvas2D dispatcher (`drawSpellProjectile` / `drawCasterAura`) for the 2D sub renderer. C++ port is pure 3D; spell visuals will be implemented as 3D billboard / particle effects, not Canvas2D arc/fill. |

### Save / Load

| Status | TS module | C++ target | Notes |
|--------|-----------|-----------|-------|
| 🟨 | (`map-factory.ts` regen pattern) | `macro/save.{h,cpp}` | Save schema v4 is built (`kSaveVersion = 4`) with magic/version/checksum gates, atomic write, inspect, and quest serialization. Binary write/read harness evidence exists (`save.bin`, `build-msvc/runtime_save_load.err`); canonical GUI save/load round-trip proof is still required. |

## External Reference: `proto_c/` (playable C++ prototype)

`proto_c/` is a separate, **playable** C++ prototype of Samosbor
(SDL2 + SDL2_image + SDL2_ttf, ~5 471 LOC). It is **raw and incomplete**
— no subworld, no OpenGL, no ECS, no event bus, no spells, no quests, no
politik, no zones — but the macroworld portion **runs as a real game** with
working title menu, HUD, top time/gold/HP bar, settlement panel, NPC
proximity panel, full save/load, screenshots, settings, pause, and a
bottom command toolbar. Use it as the **reference for what the C++ port
should *feel* like** (UX, screen layout, state transitions, panel arrangement).

Use proto_c for:

- **UX layout reference.** Top status bar (time / gold / HP / MP / SP /
  items / coords / biome), right-side proximity / settlement / NPC panels,
  bottom toolbar (pause / play / fast / rest / inventory / map / build /
  quests / party / equipment / zoom). Mirror this layout in
  `ui/screens.cpp` instead of inventing our own. Aim image:
  `aim.png` (the TS web build screenshot showing the target layout).
- **State machine.** `proto_c/src/states/*` — menu, play, pause, battle,
  map, event, load, settings, stat. Already mirrored in our `AppState`
  enum; copy any UX fixes from there.
- **Save binary I/O.** `proto_c/src/systems/save_game.h` is a working
  reference for binary read/write patterns. We use the regen-from-seed
  variant in `macro/save.cpp`.
- **Wraparound terrain generator.** `proto_c/src/core/tergen.h` —
  cross-check perf vs. our `macro/spawners.cpp`.
- **Content inspiration.** `proto_c/src/systems/random_events.cpp`
  (1 485 LOC) — large hand-written event catalogue; lift *style* into
  `content/plot/` during the L4 pass, but **never as 1:1 gameplay**.
- **Good practices.** Any small idiom (POD layouts, tile rendering tricks,
  panel widget patterns) is fair game.

Do **not** use proto_c for:

- Gameplay constants, formulas, item catalogs, NPC stats, RPS damage
  matrices, attribute formulas, economy. Those come from `../src/`
  (TypeScript) only — proto_c has its own divergent data and using it
  would silently introduce drift.
- Architecture decisions about layering (L1–L4), event bus, ECS, or
  shaders — proto_c predates all of these.

**Summary:** TS is the gameplay source of truth; proto_c is the
*playable shape* the native build is supposed to converge to.

## Intentional Skips

- `subworld/gen-worker.ts` — Web Worker threading. Native build can run gen
  on the main thread (fast enough at 1024² with C++); deferred until proven
  bottleneck.
- TS Vite glue (`vite.config.ts`, `svelte.config.js`, etc.) — N/A.
- HTML overlays — replaced by ImGui (`src/ui/overlays.cpp`).
- **2D top-down subworld pipeline** — the C++ port is pure 3D in the
  subworld (`renderer_3d.cpp`); the only top-down view is a future local
  minimap rendered as a baked texture, not a tile-id grid. The following
  TS-only members of `subworld/base-generator.ts` and `subworld/map-data.ts`
  are therefore intentionally not ported:
  - `layGrassBase` — 2D tile-id biome blending. Replaced by GLSL bilinear
    sampling across the 3×3 grid in `renderer_3d.cpp`'s fragment shader
    (per-pixel instead of per-tile, no quantisation).
  - `markOrganicMainRoad` — 2D wavy road tile stamping. Roads in 3D are
    handled by the macro feature layer + future 3D road geometry.
  - `NeighborGrid`, `EdgeAnchor`, `blendedMacroHeightmap` — sub-cell
    continuity helpers for the 2D pipeline. The 3D port already has
    cross-cell continuity via `nbHeights[9]` + bilinear blend in
    `generate_heightmap`.
  Removing these from the 🟨 backlog: `subworld/base-generator.ts` and
  `subworld/map-data.ts` are now considered ✅ for 3D-only purposes;
  remaining stub work in those files (organic road tracing in 3D,
  edge-anchored heightmap continuity beyond the 3×3 window) will be
  reopened only if a concrete 3D-renderer issue exposes the gap.
- **2D Canvas2D rendering companions** — for the same reason, the
  following modules are skipped wholesale:
  - `subworld/map-renderer.ts` — Canvas2D top-down sub renderer.
  - `subworld/citizen-sprites.ts` — pre-renders paper-doll walk strips
    into a 2D sheet for `drawImage`. C++ sub will billboard NPCs in 3D
    instead of blitting a sprite atlas.
  - `spells/spell-renderer.ts` — Canvas2D `drawSpellProjectile` /
    `drawCasterAura` dispatcher. C++ spell visuals will be 3D billboards
    or particles, not 2D arc/fill draws.

## Translation Order (Phase Plan)

The aim: move every 🟨 row to ✅ in dependency order (lower layers first).
Each phase ends with a clean Windows/MSVC build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`
and a manual playtest of the affected subsystem.

### Phase 0 — App shell (user-visible UX, blocks playtesting)
- 0.1 ✅ `AppState` state machine (Title / Playing / Paused / Dead)
- 0.2 ✅ Title screen (`TitleScreen.svelte` → `ui/screens.cpp`) —
       centred via `ImGui::GetIO().DisplaySize` (pass logical points,
       not Retina drawable pixels — the latter pushes the menu off-screen)
- 0.3 ✅ Pause overlay (`PauseOverlay.svelte` → `ui/screens.cpp`)
- 0.4 ✅ Death screen (`DeathOverlay.svelte` → `ui/screens.cpp`)
- 0.5 ✅ Player HUD (`StatOverlay.svelte` HP/MP/SP/gold/time → `ui/screens.cpp`)
- 0.6 ✅ Hint bar (key bindings — bottom of screen)
- 0.7 ✅ Camera: smooth follow + middle/right-mouse pan + wheel zoom
       (default zoom 32 px/cell — clamp 4..96 — lower values cause the
       256² map to wrap-tile across the viewport via `fract()` UVs)
- 0.8 ✅ Save (F5) / Load (F9) wired to shell; v4 binary path has
       evidence, canonical GUI round trip still needs one proof log
- 0.9 ✅ `draw_player_hud` top status bar (time / gold /
       HP / MP / SP / items / coords / biome)
- 0.10 ✅ `draw_bottom_toolbar` bottom command toolbar
- 0.11 🟨 right-side proximity panel and NPC Talk are runtime-evidenced;
       trade / attack action parity is still pending
- 0.12 ⏳ Spell overlay (`SpellOverlay.svelte`) — list of known spells, hotkeys
- 0.13 🟨 Trade overlay (`TradeOverlay.svelte`) — settlement buy/sell tab is
       runtime-evidenced; separate overlay parity is pending
- 0.14 🟨 Story / event overlay (`EventOverlay.svelte`, `StoryOverlay.svelte`) —
       encounter modal exists; `ShowDialog` / `ShowStory` consumers missing
- 0.15 ⏳ Intro slideshow (`IntroOverlay.svelte` → 9 slides from `plot/intro.ts`)
- 0.16 ✅ **Macro renderer rework** — per-biome dispatch,
       neighbour-aware shore, and climate overlay are implemented in
       `macro_renderer.cpp`

### Phase X — Performance (parallel; "TS was very slow")

C++ port should already be 5-50× faster just by being native. These are the
known wins to extract once gameplay parity is reached:

- X1. ⏳ Multithread subworld generation (`std::jthread` per cell of seamless
       9-grid; TS used a Web Worker → see `subworld/gen-worker.ts`)
- X2. ⏳ Instanced rendering for trees / structures on macroworld (one draw
       call per type instead of per-entity quads)
- X3. ⏳ SoA layout for hot ECS components (position, velocity, sprite-id)
- X4. ⏳ Replace `std::function` event handlers with type-erased
       small-buffer storage (avoid heap on every emit)
- X5. ⏳ Stable arena allocator for per-tick allocations (path-finder
       open-set, AI scratch buffers)
- X6. ⏳ SIMD biome sampling (4 cells/lane via NEON on Apple Silicon)
- X7. ⏳ Profile with `Instruments → Time Profiler` once gameplay is faithful

### Phase A — L1 pure data & formulas (small, leaf)
- A1. ✅ `attributes.ts` — full RPG schema (9 attrs, perks list, skills,
       level XP curve, carry weight)
- A2. ✅ `army.ts` — RPS matrix, hire/fire/desertion pool
- A3. ✅ `items.ts` — full catalog + NPC/fauna/settlement loot tables + useItem
- A4. ✅ `economy.ts` — resources, goods, prices, trade routes, player buy/sell, terrain → resource mapping
- A5. ✅ `language.ts` — phonotactic generator (Zipf weights + weighted syllable templates + doubling)
- A6. ✅ `flag-generator.ts` — procedural heraldic flag (128² RGBA, 4-layer composition)
- A7. ✅ `movement-cost.ts` — SP weights per biome × feature (verified)

### Phase B — L1 simulation (depends on A)
- B1. ✅ `state.ts` — `default_player` + `default_game_state` factories + faction relation matrix (bands + lineage + overrides)
- B2. ✅ `npc.ts` — full `NPC_TYPE_DEFS` registry + per-NPC level/inventory/character ECS components
- B3. ✅ `politik.ts` — capital placement + lake snap + Voronoi
- B4. ✅ `pathfinding.ts` — A* cost-grid pathfinding
- B5. ✅ `world-tick.ts` — daily settlement / village / economy / garrison ticks
- B6. ✅ `zones.ts` — BFS civ + BFS mountain + fBM compose
- B7. ✅ `npc-ai.ts` — 8-behaviour `MacroNpcRuntime` dispatch

### Phase C — L1 generation & rendering data (depends on B)
- C1. 🟨 `tree-spawner.ts`, `mountain-spawner.ts`, `road-network.ts`,
       `road-spawner.ts`, `dirt-road-spawner.ts` — `trace_roads()` is
       budgeted torus A* plus dry/short Bresenham fallback; boot is verified,
       route quality remains budget/pruning debt
- C2. ✅ `features.ts` — `FeatureType` enum + grid
- C3. ✅ `biomes.ts` — 3x3 matrix
- C4. ✅ `biome-textures.ts` + 10 per-biome
       `bt_<biome>.ts` GLSL

### Phase D — L2 subworld (depends on A–C)
- D1. ✅ `subworld/map-data.ts`, `types.ts`
- D2. ✅ `subworld/base-generator.ts` — heightmap blend,
       coastal sculpt, mountain amplify, biome variants
- D3. 🟨 `subworld/seamless-manager.ts` — 9-cell grid +
       edge recenter present; pre-gen still needs verification
- D4. 🟨 per-mode generators collapsed in
       `sub/gens/dispatch.cpp`:
       `gens/city.cpp`, `village.cpp`, `forest.cpp`, `grassland.cpp`,
       `ruin.cpp`, `mountain.cpp`, `swamp.cpp`, `water.cpp`,
       `road_generator.cpp`, `spire.cpp`
- D5. ✅ `subworld/textures.ts` — full 64x64 procedural atlas (per-biome
       pixel-art patterns)
- D6. ✅ `fauna.ts` / `spawn.ts` / `ai.ts` — critter
       tables, landmark-aware spawn, Wander/Flee/Combat dispatch
- D7. ✅ `subworld/spatial-hash.ts` — bucketed grid
- D8. ⏳ `subworld/citizen-sprites.ts` — type → sprite map
- D9. 🟨 `subworld/engine.ts` — combat constants, hostility, hit penalty,
       crowd penalty, detection radius
- D10.✅ `subworld/sky.ts` — full TS port: celestial-sphere viewRay, sun, moons, twinkling stars, FBM clouds
- D11.🟨 `subworld/renderer-3d.ts` — billboard shadows,
       4-band NdotL verified

### Phase E — L3 event system
- E1. ⏳ `event-types.ts` — full `EventTag` enum + payload schema parity
- E2. ⏳ `effect-applicator.ts` — every effect verb
- E3. ⏳ `node-registry.ts` — every system node from TS
- E4. 🟨 `quests/quest-engine.ts` + `quest-types.ts` — objective/reward registries exist and `quest_lifecycle_test` passes; some UI/game-loop objective producers still need targeted proof

### Phase F — L4 content
- F1. ⏳ `spells/spell-types.ts` + `spell-casting.ts` — full schema
- F2. ⏳ Per-spell modules — one `.cpp` each (`fireball`, `ice-shard`,
       `lightning-chain`, `energy-beam`, `magic-bolt`, `armageddon`,
       `flight`, `haste`)
- F3. ⏳ `spells/spell-renderer.ts` — visual effects
- F4. ⏳ `quests/quest-generators.ts` — full templates
- F5. 🟨 `plot/encounters.ts` — encounter table and modal path exist; runtime coverage needs targeted proof
- F6. ⏳ `plot/intro.ts` — 9-slide sequence
- F7. ⏳ `plot/chapter-1.ts` — placeholder

### Phase G — L1.character (sprites)
- G1. ⏳ `character/atlas-loader.ts` — PNG/JSON loader (stb_image already
       in deps via ImGui's textures? — verify; otherwise add)
- G2. ⏳ `character/animation.ts`
- G3. ⏳ `character/palette.ts`
- G4. ⏳ `character/character-generator.ts`
- G5. ⏳ `character/renderer.ts`

### Phase H — Audio & polish
- H1. ⏳ `audio.ts` — SDL_mixer wrapper
- H2. 🟨 Runtime proof for canonical GUI save/load round trip and built-in node side effects
- H3. ⏳ Final pass: walk every TS export and confirm 1:1 in C++

## Working Procedure (per module)

1. Read the full TS source file.
2. Read the existing C++ stub (if any) and identify drift.
3. Read every C++ consumer to scope the rename/break impact.
4. Rewrite the C++ module to match the TS source exactly:
   - Same exported symbols (translated to `snake_case` where idiomatic).
   - Same constants (numeric values unchanged).
   - Same formulas (algebraically equivalent).
   - Same enum order (so wire-format / table-index parity holds).
5. Update affected consumers.
6. Build with the Windows/MSVC `build-msvc` command above — must be clean.
7. Move the row from 🟨 / ⏳ to ✅ in this file.
