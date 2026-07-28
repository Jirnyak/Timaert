# Translation Plan â€” TS Vite â†’ C++ / OpenGL / EnTT

> **Superseded note (2026-07-28):** Mountains have migrated from a `FeatureType`
> (`FT_Mountain`) to the elevation-classified `Biome::Mountain` (land at height
> `>= kMountainBiomeLevel`). The `mountain-spawner.ts` and `features.ts` rows below
> still describe the old `FT_Mountain` / `kDefaultFeatureMountainThreshold` path;
> those are historical. TS is now reference-only legacy and the C++ authority
> classifies mountains by elevation via `biome_at()`. See [biomes.md](biomes.md).

## Goal

`timaert_c/` is a **performance rewrite** of the Timaert game. The
TypeScript / Vite / WebGL2 build at `../src/` is too slow on real maps â€”
the macroworld tick, subworld generation, and renderer all stall the main
thread. The C++ port targets a **standalone, native, single-binary
SDL2 + OpenGL 3.2 Core + EnTT game** with zero browser dependencies and
order-of-magnitude better frame times.

## Sources of Truth

Two reference codebases â€” used for **different purposes**:

| Reference | Role | When to consult |
|-----------|------|-----------------|
| `../src/` (TS / Vite / WebGL2) | **Gameplay source of truth.** Constants, formulas, AI behaviours, content tables, save schema. The C++ port must match this 1:1. | Every translation. Read the TS file before writing the C++ file. |
| `../proto_c/` (SDL2 + SDL2_image + SDL2_ttf, ~5 471 LOC) | **Playable C++ prototype reference.** Older, raw, missing many features (no subworld, no OpenGL, no ECS) but **runs as a real game** with a working menu, HUD, map, settlement panel, save/load, and toolbar. Useful for: state-machine layout, on-screen UI panel arrangement, save binary I/O patterns, terrain wraparound (`tergen.h`), random-event content style. | When designing user-visible UX (menu/HUD/toolbar/panels) or a low-level pattern that has no analogue in the TS web build. **Never** for gameplay constants â€” those come from TS. |

Read both before starting a UX/shell task. Read TS only for gameplay tasks.

This document tracks what has been translated *faithfully* (matching TS
behaviour 1:1), what is currently a *stub* (compiles but does not match),
and the order in which faithful translation will proceed.

## Principles

1. **Gameplay parity is the floor, not the ceiling.** The C++ port must
   reproduce the *playable behaviour* of the TS module â€” same content,
   same balance, same player-visible outcomes â€” *as a minimum*. Where the
   TS design is clearly suboptimal and a known-better idiom from the
   reference games below would be straight-up superior, **do better**:
   - **Macroworld:** *Mount & Blade* (free overworld camera, party-marker
     travel, click-to-move, encounter triggers on movement) and *Dwarf
     Fortress* (deep simulated settlements, faction history, emergent
     economy and politics) are the references. Better camera, smarter
     world tick, richer settlement/economy emergence than TS â€” fine,
     do it.
   - **Microworld:** *Might & Magic 6/7/8* and *Daggerfall* (first-person
     ARPG dungeon/wilderness exploration, real-time combat with cool-
     downs, generated interiors, item/loot density) are the references.
     If TS subworld is shallower than these, the C++ port is allowed â€”
     and encouraged â€” to be deeper.
