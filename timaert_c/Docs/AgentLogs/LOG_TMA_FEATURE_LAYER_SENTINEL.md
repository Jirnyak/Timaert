# LOG: TMA_FEATURE_LAYER_SENTINEL

What was wrong:
- The `features.ts` port was mostly complete, but raw feature bytes could still leak as invalid `FeatureType` values in native lookup/pathfinding/zones if future tooling or malformed data wrote outside the five valid TS IDs.
- `build_feature_layer` needed an explicit `total * 4` overflow guard before RGBA storage checks.
- Hecton-origin Timaert/Samosbor/TMA references were present only inside the broad Hecton import corpus; the user asked again for clear separation.

What was done:
- Added `FeatureLayer::decode()` and routed `FeatureLayer::at`, pathfinding cost-grid feature reads, and zone feature reads through it.
- Added invalid-byte regression checks to `feature_layer_parity_test` and `pathfinding_parity_test`.
- Added the RGBA byte-count overflow guard in `spawners.cpp::build_feature_layer`.
- Updated `translation.md` and `ARCHITECTURE.md` for the feature-domain contract.
- Created `Docs\Imported\Hecton8_Timaert_Samosbor\2026-05-15_exact_matches` with exact path/content matches from Hecton and a TSV manifest.

Cinematic cheats used:
- None for feature byte decoding. This is data contract hardening.
- The existing documented feature-domain visual cheat remains unchanged: C++ keeps the TS feature map contract but deliberately filters ocean cells so visual features do not stamp into water.

Exact microseconds saved:
- Not claimed. This change is correctness and fail-closed robustness.

Verification:
- `feature_layer_parity_test`: PASS (`Docs/AgentLogs/integrator_feature_layer_invalid_byte.log`).
- `pathfinding_parity_test`: PASS (`Docs/AgentLogs/integrator_pathfinding_invalid_feature_byte.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_after_feature_hardening.log`).
- Seed 110 boot smoke: PASS with `[roads] ... expansions=431472` and `[smoke] PASS` (`Docs/AgentLogs/integrator_smoke_boot_feature_hardening_seed110.log`).
- `git diff --check`: exit 0; CRLF warnings only.
- Hecton exact-match import: scanned 2717 selected files, copied 823 exact matches into Timaert, with 1 locked/hash-unavailable source log recorded in the index. No Hecton file was written.

STATUS: VERIFIED

---

Continuation: active map sea-level continuity.

What was wrong:
- Feature-adjacent native systems still had several `0.40f` sea-level assumptions after the map itself could be generated with a custom `LayerParameters::seaLevel`.
- This could make custom maps disagree across tree spawning, road component pruning, road water blocking, feature stamping, path-cost water classification, and macro shader water rendering.

What was done:
- Added active sea-level parameters to `spawn_trees()`, `trace_roads()`, `build_feature_layer()`, and `MacroRenderer::draw()` while keeping `0.40f` defaults for existing tools.
- Routed `boot_world()` through `lp.seaLevel` for trees, roads, features, and path-cost generation.
- Routed `frame()` through `app.gs.mapParams.seaLevel` for macro renderer `u_seaLevel`.
- Added focused regression coverage for active sea-level behavior in `feature_layer_parity_test` and `road_river_generation_test`.
- Updated Timaert `ARCHITECTURE.md` and `translation.md`; no Hecton files were written.

Cinematic cheats used:
- The macro renderer remains a single fullscreen procedural shader pass. Active sea level now feeds that existing visual cheat instead of adding geometry or a second water system.

Exact microseconds saved:
- Not claimed. This is coherence and correctness hardening; the runtime cost is existing scalar comparisons fed by an active parameter.

Verification:
- Targeted CMake/MSVC build for `road_river_generation_test`, `feature_layer_parity_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`, `quest_lifecycle_test`, `save_roundtrip_test`, and `timaert`: PASS.
- Focused tests: `road_river_generation_test`, `feature_layer_parity_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`, `quest_lifecycle_test`, `save_roundtrip_test`: PASS.
- Full current `build-msvc\*test.exe` set: 16/16 PASS.
- `git diff --check`: no whitespace errors; LF-to-CRLF normalization warnings only.
- Root artifact scan: clean for root `smoke_*.ppm`, `.diff`, and `.patch` files.
- No `dotnet` rebuilds were run.

