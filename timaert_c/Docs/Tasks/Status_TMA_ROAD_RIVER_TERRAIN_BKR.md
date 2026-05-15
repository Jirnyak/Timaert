# Status: TMA_ROAD_RIVER_TERRAIN_BKR

Domain: L1 macro terrain generation, road audit evidence, river data, feature masks

## Checklist

- [x] Prompt extracted from `TIMAERT BATCH.md`.
  - Evidence: exact `<AGENT_PROMPT id="TMA_ROAD_RIVER_TERRAIN_BKR">` block read by CLI.
- [x] Project rules and TS authority files read.
  - Evidence: `AGENTS.md`, `matwej.md` road sections, `translation.md`, `MERGE_PLAN.md`, `ARCHITECTURE.md`, and TS macro terrain/road/tree/feature files inspected.
- [x] Road baseline preserved.
  - Evidence: `trace_roads` algorithm not replaced with TS corridor snapping; root docs state the intentional native A* divergence. Current source keeps component pre-prune plus generation-tagged terrain-cost A* with a large-map step cap and water blocked during expansion.
- [x] Road invariant test added.
  - Evidence: `tests/road_river_generation_test.cpp` covers pruning a water-only Politik edge, preserving a small dry detour, using A* for terrain-cost validation, and pruning over-budget large detours without stamping rejected water cells.
- [x] Road generation performance upgraded without topology drift.
  - Evidence: `trace_roads` now pre-prunes city pairs in different 8-connected land components, uses reusable generation-tagged A* scratch buffers, blocks water cells during road expansion, and caps large-map searches at 4096 expansions per edge.
- [x] Continuation audit removed stale direct-line/fallback road path.
  - Evidence: `trace_roads` now has no direct-line, water-stamping fallback, bounded-window helper, or cap-hit stat fields. Large-map A* is step-capped, and unproven same-component routes are pruned.
- [x] Native river data added.
  - Evidence: `TerrainData::riverData` and `TerrainData::riverTexture` added; `generate_terrain` builds a CPU river mask, carves river cells below sea threshold, recomputes land alpha, and uploads R8 data.
- [x] Tree spawning respects rivers.
  - Evidence: `spawn_trees` builds a TS-style two-cell river exclusion buffer from `TerrainData::riverData`.
- [x] Macro renderer exposes rivers.
  - Evidence: `MacroRenderer` samples `u_riverMap` and applies a TS-style blue river overlay before feature overlays.
- [x] `features.ts` transfer hardened.
  - Evidence: `FeatureLayer::resize/at/set` now guard empty dimensions, `build_feature_layer` validates terrain/mask sizes before reading, preserves TS feature priority `Mountain -> Tree -> DirtRoad -> Road`, uses TS flattened-index tree semantics, keeps the deliberate native alpha/sea-level water filter, and `feature_layer_parity_test` verifies priority, torus lookup, water filtering, empty layer safety, malformed mask safety, threshold behavior, and tree index semantics.
- [x] Full MSVC build.
  - Evidence: isolated `build-msvc-roadriver` configured with MSVC/Ninja plus `SDL2_DIR`, `SDL2_mixer_DIR`, and disconnected FetchContent; latest targeted build for `timaert`, feature, road, pathfinding, quest, save, and subworld tests returned exit 0.
- [x] CMake target test executable built and run.
  - Evidence: `build-msvc-roadriver\feature_layer_parity_test.exe` output `feature_layer_parity_test: ok`; `build-msvc-roadriver\road_river_generation_test.exe` output `road_river_generation_test: ok`.
- [x] Baseline tests run.
  - Evidence: `pathfinding_parity_test`, `quest_lifecycle_test`, `save_roundtrip_test`, and `subworld_generator_parity_test` returned OK from `build-msvc-roadriver` after the Hecton documentation import.
- [x] Seed smoke seeds 1..10.
  - Evidence: `TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,wait_visible,quit`, `TIMAERT_BOOT_TRACE=1`, seeds 1..10 all returned exit 0 and `[smoke] PASS`.
- [x] Parity docs updated.
  - Evidence: `translation.md`, `ARCHITECTURE.md`, and `MERGE_PLAN.md` now mark the road audit completed, record river-aware tree exclusion, and identify `feature_layer_parity_test` as the `features.ts` evidence.
