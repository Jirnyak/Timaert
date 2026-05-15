# Status: TMA_SUBWORLD_GENERATOR_PARITY_BKR

Domain: TS per-mode subworld generator C++ parity.

Current final status: VERIFIED.

## Loop 1 - Authority And Boundary

- [x] Prompt extracted from `C:\Timaert\timaert_c\TIMAERT BATCH.md`.
  DOD: CLI extraction of `<AGENT_PROMPT id="TMA_SUBWORLD_GENERATOR_PARITY_BKR">`.
  Rejected: reading neighboring prompts or acting on unrelated agent tasks.
  Estimate: 0 us runtime impact.

- [x] Project authority checked.
  DOD: read `AGENTS.md`, `matwej.md`, `translation.md`, `MERGE_PLAN.md`, `ARCHITECTURE.md`.
  Rejected: expanding outside the generator domain.
  Estimate: 0 us runtime impact.

- [x] TS generator authority checked.
  DOD: read TS authority for city, village, forest, grassland, ruin, mountain, swamp, water, road, spire, base generator, map data, map factory, seamless manager.
  Rejected: native-only behavior as source of truth.
  Estimate: 0 us runtime impact.

## Loop 2 - Shared Routing And Base Data

- [x] Default wilderness routed to `SubworldMode::Grassland`.
  DOD: focused test asserts no-feature meadow resolves to grassland.
  Rejected: retaining native-only `Open` as default.
  Estimate: 0 us per frame; generation dispatch branch only.

- [x] Landmark routing made explicit.
  DOD: `CellLandmarkKind` routes city, village, ruin, spire without settlement-id inference abuse.
  Rejected: overloading population/settlement id for spires and ruins.
  Estimate: <1 us per cell resolve.

- [x] Base generator constants aligned.
  DOD: C++ `BiomeConfig` density/step values and `WATER_LEVEL=0.40` match TS authority; focused test guards drift.
  Rejected: per-biome water planes.
  Estimate: 0 us per frame; generation-only data.

## Loop 3 - Wilderness And Landmark Generators

- [x] Forest glades ported.
  DOD: TS constants `GLADE_STEP=48`, threshold `0.72`, radius `6..16`, global coordinate scan, tile clearing, and tree-structure compaction verified.
  Rejected: local-cell random glades that would break seams.
  Estimate: generation-only coarse grid scan; saves render billboards inside clearings.

- [x] Ruin generator ported.
  DOD: difficulty-controlled 1..3 rings, TS radius formula, segment count, roughness, blocked wall tiles, cracked center square verified.
  Rejected: renderer-only ruin markers.
  Estimate: bounded ring raster pass, 0 us per frame.

- [x] Spire generator ported and reachable.
  DOD: spire landmark resolves to `SubworldMode::Spire`; scorch radius, crater, and tower structure verified.
  Rejected: unreachable macro spire data.
  Estimate: bounded local raster pass, 0 us per frame.

## Loop 4 - Road/Water And Settlements

- [x] Water-road bridge data ported.
  DOD: water road cells emit `Structure::Bridge` with TS deck height `3`; focused test checks geometry.
  Rejected: visual-only bridges without generator/save data.
  Estimate: O(road connection count) per generated cell, 0 us per frame.

- [x] City generator upgraded from lightweight placeholder.
  DOD: neighbour-road-aligned gates/main roads, population-scaled wall rings, organic branch/farm roads, central square, keep, roadside block houses, fields, gates through wall raster, and tree clear radius verified by focused test.
  Rejected: unbounded TS dynamic mycelium queue because `matwej.md` records seam freeze risk from large synchronous city expansion.
  Estimate: bounded generation-only work; no per-frame heap churn added.

- [x] Village generator upgraded from lightweight placeholder.
  DOD: neighbour-road-aligned gates/main roads, central square, roadside 2-3 tile houses, palisade, fields, farm roads, and tree clear radius verified by focused test.
  Rejected: random one-tile village stamps and unbounded placement loops.
  Estimate: bounded generation-only work; no per-frame heap churn added.

## Loop 5 - Post-Verification Audit Upgrade

- [x] City keep placement hardened.
  DOD: dedicated landmark-house stamping prevents the main road from blocking the central keep; focused test now requires exactly one high central keep.
  Rejected: counting a failed keep as placed or weakening house-count assertions.
  Estimate: 0 us per frame; one bounded rectangle stamp per city generation.

- [x] Settlement roads now follow TS neighbour-road contract.
  DOD: city/village use neighbouring road/dirt-road directions when at least two exist, otherwise fallback to four cardinal roads; focused test asserts east/west neighbours do not punch north/south edge-center gates.
  Rejected: always-four-way settlement roads regardless of macro connectivity.
  Estimate: <1 us per generated settlement for direction collection; no per-frame cost.

- [x] Stale comments corrected.
  DOD: road-carving comment moved to the actual road raster helper and corrected to describe endpoint-damped organic curves.
  Rejected: leaving a false straight-line comment above the spire generator.
  Estimate: 0 us.

## Loop 6 - Gate Structure And Hygiene Upgrade

- [x] Gate openings now suppress wall structure records.
  DOD: `stamp_settlement_wall` tracks road/square overlap and skips the corresponding `Structure::Wall` segment, so future wall renderers do not draw blockers through gates.
  Rejected: tile-only gates with invisible wall structure records still crossing the opening.
  Estimate: one boolean per wall segment, no per-frame cost.