2. **Constants and balance numbers come from TS by default.** Damage
   matrices, XP curves, item prices, spawn weights, etc. â€” copy these
   verbatim *unless* keeping them produces an obviously worse-than-
   reference experience. Then redesign with a short note explaining what
   was changed and why (one line in the row's `Notes`).
3. **Code parity is not required.** Algorithms, data layouts, control
   flow are free to be redesigned for C++ idioms (SoA, contiguous tables,
   EnTT views, branch-free dispatch, POD components, GLSL-side work).
4. **One TS module â†’ one C++ TU pair (`.h` + `.cpp` if needed)** *as a
   default* â€” but merge or split freely when there is a real
   architectural seam, when a single C++ TU subsumes several tiny TS
   files, or when a single TS file has multiple unrelated concerns.
5. **Data-driven tables are constexpr `inline` arrays** in headers,
   indexed by enum value. No `if`/`switch` on type ids in engine code.
6. **Never silently weaker than TS.** A C++ port must never make the
   game noticeably *worse* than TS â€” fewer events, dumber AI, missing
   biome variety, missing UX affordances. Weaker is a bug.
7. **Verify by playtest + diff.** After porting, walk every TS exported
   symbol and confirm there is *some* representation in the C++ build
   (even if reorganised, merged, or replaced by a better idiom). Drop a
   symbol only with an explicit note in the "Intentional Skips" section.

## Status Legend

| Tag | Meaning |
|-----|---------|
| âœ…  | Faithful â€” matches TS gameplay; reviewed line-by-line |
| ðŸŸ¨  | Stub â€” compiles, structure exists, values/branches drift from TS |
| â³  | Planned â€” not started |
| â›”  | Intentionally skipped (Web-only / N/A in C++) |

Corrected 2026-05-11: Windows/MSVC smoke evidence is a verification target,
not gameplay authority. TS/Svelte under `C:\Timaert\src` remains the behavior
reference. Rows marked complete must mean TS-reviewed translation, not just a
passing Windows build.

Windows/MSVC smoke evidence exists for `build-msvc`, title launch, New Game
`[boot] done`, macro walking, Load screen, settlement trade/quest accept, NPC
Talk, and character tabs. Runtime-only behaviours still need their own smoke
proof when noted.

## Module Inventory

### L1 â€” Macroworld Core (`src/game/*.ts` â†’ `timaert_c/src/macro/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| âœ… | `attributes.ts` (283) | `macro/attributes.{h,cpp}` | All 9 attrs, perks, skills, level XP, garrison helpers |
| âœ… | `army.ts` (262) | `macro/army.h` + `macro/npc.h` | Universal-NPC-as-soldier model: any hireable NPC kind can become a concrete `SoldierRecord`, single `upkeepGoldPerDay` per kind (1 gold/day baseline for weakest), no RPS matrix, no separate unit types. Legacy `UnitType` / `kUnitStats` / `damage_multiplier` / `kHireCost` / `kUpkeepCost` / `hire_unit` symbols are absent from current source; `hire_npc` moves concrete garrison soldiers into the player squad and is covered by `combat_squad_test`. App smoke evidence covers danger-zone exit blocking plus corpse loot and XP attribution: `subworld_exit_gate` blocked zone 9, `subworld_loot_xp` produced XP `0->25` and `misc_gem 0->2`. |
| âœ… | `state.ts` (989) | `macro/state.{h,cpp}` | `default_player` + `default_game_state` factories, faction band matrix with PAIR_OVERRIDES + lineage logic, `EconomyState eco` embedded on Settlement+Village. Current save schema is `kSaveVersion=8`. `populate_landmarks_from_politik(gs, terrain, seaLevel)` bridges politik cities â†’ `gs.settlements` (full Settlement init: language-named, biome-derived economy archetype + local resources, default garrison) and scatters 1â€“3 villages per settlement on land cells in a 4â€“14 cell ring. Without this bridge npc_spawn / overlay markers / world-tick / macro renderer landmark layer all silently saw empty lists. |
| âœ… | `economy.ts` (516) | `macro/economy.{h,cpp}` | 6 resources, 15 hand-tuned goods, gather/produce/prices/consume, trade routes, player buy/sell, terrain mapping |
| âœ… | `items.ts` (514) | `macro/items.{h,cpp}` | Full catalog (12 ids), 8 NPC loot tables, fauna loot, gold formula, settlement loot, useItem |
| âœ… | `npc.ts` (644) | `macro/npc.h` + `macro/npc_spawn.cpp` | Full `NPC_TYPE_DEFS[8]` (label, portrait, baseHp, baseLevel, AIBehaviour, CombatTemplate, names[16], talkLines[6]); per-NPC `NpcLevel`/`NpcInventory`/`NpcCharacter` ECS components emplaced at spawn. Macro spawn terrain lookup treats invalid or mismatched terrain as absent terrain, invalid map dimensions fail closed before wrapping, and fallback positions stay inside map bounds; `npc_spawn_contract_test` covers malformed terrain and zero-sized maps. Visual character data redesigned as compact POD seed (visualSeed + bodyShape + tint + nameIdx) per relaxed translation policy â€” no HTML-canvas atlas mirror needed. |
| âœ… | `politik.ts` (646) | `macro/politik.{h,cpp}` | Kingdom defs + Prim's MST per kingdom + 1 extra nearest edge for redundancy + inter-kingdom bridge roads (dist â‰¤ 0.35) + `finalize_politik` (lake-snap for capitals with `capital_requires_lake` + multi-source 4-neighbour BFS Voronoi over land cells, terrain-aware territories that never jump the sea). Malformed or mismatched terrain RGBA storage fails closed as absent terrain, and invalid map dimensions return an empty Politik. |
| âœ… | `language.ts` (247) | `macro/language.{h,cpp}` | Full phonotactics: 6 vowels, 17 consonants, Zipf weights, 3-5 weighted syllable templates, doubling, unique-name |
| âœ… | `pathfinding.ts` (244) | `macro/pathfinding.{h,cpp}` | TS-faithful: cost-grid A* with octile heuristic + indexed binary min-heap (key=y*W+x dedup), torus wrap, edge cost = costGrid[dest] * stepLen. `build_cost_grid` ports buildCostGrid from movement-cost.ts. |
| âœ… | `world-tick.ts` (240) | `macro/world_tick.{h,cpp}` | Daily settlement & village ticks (economy â†’ mood â†’ garrison â†’ 30-day rolling history), trade-route settle + dispatch (villages â†’ nearest city, cities â†’ cities & villages), player upkeep + ageing. Sub-minute accumulator (`kRealSecondsPerDay = 100`). Village history is included in the current v8 save schema. Hourly `TimeAdvance` event emit is wired through current event processing. Runtime proof exists: `subworld_time` smoke passes on seed 42. |
| âœ… | `tree-spawner.ts` (441) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | GLSL renderer + CPU density: domain-warped multi-scale FBM (largeÃ—0.40 + medÃ—0.35 + fineÃ—0.25) with smoothstep curve t0=0.35 t1=0.55, biome exclusion via 3Ã—3 climate matrix, active-map-sea-level shoreline buffer + mountain cap, and TS-style 2-cell `riverData` exclusion buffer; malformed terrain RGBA storage fails closed before tree loops; `ihash01` bit-identical to TS |
| âœ… | `mountain-spawner.ts` (199) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | TS file is GLSL-only (no CPU spawner). GLSL inlined verbatim into kFS: 4 rock/snow palettes, `hParam = (h - threshold) / (1 - threshold)`-driven peak height, 2-peak composition above hParam>0.55, snow line, drop shadow, 2Ã—2 cell footprint with torus-safe local UV. CPU "placement" = pass 1 of `build_feature_layer` (height â‰¥ threshold â†’ FT_Mountain). |
| âœ… | `road-network.ts` (180) + `road-spawner.ts` (143) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | TS parity audit completed 2026-05-15. Current C++ `trace_roads()` is a documented intentional divergence from TS corridor-guided Bresenham over `tData.roadData`: it keeps Politik topology, component-prunes cross-island pairs using active map sea level, then runs generation-tagged terrain-cost A* with a large-map step cap. Water cells are blocked during expansion; routes not proven inside budget are pruned. Malformed terrain RGBA storage fails closed before road masks/cost grids are allocated. There is no straight-line road shortcut or cap-hit stat field in the current source. Evidence: `road_river_generation_test`, seeds 1..10 boot smokes, spells/story/battle/settlement-ui smokes. |
| âœ… | `dirt-road-spawner.ts` (180) | `macro/spawners.{h,cpp}` + `macro_renderer.cpp` (GLSL) | GLSL renderer + CPU placement: spiral search up to 60 tiles â†’ torus-aware lerp trace, skips villages already on roads, doesn't overwrite road cells, `landMaskA` filters water/ice. Native boundary guard fails closed on invalid map dimensions, short road masks, mismatched village coordinate arrays, and short supplied terrain RGBA byte counts; village coordinates wrap before indexing. Evidence: `road_river_generation_test` and seed 47 boot smoke. |
| âœ… | `features.ts` (89) | `macro/features.h` + `spawners.cpp::build_feature_layer` | Enum + grid + 4-pass writer (Mountain via TS-default `kDefaultFeatureMountainThreshold = 0.75f` from `snowLevel 0.80 - 0.05` and TS-faithful `height/255 >= threshold` comparison â†’ Tree via TS flattened index semantics â†’ DirtRoad â†’ Road, last-writer-wins), overflow-safe torus lookup/setter, empty-layer guards, complete-storage validation, and TS short-mask prefix reads for road/dirt masks. C++ adds a deliberate active-map-sea-level water filter (alpha-zero or red-channel below the current sea level) so ocean cells never receive features, including custom-game sea-level settings. Movement-cost, zones, render upload, subworld entry, and subworld generator dispatch validate feature/terrain storage at boundaries and fail closed to `FT_None`, empty masks, or blank R8 maps if malformed; unknown feature bytes decode through `FeatureLayer::decode()` / `complete_cells_or_sanitized()` to `FT_None` before reaching lookup, movement cost, zone generation, setter writes, GPU feature upload, or subworld height/road generation. Valid feature texture uploads remain zero-copy; corrupt complete grids use a sanitized scratch buffer. Zone and landmark texture uploads now also sanitise invalid bytes and blank invalid dimensions/data pointers. The byte grid drives movement cost, zones, subworld mode/fauna, and the macroworld renderer's TS-style 3Ã—3 decoration painter order. Evidence: fresh `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, async seam, baseline tests, full 14-test MSVC suite, and seed 45 + 112 + 113 boot smoke passes on 2026-05-15. |
| âœ… | `zones.ts` (305) | `macro/zones.{h,cpp}` | TS-faithful three-stage compose: civ pull BFS (city 1.10 / village 0.55 / road 0.35 / dirt 0.22, decay 0.06 ortho / 0.085 diag, max-strength wins) âˆ’ applied to fbm_zone (5-octave value noise wavelength 96, smoothstep, **toroidally wrapped per octave** so no seam at world edges) + mountain-depth BFS only into mountain cells (1 ortho / 1.414 diag) â†’ boost = 0.08 + min(0.45âˆ’0.08, depthÃ—0.04). Forest +0.04, water (alpha < 128) +0.05 through the active terrain RGBA byte-counted mask passed by boot. Field clamp01 + quantise floor(zÃ—10). Zone lookup and GPU upload fail closed on short storage; invalid zone bytes decode to zone 0, and short supplied water masks are ignored. |
| âœ… | `biomes.ts` (141) | `macro/biomes.h` | 3Ã—3 climate matrix verbatim (Tundra/Taiga/Snow / Valley/Meadow/Swamp / Desert/Steppe/Tropics + Water). `biome_from_climate` now uses round-to-nearest (TS `Math.round(t01 * 2)`) instead of floor â€” fixes biome boundary drift on borderline climate cells. GPU lookup texture composed inline in `macro_renderer.cpp` (kFS). |
| âœ… | `markers.ts` (64) | `macro/markers.h` | Full TS API (add/remove/has/by-prefix dedup), TS unicode glyphs (â˜… â—†), TS hex colours |
| âœ… | `flag-generator.ts` (175) | `macro/flag_generator.{h,cpp}` | 128Ã—128 RGBA, sin-based RNG bit-identical to TS, 4 background/body/symbol/detail layers |
| âœ… | `npc-ai.ts` (544) | `macro/npc_ai.{h,cpp}` | All 8 TS behaviours dispatched via `AIBehaviour` enum: HomeWanderer/Woodcutter/Trader/Nomad/Aggressive/Patrol/Teleporter/Wanderer. `TreeGrid` hashed spatial grid for O(1) nearest-tree. 0.5 s tick accumulator. New `ecs::MacroNpcRuntime` POD carries state/timers/sp/home/target/teleport/visualSpeed. |
| âœ… | `movement-cost.ts` (97) | `macro/movement_cost.h` + `macro/pathfinding.cpp::build_cost_grid` | Biome + feature SP weight tables verified vs TS, including unknown-feature fallthrough, the TS default weight `2` for unknown biome ids, and active-map float `height/255 < seaLevel` water classification for the cost grid |
| âœ… | `rng.ts` (15) | `core/rng.h` | bit-exact xorshift32 |
| âœ… | `torus.ts` (78) | `core/torus.h` | wrap+dist+step ported |
| âœ… | `audio.ts` (174) | `macro/audio.{h,cpp}` | SDL_mixer-backed native audio: init/shutdown, RAII no-copy handle ownership, MP3 music track registry (`explore`, `empire_theme`, `subworld`), one-shot `witch` SFX, master/music/sfx volume, mute toggle, fade play/stop, title/macro/subworld music trigger in `app/main.cpp`, and same-desired-track failure latch to avoid repeated per-frame replay attempts after an audio failure. Native CMake hard-fails when SDL2_mixer cannot be resolved outside Emscripten; the C++ no-mixer backend is only for configurations that do not define `TIMAERT_HAS_SDL_MIXER`, not a silent native fallback. Verified by MSVC app target rebuild, `audio_contract_test` stable-key/asset-filename/ownership checks, `audio_runtime_test` dummy-driver init/decode/play/stop/destructor/shutdown, title/new-game smokes, build-directory title smoke, and 2026-05-15 `new_game,wait_boot_done,subworld_audio,quit` seed-42 app smoke with SDL dummy audio proving `explore -> subworld -> explore`; all `public/assets/sound/*.mp3` loaded once at startup when the mixer backend is present, no per-frame missing-asset spam. |
| ðŸŸ¨ | `renderer.ts` (985) | `macro/macro_renderer.cpp` | Macro map fragment path is native: biome ground, river overlay, road/dirt ground passes, active map sea-level uniform, TS-style 3Ã—3 decoration painter order for trees/mountains/landmarks, zone tint for zone >4, cell grid, and time tint. TS sprite batching/entity rendering remains only partial because native macro actors now use ImGui/GL paper-doll overlays instead of the original WebGL sprite batch. |
| âœ… | `biome-textures.ts` (302) | `macro/macro_renderer.cpp` (GLSL) | Full port: noise primitives, 3Ã—3 climate dispatch, 8-neighbour signed-distance shore band, landâ†”land biome blend (5px), bt_climateOverlay (snow + drift-ice with cracks), zoom strength fade |
| âœ… | `tundra.ts`, `taiga.ts`, `snow.ts`, `valley.ts`, `meadow.ts`, `swamp.ts`, `desert.ts`, `steppe.ts`, `tropics.ts`, `water-biome.ts` | `macro/macro_renderer.cpp` (GLSL) | All 10 `bt_<biome>` functions inlined verbatim into kFS |
| âœ… | `landmark-registry.ts` | `macro/landmark_registry.{h,cpp}` | TS-faithful runtime aggregator: `collect_landmarks(GameState&)` walks `settlements` / `villages` / `spires` / `markers` and yields a unified `LandmarkEntry` list (kind, id, name, detail, x, y, ARGB). TS Tailwind colours mapped to ARGB (amber-300 / lime-300 / purple-300 / cyan-300). |
| âœ… | `effect-applicator.ts` | `events/effect_applicator.cpp` | TS effect verbs verified: `heal_hp` == `restore_hp` (clamp-add by value, not full restore); `damage_hp` / `restore_mp` / `restore_sp` / `drain_sp` / `grant_xp` mirror TS dispatcher; CodexUnlock dedups via `std::find`; `QuestFailed` now records `failedQuestIds`, not `completedQuestIds`; invalid level data no longer force-initialises during `PlayerLevelUp`. Evidence: `quest_lifecycle_test_manual.exe`. |

### L1.character â€” `src/character/` (sprite atlas, animation, palette)

| Status | TS module | C++ target | Notes |
|--------|-----------|-----------|-------|
| âœ… | (TS `public/assets/sprites/*.png`) | `assets/sprite_atlas.{h,cpp}` | Lazy-loaded GL textures for the real PNG sprite set (city, village, spireA/D, player, peasant, corovan, witch, cultistka, imp_golem, coins). stb_image via FetchContent; CMake symlinks `build/assets â†’ ../public/assets`. Macro overlay now draws cities/villages/spires/NPCs/player as actual sprites instead of pixel-art placeholders. |
| âœ… | `character/atlas-loader.ts`, `animation.ts`, `animation-constants.ts`, `palette.ts`, `palette-data.json`, `character-generator.ts`, `renderer.ts`, `sprite-data.ts`, `sprite-counts.ts`, `z-index-library.json` | `assets/character_paperdoll.{h,cpp}` + `assets/character_paperdoll_gl.{h,cpp}` + `ui/macro_overlay.cpp` + `sub/renderer_3d.cpp` | Native paper-doll path: `atlas.bin` parser (`ATLS` v1, 286 sheets, 45760 entries, 964x964 atlas), TS animation frame starts/delays, palette rows, deterministic xorshift seed generation, z-order, hide/mirror rules, and compact render descriptors. Macro overlay draws player/NPC/portrait paper-dolls with PNG fallback; player/NPC markers now use movement-aware idle/walk and front/back/left/right frames plus TS night-darken tint. Subworld 3D has cached descriptor billboards for `NpcCharacter` and chooses camera-relative directional frames from `SubworldAi` velocity. NPC type appearance presets mirror TS `withBackpack`, `withShoulderArmor`, and `withHorns`; event-spawned `BattleStart` hostiles now get `NpcCharacter` so they enter the paper-doll billboard path instead of generic sprite-only fallback. GL cache loads `atlas.png` once, keeps fixed descriptor and texture caches, caches failed uploads as null fallback entries, and uploads composed 48x48 frames only on cache miss. Evidence: `character_paperdoll_test`, `character_paperdoll_gl_smoke_test`, full MSVC build, boot smoke logging `[character] loaded paperdoll atlas` followed by `[smoke] PASS`, and `trigger_battle_start` smoke asserting spawned hostiles carry paper-doll character data. Latest continuation verifies cache reuse, directional walk texture uploads, integer-only generation chance gates for TS 100% layers, TS NPC appearance presets, and stable fixed-seed descriptor hash `6629795152062431341`. |

### L2 â€” Subworld (`src/game/subworld/*.ts` â†’ `timaert_c/src/sub/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| ðŸŸ¨ | `subworld/engine.ts` (1413) | `sub/engine.{h,cpp}` | Game loop + input present. Combat constants verified TS-verbatim in `sub/ai.h`: `kHostileThreshold=-50`, `kHitRepPenalty=-1`, `kCrowdPenalty=40`, `kDetectionRadius=200`. Visual water plane = `WATER_LEVEL = 0.40` (TS-faithful: `renderer-3d.ts:728` default `0.3` is dead code; `SubworldScreen.svelte` always overrides it via `setWaterLevel(seamless.compositeWaterLevel())` which returns `mapData.waterLevel = WATER_LEVEL = 0.40`). Aligning the visual plane with the heightmap sea-level anchor is also the only self-consistent setting at the C++ port's `kHeightScale = 1500` (any gap blows up to a 150 m cliff and lifts bilinear-blended water cells above the waterline). Spell casting wiring now covers projectile/AoE/chain/beam effects; remaining stub work: full event-bus subscriptions. |
| âœ… | `subworld/seamless-manager.ts` (1060) | `sub/seamless_manager.{h,cpp}` | Native 9-cell grid, edge re-center, worker-backed exposed-cell generation, deterministic flat placeholders, stale-work pruning, async save jobs, async composite road smoothing, saved-cell restore, rapid-reversal protection, and sparse road-mask metadata are implemented. The TS Web Worker / preloaded-ring behavior is represented as the prompt-mandated native placeholder + `std::jthread` stitch contract rather than synchronous generation on the boundary path. Evidence: `subworld_async_seam_test` covers road/plain axis seams, diagonal seams, rapid reversal, pending snapshot flush, placeholder fill, saved height restore, saved structure restore, and road-mask index correctness; `subworld_seam` app smoke crosses a real engine seam and emits `[seam-cross]` upload timings. |
| âœ… | `subworld/base-generator.ts` (1712) | `sub/base_generator.{h,cpp}` | TS-faithful per-cell remap with land baseline lifted by `kLandMargin = 0.02` (single source of truth in `base_generator.h`: `WATER_LEVEL = kMacroSeaLevel = 0.40`, `kLandMargin = 0.02` â†’ `kLandFloor = 0.42`, `landScale = 0.967`). Final `dispatch.cpp` post-pass clamps by final tile class after mode tile edits and road smoothing: `TILE_WATER <= WATER_LEVEL`, every non-water tile `>= WATER_LEVEL + kLandMargin`; `subworld_async_seam_test` clears saved-cache contamination around the water-plane case, scans the full 3x3 composite, and reports `water=3145728`, `land=6291456`, `badWater=0`, `badLand=0`, `maxWater=0.40000`, `minLand=0.42000`. Plus TS-faithful desert dunes, swamp pre-flatten, boggy lowland dips, **scatter_universal_trees** (global-coord aligned grid stitch, smooth_noise_ts cluster FBM, terrain_noise_ts placement hash, biome density / step / size from BiomeConfig, urban clearRadius), and **apply_mountain_ridges** (TS-verbatim domain-warped 4-octave ridged multifractal at freqs 0.004/0.009/0.02/0.045, 90px warp, valleyFloor=max(WATER+0.08, macroH*0.5), peak=min(1, macroH+0.08), blend by rw=clamp((amp-1)*0.5)). 2D-pipeline-only members (`layGrassBase`, `markOrganicMainRoad`) intentionally skipped â€” see Intentional Skips. |
| âœ… | `subworld/map-data.ts` (674) | `sub/map_data.h` | Types + `Dir` / `DIR_OFFSETS` / `angularDistance` / `findTileNear` / `oppositeDir` helpers ported. `EdgeAnchor`, `blendedMacroHeightmap`, `NeighborGrid` intentionally skipped â€” see Intentional Skips (2D-pipeline helpers; 3D port already has cross-cell continuity via `nbHeights[9]` + bilinear blend in `generate_heightmap`). |
| ðŸŸ¨ | `subworld/renderer.ts` (361) | `sub/renderer_2d.{h,cpp}` | Minimal native 2D top-down subworld renderer exists and is toggled with `F`; it uploads the composite tile/height map and draws a diagnostic top-down view. It is not TS Canvas2D sprite-grid parity: no citizen sprite sheets, projectile sprites, or fauna icons. |
| ðŸŸ¨ | `subworld/renderer-3d.ts` (2040) | `sub/renderer_3d.{h,cpp}` | Compact 192Â² mesh + 4-band quantised NdotL + **TS-faithful per-biome atlas sampling** (TileAtlas binds gen_tundra/.../gen_water 64Ã—64 procedural textures, fragment bilinearly samples 4 nearest cells across the 3Ã—3 grid, repeat-wrap, uTileScale=9 atlas tiles per world). Slope-driven rock/snow overlay sits on top â€” flat ground stays pure biome, only steep mountainsides expose rock/snow. **World physical scale = TS (kTileMeters = 1.0 â†’ 1024 m per cell; full 3Ã—3 grid spans 3072 m).** **kHeightScale = 1500** = TS `HEIGHT_SCALE = 500` Ã— `kFullSize/kCellSize = 3` â€” TS only renders one 1024 m cell, C++ renders the full 3Ã—3 for far-view fog; scaling proportionally preserves TS vertical:horizontal proportions exactly. **Distance fog (kFogStart=800 / kFogEnd=2800, fog colour follows ambient â†’ blends seamlessly with sky tint at any TOD).** **Water plane upgraded to TS-faithful Blinn-Phong sun specular (perturbed wave normal, 48-power highlight), Fresnel-style alpha, and matching distance fog.** Seam upload polish: terrain index buffer is static, no-road composites upload a 1Ã—1 zero road mask, road composites build a 1024Â² R8 mask with one-pixel dilation from sparse manager indices instead of scanning/uploading the full 3072Â² tile field, and CPU scratch buffers are reused. Latest freshly rebuilt Debug smoke: `upload3d=118.795ms`, `upload2d=0.000ms`, total `157.938ms`; best accepted 1024-mask Debug smoke remains `upload3d=51.785ms`, `total=74.603ms`. GL sub-update/storage-retention and terrain-payload shader-grid trials were both measured and rejected after regressions. Missing: billboard shadows. |
| âœ… | `subworld/sky.ts` (271) | `sub/sky.{h,cpp}` | TS-faithful celestial sphere: per-pixel view ray (yaw/pitch/fov/aspect); gradient + sun(disc/glow/scatter) + procedural moons (1-3 per world) + animated FBM clouds with sun-lit edges. **Stars rewritten as a 3D Fibonacci-lattice catalog** â€” 220 quasi-uniform sphere points + per-star jitter / brightness / twinkle phase from a 1-D hash; rendered like the sun via `dot(rd, starDir)` with no 2D projection (no polar singularity, no grid pattern). |
| âœ… | `subworld/lighting.ts` (75) | `sub/lighting.h` | TS-faithful: `sunDir = (cos(ang), sin(ang), 0)` toward sun; `sunIntensity = smoothstep(-0.05, 0.30, elevation)`; sun colour `(1-wÂ·0.10, 1-wÂ·0.30, 1-wÂ·0.55)` with `w = 1-smoothstep(0,0.4,elev)`; ambient `(0.12+dÂ·0.28, 0.12+dÂ·0.28, 0.18+dÂ·0.22)` with `d = clamp01(smoothstep(0.22,0.35,tod) - smoothstep(0.65,0.78,tod))`. Adds `PointLight` POD + `kMaxPointLights = 8`. Renderer_3d negates `sunDir` on upload (shader expects from-sun-toward-world). |
| âœ… | `subworld/camera.ts` (127) | `sub/camera.h` | TS-faithful constants (`kEyeHeight=2`, `kFovRad=1.309 â‰ˆ 75Â°`, `kMaxPitchRad=Ï€/3`) plus helpers `sample_height` (bilinear, clamp-to-edge), `rotate_camera` (sensitivity 0.002, pitch clamp), `move_vector` / `move_vector_3d`. Renderer keeps its own `kHeightScale` since the C++ port uses a smaller world scale than TS. |
| âœ… | `subworld/math3d.ts` (149) | `core/math.h` | TS-faithful: `mat4_perspective` / `mat4_lookAt` / `mat4_mul` verified column-by-column vs TS (z-axis sign, cross order, translation column). Added missing TS post-multiply helpers `mat4_translate`, `mat4_scale`, `mat4_rotate_y` (M' = M Â· T/S/Ry). vec3 helpers (`normalize`, `cross`, `operator-`) already 1:1. |
| âœ… | `subworld/textures.ts` (1123) | `sub/textures.{h,cpp}` | 64Ã—64 atlas, all 10 biomes (Tundraâ€¦Water) bit-faithful with TS texNoise hash; fixes prior Steppe/Desert atlas swap |
| âœ… | `subworld/fauna.ts` (386) | `sub/fauna.{h,cpp}` | All 18 critter PODs + 14 tables 1:1 with TS (hp/damage/speed/range/cooldown/colour/radius); landmark > feature > biome routing; weighted random `roll_fauna` now uses the TS float path (`floor(rng()*span)`, then `rng()*totalWeight` subtraction); ruin/spire faction-override. |
| âœ… | `subworld/spawn.ts` (545) | `sub/spawn.{h,cpp}` | Landmark-aware: `(biome, feature, landmark)` signature; `get_fauna_table`+`roll_fauna`; 20-attempt water-tile retry; TS-fauna RNG continues from `roll_fauna`, then consumes placement rolls before `baseLevel + floor(rng()*2)`; `deriveStats` 15% per-level HP/damage scaling and `deriveContextScale` are ported (sqrt(pop/100) level bonus + zones >2 -> +(z-2) levels and 1+(z-2)*0.18 hp/damage multipliers); zone level is looked up from `ZoneLayer::at` at `enter()`. Evidence: `subworld_spawn_parity_test` (`fauna=6 seed=324478056 zone=5 water_squad_blocked=1`). |
| âœ… | `subworld/ai.ts` (191) | `sub/ai.{h,cpp}` | TS-port: `SubworldAi` component dispatched as Wander / Flee / Combat. Wander = random-walk with bounce-off-bounds; Flee = sprint away from player when within 60u, else wander; Combat = chase + attack-on-cooldown. Macro NPCs without SubworldAi keep legacy chase behaviour. |
| âœ… | `subworld/{city-generator,village,forest,grassland,ruin,mountain,swamp,water,road-generator,spire}.ts` | `sub/gens/dispatch.cpp` | Generator domain ported with bounded native parity: grassland default wilderness; water/swamp/mountain use TS `WATER_LEVEL=0.40` and TS BiomeConfig density/step values; grassland/forest/swamp/mountain carve TS deterministic edge-anchor trails toward neighbouring road cells; forest glades use TS `GLADE_STEP=48`, threshold `0.72`, radius `6..16`; ruin rings/central cracked square use TS radius/segment/roughness/difficulty rules, and ruined paths use TS deterministic edge anchors with wall records suppressed across road/square gates; spire scorch/crater/tower constants match TS; water+road emits bridge structures with `BRIDGE_DECK_H=3`; spires are reachable through `CellLandmarkKind::Spire`. City/village now include neighbour-road-aligned gates/main roads using TS 35-65% deterministic edge anchors, population-scaled gate-safe wall/palisade structure records, organic branch/farm roads, central square/keep, roadside block houses, outer fields, and tree-clear radii. Road bridges use the same anchored endpoints as the road raster, including the native single-neighbour through-road enhancement. City walls are stamped after every road/farm cut, and tests reject wall structure records on protected road/square/house/field tiles. The prior unbounded TS mycelium queue is intentionally represented by bounded deterministic branch passes to respect the seam-freeze warning in `matwej.md`. Evidence: `subworld_generator_parity_test` / direct parity test / direct seam-crossing test / full MSVC build / app smoke. |
| âœ… | `subworld/map-factory.ts` (319) | `sub/map_factory.{h,cpp}` | In-memory snapshot cache keyed by (cellSeed, mode); `snapshot_subworld` quantises heightmap to u16, `restore_into` merges per `Structure::Kind` (matching prefix kept, surplus saved structures marked decayed via negative height sentinel, fresh extras appended). Wired into `SeamlessSubworldManager`: every cell consults `find_saved_subworld` after `dispatch_generate`, all 9 cells are snapshotted on 3Ã—3 re-centre and on `SubworldEngine::leave()`. |
| â›” | `subworld/map-renderer.ts` (354) | skipped | TS Canvas2D companion for sprite-grid rendering. Native keeps a minimal GL 2D renderer for diagnostics/toggle and uses 3D billboards for gameplay visuals; the TS Canvas2D `drawImage` path is not ported. |
| âœ… | `subworld/spatial-hash.ts` (87) | `sub/spatial_hash.h` | TS-faithful uniform grid (`CELL=64` world units). Header-only POD; data-orientated layout â€” single contiguous `entries` vector with per-bucket `{begin,end}` ranges (two-pass build: count â†’ prefix-sum â†’ scatter; zero per-bucket heap allocs). `build_spatial_hash(reg, w, h)` walks `view<Position, Health>(exclude<Dead>)` and includes any entity with `SubworldTag` or `PlayerTag`. `for_each_in_radius` matches TS `forEachInRadius` semantics, also passing exact dÂ² to the visitor. |
| â›” | `subworld/citizen-sprites.ts` (220) | skipped | Pre-renders citizen/player paper-doll walk strips for TS Canvas2D `drawImage`. Native NPC visuals use the paper-doll 3D billboard path; no 2D walk-strip atlas is planned for the current renderer. |
| âœ… | `subworld/gen-worker.ts` (140) | `sub/seamless_manager.{h,cpp}` | Native equivalent is implemented with owned `std::jthread` workers: seam crossing installs placeholder cells, queues exposed-cell generation, stitches completed cells on the main thread, drains outgoing save jobs, and runs async composite road smoothing. Worker scheduling keeps exposed-cell generation ahead of save snapshots and keeps cosmetic smoothing behind persistence. Evidence: `subworld_async_seam_test` CMake target; direct MSVC run on 2026-05-15 exits 0 with road/plain/diagonal/rapid-reversal coverage, `snapshot_pending ok`, `worker_restore_saved ok`, saved-structure restore, and sparse road-mask verification. Observed Debug generation slices vary by local load; after the saved-cache lifetime fix a 5-run focused stability loop passed, and the latest shared 13-test run logged 22.451 ms road, 94.605 ms plain, 35.042 ms diagonal, and 54.911 ms reversal, all with seam-path smoothing at 0.000 ms. |

### L3 â€” Event System (`src/game/*.ts` â†’ `timaert_c/src/events/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| âœ… | `event-bus.ts` (180) | `events/event_bus.{h,cpp}` | TS-faithful: tick buffer + lastTick + history, per-tag listeners, `emit`/`emit_all`/`on`/`unsubscribe`/`flush`. Adds the full TS query surface â€” `has_tag`, `find`, `find_all`, `query_history` (newest-first scan with limit), `trim_history`, `reset`, `tick()`. Common tick/listener buffers are pre-reserved and query result vectors reserve expected size to avoid first-event/query heap churn in normal gameplay; listener subscribe/unsubscribe/reset during dispatch is deferred safely so UI callbacks cannot invalidate the dispatch vector. Evidence: CMake `quest_lifecycle_test.exe` reports `event_bus=ok`, covering emit_all, listener mutation, flush promotion, newest-first history queries, trim, and reset. |
| ðŸŸ¨ | `event-types.ts` (322) | `events/event_types.h` | C++ `EventTag` is a parallel design. `SpawnEntity` is present for procedural quest `onAccept`; `PlayerEnterSettlement` and `PlayerLeaveSettlement` are present because native settlement transitions now emit the TS-shaped enter/leave tags. Quest lifecycle producers now use TS canonical names `QuestStart`, `QuestUpdate`, `QuestComplete`, and `QuestFail`; old native names are compatibility aliases with static assertions preserving the v8 serialized values. Extended TS tags `NpcHpChange`, `SettlementMoodChange`, `PlayerStatChange`, `BattleEnd`, `MagicSurge`, `FactionRelationChange`, `DialogStart`, and `CameraMove` now exist in the native enum and have v8 save round-trip coverage. This is schema/save parity only; normal gameplay producers/consumers are still incomplete for several tags. C++-only useful tags: `PlayerDeath`, `NpcGreeted`, `SpellCast`, `SpellLearned`, `Trade`, `SettlementVisit` legacy compatibility, `QuestAbandoned` native extension. Evidence: `quest_lifecycle_test.exe` `quest_tags=ok settlement_enter=ok settlement_leave=ok`, `save_roundtrip_test.exe` active quest event round-trip including the extended tags. |
| âœ… | `logic-nodes.ts` (230) | `events/logic_nodes.{h,cpp}` | Runtime parity: registration is separate from activation, conditions consume `last_tick_events`, self-linked nodes remain active, routed nodes activate `next`, and node effects can safely add/remove nodes during dispatch. Evidence: CMake `quest_lifecycle_test.exe` with `logic_register=ok`, `logic_rehash=ok`, and `logic_self_remove=ok`. C++ debug surface is count/consistency based rather than TS `allNodes`/`activeNodeIds`, but gameplay semantics match. |
| âœ… | `effect-applicator.ts` | `events/effect_applicator.{h,cpp}` | TS effect verbs verified; native quest-ledger bridge is explicit: `QuestComplete` appends completed IDs, `QuestFail` appends failed IDs only. Legacy `QuestCompleted`/`QuestFailed` aliases resolve to the same serialized tags. Evidence: CMake `quest_lifecycle_test.exe` `quest_tags=ok quest_failed=ok`. |
| âœ… | `node-registry.ts` (106) | `events/node_registry.{h,cpp}` | `enc_random`, `sys_level_up`, and `sys_settlement` run through `LogicNodeEngine` and emit TS-shaped `ShowDialog` payloads. `enc_random` consumes `PlayerMove` distance/step payloads, rolls TS-style chance, and copies encounter choices/effects into `DialogChoicePayload`; the previous app-loop random modal shortcut was removed to avoid duplicate encounter rolls. `sys_settlement` consumes TS `PlayerEnterSettlement` and keeps `SettlementVisit` as a legacy compatibility input. Evidence: CMake `quest_lifecycle_test.exe` `enc_random=ok settlement_enter=ok settlement_leave=ok` plus level/settlement dialog checks, and app smoke boot shows `logic=5 active=3`. |
| âœ… | `quests/quest-engine.ts` (295) | `events/quests/quest_engine.{h,cpp}` | Six universal verbs (`VisitCell`, `FindLocation`, `DeliverItems`, `DestroyNpc`, `WaitAt`, `InteractCell`) with TS-faithful per-tick checker dispatch; reward application for all 5 kinds (`Gold`, `Xp`, `Item`, `Reputation`, `Event`); accept/abandon lifecycle emits matching events; deadline expiry on `time.day > expireDay`. Tick consumes `bus.last_tick_events()` for kill / interact / cell-change matching. |
| âœ… | `quests/quest-types.ts` (102) | `events/quests/quest_types.h` | Six `ObjectiveKind` verbs and five `RewardKind` kinds match TS 1:1; `Quest` POD carries id/title/description/category/giver/objectives/rewards/onAccept/expireDay/difficulty. Naming uses C++ idiom (`kind` for discriminant, capitalised enums) but data layout is bit-equivalent. |

### L4 â€” Plot Content (`src/game/plot/`, `src/game/spells/`, `src/game/quests/`)

| Status | TS module (LoC) | C++ target | Notes |
|--------|-----------------|-----------|-------|
| âœ… | `quests/quest-generators.ts` (424) | `content/quests/procedural.{h,cpp}` | Seven TS generator templates ported: delivery, visit, destroy, protect, fetch, scout, sanctuary. City/village APIs are separate in C++ because native UI currently requests settlement quests by concrete landmark type. Village quest IDs use a `v` segment because native village IDs can overlap city IDs, while TS village IDs are globally offset. Evidence: economy-driven delivery, village protect `SpawnEntity`, and same-ID city/village quest ID scope checks in CMake `quest_lifecycle_test.exe`. |
| âœ… | `plot/encounters.ts` (406) | `content/plot/encounters.{h,cpp}` | 15 encounters with full choice/effect data; modal UI + per-tick trigger wired. Corrected the Abandoned Campfire search branch to a legal TS outcome and added focused data proof. Evidence: CMake `quest_lifecycle_test.exe` `encounter_table=ok`. |
| âœ… | `plot/intro.ts` (85) | `content/plot/intro.{h,cpp}` | Four-phase intro story ported: 9 slides, sex choice, name input (`maxLength=24`), realm choice, and `intro_main` LogicNode emitting `ShowStory`. Text uses ASCII punctuation in C++ but preserves content/IDs/counts. Evidence: CMake `quest_lifecycle_test.exe` `intro_story=ok`. |
| âœ… | `plot/chapter-1.ts` (18) | `content/plot/chapter_1.h` | TS placeholder ported as dormant `plot_chapter_1` LogicNode: registered with plot content, inactive at boot, activatable after intro, false condition emits nothing and stays active. Evidence: CMake `quest_lifecycle_test.exe` `chapter_placeholder=ok`. |
| âœ… | `spells/spell-types.ts` (227) + `spell-casting.ts` (198) | `content/spells/spell_types.h` + `spell_book.{h,cpp}` + `registry.cpp` | Casting path covers learned/active/cooldowns/sustained drain with fractional mana carry, TS-style reverse-order partial depletion for multiple sustained buffs, TS-style damage/radius scaling, `SpellCast` events, projectile/AoE/chain/beam descriptors, and haste/flight sustained toggles. Spell definitions now also preserve TS multi-tag metadata, status effect names/durations, MacroEffect type/power/duration, and pros/cons flavor lists. Low-level world-map casts no longer spawn micro projectiles or claim `can_cast` success for unimplemented native macro damage effects. Evidence: CMake `spell_casting_effects_test` passed (`projectiles=4 mp=0 cooldowns=2 sustained=0`) and now exercises production damage for bolt/beam/chain, TS-default 3s basic projectile lifetime, Haste+Flight partial sustained depletion, multi-tag/status/macro/flavor metadata, Armageddon expiry blasts, friendly filtering, entity-0 owner sentinel handling, projectile reap, honest world-map non-support, and nonlethal `LastHit`; `save_roundtrip_test` passed; app smoke opened SpellOverlay, cast a projectile, emitted a `SpellCast` event, toggled Haste, and toggled Flight with direct macro path plus subworld height proof (`subFlight=6.69`). |
| âœ… | `spells/{fireball,ice-shard,lightning-chain,energy-beam,magic-bolt,armageddon,flight,haste}.ts` (~840) | `content/spells/registry.cpp` per-spell `spawn_*` + `sub/spell_effects.*` + `sub/engine.cpp` hook | All 8 spells registered with TS-faithful constants, multi-tag metadata, status effect names/durations, MacroEffect metadata, pros/cons lists, and native gameplay hooks. Magic bolt/fireball/ice shard spawn projectile/AoE descriptors with the TS default 3s projectile lifetime; lightning chain carries chain count/decay/radius and applies chained damage; energy beam is a beam visual/line-damage effect, not a mislabeled projectile; Armageddon now spawns a deterministic bounded meteor swarm with per-meteor expiry blasts instead of one fake giant AoE; haste is a sustained speed buff; flight is a sustained macro direct-path mode and subworld pitch-based flying camera height. The production effect loop is isolated in `sub/spell_effects.*` and covered by focused runtime tests. |
| âœ… | `spells/spell-renderer.ts` (96) | `sub/renderer_3d.{h,cpp}` spell visual pass | Canvas2D dispatcher intentionally remains skipped because native subworld is 3D. Equivalent spell visuals are implemented as additive 3D billboards/ribbons fed from ECS projectile descriptors plus sustained Haste/Flight caster aura rings and motes; no Canvas2D path and no per-frame asset creation. |

### Save / Load

| Status | TS module | C++ target | Notes |
|--------|-----------|-----------|-------|
| âœ… | (`map-factory.ts` regen pattern) | `macro/save.{h,cpp}` | Save schema v8 is built (`kSaveVersion = 8`) with magic/version/checksum gates, atomic write, inspect, quest serialization, `failedQuestIds`, `SpawnEntity`, `PlayerEnterSettlement`, and `PlayerLeaveSettlement` quest event payload proof. Evidence: latest `build-msvc\save_roundtrip_test.exe` passed with a 2126-byte focused v8 fixture; earlier native save/load smoke wrote a 51256-byte save slot, while the latest longer smoke rerun exited during boot before reaching save/load and is not counted as fresh proof. |

## External Reference: `proto_c/` (playable C++ prototype)

`proto_c/` is a separate, **playable** C++ prototype of Samosbor
(SDL2 + SDL2_image + SDL2_ttf, ~5 471 LOC). It is **raw and incomplete**
â€” no subworld, no OpenGL, no ECS, no event bus, no spells, no quests, no
politik, no zones â€” but the macroworld portion **runs as a real game** with
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
- **State machine.** `proto_c/src/states/*` â€” menu, play, pause,
  map, event, load, settings, stat. Already mirrored in our `AppState`
  enum; copy any UX fixes from there.
- **Save binary I/O.** `proto_c/src/systems/save_game.h` is a working
  reference for binary read/write patterns. We use the regen-from-seed
  variant in `macro/save.cpp`.
- **Wraparound terrain generator.** `proto_c/src/core/tergen.h` â€”
  cross-check perf vs. our `macro/spawners.cpp`.
- **Content inspiration.** `proto_c/src/systems/random_events.cpp`
  (1 485 LOC) â€” large hand-written event catalogue; lift *style* into
  `content/plot/` during the L4 pass, but **never as 1:1 gameplay**.
- **Good practices.** Any small idiom (POD layouts, tile rendering tricks,
  panel widget patterns) is fair game.

Do **not** use proto_c for:

- Gameplay constants, formulas, item catalogs, NPC stats, NPC loot tables
  matrices, attribute formulas, economy. Those come from `../src/`
  (TypeScript) only â€” proto_c has its own divergent data and using it
  would silently introduce drift.
- Architecture decisions about layering (L1â€“L4), event bus, ECS, or
  shaders â€” proto_c predates all of these.

**Summary:** TS is the gameplay source of truth; proto_c is the
*playable shape* the native build is supposed to converge to.

## Intentional Skips

- `subworld/gen-worker.ts` â€” TS Web Worker object model is not ported as a
  separate module. The native equivalent now lives in
  `sub/seamless_manager.{h,cpp}` with owned `std::jthread` workers,
  placeholder cells, completed-job stitching, outgoing save jobs, and async
  composite road smoothing.
- TS Vite glue (`vite.config.ts`, `svelte.config.js`, etc.) â€” N/A.
- HTML overlays â€” replaced by ImGui (`src/ui/overlays.cpp`).
- **2D Canvas2D-specific subworld helpers** â€” the C++ port has a minimal
  OpenGL 2D top-down toggle for diagnostics, but gameplay visuals are driven
  by the 3D renderer (`renderer_3d.cpp`). The following TS-only members of
  `subworld/base-generator.ts` and `subworld/map-data.ts` are therefore not
  ported as Canvas2D tile-grid logic:
  - `layGrassBase` â€” 2D tile-id biome blending. Replaced by GLSL bilinear
    sampling across the 3Ã—3 grid in `renderer_3d.cpp`'s fragment shader
    (per-pixel instead of per-tile, no quantisation).
  - `markOrganicMainRoad` â€” 2D wavy road tile stamping. Roads in 3D are
    handled by the macro feature layer + future 3D road geometry.
  - `NeighborGrid`, `EdgeAnchor`, `blendedMacroHeightmap` â€” sub-cell
    continuity helpers for the 2D pipeline. The 3D port already has
    cross-cell continuity via `nbHeights[9]` + bilinear blend in
    `generate_heightmap`.
  Removing these from the ðŸŸ¨ backlog: `subworld/base-generator.ts` and
  `subworld/map-data.ts` are now considered âœ… for native 3D/minimal-2D
  purposes;
  remaining stub work in those files (organic road tracing in 3D,
  edge-anchored heightmap continuity beyond the 3Ã—3 window) will be
  reopened only if a concrete 3D-renderer issue exposes the gap.
- **2D Canvas2D rendering companions** â€” the following TS modules are not
  ported as Canvas2D draw paths:
  - `subworld/map-renderer.ts` â€” Canvas2D top-down sub renderer. Native has
    `sub/renderer_2d.{h,cpp}` as a minimal GL tile/height diagnostic renderer,
    not TS sprite-grid parity.
  - `subworld/citizen-sprites.ts` â€” pre-renders paper-doll walk strips
    into a 2D sheet for `drawImage`. C++ sub billboards NPCs in 3D instead
    of blitting a 2D walk-strip atlas.
  - `spells/spell-renderer.ts` â€” Canvas2D `drawSpellProjectile` /
    `drawCasterAura` dispatcher. C++ spell visuals are additive 3D
    billboards/ribbons plus sustained aura ring/mote instances, not 2D
    arc/fill draws.

## Translation Order (Phase Plan)

The aim: move every ðŸŸ¨ row to âœ… in dependency order (lower layers first).
Each phase ends with a clean Windows/MSVC build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`
and a manual playtest of the affected subsystem.

### Phase 0 â€” App shell (user-visible UX, blocks playtesting)
- 0.1 âœ… `AppState` state machine (Title / Playing / Paused / Dead)
- 0.2 âœ… Title screen (`TitleScreen.svelte` â†’ `ui/screens.cpp`) â€”
       centred via `ImGui::GetIO().DisplaySize` (pass logical points,
       not Retina drawable pixels â€” the latter pushes the menu off-screen)
- 0.3 âœ… Pause overlay (`PauseOverlay.svelte` â†’ `ui/screens.cpp`)
- 0.4 âœ… Death screen (`DeathOverlay.svelte` â†’ `ui/screens.cpp`)
- 0.5 âœ… Player HUD (`StatOverlay.svelte` HP/MP/SP/gold/time â†’ `ui/screens.cpp`)
- 0.6 âœ… Hint bar (key bindings â€” bottom of screen)
- 0.7 âœ… Camera: smooth follow + middle/right-mouse pan + wheel zoom
       (default zoom 32 px/cell â€” clamp 4..96 â€” lower values cause the
       256Â² map to wrap-tile across the viewport via `fract()` UVs)
- 0.8 âœ… Save (F5) / Load (F9) wired to shell; `save_roundtrip_test`
       passes, canonical GUI round trip still needs one proof log
- 0.9 âœ… `draw_player_hud` top status bar (time / gold /
       HP / MP / SP / items / coords / biome)
- 0.10 âœ… `draw_bottom_toolbar` bottom command toolbar
- 0.11 âœ… right-side proximity panel, NPC Talk, NPC Trade, and NPC Attack are
       runtime-evidenced. Trade uses real `ecs::NpcInventory` stacks,
       native economy buy/sell helpers, transaction messages, and
       `LogType::Economy` entries. Attack routes the selected macro NPC into
       normal subworld combat through `SubworldEngine::spawn_hostile_npc`.
       Selected inventory, traits, and visual identity are projected into
       subworld; Talk closes on Escape; faction diplomacy/survivor
       persistence remains outside this UI action contract.
 - 0.12 âœ… Spell overlay (`SpellOverlay.svelte`) â€” character-panel Spells tab reads real `SpellBook.learned`, active spell, MP, cooldowns, sustained state, native fallback icon glyphs, original TS icon metadata, rarity/cast timing, TS tags/status/macro metadata, full TS descriptions, pros/cons flavor, and TS-derived damage/radius. Runtime proof: `open_spells` smoke logged `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`, followed by projectile cast, sustained Haste drain, sustained Flight smoke (`path=18`, `projectileDelta=0`, `subFlight=6.69`), and a 3D subworld aura capture with Haste+Flight active.
- 0.13 âœ… Trade overlay (`TradeOverlay.svelte`) â€” the TS buy/sell behaviour
       is covered by native settlement Trade and NPC proximity Trade surfaces.
       A separate full-screen wrapper is intentionally not duplicated because
       native UX owns trade from the active landmark/NPC panel. Mood pricing,
       CHA discount, NPC trait modifiers, inventory validation, transaction
       messages, economy log entries, and native `ItemDef` description/weight
       tooltips are implemented. Runtime proof:
       `open_settlement_trade,open_npc_trade` smoke on seed 107, plus
       Build/NPC panel/NPC Trade/Attack smoke on seed 42.
- 0.14 âœ… Story / event overlay (`EventOverlay.svelte`, `StoryOverlay.svelte`) â€”
       encounter modal exists; `ShowDialog` consumes `DialogChoicePayload`
       labels/effects and routes `nodeId` choices through app-layer logic
       activation; malformed count-only dialogs show disabled placeholders.
       `ShowStory` opens the native story overlay and emits `StoryResult`.
       Runtime proof: `trigger_level_dialog`, `trigger_count_only_dialog`,
       `trigger_story_overlay`, and `complete_story_overlay` smokes.
- 0.15 âœ… Codex overlay (`CodexOverlay.svelte`) â€” pause menu now opens
       Codex like TS, and the native overlay renders the static
       category/article content table filtered by `player.codexUnlocked`
       instead of raw ids. Runtime proof: `open_codex` smoke on seed 106.
- 0.16 âœ… Intro slideshow (`plot/intro.ts` + `StoryOverlay.svelte`) â€” there
       is no `IntroOverlay.svelte` in the TS source. Native intro runs through
       `content/plot/intro.{h,cpp}` and `StoryOverlayState`, with sex choice,
       name input, realm choice, and `StoryResult` routing. Evidence:
       `quest_lifecycle_test` `intro_story=ok`, `trigger_story_overlay`,
       and `complete_story_overlay` smokes.
- 0.17 âœ… **Macro renderer rework** â€” per-biome dispatch,
       neighbour-aware shore, and climate overlay are implemented in
       `macro_renderer.cpp`

### Phase X â€” Performance (parallel; "TS was very slow")

C++ port should already be 5-50Ã— faster just by being native. These are the
known wins to extract once gameplay parity is reached:

- X1. âœ… Worker-backed exposed-cell subworld generation is implemented in
       `SeamlessSubworldManager`; latest shared 13-test MSVC run exits 0 and
       logs 22.451-94.605 ms worker-generation slices (`roadGen=22.451ms`,
       `plainGen=94.605ms`, `diagonalGen=35.042ms`,
       `reversalGen=54.911ms`). A 5-run focused stability loop after the
       saved-cache lifetime fix also exited 0. Remaining performance work is
       measuring full seam-crossing upload/smoothing cost on target hardware.
- X2. â³ Instanced rendering for trees / structures on macroworld (one draw
       call per type instead of per-entity quads)
- X3. â³ SoA layout for hot ECS components (position, velocity, sprite-id)
- X4. âœ… Event and logic callback storage no longer uses `std::function`.
       `core/small_function.h` provides copyable 64-byte inline
       type-erased storage for `EventBus` handlers plus `LogicNode`
       predicates/effects/checks, avoiding callback target heap allocation
       on the event/logic dispatch path. Fresh proof:
       `quest_lifecycle_test`, `save_roundtrip_test`.
- X5. â³ Stable arena allocator for per-tick allocations (path-finder
       open-set, AI scratch buffers)
- X6. â³ SIMD biome sampling (4 cells/lane via NEON on Apple Silicon)
- X7. â³ Profile with `Instruments â†’ Time Profiler` once gameplay is faithful

### Phase A â€” L1 pure data & formulas (small, leaf)
- A1. âœ… `attributes.ts` â€” full RPG schema (9 attrs, perks list, skills,
       level XP curve, carry weight)
- A2. âœ… `army.ts` â€” universal-NPC-as-soldier; per-kind `upkeepGoldPerDay`, concrete squad records, danger-zone exit gate, corpse loot, and XP smoke proof
- A3. âœ… `items.ts` â€” full catalog + NPC/fauna/settlement loot tables + useItem
- A4. âœ… `economy.ts` â€” resources, goods, prices, trade routes, player buy/sell, terrain â†’ resource mapping
- A5. âœ… `language.ts` â€” phonotactic generator (Zipf weights + weighted syllable templates + doubling)
- A6. âœ… `flag-generator.ts` â€” procedural heraldic flag (128Â² RGBA, 4-layer composition)
- A7. âœ… `movement-cost.ts` â€” SP weights per biome Ã— feature (verified)

### Phase B â€” L1 simulation (depends on A)
- B1. âœ… `state.ts` â€” `default_player` + `default_game_state` factories + faction relation matrix (bands + lineage + overrides)
- B2. âœ… `npc.ts` â€” full `NPC_TYPE_DEFS` registry + per-NPC level/inventory/character ECS components
- B3. âœ… `politik.ts` â€” capital placement + lake snap + Voronoi
- B4. âœ… `pathfinding.ts` â€” A* cost-grid pathfinding
- B5. âœ… `world-tick.ts` â€” daily settlement / village / economy / garrison ticks
- B6. âœ… `zones.ts` â€” BFS civ + BFS mountain + fBM compose
- B7. âœ… `npc-ai.ts` â€” 8-behaviour `MacroNpcRuntime` dispatch

### Phase C â€” L1 generation & rendering data (depends on B)
- C1. âœ… `tree-spawner.ts`, `mountain-spawner.ts`, `road-network.ts`,
       `road-spawner.ts`, `dirt-road-spawner.ts` â€” road audit completed:
       `trace_roads()` remains the native A* baseline with documented
       intentional divergence from TS corridor snapping; river-aware tree
       exclusion and road/water invariants are covered by
       `road_river_generation_test`
- C2. âœ… `features.ts` â€” `FeatureType` enum + grid
- C3. âœ… `biomes.ts` â€” 3x3 matrix
- C4. âœ… `biome-textures.ts` + 10 per-biome
       `bt_<biome>.ts` GLSL

### Phase D â€” L2 subworld (depends on Aâ€“C)
- D1. âœ… `subworld/map-data.ts`, `types.ts`
- D2. âœ… `subworld/base-generator.ts` â€” heightmap blend,
       coastal sculpt, mountain amplify, biome variants
- D3. âœ… `subworld/seamless-manager.ts` â€” 9-cell grid, edge recenter,
       worker-backed exposed-cell generation, outgoing snapshot jobs, saved
       height/structure restore, rapid-reversal stale-work pruning, and async
       road smoothing are implemented. `subworld_async_seam_test` passes after
       the current post-agent rebuild, and the `subworld_seam` app smoke
       crosses a real runtime seam with `[seam-cross]` upload timing evidence.
- D4. âœ… per-mode generators collapsed in
       `sub/gens/dispatch.cpp`:
       `gens/city.cpp`, `village.cpp`, `forest.cpp`, `grassland.cpp`,
       `ruin.cpp`, `mountain.cpp`, `swamp.cpp`, `water.cpp`,
       `road_generator.cpp`, `spire.cpp`
- D5. âœ… `subworld/textures.ts` â€” full 64x64 procedural atlas (per-biome
       pixel-art patterns)
- D6. âœ… `fauna.ts` / `spawn.ts` / `ai.ts` â€” critter
       tables, landmark-aware spawn, TS-fauna RNG/level/stat parity
       covered by `subworld_spawn_parity_test`, Wander/Flee/Combat dispatch
- D7. âœ… `subworld/spatial-hash.ts` â€” bucketed grid
- D8. â›” `subworld/citizen-sprites.ts` â€” skipped Canvas2D walk-strip path;
       native uses paper-doll billboards in 3D
- D9. ðŸŸ¨ `subworld/engine.ts` â€” combat constants, hostility, hit penalty,
       crowd penalty, detection radius
- D10.âœ… `subworld/sky.ts` â€” full TS port: celestial-sphere viewRay, sun, moons, twinkling stars, FBM clouds
- D11.ðŸŸ¨ `subworld/renderer-3d.ts` â€” billboard shadows,
       4-band NdotL verified

### Phase E â€” L3 event system
- E1. ðŸŸ¨ `event-types.ts` â€” `SpawnEntity`, `PlayerEnterSettlement`, `PlayerLeaveSettlement`, TS quest tags `QuestStart/QuestUpdate/QuestComplete/QuestFail`, and extended TS tags `NpcHpChange`/`SettlementMoodChange`/`PlayerStatChange`/`BattleEnd`/`MagicSurge`/`FactionRelationChange`/`DialogStart`/`CameraMove` are native schema values with save v8 proof; normal gameplay producers/consumers remain partial.
- E2. âœ… `effect-applicator.ts` â€” every TS effect verb verified; native failed-quest ledger bridge tested
- E3. âœ… `node-registry.ts` â€” `enc_random`, level-up, and settlement dialog nodes run through `LogicNodeEngine`; random encounter app-loop shortcut removed after `enc_random=ok`
- E4. âœ… `quests/quest-engine.ts` + `quest-types.ts` â€” all six objective verbs and all reward/lifecycle paths have focused native runtime proof: delivery, find-location, visit-cell, wait-at, destroy-npc, interact-cell, accept/complete/fail/abandon.

### Phase F â€” L4 content
- F1. âœ… `spells/spell-types.ts` + `spell-casting.ts` â€” full schema and cast-state path
- F2. âœ… Per-spell modules â€” native registry/effect hooks for (`fireball`, `ice-shard`,
       `lightning-chain`, `energy-beam`, `magic-bolt`, `armageddon`,
       `flight`, `haste`)
- F3. âœ… `spells/spell-renderer.ts` â€” native 3D billboard/ribbon visual effects plus Haste/Flight sustained aura instances
- F4. âœ… `quests/quest-generators.ts` â€” full procedural templates ported; delivery/protect tests cover economy and onAccept spawn payloads
- F5. âœ… `plot/encounters.ts` â€” encounter table and modal path exist; `encounter_table=ok` proves table count, branch effects, battle payloads, codex/reputation effects, and legal randomized TS branch outcomes
- F6. âœ… `plot/intro.ts` â€” 9-slide sequence + choices/input ported; `intro_story=ok`
- F7. âœ… `plot/chapter-1.ts` â€” dormant placeholder node registered and activatable; `chapter_placeholder=ok`

### Phase G â€” L1.character (sprites)
- G1-G5. âœ… Paper-doll atlas, animation, palette, deterministic generator,
         z-order, renderer descriptor, movement-aware macro overlay hook,
         fixed texture cache, and subworld 3D billboard hook are native.
         Evidence: `character_paperdoll_test`,
         `character_paperdoll_gl_smoke_test`, full MSVC build, and boot smoke
         with atlas load + visible-frame PASS.

### Phase H â€” Audio & polish
- H1. âœ… `audio.ts` â€” SDL_mixer wrapper, track/SFX registry, stable asset contract, required native mixer dependency, volume/mute contract test, dummy-driver runtime playback test, title/macro/subworld state music hooks, and failed-track replay latch; audio tests plus the seed-42 `subworld_audio` app smoke pass, including `explore -> subworld -> explore` music transition proof
- H2. âœ… Runtime proof for canonical GUI save/load round trip, TS save-field parity, and built-in node side effects. Evidence: save/load smoke passed; `trigger_level_dialog` smoke captured `Level Up!`; `trigger_story_overlay` smoke captured intro story phase 0; `complete_story_overlay` smoke verified `StoryResult` application.
- H3. â³ Final pass: walk every TS export and confirm 1:1 in C++

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
6. Build with the Windows/MSVC `build-msvc` command above â€” must be clean.
7. Move the row from ðŸŸ¨ / â³ to âœ… in this file.