- [x] Hecton documentation/tasks/logs imported into Timaert.
  - Evidence: `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs` contains the 1659-file base `MANIFEST.tsv`, `IMPORT_INDEX_2026-05-15.md`, and later `DELTA_SYNC_*.tsv` manifests. Stable sync `DELTA_SYNC_20260515_170957.tsv` selected 2048 Hecton source files, copied/refreshed 21 files in round 1, then reached two consecutive zero-change rounds with remaining missing/stale-by-size counts at 0/0. Latest Timaert-side import tree count is 3084 files / 305885792 bytes. No Timaert docs were written to Hecton.
- [x] Rationale recorded.
  - Evidence: `Docs/AgentLogs/Rationale_TMA_ROAD_RIVER_TERRAIN_BKR.md` contains decisions for road baseline, river post-pass, tree exclusion, renderer overlay, isolated verification, exact A* acceleration, Hecton import layout, bounded-path removal, feature parity locking, and the stable Hecton import boundary.
- [x] Final feature-layer malformed-storage hardening.
  - Evidence: `src/macro/features.h` now bounds-checks computed indices against `FeatureLayer::data.size()` in both `at()` and `set()`, so externally malformed non-empty dimensions fail closed instead of reading/writing beyond the backing byte grid.
  - Evidence: `FeatureLayer::resize()` now rejects overflowed `width * height` dimensions before allocating.
  - Evidence: `tests/feature_layer_parity_test.cpp` now covers short backing storage: valid prefix lookup works, while out-of-range lookup/set fails closed.
- [x] Macro renderer MSVC shader-literal portability fix.
  - Evidence: `src/macro/macro_renderer.cpp` splits the large fragment shader into independent static chunks and concatenates into `std::string` before `gl_link`, preserving GLSL source while avoiding MSVC `C2026 string too big`.
  - Verification: `cmake --build build-msvc-roadriver --target feature_layer_parity_test road_river_generation_test pathfinding_parity_test timaert -- -j 4` passed through `VsDevCmd.bat`.
- [x] Final verification pass after feature hardening.
  - Verification: `build-msvc-roadriver\feature_layer_parity_test.exe` passed: `feature_layer_parity_test: ok`.
  - Verification: `build-msvc-roadriver\road_river_generation_test.exe` passed: `road_river_generation_test: ok`.
  - Verification: `build-msvc-roadriver\pathfinding_parity_test.exe` passed: `pathfinding_parity_test: ok`.
  - Verification: seed 42 boot smoke `new_game,wait_boot_done,wait_visible,quit` passed with `[smoke] PASS`.
  - Verification: baseline tests passed: `quest_lifecycle_test.exe`, `save_roundtrip_test.exe`, `subworld_generator_parity_test.exe`.
  - Note: no dotnet rebuilds were run.
- [x] Final Hecton import refresh into Timaert only.
  - Evidence: selected Hecton source files observed: `2694`.
  - Evidence: copied `3` newly missing selected files into `C:\Timaert\timaert_c\Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`.
  - Evidence: new delta manifest: `MANIFEST_DELTA_2026-05-15_TMA_ROAD_RIVER_TERRAIN_BKR_REFRESH_FINAL.tsv`.
  - Verification: copy errors: `0`; remaining selected missing files after refresh: `0`.
  - Verification: import tree total files: `3088`; imported `Docs` files: `2370`; root-level files: `79`; task-path files: `296`; agent-log-path files: `1085`; report-path files: `117`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Final live Hecton import recheck closed.
  - Evidence: Hecton emitted additional selected files after the previous refresh, so a final Timaert-only missing-file pass was run.
  - Evidence: selected Hecton source files observed: `2699`.
  - Evidence: copied `5` newly missing selected files into `C:\Timaert\timaert_c\Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`.
  - Evidence: new delta manifest: `MANIFEST_DELTA_2026-05-15_TMA_ROAD_RIVER_TERRAIN_BKR_REFRESH_LIVE_FINAL2.tsv`.
  - Verification: copy errors: `0`; remaining selected missing files after refresh: `0`; import tree total files: `3095`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Final bounded live Hecton sync stabilized.
  - Evidence: Hecton continued emitting selected files after the `LIVE_FINAL2` pass, so a bounded live-sync loop was run.
  - Evidence: loop rounds: `round=1 selected=2701 copied=2 missingAfter=0`; `round=2 selected=2703 copied=2 missingAfter=0`; `round=3 selected=2704 copied=1 missingAfter=0`; `round=4 selected=2704 copied=0 missingAfter=0`; `round=5 selected=2704 copied=0 missingAfter=0`.
  - Evidence: new delta manifest: `MANIFEST_DELTA_2026-05-15_TMA_ROAD_RIVER_TERRAIN_BKR_REFRESH_LIVE_STABLE_FINAL3.tsv`.
  - Verification: total copied by stable loop: `5`; total copy errors: `0`; final selected missing files: `0`; import tree total files: `3101`; import tree bytes: `306549145`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest bounded live Hecton sync stabilized.
  - Evidence: Hecton emitted `3` additional selected files after `LIVE_STABLE_FINAL3`, so a final catch-up loop was run.
  - Evidence: loop rounds: `round=1 selected=2707 copied=3 missingAfter=0`; `round=2 selected=2707 copied=0 missingAfter=0`; `round=3 selected=2707 copied=0 missingAfter=0`; `round=4 selected=2707 copied=0 missingAfter=0`.
  - Evidence: new delta manifest: `MANIFEST_DELTA_2026-05-15_TMA_ROAD_RIVER_TERRAIN_BKR_REFRESH_LIVE_STABLE_FINAL4.tsv`.
  - Verification: total copied by latest loop: `3`; total copy errors: `0`; final selected missing files: `0`; import tree total files: `3107`; import tree bytes: `306894645`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Feature consumers fail closed on malformed feature storage.
  - Evidence: `FeatureLayer` now exposes `cell_count_for`, `cell_count`, `has_complete_storage`, and `covers` so consumers validate dimensions/storage once before hot loops.
  - Evidence: `build_cost_grid` now rejects malformed terrain RGBA and ignores incomplete or mismatched feature storage instead of indexing `features.data[i]` blindly.
  - Evidence: `generate_zones` now rejects invalid dimensions and treats malformed feature storage as all `FT_None`; `ZoneLayer::at/field_at` return safe defaults for empty or short backing storage.
  - Evidence: `MacroRenderer::upload_features` falls back to a 1x1 blank feature texture if the feature layer is incomplete.
  - Verification: `pathfinding_parity_test` now covers complete feature movement costs, malformed feature storage, mismatched dimensions, malformed terrain storage, zone-generation fallback, and invalid zone dimensions.