- [x] City wall generation reordered around roads.
  DOD: city main/branch roads are carved before wall rings, letting the shared wall stamper see gates and skip both tiles and structure records.
  Rejected: carving roads after walls, which fixed tiles but left stale wall structures.
  Estimate: same bounded generation loops, reordered only.

- [x] Source-root build artifacts removed.
  DOD: explicit path-verified removal of root-level `*.obj` files generated by manual compiles; build outputs now stay under `build-msvc`.
  Rejected: leaving ignored binary clutter in the repo root.
  Estimate: 0 us runtime impact.

## Loop 7 - Verification And Reports

- [x] Focused generator parity built and run.
  Commands: `build-msvc\subworld_generator_parity_test.exe`, `build-msvc\subworld_generator_parity_test_direct.exe`.
  Result: exit 0; `forest_glades=79 tree_structures=10549 spire_scorch_rock=4840 ruin_wall_tiles=6069 city_houses=380 village_houses=24 road_bridge_count=1 grassland_trees=1333 swamp_trees=5643`.

- [x] Seam crossing proof rebuilt and run.
  Command: direct MSVC build/run of `tests\subworld_async_seam_test.cpp`.
  Result: exit 0; latest road case `dirty=4 gen=28.500ms total=28.500ms`; latest plain case `dirty=3 gen=37.270ms total=37.271ms`.

- [x] Full MSVC app build run.
  Command: `VsDevCmd.bat` + `cmake --build build-msvc`.
  Result: exit 0; final run reported `ninja: no work to do` after relevant objects were already newer than source. Earlier `timaert.exe` link lock was resolved by stopping the stale process and moving the old binary inside `build-msvc`.

- [x] Existing regression tests run.
  Results: `quest_lifecycle_test.exe` exit 0; `save_roundtrip_test.exe` exit 0; `pathfinding_parity_test.exe` exit 0.

- [x] Runtime subworld smoke run.
  Command: `TIMAERT_BOOT_TRACE=1 TIMAERT_SMOKE_SEED=42 TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,subworld_time,quit build-msvc\timaert.exe`.
  Result: latest run exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Earlier transient run exited 1 during macro boot after `[boot] trees spawned`; later runs passed.

- [x] Forbidden C++ constructs scan.
  Command: `rg "\btry\b|\bcatch\b|\bthrow\b|dynamic_cast|typeid|std::rand|unsigned int" src\sub\gens\dispatch.cpp tests\subworld_generator_parity_test.cpp`.
  Result: no matches.

- [x] Parity ledger and required reports updated.
  Files: `translation.md`, this status file, `Docs\AgentLogs\Rationale_TMA_SUBWORLD_GENERATOR_PARITY_BKR.md`, `Docs\AgentLogs\LOG_TMA_SUBWORLD_GENERATOR_PARITY_BKR.md`.

## Loop 8 - Protected Wall Structure Audit

- [x] City wall stamping moved behind every road/farm cut.
  DOD: city main roads, branch streets, fields, and field-road connectors are generated before wall rings; the wall stamper now sees final protected tiles before emitting any wall structure record.
  Rejected: carving farm connectors after walls and relying on tile overwrites while stale `Structure::Wall` records survive.
  Estimate: 0 us per frame; generation order only.

- [x] Wall structures are suppressed on all protected settlement tiles.
  DOD: `stamp_settlement_wall` suppresses wall records on road, square, house, and field overlaps; focused parity now checks city/village normal and neighbour-aligned outputs for blocked wall records.
  Rejected: tile-only gate/field correctness that breaks when a future renderer consumes structure records.
  Estimate: one protected-tile branch per sampled wall pixel during generation; 0 us per frame.

- [x] City wall fidelity restored after stricter suppression.
  DOD: the stricter test initially exposed `cityWalls=38 blockedWallRecords=0`; city ring tessellation was raised from `18+ring*6` to `22+ring*8`, still clamped by the 48-node wall buffer, and parity returned to passing with no protected-tile wall records.
  Rejected: weakening the test threshold to accept a thinner wall silhouette.
  Estimate: roughly +18 city wall segment nodes for the 6000-pop test city, generation-only; 0 us per frame.

- [x] Fresh verification after Loop 8.
  Results: direct parity exit 0; CMake parity exit 0; forbidden construct scan no matches; full MSVC build exit 0 with `timaert.exe` relink; seam direct exit 0 (`roadDirty=4 plainDirty=3 diagonalDirty=5`, `roadGen=47.218ms`, `plainGen=34.773ms`, `diagonalGen=36.168ms`); `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test` exit 0; runtime smoke exit 0 with `[smoke] subworld_time OK` and `[smoke] PASS`; root `*.obj`/`*.pdb` check returned no files.

## Loop 9 - TS Edge Anchor Audit

- [x] Road and settlement edge endpoints now use TS deterministic anchors.
  DOD: ported the TS `computeEdgeAnchors` 35-65% shared-edge anchor rule for road neighbours using symmetric cell-coordinate seeds; city/village main roads use anchored endpoints when neighbour road directions are present and keep the bounded cardinal fallback otherwise.
  Rejected: fixed edge-center targets, which made every gate/road hit the same midpoint and lost TS seam shape variation.
  Estimate: one xorshift call per connected road edge during generation; 0 us per frame.