STATUS: VERIFIED

---

Continuation: dirt-road land-mask boundary count.

What was wrong:
- `trace_dirt_roads()` had been extended to accept `landMaskByteCount`, but `boot_world()` still passed only `app.terrain.rgba.data()`.
- During the same concurrent window, one build linked against an intermediate `trace_roads(..., float)` mismatch; a clean rerun after the active sea-level API stabilized passed.

What was done:
- Updated `boot_world()` to pass `app.terrain.rgba.size()` with the RGBA land mask pointer.
- Re-ran the app/feature/road build, smoke, full build, and all current test executables after the last code change.

Verification:
- Targeted build: PASS (`Docs/AgentLogs/integrator_build_after_dirt_landmask_count.log`).
- `feature_layer_parity_test`: PASS (`Docs/AgentLogs/integrator_feature_after_dirt_landmask_count.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_after_dirt_landmask_count.log`).
- Seed 114 boot smoke: PASS with `[roads] cities=68 attempted=157 kept=26 pruned=131 componentPruned=44 expansions=394130` and `[smoke] PASS` (`Docs/AgentLogs/integrator_smoke_boot_dirt_landmask_count_seed114.log`).
- Full `build-msvc-integrator`: PASS (`Docs/AgentLogs/integrator_full_build_after_dirt_landmask_count.log`).
- All 14 current `*test.exe` executables: PASS (`Docs/AgentLogs/integrator_alltests_after_dirt_landmask_*.log`).
- `git diff --check`: exit 0; CRLF warnings only.
- No active `cl`, `link`, `ninja`, `cmake`, or `timaert` processes remained.

STATUS: VERIFIED

---

Continuation: safe feature wrapping and live C++ breakage repair.

What was wrong:
- `FeatureLayer::at()` / `set()` used `((coord % extent) + extent) % extent`. That matches TS visually, but in C++ a pathological malformed layer can overflow signed `int` before the fail-closed backing-storage check.
- Targeted build initially failed before compiling because an existing CMake cache still tried to update `stb` from GitHub.
- After forcing FetchContent disconnected mode, the build exposed a real concurrent-agent compile break: `renderer_3d.cpp` used `glDisableVertexAttribArray`, but the Win32 GL loader did not declare/load it.

What was done:
- Added `FeatureLayer::wrap_coord()` and routed `at()`/`set()` through it.
- Added feature tests for `INT_MIN` wrapping and `INT_MAX - 1` malformed extents.
- Forced `FETCHCONTENT_UPDATES_DISCONNECTED=ON` in the CMake cache.
- Added `glDisableVertexAttribArray` to the local Win32 GL proc table.

Cinematic cheats used:
- None. This is correctness/build hardening.

Exact microseconds saved:
- Not claimed. The wrap helper removes one modulo from `FeatureLayer::at()`/`set()`, but the goal is avoiding C++ signed-overflow UB, not performance accounting.

Verification:
- Targeted build after fixes: PASS (`Docs/AgentLogs/integrator_build_feature_wrap_coord_safe.log`).
- `feature_layer_parity_test`: PASS (`Docs/AgentLogs/integrator_feature_wrap_coord_safe.log`).
- `pathfinding_parity_test`: PASS (`Docs/AgentLogs/integrator_pathfinding_after_feature_wrap_coord_safe.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_after_feature_wrap_coord_safe.log`).
- Seed 113 boot smoke: PASS with `[roads] cities=65 attempted=149 kept=25 pruned=124 componentPruned=46 expansions=360326` and `[smoke] PASS` (`Docs/AgentLogs/integrator_smoke_boot_feature_wrap_coord_safe_seed113.log`).
- Full `build-msvc-integrator`: PASS (`Docs/AgentLogs/integrator_full_build_after_feature_wrap_coord_safe.log`).
- All 14 current `*test.exe` executables: PASS (`Docs/AgentLogs/integrator_alltests_after_wrapcoord_*.log`).
- `git diff --check`: exit 0; CRLF warnings only.
- No active `cl`, `link`, `ninja`, `cmake`, or `timaert` processes remained.

STATUS: VERIFIED

---

Continuation: TS default mountain threshold parity.