- [x] Continued Hecton import sync stabilized after user repeated the transfer order.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE_20260515_173924.tsv` loop rounds: `round=1 selected=2707 copied=18 missing=0 stale=0`; `round=2 selected=2709 copied=2 missing=0 stale=0`; `round=3 selected=2709 copied=0 missing=0 stale=0`.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE_VERIFY_20260515_174057.tsv` verification round: `selected=2709 copied=0 errors=0 missing=0 stale=0`.
  - Verification: final import tree total files: `3111`; import tree bytes: `309483263`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest Hecton import snapshot boundary captured.
  - Evidence: Hecton emitted late integration build logs after the prior stable boundary, so a final boundary sync was run and documented as a snapshot, not a permanent mirror.
  - Evidence: loop rounds: `round=1 selected=2711 copied=2 missingAfter=0`; `round=2 selected=2711 copied=0 missingAfter=0`; `round=3 selected=2711 copied=0 missingAfter=0`.
  - Evidence: new delta manifest: `MANIFEST_DELTA_2026-05-15_TMA_ROAD_RIVER_TERRAIN_BKR_REFRESH_LIVE_BOUNDARY_FINAL5.tsv`.
  - Verification: total copied by latest boundary loop: `2`; total copy errors: `0`; final selected missing files at boundary: `0`; import tree total files: `3114`; import tree bytes: `309485893`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Current tree revalidated after concurrent workspace changes.
  - Build: `cmake --build build-msvc-roadriver --target feature_layer_parity_test road_river_generation_test pathfinding_parity_test timaert -- -j 4` returned exit `0`.
  - Verification: `feature_layer_parity_test.exe`, `road_river_generation_test.exe`, `pathfinding_parity_test.exe`, `quest_lifecycle_test.exe`, `save_roundtrip_test.exe`, and `subworld_generator_parity_test.exe` passed.
  - Verification: seed `42` boot smoke `new_game,wait_boot_done,wait_visible,quit` passed with `[smoke] PASS`.
  - Note: no dotnet rebuilds were run.
- [x] Continued hardening verification pass completed.
  - Verification: MSVC build target set `timaert pathfinding_parity_test feature_layer_parity_test road_river_generation_test quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test` returned exit `0`.
  - Verification: `pathfinding_parity_test`, `feature_layer_parity_test`, `road_river_generation_test`, `quest_lifecycle_test`, `save_roundtrip_test`, and `subworld_generator_parity_test` all passed.
  - Verification: fresh boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; road stats `cities=66 attempted=157 kept=28 pruned=129 componentPruned=18 expansions=497156`.
  - Verification: forbidden construct scan, obsolete road shortcut/fallback scan, and raw unchecked feature consumer scan all returned no matches.