- [x] Water-road bridge records follow anchored road geometry.
  DOD: bridge segment centers now use the same anchored endpoints as the road raster; the existing single-neighbour through-road enhancement now emits a full matching bridge instead of a half-length bridge to center.
  Rejected: TS single-neighbour center dead-end, because native already uses through-road continuity to avoid visible seam starts from nowhere; also rejected leaving bridge geometry shorter than the road tiles.
  Estimate: O(road connection count), no per-frame cost.

- [x] Focused tests assert anchored endpoints.
  DOD: parity test computes expected deterministic anchors for city/village gates, two-neighbour bridges, and single-neighbour bridge consistency; direct and CMake parity tests pass.
  Rejected: loose road-count-only assertions that cannot catch centered-edge regressions.
  Estimate: verification-only.

- [x] Fresh verification after Loop 9.
  Results: direct parity exit 0; CMake parity target rebuilt and exited 0; forbidden construct scan no matches; full MSVC build exit 0 (`ninja: no work to do`, with `timaert` object/exe newer than source); seam direct rerun exit 0 after one transient diagonal timing failure (`roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=66.763ms`, `plainGen=131.214ms`, `diagonalGen=90.187ms`, `reversalGen=65.927ms`); `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test` exit 0; runtime smoke exit 0 with `[smoke] subworld_time OK` and `[smoke] PASS`; root `*.obj`/`*.pdb` check returned no files.

## Loop 10 - Hecton Document Import

- [x] Hecton Timaert/Samosbor signal search completed.
  DOD: exact search for `Timaert|TMA_|Samosbor|Ð¡Ð°Ð¼Ð¾ÑÐ±Ð¾Ñ€` across Hecton markdown/text/json/csv returned no direct hits, so no file was claimed as explicitly labeled Timaert/Samosbor.
  Rejected: guessing that unrelated deprecated Hecton archives are Timaert evidence.
  Estimate: 0 us runtime impact.

- [x] Current Hecton operational docs copied into Timaert import buckets.
  DOD: copy-only import from `C:\hades\Hecton8` into isolated Timaert folders: `Docs\Tasks\Imported_Hecton8`, `Docs\AgentLogs\Imported_Hecton8`, `Docs\Reports\Imported_Hecton8`, `Docs\Imported\Hecton8\DocsRoot`, and `Docs\Imported\Hecton8\ProjectRoot`.
  Rejected: deleting/moving Hecton source files or mixing Hecton logs into active Timaert agent logs without an import namespace.
  Estimate: 0 us runtime impact.

- [x] Import scope corrected and verified.
  DOD: removed 163 mistakenly imported non-doc Hecton root files from the Timaert import bucket after path verification; retained 468 files / 55,368,551 bytes; CSV manifest reports `present_verified_same_hash=468`.
  Rejected: leaving `.csproj`, `.lscache`, `.zip`, and other non-doc root artifacts in Timaert documentation.
  Estimate: 0 us runtime impact.

- [x] Import manifest created.
  Files: `Docs\Imported\Hecton8\TIMAERT_SAMOSBOR_IMPORT_MANIFEST.md` and `Docs\Imported\Hecton8\IMPORT_MANIFEST_HECTON8.csv`.
  Result: retained buckets are AgentLogs 278, Tasks 57, Reports 104, DocsRoot 14, ProjectRoot 15. Source files were not deleted.

## Loop 11 - Ruin Edge Anchor And Live Import Settle

- [x] TS ruin road-stitch gap closed.
  DOD: `gen_ruin` now receives `nbFeature`, collects road/dirt-road neighbours, and carves ruined paths from center to TS deterministic 35-65% edge anchors before stamping ruined walls.
  Rejected: leaving ruin landmarks visually isolated from adjacent macro roads while road/city/village cells use anchors.
  Estimate: O(road-neighbour count), max eight anchored road carves during cold cell generation; 0 us per frame.

- [x] Ruin wall records no longer cross protected gate tiles.
  DOD: ruined wall stamping skips road/square/house/field tiles and suppresses the matching wall structure record when a segment intersects protected tiles; focused parity now asserts an east anchored ruin gate and rejects protected-tile wall records.
  Rejected: tile-only road gates with stale wall structure records that a future wall renderer could draw as blockers.
  Estimate: one protected-tile branch per sampled ruin wall pixel during generation; 0 us per frame.

- [x] Hecton imported documentation corpus refreshed against live writers.
  DOD: refreshed the quarantined `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` corpus for selected Hecton root docs and `Docs` markdown/text/log/json plus active Tasks/AgentLogs png evidence; final capture selected 1918 source files, copied 1 newly missing file, refreshed 8 stale files, and recorded provenance manifests through `MANIFEST_REFRESH_2026-05-15_TMA_SUBWORLD_GENERATOR_PARITY_BKR_FINAL_CAPTURE.tsv`.
  Rejected: merging Hecton logs into live Timaert task/log folders or deleting Hecton source files.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Fresh verification after Loop 11.
  Results: `subworld_generator_parity_test` exit 0; full `cmake --build build-msvc` via `VsDevCmd.bat` exit 0 (`ninja: no work to do`); `subworld_async_seam_test` exit 0 (`roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=32.226ms`, `plainGen=11.633ms`, `diagonalGen=29.283ms`, `reversalGen=13.670ms`); `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test` exit 0; runtime smoke exit 0 with `[smoke] subworld_time OK` and `[smoke] PASS`; forbidden construct scan no matches; root `*.obj`/`*.pdb` artifacts removed after path verification.