What was wrong:
- TS `GameScreen.svelte` calls `buildFeatureLayer(..., gState.mapParams.snowLevel - 0.05, ...)`.
- TS default `snowLevel` is `0.80`, so default feature mountain threshold is `0.75`.
- Native boot and macro renderer still used `0.78`, which silently under-classified mountain feature cells versus TS default and made the shader threshold diverge from the feature-builder threshold.

What was done:
- Added `kDefaultFeatureMountainThreshold = 0.75f` in `macro/features.h`.
- Routed native boot `build_feature_layer()` through that constant.
- Routed macro renderer `u_mtnThreshold` through the same constant.
- Updated `feature_layer_parity_test` threshold boundary from byte `198/199` to `191/192`, matching `0.75`.

Cinematic cheats used:
- None. This is exact TS default threshold parity.

Exact microseconds saved:
- Not claimed. Constant-only gameplay/visual parity fix.

Verification:
- Targeted build: PASS (`Docs/AgentLogs/integrator_build_feature_ts_threshold_075.log`).
- `feature_layer_parity_test`: PASS (`Docs/AgentLogs/integrator_feature_layer_ts_threshold_075.log`).
- `pathfinding_parity_test`: PASS (`Docs/AgentLogs/integrator_pathfinding_after_ts_threshold_075.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_after_ts_threshold_075.log`).
- Seed 112 boot smoke: PASS with `[roads] cities=64 attempted=152 kept=26 pruned=126 componentPruned=28 expansions=437863` and `[smoke] PASS` (`Docs/AgentLogs/integrator_smoke_boot_feature_ts_threshold_075_seed112.log`).
- Full `build-msvc-integrator`: PASS (`Docs/AgentLogs/integrator_full_build_after_feature_ts_threshold_075.log`).
- All 14 current `*test.exe` executables: PASS (`Docs/AgentLogs/integrator_alltests_after_threshold075_*.log`).
- `git diff --check`: exit 0; CRLF warnings only.

STATUS: VERIFIED

---

Continuation: feature-render painter parity and quarantine repair.

What was wrong:
- C++ macro feature bytes matched the TS `features.ts` priority contract, but the macro renderer still composed trees, mountains, and landmarks as separate global overlays. TS `renderer.ts` paints decorations per nearby cell in a single 3x3 painter pass, so independent overlays can produce wrong occlusion between near/far decorations.
- A Hecton import refresh used an unavailable PowerShell `GetRelativePath` API and copied thousands of selected Hecton files into the top level of the Timaert quarantine.
- The exact active Imported_Hecton8 bucket files were stale for the live `COMPUTE_LOGISTICS_AUDITOR` docs after Hecton moved compute reports under `Docs\Reports\2026-05-15_COMPUTE_AUDIT`.

What was done:
- Replaced independent tree/mountain/landmark overlay calls with `decorationOverlay()`: 3x3 far-to-near cells, tree -> mountain -> landmark per cell.
- Kept road/dirt overlays before decoration, added river overlay before roads, and made zone tint consume `u_zoneMap` after decorations.
- Preserved fail-closed upload behaviour for malformed feature/zone/landmark storage.
- Removed `2583` erroneous top-level duplicate files from `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` after resolving the import root.
- Re-ran source-relative Hecton refresh passes and refreshed 7 exact-match active bucket files with matching SHA-256.
- Moved new smoke images out of the project root into `artifacts\runtime-smoke\images\verification-20260515`.

Cinematic cheats used:
- Renderer decoration order is a deterministic painter cheat, not a physical canopy/building depth simulation.
- Zone tint is a single low-opacity R8 sample per cell, buying danger readability without extra geometry.
- River overlay is an R8 mask blend over terrain color, not a simulated water mesh on the macro map.

Exact microseconds saved:
- Not claimed for the macro renderer painter change. The change is correctness and deterministic draw order.
- Import cleanup removed `284854493` bytes of duplicated quarantine top-level copies; runtime cost is unaffected.