- [x] Root parity plan corrected for completed road/river work.
  - Evidence: `matwej.md` Tier A now marks the road parity audit and first native river integration as done, with current evidence and future-work boundaries.
  - Reason: the stale line `C++ has none` for rivers contradicted `TerrainData::riverData`, river-aware tree exclusion, renderer `u_riverMap`, and the verified road/river tests.
- [x] Terrain-storage boundary hardening completed.
  - Evidence: `TerrainData` now exposes `cell_count`, `has_rgba_storage`, and `has_river_storage` helpers.
  - Evidence: `spawn_trees`, `trace_roads`, `build_cost_grid`, and `SubworldEngine::enter` fail closed when macro terrain RGBA storage is malformed instead of indexing invalid buffers.
  - Verification: `road_river_generation_test` covers malformed terrain fail-closed behavior for roads and trees; `pathfinding_parity_test` covers terrain storage helpers and malformed cost-grid input.
- [x] Latest Hecton continued boundary sync stabilized.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE2_20260515_180636.tsv` loop rounds: `round=1 selected=2730 copied=3 missing=0 stale=0`; `round=2 selected=2733 copied=3 missing=0 stale=0`; `round=3 selected=2733 copied=0 missing=0 stale=0`; `round=4 selected=2733 copied=0 missing=0 stale=0`.
  - Verification: final import tree total files: `3143`; import tree bytes: `313081010`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest verification after terrain-storage hardening completed.
  - Verification: MSVC build target set `timaert pathfinding_parity_test feature_layer_parity_test road_river_generation_test` returned exit `0`.
  - Verification: baseline build target set `quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test` returned exit `0`.
  - Verification: `pathfinding_parity_test`, `feature_layer_parity_test`, `road_river_generation_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` all passed.
  - Verification: seed `44` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; road stats `cities=67 attempted=155 kept=33 pruned=122 componentPruned=27 expansions=429490`.
  - Verification: forbidden construct scan, obsolete road shortcut/fallback scan, and raw unchecked feature consumer scan all returned no matches.
- [x] Politik terrain boundary hardening completed.
  - Evidence: `generate_politik` now validates map dimensions before ownership allocation and uses terrain only when `TerrainData` dimensions match the requested map and RGBA storage is complete.
  - Evidence: malformed or mismatched terrain falls back to no-terrain placement instead of direct `rgba` reads; invalid map dimensions return an empty `Politik`.
  - Evidence: `snap_cities_to_land` and `finalize_politik` return early on malformed terrain storage.
  - Verification: `road_river_generation_test` now covers malformed Politik terrain fallback, city bounds, ownership stability, and invalid map dimensions.
- [x] Zone and landmark upload boundary hardening completed.
  - Evidence: `ZoneLayer` exposes data/field storage helpers, lookup decodes invalid zone bytes to zone `0`, and `MacroRenderer::upload_zones` uploads a 1x1 blank map for short data storage.
  - Evidence: zone upload sanitizes invalid zone bytes before they reach the GPU; landmark upload already sanitizes invalid landmark ids and falls back to a 1x1 blank map for invalid inputs.
  - Verification: MSVC focused build and tests passed after the upload-boundary change.
- [x] Latest Hecton continued boundary sync stabilized.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE3_20260515_185300.tsv` loop rounds: `round=1 selected=2793 copied=1 missing=0 stale=0`; `round=2 selected=2793 copied=0 missing=0 stale=0`; `round=3 selected=2793 copied=0 missing=0 stale=0`.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE4_20260515_190948.tsv` loop rounds: `round=1 selected=2813 copied=0 missing=0 stale=0`; `round=2 selected=2813 copied=0 missing=0 stale=0`.
  - Verification: latest import tree total files: `3280`; import tree bytes: `331498308`; Hecton road-domain report paths remained absent.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest live Hecton boundary captured under loop cap.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE5_20260515_192049.tsv` loop copied/refreshed `34` late selected files across 10 rounds; final selected files: `2825`; final missing/stale: `0/0`.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE5_VERIFY_20260515_192428.tsv` copied/refreshed another `13` late selected files across 6 verification rounds; final selected files: `2832`; final missing/stale: `0/0`; final zero-change streak: `1`.
  - Verification: latest import tree total files: `3304`; import tree bytes: `338201505`.
  - Note: Hecton was still emitting selected files during the verification loop, so this is a latest snapshot boundary rather than proof that the separate Hecton workspace stopped changing.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest verification after Politik and upload-boundary hardening completed.
  - Build: `cmake --build build-msvc-roadriver --target road_river_generation_test pathfinding_parity_test feature_layer_parity_test timaert -- -j 4` returned exit `0`.
  - Verification: `road_river_generation_test`, `pathfinding_parity_test`, and `feature_layer_parity_test` all passed.
  - Build: `cmake --build build-msvc-roadriver --target quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test -- -j 4` returned exit `0`.
  - Verification: `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` all passed.
  - Verification: seed `45` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; road stats `cities=61 attempted=142 kept=22 pruned=120 componentPruned=40 expansions=358819`.
  - Verification: stale Politik terrain checks, obsolete road shortcut/fallback scan, and diff whitespace check returned no blocking errors.
- [x] Continued renderer/upload verification completed.
  - Evidence: `ZoneLayer` malformed lookup coverage remains in `pathfinding_parity_test`; `MacroRenderer::upload_zones` and `upload_landmarks` sanitize invalid bytes and blank malformed dimensions/data.
  - Build: explicit CMake configure regenerated `build-msvc-roadriver`, then target set `timaert pathfinding_parity_test feature_layer_parity_test road_river_generation_test` returned exit `0`.
  - Verification: `pathfinding_parity_test`, `feature_layer_parity_test`, and `road_river_generation_test` all passed.
  - Verification: baseline build target set `quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test` returned exit `0`; all four tests passed.
  - Verification: seed `45` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; road stats `cities=67 attempted=155 kept=30 pruned=125 componentPruned=25 expansions=456318`.
  - Verification: forbidden construct scan returned no matches.
- [x] Latest Hecton continued boundary sync stabilized after live churn.
  - Evidence: first loop `DELTA_SYNC_20260515_CONTINUE3_20260515_190613.tsv` copied/refreshed `47, 1, 2, 3, 0, 1` files across six rounds, so it was not accepted as stable.
  - Evidence: verification loop `DELTA_SYNC_20260515_CONTINUE3_VERIFY_20260515_190908.tsv` copied/refreshed `5, 3, 0, 0` files across four rounds and ended with missing/stale `0/0`.
  - Verification: final import tree total files: `3281`; import tree bytes: `334213450`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Final renderer/upload closeout scans completed.
  - Verification: `git diff --check` on the road/river/feature touched files and mandatory docs returned no whitespace errors.
  - Verification: forbidden construct scan on touched C++/test files returned no matches.
  - Verification: obsolete road direct/fallback/cap-hit scan returned no matches.
  - Verification: unchecked raw feature consumer scan returned no matches.
  - Evidence: final rationale decisions and append-only log report were written to `Docs/AgentLogs/Rationale_TMA_ROAD_RIVER_TERRAIN_BKR.md` and `Docs/AgentLogs/LOG_TMA_ROAD_RIVER_TERRAIN_BKR.md`.
- [x] Dirt-road boundary hardening completed.
  - Evidence: `trace_dirt_roads` now validates map dimensions through `FeatureLayer::cell_count_for`, rejects short road masks and mismatched village coordinate arrays, and wraps village coordinates before the first road-mask lookup.
  - Verification: `road_river_generation_test` now covers invalid dimensions, short road masks, mismatched village arrays, wrapped out-of-range village coordinates, and preserving the main-road target cell.
- [x] Latest Hecton continued boundary sync stabilized.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE6_20260515_194800.tsv` loop rounds: `round=1 selected=2870 copied=13 refreshed=5 missing=0 stale=0`; `round=2 selected=2871 copied=1 refreshed=0 missing=0 stale=0`; `round=3 selected=2871 copied=0 refreshed=0 missing=0 stale=0`; `round=4 selected=2871 copied=0 refreshed=0 missing=0 stale=0`.
  - Verification: final import tree total files: `3348`; import tree bytes: `338890653`; task-path files: `297`; agent-log-path files: `1252`; report-path files: `139`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Final Hecton boundary sync stabilized.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE7_FINAL_20260515_200231.tsv` loop rounds: `round=1 selected=2882 copied=2 refreshed=2 missing=0 stale=0`; `round=2 selected=2882 copied=0 refreshed=3 missing=0 stale=0`; `round=3 selected=2882 copied=0 refreshed=0 missing=0 stale=0`; `round=4 selected=2882 copied=0 refreshed=0 missing=0 stale=0`.
  - Verification: final import tree total files: `3361`; import tree bytes: `341977403`; task-path files: `298`; agent-log-path files: `1262`; report-path files: `139`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest verification after dirt-road hardening completed.
  - Build: `cmake --build build-msvc-roadriver --target road_river_generation_test feature_layer_parity_test pathfinding_parity_test timaert --parallel 2` returned exit `0`.
  - Verification: `road_river_generation_test`, `feature_layer_parity_test`, and `pathfinding_parity_test` all passed.
  - Build: `cmake --build build-msvc-roadriver --target quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test --parallel 2` returned exit `0`.
  - Verification: `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` all passed.
  - Verification: seed `46` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; road stats `cities=70 attempted=159 kept=35 pruned=124 componentPruned=12 expansions=513893`.
  - Verification: diff whitespace scan, forbidden construct scan, obsolete road shortcut/fallback scan, and raw unchecked feature consumer scan returned no blocking errors.
- [x] Macro NPC spawn terrain boundary hardened.
  - Evidence: `src/macro/npc_spawn.cpp::is_land` now validates map dimensions and complete terrain RGBA storage before torus wrapping or alpha lookup.
  - Evidence: invalid or mismatched terrain is treated as absent terrain, preserving spawn behavior while avoiding invalid dimension/data access.
  - Evidence: invalid `GameState` map dimensions now fail closed before any `wrapi` call.
  - Documentation: `ARCHITECTURE.md` and `translation.md` now record the NPC spawn terrain-boundary contract.
  - Rejected alternative: per-attempt defensive indexing in the hot spawn search was rejected; one boundary gate keeps valid data branch-light.
  - Microsecond estimate: `0 us/frame`; one generation-time validation branch, no runtime-frame cost.
- [x] Macro NPC spawn contract coverage added.
  - Evidence: `tests/npc_spawn_contract_test.cpp` verifies malformed zero-dimension terrain and mismatched all-water terrain still spawn NPCs inside map bounds, and verifies invalid map dimensions spawn nothing instead of wrapping by zero.
  - Build: `cmake --build build-msvc-roadriver --target npc_spawn_contract_test road_river_generation_test pathfinding_parity_test feature_layer_parity_test timaert -- -j 4` returned exit `0`.
  - Verification: `npc_spawn_contract_test`, `road_river_generation_test`, `pathfinding_parity_test`, and `feature_layer_parity_test` all passed.
  - Rebuild after invalid-map guard: focused targets `npc_spawn_contract_test road_river_generation_test pathfinding_parity_test feature_layer_parity_test timaert` returned exit `0`; all four tests passed.
- [x] Latest baseline/runtime verification after NPC terrain hardening completed.
  - Build: `cmake --build build-msvc-roadriver --target quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test -- -j 4` returned exit `0` after the invalid-map guard.
  - Verification: `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` all passed after the invalid-map guard.
  - Verification: seed `47` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; trace reached `macro npcs spawned`; road stats `cities=63 attempted=142 kept=28 pruned=114 componentPruned=16 expansions=443233`.
  - Verification: seed `48` boot smoke after the invalid-map guard returned `[smoke] PASS`; trace reached `macro npcs spawned`; road stats `cities=64 attempted=150 kept=27 pruned=123 componentPruned=34 expansions=413248`.
- [x] Latest Hecton docs/tasks/logs boundary refreshed under Timaert only.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE8_20260515_202619.tsv` loop rounds ended with selected `3772`, missing/stale `0/0`, and two consecutive zero-change rounds.
  - Verification: latest import tree total files: `3889`; import tree bytes: `523232925`; task-path files: `85`; agent-log-path files: `758`; report-path files: `140`.
  - Note: this expanded selector includes Hecton `Docs`, `Logs`, `CodexArtifacts`, `.codex-artifacts`, and root doc/log/index files. Hecton remained read-only and no Timaert road-domain report files were created in `C:\hades\Hecton8`.