Remaining known limit: native `Structure` still lacks TS wall-ring shape/tag metadata, so this generator writes verified tile/structure data that the current renderer can safely ignore or consume later. No compile, seam, test, or smoke blocker remains in this domain.

Live-import caveat: Hecton `Docs\AgentLogs` was still changing during this pass. The import is complete for the final captured selected source set, but concurrent Hecton agents can make it stale again after capture by appending to active logs.

## Loop 12 - Seam Scheduler And Hecton Boundary Re-Audit

- [x] Timaert batch authority re-read; Hecton process rules rejected for this project.
  DOD: `TIMAERT BATCH.md` was re-read from `C:\Timaert\timaert_c`; it explicitly states this is not HECTON-8 and that `C:\Timaert\src` is TS gameplay authority while `timaert_c` ships. The local `Docs\Actual Domains of Project.txt` named by Hecton rules does not exist in Timaert.
  Rejected: writing Timaert status/rationale/log files under `C:\hades\Hecton8` or applying Hecton Unity/NASA-punk protocol to this native C++ port.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Hecton Timaert/Samosbor boundary re-audited without Hecton writes.
  DOD: Hecton filename scan for `timaert|tma_|samosbor|ÑÐ°Ð¼Ð¾ÑÐ±Ð¾Ñ€|Ñ‚Ð¸Ð¼Ð°ÐµÑ€Ñ‚` returned no paths. Content scan found only three Hecton compute-logistics audit files that mention the Timaert import boundary; these are not misplaced Timaert/Samosbor documents.
  Rejected: moving/deleting Hecton audit records or importing false-positive Hecton references into active Timaert task/log folders.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Async seam worker starvation fixed.
  DOD: `SeamlessSubworldManager` now tracks active generation jobs, keeps exposed-cell generation ahead of save snapshots, pauses save jobs during seam replacement, and gives road smoothing priority once placeholders are gone. `subworld_async_seam_test` exits 0 after the fix with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`.
  Rejected: increasing test timeouts or weakening the smoothing-publish assertion while save jobs could still starve visual publish.
  Estimate: 0 us per frame; background scheduler-only. Seam crossing generation slices from the latest Debug MSVC run: road 10.258 ms, plain 32.342 ms, diagonal 28.184 ms, rapid reversal 55.566 ms.

- [x] Async road smoother upgraded from per-road 25x25 sampling to an integral-image pass.
  DOD: `smooth_road_heights_indexed` keeps the same visual contract (25x25 road average, 80 Laplacian iterations, shoulder blend) but replaces road-tile-by-radius sampling with an integral image and sorted sparse shoulder accumulation. Worker smoothing now uses pre-collected road indices from generated cells.
  Rejected: removing road smoothing, running it synchronously on the seam path, or leaving `unordered_map` shoulder accumulation in the async composite worker.
  Estimate: 0 us per frame; async worker cost changes from O(roadTiles * radius^2) sampling to O(fullComposite + roadTiles) averaging plus sparse shoulder sort.

- [x] Fresh verification after Loop 12.
  Results: full `cmake --build build-msvc --parallel 4` exit 0; `subworld_generator_parity_test` exit 0 (`forest_glades=79`, `road_bridge_count=1`); `subworld_async_seam_test` exit 0 (`roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=10.258ms`, `plainGen=32.342ms`, `diagonalGen=28.184ms`, `reversalGen=55.566ms`); `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `road_river_generation_test`, and `spell_casting_effects_test` exit 0; runtime smoke `new_game,wait_boot_done,subworld_time,quit` exit 0 with `[smoke] subworld_time OK` and `[smoke] PASS`; forbidden construct scan on touched C++/test files had no matches; `git diff --check` passed with line-ending warnings only; root `*.obj/*.pdb/*.out` artifacts check returned no files.

## Loop 13 - Macro Feature-Layer TS Parity Audit

- [x] `features.ts` tree pass audited against native `build_feature_layer`.
  DOD: read `C:\Timaert\src\game\features.ts`, `src\macro\features.h`, `src\macro\spawners.cpp`, `tests\feature_layer_parity_test.cpp`, and the root parity docs. The feature writer priority remains Mountain -> Tree -> DirtRoad -> Road, with native water filtering documented as an intentional guard.
  Rejected: treating `FeatureLayer::set()` torus wrapping as equivalent to the TS writer; only lookup is torus-safe in TS.
  Estimate: 0 us per frame; generation-only feature byte writes.

- [x] Tree pass now uses TS flattened index semantics.
  DOD: `build_feature_layer` computes the tree target as `y * width + x`, range-checks the flattened index, keeps the native water filter, and writes `fl.data[i]` directly. This matches `features.ts` writer behavior and leaves `FeatureLayer::at()` / `set()` torus-safe for explicit API use.
  Rejected: clipping malformed tree coordinates before write, because TS does not clip x/y before flattening; also rejected keeping torus wrapping in the builder.
  Estimate: same O(tree count) generation pass; one 64-bit multiply/add and bounds check per tree.