Verification:
- `cmake --build build-msvc`: PASS, linked `timaert.exe`.
- 13 native test executables: PASS (`quest_lifecycle_test`, `save_roundtrip_test`, `spell_casting_effects_test`, `combat_squad_test`, `audio_contract_test`, `audio_runtime_test`, `pathfinding_parity_test`, `feature_layer_parity_test`, `character_paperdoll_test`, `character_paperdoll_gl_smoke_test`, `road_river_generation_test`, `subworld_generator_parity_test`, `subworld_async_seam_test`).
- Latest shared async seam slices: `roadGen=22.451ms`, `plainGen=94.605ms`, `diagonalGen=35.042ms`, `reversalGen=54.911ms`, all seam-path smoothing `0.000ms`.
- App smoke `new_game,wait_boot_done,wait_visible,capture_frame,quit`: PASS with macro renderer initialized, features/zones/landmarks uploaded, framebuffer visible samples=9, and frame captured.
- Root garbage scan: PASS, no `runtime_*`, `smoke_*`, `save.bin`, `.diff`, `.patch`, `.obj`, `.ppm`, or `.png` files left at project root.
- Hecton exact search: 7 current files; active Imported_Hecton8 bucket hashes match all 7; no files were written under `C:\hades\Hecton8`.
- `git diff --check`: PASS except expected LF-to-CRLF normalization warnings.

STATUS: VERIFIED

---

Continuation: wider C++ agent-damage audit.

What was wrong:
- A full `build-msvc-integrator` build initially failed during CMake regenerate before compiling Timaert code because FetchContent attempted a live GitHub update for `stb` and the connection reset.
- That failure made the "did agents break C++?" question unanswerable from the full graph until the build stopped depending on live network during incremental verification.

What was done:
- Added `FETCHCONTENT_UPDATES_DISCONNECTED=ON` before FetchContent declarations in `CMakeLists.txt`.
- Re-ran the full MSVC build successfully.
- Ran the wider non-window suite plus the paperdoll GL smoke test.

Verification:
- Initial failure captured: `Docs/AgentLogs/integrator_full_build_after_feature_setter_sanitize.log`.
- Full build after fix: PASS (`Docs/AgentLogs/integrator_full_build_after_fetchcontent_disconnect.log`).
- Suite: PASS 12 CPU/runtime tests (`integrator_suite_*.log` for quest, save, spells, combat, audio, character, feature, pathfinding, road, and subworld).
- GL smoke: PASS (`Docs/AgentLogs/integrator_suite_character_paperdoll_gl_smoke_test.log`).

Conclusion:
- No evidence from the current full build/test pass that parallel agents removed required C++ logic in the compiled graph. This is not a claim that every gameplay branch is complete; it is a build-and-regression proof for the current target set.

STATUS: VERIFIED

---

Continuation: setter/API hardening.

What was wrong:
- `FeatureLayer::set()` could still write a raw invalid `FeatureType` cast such as `FeatureType(255)`, even though read paths decoded unknown bytes to `FT_None`.
- MSVC build initially failed only because the shell had not loaded the Visual Studio developer environment; `cl` could not find standard headers. Re-run through `VsDevCmd.bat` fixed the environment issue.

What was done:
- Added compile-time asserts for the five TS feature byte values.
- Sanitized `FeatureLayer::set()` through `FeatureLayer::decode()`.
- Added `feature_layer_parity_test` coverage for invalid setter input.
- Verified the concurrently improved renderer upload path still compiles: invalid complete feature buffers are sanitized via `copy_sanitized_cells()` before GPU upload.

Cinematic cheats used:
- None. This is byte-contract hardening, not simulation or rendering work.

Exact microseconds saved:
- Not claimed. The win is correctness: invalid feature bytes die at the API boundary.

Verification:
- Build: PASS through `VsDevCmd.bat` (`Docs/AgentLogs/integrator_build_feature_setter_sanitize.log`).
- `feature_layer_parity_test`: PASS (`Docs/AgentLogs/integrator_feature_layer_setter_sanitize.log`).
- `pathfinding_parity_test`: PASS (`Docs/AgentLogs/integrator_pathfinding_after_feature_setter_sanitize.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_after_feature_setter_sanitize.log`).
- Seed 111 boot smoke: PASS with `[roads] cities=61 attempted=146 kept=21 pruned=125 componentPruned=24 expansions=450140` and `[smoke] PASS` (`Docs/AgentLogs/integrator_smoke_boot_feature_setter_sanitize_seed111.log`).
- `git diff --check`: exit 0; CRLF warnings only.
- Hecton exact-match import refreshed read-only: scanned 2784 selected files, matched 836 Timaert/Samosbor/TMA files, copied 17 missing, refreshed 28 stale, unchanged 791; 1 locked source log remained hash-unavailable and is recorded in `IMPORT_INDEX.md`.

STATUS: VERIFIED