- [x] Dirt-road land-mask byte-count hardening completed.
  - Evidence: `trace_dirt_roads` now accepts an optional terrain RGBA byte count and fails closed if a supplied count does not cover `width * height * 4`.
  - Evidence: the active `boot_world` dirt-road call passes `app.terrain.rgba.size()` with `app.terrain.rgba.data()`, so runtime generation uses the byte-count contract instead of relying on the pointer default.
  - Verification: `road_river_generation_test` covers short supplied terrain land masks and valid alpha filtering of water cells.
- [x] Final Hecton boundary sync 8 stabilized after land-mask hardening.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE8_FINAL_20260515_204712.tsv` loop rounds: `round=1 selected=2928 copied=17 refreshed=26 errors=1 missing=0 stale=1`; `round=2 selected=2929 copied=1 refreshed=4 errors=0 missing=0 stale=0`; `round=3 selected=2929 copied=0 refreshed=0 errors=0 missing=0 stale=0`; `round=4 selected=2929 copied=0 refreshed=0 errors=0 missing=0 stale=0`.
  - Evidence: transient copy mismatch was `Docs\AgentLogs\Build_CORE_RESONANCE_AssemblyCSharp_loop11_current_20260515.log`, which changed while being copied and stabilized in later rounds.
  - Verification: final import tree total files: `3909`; import tree bytes: `523915969`; task-path files: `298`; agent-log-path files: `1308`; report-path files: `140`; final missing/stale `0/0`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Latest verification after dirt-road land-mask hardening completed.
  - Build: `cmake --build build-msvc-roadriver --target road_river_generation_test feature_layer_parity_test pathfinding_parity_test timaert --parallel 2` returned exit `0`.
  - Verification: `road_river_generation_test`, `feature_layer_parity_test`, and `pathfinding_parity_test` all passed.
  - Build: `cmake --build build-msvc-roadriver --target quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test --parallel 2` returned exit `0`.
  - Verification: `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` all passed.
  - Verification: seed `47` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; road stats `cities=63 attempted=142 kept=28 pruned=114 componentPruned=16 expansions=443233`.
- [x] Final Hecton docs/tasks/logs boundary refreshed after all local verification.
  - Evidence: `DELTA_SYNC_20260515_CONTINUE9_FINAL_20260515_210146.tsv` loop rounds ended with selected `3803`, missing/stale `0/0`, and two consecutive zero-change rounds.
  - Verification: latest import tree total files: `3922`; import tree bytes: `524582898`; task-path files: `85`; agent-log-path files: `789`; report-path files: `140`.
  - Note: Hecton remained read-only; forbidden Hecton-side road-domain report paths still returned `False`.
- [x] Final current-workspace verification after runtime dirt-road callsite audit completed.
  - Evidence: `src/app/main.cpp::boot_world` passes `app.terrain.rgba.size()` together with `app.terrain.rgba.data()` into `sm::trace_dirt_roads`.
  - Build: focused MSVC target set `road_river_generation_test feature_layer_parity_test pathfinding_parity_test timaert` returned exit `0` and rebuilt `src/app/main.cpp`, `src/macro/spawners.cpp`, `timaert.exe`, and focused tests.
  - Verification: `road_river_generation_test`, `feature_layer_parity_test`, and `pathfinding_parity_test` all passed.
  - Build: baseline target set `quest_lifecycle_test save_roundtrip_test subworld_generator_parity_test subworld_async_seam_test` returned exit `0`.
  - Verification: `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` all passed.
  - Verification: seed `49` boot smoke `new_game,wait_boot_done,wait_visible,quit` returned `[smoke] PASS`; trace reached `dirt roads traced`, `features and tree grid built`, `macro npcs spawned`, and visible samples; road stats `cities=78 attempted=183 kept=47 pruned=136 componentPruned=53 expansions=399306`.
  - Verification: final diff whitespace scan, forbidden construct scan, obsolete road shortcut/fallback scan, raw unchecked feature consumer scan, and dirt-road callsite scan returned no blocking errors.
- [x] Final Hecton docs/tasks/logs boundary sync 9 verification stabilized under Timaert only.
  - Evidence: dirty loop `DELTA_SYNC_20260515_CONTINUE9_FINAL_20260515_210842.tsv` ended with selected `3812`, errors `0`, missing `0`, stale `1`, so it was not accepted as final.
  - Evidence: accepted loop `DELTA_SYNC_20260515_CONTINUE9_VERIFY_20260515_211221.tsv` selected `3812` files and reached two zero-change rounds with errors `0`, missing `0`, stale `0`.
  - Verification: final import tree total files: `3934`; import tree bytes: `556140914`; active evidence counts `Docs/Tasks=85`, `Docs/AgentLogs=798`, `Docs/Reports=140`.
  - Note: no files were written to `C:\hades\Hecton8`.
- [x] Zone water-boost runtime parity restored.
  - Evidence: TS `GameScreen.svelte` passes `isWater` into `generateZones`; native `boot_world` now passes `app.terrain.rgba.data()` plus `app.terrain.rgba.size()` into `generate_zones`.
  - Evidence: `generate_zones` now applies water boost only when the supplied RGBA byte count covers `width * height * 4`; short supplied masks are ignored instead of indexed.
  - Verification: `pathfinding_parity_test` now covers valid water-mask boost, unchanged land cells, and short water-mask fallback.
  - Build: `cmake --build build-msvc-roadriver --target pathfinding_parity_test feature_layer_parity_test road_river_generation_test timaert -- -j 4` returned exit `0`.
  - Verification: `pathfinding_parity_test`, `feature_layer_parity_test`, and `road_river_generation_test` passed.
  - Verification: seed `49` boot smoke returned `[smoke] PASS`; trace reached `zones generated`, `zones uploaded`, and `macro npcs spawned`; road stats `cities=78 attempted=183 kept=47 pruned=136 componentPruned=53 expansions=399306`.
  - Baseline verification: `quest_lifecycle_test`, `save_roundtrip_test`, `subworld_generator_parity_test`, and `subworld_async_seam_test` passed.
  - Microsecond estimate: `0 us/frame`; the water check is zone-generation time, not frame time.
- [x] Final Hecton docs/tasks/logs boundary sync 10 verification stabilized under Timaert only.
  - Evidence: dirty loop `DELTA_SYNC_20260515_CONTINUE10_ZONE_20260515_213959.tsv` ended with zero-change streak `0`, so it was not accepted as final.
  - Evidence: accepted loop `DELTA_SYNC_20260515_CONTINUE10_ZONE_VERIFY_20260515_214418.tsv` selected `3844` files and reached two zero-change rounds with errors `0`, missing `0`, stale `0`.
  - Verification: final import tree total files: `3968`; import tree bytes: `560310159`; active evidence counts `Docs/Tasks=85`, `Docs/AgentLogs=830`, `Docs/Reports=140`.
  - Note: no files were written to `C:\hades\Hecton8`.

## Road Classification

Road algorithm: KEEP WITH DOCUMENTED INTENTIONAL DIVERGENCE.

Reason: TS corridor-guided Bresenham depends on `roadData`, but project docs and `matwej.md` identify the current native terrain-cost A* road baseline as the production baseline. The audit did not replace it. The required native guarantee is now tested: surviving Politik connections must not cross rejected water cells.

## Seed Smoke Road Stats

| Seed | Cities | Attempted | Kept | Pruned | ComponentPruned | Expansions | SmokeMs |
|------|--------|-----------|------|--------|-----------------|------------|---------|
| 1 | 59 | 138 | 22 | 116 | 36 | 361606 | 29215 |
| 2 | 63 | 148 | 23 | 125 | 31 | 425977 | 32525 |
| 3 | 70 | 161 | 35 | 126 | 33 | 432575 | 36272 |
| 4 | 65 | 151 | 22 | 129 | 35 | 420234 | 47453 |
| 5 | 61 | 143 | 12 | 131 | 36 | 405320 | 38259 |
| 6 | 69 | 160 | 29 | 131 | 45 | 404913 | 56200 |
| 7 | 61 | 143 | 12 | 131 | 50 | 352730 | 36539 |
| 8 | 67 | 154 | 35 | 119 | 18 | 470439 | 44113 |
| 9 | 67 | 152 | 19 | 133 | 56 | 339334 | 33942 |
| 10 | 72 | 166 | 41 | 125 | 27 | 456542 | 38661 |

Final audit removed stale direct-line, bounded-window helper, fallback, and cap-hit road stat fields from `trace_roads`; road tracing now uses component pre-prune plus generation-tagged terrain-cost A* with a large-map step cap and blocked water expansion. Seeds 1..10 were re-run after this correction and all returned exit 0 with `[smoke] PASS`.

## Final Status

STATUS: VERIFIED.

Reason: Timaert road/river/feature code is implemented and verified by isolated MSVC build, focused CMake tests, key baseline tests, previous seeds 1..10 runtime smoke, a seed 42 smoke after the earlier feature hardening, a fresh boot smoke after continued feature-consumer hardening, seed 44 after terrain-storage hardening, seed 45 after Politik/upload-boundary hardening, seed 46 after dirt-road boundary hardening, seed 47 after dirt-road land-mask byte-count hardening, seeds 47/48 after macro NPC terrain-boundary hardening, and seed 49 after the runtime dirt-road byte-count callsite audit and zone water-boost runtime parity restoration. The `features.ts` domain is transferred to native C++ with explicit parity/safety tests; current feature, zone, landmark, dirt-road, Politik, macro NPC spawn, and terrain consumers fail closed on malformed storage. Root road/river plan docs no longer list completed work as missing. The Hecton documentation/task/log import is present under Timaert with delta provenance and reports zero selected missing/stale files at latest boundary `DELTA_SYNC_20260515_CONTINUE10_ZONE_VERIFY_20260515_214418.tsv`; Hecton was still live during verification, so the import is a current snapshot, not a permanent mirror. Hecton remained read-only.