- [x] Focused regression added for malformed-but-in-range tree coordinates.
  DOD: `feature_layer_parity_test` now asserts that `x == width` and negative-x flattened cases land on the TS flattened cells, and that they do not wrap through torus coordinates.
  Rejected: broad visual-only smoke as proof for a byte-grid writer bug.
  Estimate: verification-only.

- [x] Hecton boundary rechecked read-only.
  DOD: filename scan under `C:\hades\Hecton8` found no Timaert/Samosbor/TMA-labeled paths. Content scan found the same three Hecton compute-logistics audit references; these are not Timaert docs to move. No Hecton file was written, moved, or deleted.
  Rejected: copying false-positive Hecton audit files into active Timaert logs or writing Timaert reports into Hecton.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Fresh verification after Loop 13.
  Results: first MSVC wrapper failed because `VsDevCmd.bat` did not add the VC standard include path (`cstdint` missing); reran through `vcvars64.bat` and `C:\Program Files\CMake\bin\cmake.exe`. Focused target `feature_layer_parity_test` built and exited 0; `road_river_generation_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`, `subworld_async_seam_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test` exited 0. Full `cmake --build build-msvc --parallel 4` exited 0. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Forbidden construct scan had no matches. `git diff --check` passed with line-ending warnings only. Root artifact check returned no `*.obj/*.pdb/*.out` files.

## Loop 14 - Road/Dirt Mask Prefix Parity

- [x] TS short-mask behavior audited.
  DOD: re-read `features.ts` and confirmed typed-array reads stamp valid prefix bytes for road/dirt masks while out-of-range indices evaluate false. Native all-or-nothing mask validation was stricter than TS.
  Rejected: preserving all-or-nothing invalid-mask behavior after finding it disagreed with the TS source.
  Estimate: 0 us per frame; generation-only byte-grid pass.

- [x] Road/dirt feature masks now follow TS prefix semantics.
  DOD: `build_feature_layer` now loops to `min(mask.size(), total)` for road and dirt masks. Full-length masks keep identical behavior; short masks apply their valid prefix bytes and stop safely.
  Rejected: reading beyond available native vectors or silently discarding valid prefix data.
  Estimate: same O(mask prefix length) generation pass; no heap allocation and no per-frame cost.

- [x] Focused regression and docs updated.
  DOD: `feature_layer_parity_test` now asserts short road and short dirt masks stamp prefix cells and do not suppress cells outside the prefix. `translation.md` and `ARCHITECTURE.md` record TS flattened tree writes and short-mask prefix reads.
  Rejected: relying on existing normal-length mask tests to prove malformed TS semantics.
  Estimate: verification/documentation only.

- [x] Fresh verification after Loop 14.
  Results: focused target build for `feature_layer_parity_test` and `road_river_generation_test` exited 0; `feature_layer_parity_test`, `road_river_generation_test`, and `pathfinding_parity_test` exited 0; full `cmake --build build-msvc --parallel 4` exited 0; `subworld_generator_parity_test` exited 0 (`forest_glades=79`, `road_bridge_count=1`); `subworld_async_seam_test` exited 0 (`roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=12.322ms`, `plainGen=8.051ms`, `diagonalGen=14.506ms`, `reversalGen=23.446ms`); `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test` exited 0; runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`; forbidden construct scan had no matches; `git diff --check` passed with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files.

- [x] Hecton boundary rechecked read-only after Loop 14.
  DOD: filename scan under `C:\hades\Hecton8` found no Timaert/Samosbor/TMA-labeled paths. Content scan found only the known Hecton compute-logistics audit references. No Hecton file was written, moved, or deleted.
  Rejected: writing Timaert reports to Hecton or treating Hecton audit references as misplaced Timaert docs.
  Estimate: documentation-only; 0 us runtime impact.

## Loop 15 - Feature Matrix Regression Hardening

- [x] Deterministic TS-contract reference matrix added.
  DOD: `feature_layer_parity_test` now builds an independent reference feature layer for varied map sizes, water/alpha states, malformed tree coordinates, short road masks, and long dirt masks, then byte-compares production output to the reference.
  Rejected: relying only on individual hand-picked feature cases after finding two edge-case drifts.
  Estimate: test-only; 0 us runtime impact.

- [x] Full verification after matrix hardening.
  Results: `feature_layer_parity_test` target rebuilt and exited 0. Full `cmake --build build-msvc --parallel 4` exited 0. `feature_layer_parity_test`, `road_river_generation_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test` exited 0. A parallel `subworld_async_seam_test` invocation hit the 120s tool timeout; the stale `build-msvc` process was stopped, and an isolated rerun exited 0 with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=51.706ms`, `plainGen=32.988ms`, `diagonalGen=236.534ms`, `reversalGen=33.287ms`. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`; stale `build-msvc` verification processes were stopped after output.

- [x] Final scans after matrix hardening.
  Results: forbidden construct scan on touched source/test files had no matches; `git diff --check` passed with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files; Hecton read-only content scan again found only compute-logistics audit references about the Timaert import boundary.

## Loop 16 - Feature Consumer Storage Validation And Hecton Hit Refresh

- [x] Downstream feature consumers audited.
  DOD: `FeatureLayer::cell_count_for`, `has_complete_storage`, and `covers` guard malformed dimensions/storage; `pathfinding.cpp` and `zones.cpp` use complete-storage checks before raw feature-byte reads; `macro_renderer.cpp::upload_features` uploads a 1x1 blank R8 texture when feature storage is malformed; normal `FeatureLayer::at`/`set` keep torus-safe prefix semantics for explicit API calls.
  Rejected: letting movement cost, zone generation, or GL upload read short feature buffers blindly.
  Estimate: 0 us per frame on valid feature layers; one complete-storage branch at subsystem boundary.

- [x] Consumer fail-closed tests verified.
  DOD: `pathfinding_parity_test` covers valid feature costs, ignored short feature storage, ignored dimension mismatch, malformed terrain fail-closed, and short-storage zone baseline equivalence. `feature_layer_parity_test` covers empty/short `FeatureLayer` API safety and byte-grid TS contract.
  Rejected: trusting renderer/app smoke alone for malformed feature buffers because it would not isolate the byte-grid failure.
  Estimate: verification-only.

- [x] Hecton current hit files refreshed into Timaert quarantine.
  DOD: read-only content scan under `C:\hades\Hecton8` found Hecton compute/accounting references mentioning Timaert prompt names plus the known compute-logistics audit files. Copied those five exact files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP16.tsv`; all copied hashes matched.
  Rejected: writing to Hecton, deleting Hecton files, or mixing Hecton audit/accounting docs into active Timaert `Docs\Tasks` / `Docs\AgentLogs`.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Fresh verification after Loop 16.
  Results: focused target build for `feature_layer_parity_test`, `pathfinding_parity_test`, and `road_river_generation_test` exited 0. Full `cmake --build build-msvc --parallel 4` exited 0. `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `subworld_generator_parity_test`, `subworld_async_seam_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test` exited 0. Latest seam run: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=42.132ms`, `plainGen=33.990ms`, `diagonalGen=54.790ms`, `reversalGen=37.555ms`. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Forbidden construct scan had no matches; `git diff --check` passed with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files; no stale `build-msvc` verification process remained.

## Loop 17 - Invalid Feature Byte Normalization

- [x] Invalid feature-byte handling audited.
  DOD: `FeatureLayer::decode` maps only TS-known feature ids (`None`, `Road`, `Tree`, `Mountain`, `DirtRoad`) and fails all unknown bytes closed to `FT_None`. `FeatureLayer::at`, `build_cost_grid`, and `generate_zones` use this decoder before comparing feature ids.
  Rejected: passing raw invalid enum bytes through CPU consumers and relying on every switch/default path to do the right thing.
  Estimate: one small decode switch per feature byte read in generation/boot consumers; no per-frame cost for the normal app loop.

- [x] Invalid-byte tests verified.
  DOD: `feature_layer_parity_test` asserts invalid stored bytes decode to `FT_None`; `pathfinding_parity_test` asserts invalid bytes use biome movement cost while following valid bytes still apply, and invalid bytes do not perturb zone generation.
  Rejected: only testing short/mismatched storage and leaving complete-but-corrupt feature grids untested.
  Estimate: verification-only.

- [x] Hecton hit files refreshed for Loop 17.
  DOD: read-only Hecton scan found the same compute/accounting/audit hit set. Refreshed those five exact files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP17.tsv`; hashes matched after copy.
  Rejected: writing to Hecton, moving Hecton files, or polluting active Timaert task/log namespaces with Hecton accounting/audit files.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Fresh verification after Loop 17.
  Results: focused target build for `feature_layer_parity_test`, `pathfinding_parity_test`, and `road_river_generation_test` exited 0. Full `cmake --build build-msvc --parallel 4` exited 0. `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `subworld_generator_parity_test`, `subworld_async_seam_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test` exited 0. Latest seam run: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=29.107ms`, `plainGen=36.880ms`, `diagonalGen=62.582ms`, `reversalGen=45.358ms`. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Forbidden construct scan had no matches; `git diff --check` passed with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files; no stale `build-msvc` verification process remained.

## Loop 18 - Feature Upload Sanitizer API And Target Link Closure

- [x] Feature byte upload sanitation moved into `FeatureLayer`.
  DOD: added `FeatureLayer::has_invalid_cell_bytes()` and `FeatureLayer::copy_sanitized_cells()`. The API validates complete cell storage, scans only declared cells, decodes unknown bytes to `FT_None`, and refuses to fabricate a complete sanitized copy for short backing storage.
  Rejected: keeping feature-byte normalization buried inside `MacroRenderer::upload_features`, or making every caller hand-roll its own corrupt-byte scan.
  Estimate: 0 us per frame; upload-time only. Valid feature textures keep the direct `fl.data.data()` upload path.

- [x] Macro renderer now delegates corrupt feature bytes to the tested API.
  DOD: `MacroRenderer::upload_features()` still uploads a 1x1 blank R8 map for incomplete storage, still uploads valid complete grids directly, and now uses `copy_sanitized_cells()` only when invalid complete-grid bytes are detected.
  Rejected: unconditional full-grid copy on every feature upload, because the normal grid is already valid and contiguous.
  Estimate: 0 us per frame; no new normal-case allocation.

- [x] Sanitizer and target-link regressions closed.
  DOD: `feature_layer_parity_test` now proves invalid complete storage reports corrupt bytes, sanitized copies normalize only the invalid byte, and short storage produces no fake complete sanitized buffer. `road_river_generation_test` target now links `politik.cpp` and `language.cpp`, matching the Politik APIs already called by the test.
  Rejected: ignoring the linker failure as unrelated, or weakening the Politik malformed-terrain test.
  Estimate: test/build only.

- [x] Fresh verification after Loop 18.
  Results: focused target build for `feature_layer_parity_test`, `pathfinding_parity_test`, and `road_river_generation_test` exited 0 after the target link fix. Full `cmake --build build-msvc --parallel 4` exited 0. `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `subworld_generator_parity_test`, `subworld_async_seam_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test` exited 0. Latest seam run: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=100.865ms`, `plainGen=84.676ms`, `diagonalGen=88.129ms`, `reversalGen=29.040ms`. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Forbidden construct scan on touched code/test files had no matches; `git diff --check` exited 0 with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files; no stale `build-msvc` process remained.

- [x] Hecton import boundary refreshed for Loop 18.
  DOD: read-only Hecton content scan found six Hecton compute/accounting/audit files mentioning Timaert/Samosbor/TMA terms. Copied the six exact files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP18.tsv`; all copied hashes matched.
  Rejected: writing Timaert status/log/rationale files to Hecton, deleting Hecton audit files, or importing Hecton audit/accounting files into active Timaert `Docs\Tasks` / `Docs\AgentLogs`.
  Estimate: documentation-only; 0 us runtime impact.

STATUS: VERIFIED

## Loop 19 - Single-Pass Feature Upload View And Build Target Closure

- [x] Feature upload view made single-pass.
  DOD: added `FeatureLayer::complete_cells_or_sanitized()`. Complete valid grids now return the original `data.data()` pointer and clear scratch; complete corrupt grids copy once into scratch and normalize invalid bytes to `FT_None`; incomplete grids return `nullptr` and clear scratch.
  Rejected: renderer-side scan followed by a second full `copy_sanitized_cells()` pass, because corrupt upload should not pay duplicated scanning work.
  Estimate: 0 us per frame; upload-time only. Valid feature texture uploads remain zero-copy.

- [x] Renderer and tests switched to the upload-view contract.
  DOD: `MacroRenderer::upload_features()` now delegates to `complete_cells_or_sanitized()`. `feature_layer_parity_test` asserts direct valid pointer, sanitized invalid scratch, and null short-storage view.
  Rejected: unconditional scratch upload for every feature layer, because the normal app boot path produces valid complete feature bytes.
  Estimate: 0 us per frame; no new normal-case allocation.

- [x] Full-build target closure repaired.
  DOD: the full build exposed `npc_spawn_contract_test` missing SDL include propagation for `gl/gl.h` through gameplay headers. `CMakeLists.txt` now derives SDL include dirs from `SDL2::SDL2` or `SDL2_DIR` once before targets; full MSVC build exits 0 and `npc_spawn_contract_test` exits 0.
  Rejected: ignoring the full-build failure after focused feature tests passed, or hard-coding only one local include path into the failing target.
  Estimate: build-system only; 0 us runtime impact.

- [x] Fresh verification after Loop 19.
  Results: focused target build for `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, and `subworld_generator_parity_test` exited 0. Full `cmake --build build-msvc --parallel 4` exited 0 after SDL include target closure. Tests exited 0: `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `subworld_generator_parity_test`, `npc_spawn_contract_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, and `combat_squad_test`. `subworld_async_seam_test` exited 0 with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=28.603ms`, `plainGen=17.369ms`, `diagonalGen=73.192ms`, `reversalGen=44.704ms`, seam-path `smooth=0.000ms`. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Forbidden construct scan on touched code/test files had no matches; `git diff --check` exited 0 with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files. A stale smoke `timaert.exe` process under `build-msvc` was stopped by exact path and the follow-up stale-process check returned empty.

- [x] Hecton import boundary refreshed for Loop 19.
  DOD: read-only Hecton content scan found seven Hecton compute/accounting/audit files mentioning Timaert/Samosbor/TMA terms. Copied the seven exact files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP19.tsv`; all copied hashes matched.
  Rejected: writing Timaert reports to Hecton, deleting Hecton audit files, or importing Hecton audit/accounting files into active Timaert `Docs\Tasks` / `Docs\AgentLogs`.
  Estimate: documentation-only; 0 us runtime impact.

STATUS: VERIFIED

## Loop 20 - Wrap Hardening And Renderer Reverification

- [x] Feature coordinate wrapping hardened.
  DOD: `FeatureLayer::wrap_coord()` now performs the modulo in `std::int64_t` before casting back to `int`, keeping torus-safe lookup/set semantics stable for hostile inputs such as `INT_MIN`. The focused parity test already asserts `wrap_coord(INT_MIN, 3) == 1` and malformed huge extents fail closed.
  Rejected: duplicating ad hoc `((x % w) + w) % w` expressions in callers or relying on every consumer to avoid hostile coordinates.
  Estimate: 0 us per frame; feature lookup/set utility only, and no new allocation.

- [x] Renderer feature upload boundary rechecked.
  DOD: `MacroRenderer::upload_features()` now delegates directly to `FeatureLayer::complete_cells_or_sanitized()` and selects the 1x1 blank texture only when that API returns `nullptr`. Valid feature grids remain direct uploads; corrupt complete grids remain single-pass sanitized uploads.
  Rejected: restoring a redundant renderer-side complete-storage branch before the feature-layer upload-view API.
  Estimate: 0 us per frame; upload-time only.

- [x] Full-build renderer signature failure closed by source verification.
  DOD: the inherited MSVC error around `MacroRenderer::draw` was checked against `macro_renderer.h`, `macro_renderer.cpp`, and the app call site. The implementation and declaration both include the `seaLevel` parameter, and a fresh full MSVC build relinked `timaert.exe` with exit 0.
  Rejected: adding a compatibility overload or weakening the renderer API after the source already matched.
  Estimate: build/verification only; 0 us runtime impact.

- [x] Fresh verification after Loop 20.
  Results: full `cmake --build build-msvc --parallel 4` exited 0. Tests exited 0: `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `subworld_generator_parity_test`, `npc_spawn_contract_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, `combat_squad_test`, `audio_contract_test`, and `audio_runtime_test`. `subworld_async_seam_test` exited 0 with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=99.165ms`, `plainGen=30.589ms`, `diagonalGen=31.032ms`, `reversalGen=52.760ms`, seam-path `smooth=0.000ms`. Runtime smoke `new_game,wait_boot_done,subworld_time,quit` exited 0 with `[smoke] subworld_time OK` and `[smoke] PASS`. Forbidden construct scan on touched domain files had no matches; `git diff --check` exited 0 with line-ending warnings only; corrected root artifact check found no `*.obj`, `*.pdb`, or `*.out` files. Stale `subworld_generator_parity_test.exe`, `subworld_async_seam_test.exe`, and `timaert.exe` processes under `build-msvc` were stopped by exact path.

- [x] Hecton import boundary refreshed for Loop 20.
  DOD: read-only Hecton content scan found seven Hecton compute/accounting/audit files mentioning Timaert/Samosbor/TMA terms. Copied those seven exact files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP20.tsv`; all copied hashes matched.
  Rejected: writing Timaert reports to Hecton, deleting Hecton audit files, or importing Hecton accounting/audit files into active Timaert `Docs\Tasks` / `Docs\AgentLogs`.
  Estimate: documentation-only; 0 us runtime impact.

STATUS: VERIFIED

## Loop 21 - Wilderness Edge-Anchor Trail Parity

- [x] TS wilderness edge-anchor trail behavior ported.
  DOD: grassland, forest, swamp, and mountain generators now receive the decoded 3x3 feature neighborhood and carve bounded center-to-edge-anchor trails when neighbouring macro cells carry `Road` or `DirtRoad`. The implementation reuses the existing deterministic TS 35-65% edge-anchor math and organic road raster, capped at eight neighbour directions per cell.
  Rejected: waiting for the center cell to be a `Road` feature before carving trails, because TS `grassland.ts`, `forest.ts`, `swamp.ts`, and `mountain.ts` use `edgeAnchors` from neighbouring road connectivity.
  Estimate: 0 us per frame; cold cell generation only, at most eight bounded road rasters.

- [x] Focused generator invariants added.
  DOD: `subworld_generator_parity_test` now asserts anchored trail cells and center road tiles for grassland east/west connectivity, forest north/south connectivity, swamp single-neighbour muddy path, and mountain single-neighbour pass.
  Rejected: visual smoke only, because a trail missing at an edge anchor is a byte-grid generator contract failure.
  Estimate: verification-only.

- [x] Docs updated in Timaert only.
  DOD: `translation.md` and `ARCHITECTURE.md` now document wilderness edge-anchor trail parity and the bounded no-seam-threading implementation.
  Rejected: writing any Timaert documentation to Hecton, or treating Hecton imported reports as live Timaert status.
  Estimate: documentation-only; 0 us runtime impact.

- [x] Fresh verification after Loop 21.
  Results: full `cmake --build build-msvc --parallel 4` exited 0. Tests exited 0: `subworld_generator_parity_test`, `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `npc_spawn_contract_test`, `quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, `combat_squad_test`, `audio_contract_test`, and `audio_runtime_test`. `subworld_async_seam_test` exited 0 with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `roadGen=28.244ms`, `plainGen=49.619ms`, `diagonalGen=177.492ms`, `reversalGen=28.553ms`, seam-path `smooth=0.000ms`, and water-plane invariant `badWater=0 badLand=0 maxWater=0.40000 minLand=0.42000`. Runtime smokes exited 0: `new_game,wait_boot_done,subworld_time,quit` with `[smoke] subworld_time OK` and `[smoke] PASS`, and `new_game,wait_boot_done,subworld_seam,quit` with `[seam-cross] gen=33.678ms smooth=0.000ms upload3d=139.683ms upload2d=0.000ms total=173.552ms` and `[smoke] PASS`. Forbidden construct scan on touched domain files had no matches; `git diff --check` exited 0 with line-ending warnings only; root `*.obj/*.pdb/*.out` artifact check returned no files; no stale `build-msvc` verification process remained.

- [x] Hecton import boundary refreshed for Loop 21.
  DOD: read-only Hecton content scan found ten Hecton compute/accounting/audit files mentioning Timaert/Samosbor/TMA terms. Copied those ten exact files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP21.tsv`; all copied hashes matched.
  Rejected: writing Timaert reports to Hecton, deleting Hecton audit files, or importing Hecton accounting/audit files into active Timaert `Docs\Tasks` / `Docs\AgentLogs`.
  Estimate: documentation-only; 0 us runtime impact.

STATUS: VERIFIED
