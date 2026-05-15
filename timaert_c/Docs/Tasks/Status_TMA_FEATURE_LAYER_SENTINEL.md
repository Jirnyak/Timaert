# Status: TMA_FEATURE_LAYER_SENTINEL

Domain: `features.ts` -> C++ `FeatureLayer`, feature builder, and core feature consumers.

## Checklist

- [x] TS source re-read.
  - Evidence: `C:\Timaert\src\game\features.ts` was checked against `macro/features.h`, `spawners.cpp::build_feature_layer`, `pathfinding.cpp`, `zones.cpp`, and `macro_renderer.cpp`.
- [x] Feature builder parity verified.
  - Evidence: priority order remains Mountain -> Tree -> DirtRoad -> Road, tree writes use TS flattened-index semantics, short road/dirt masks use TS prefix reads, and C++ keeps the documented water filter.
- [x] Malformed feature bytes fail closed.
  - Evidence: `FeatureLayer::decode()` maps unknown bytes to `FT_None`; `FeatureLayer::at`, pathfinding cost-grid build, and zone generation now use it.
- [x] Feature API write path locked.
  - Evidence: `FeatureType` byte values are compile-time asserted against the TS contract, `FeatureLayer::set()` sanitizes invalid enum casts through `decode()`, and `feature_layer_parity_test` covers the invalid-setter case.
- [x] RGBA byte-count overflow guard added to builder.
  - Evidence: `build_feature_layer` validates `cell_count_for()` and `total <= max/4` before `total * 4` RGBA checks.
- [x] Hecton exact-match Timaert/Samosbor/TMA import completed.
  - Evidence: exact-match quarantine under `Docs\Imported\Hecton8_Timaert_Samosbor\2026-05-15_exact_matches` was refreshed read-only from Hecton; latest pass found 836 exact matches, copied 17 missing files, refreshed 28 stale files, and recorded 1 locked source log.
- [x] Focused verification passed.
  - Evidence: `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, `git diff --check`, and seed 110 + 111 boot smokes all passed after the hardening patch.
- [x] Wider C++ regression check passed.
  - Evidence: full `build-msvc-integrator` build passes after disabling incremental FetchContent network updates; 13 test executables pass (`quest`, `save`, `spells`, `combat`, `audio`, `character`, `feature`, `pathfinding`, `road`, `subworld`, and `character_paperdoll_gl_smoke_test`).
- [x] TS default mountain feature threshold restored.
  - Evidence: TS uses `defaultParameters.snowLevel = 0.8` and calls `buildFeatureLayer(..., snowLevel - 0.05, ...)`; C++ now uses `kDefaultFeatureMountainThreshold = 0.75f` for both boot-time `build_feature_layer()` and `u_mtnThreshold`.

## Current Status

STATUS: VERIFIED.

## 2026-05-15 active sea-level continuity continuation

- [x] Runtime feature-adjacent systems now use the active map sea level.
  - Evidence: `boot_world()` passes `LayerParameters::seaLevel` into `spawn_trees()`, `trace_roads()`, `build_feature_layer()`, and `build_cost_grid()`, and `MacroRenderer::draw()` uploads `app.gs.mapParams.seaLevel` to `u_seaLevel`.
- [x] Feature, road, tree, path-cost, and renderer contracts stay backward-compatible.
  - Evidence: public headers keep `0.40f` defaults for existing focused tests/tools, while normal app boot uses the actual map parameter. Legacy hard-coded app calls were removed.
- [x] Active sea-level regressions are locked.
  - Evidence: `feature_layer_parity_test` covers road/tree feature rejection below the active sea level; `road_river_generation_test` covers tree spawn and road tracing differences between `0.30f` and `0.40f`; `pathfinding_parity_test` covers float sea-level boundary behavior.
- [x] Documentation updated in Timaert, not Hecton.
  - Evidence: `ARCHITECTURE.md` and `translation.md` describe active-map sea-level filtering for tree, road, feature, path-cost, and macro renderer paths.
- [x] Verification passed without dotnet rebuilds.
  - Evidence: targeted CMake/MSVC build for `road_river_generation_test`, `feature_layer_parity_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`, `quest_lifecycle_test`, `save_roundtrip_test`, and `timaert` exited 0; all 16 current `build-msvc\*test.exe` executables exited 0; `git diff --check` reports only LF-to-CRLF normalization warnings; project-root artifact scan is clean.

STATUS: VERIFIED.

The `features.ts` domain is transferred and hardened in C++. Remaining work is outside this domain: macro `renderer.ts` sprite batching remains partial by design, and broader event producers/consumers are tracked separately in `translation.md`.

## 2026-05-15 feature-render continuation

- [x] Re-read TS `features.ts` and renderer feature consumers before editing.
  - Evidence: TS feature order remains Mountain -> Tree -> DirtRoad -> Road, and `renderer.ts` uses a 3x3 decoration painter instead of independent global tree/mountain/landmark overlays.
- [x] Corrected macro decoration painter order.
  - Evidence: `macro_renderer.cpp` now uses `decorationOverlay()` with 3x3 far-to-near cell order and tree -> mountain -> landmark painting inside each candidate cell.
- [x] Kept feature/zone/landmark upload fail-closed.
  - Evidence: malformed feature storage uploads a 1x1 blank feature texture unless complete, invalid feature/zone/landmark bytes are sanitized before GPU upload.
- [x] Repaired Hecton import quarantine after a bad relative-path refresh.
  - Evidence: removed 2583 erroneous top-level duplicate files from `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, re-imported source-relative, refreshed 7 exact-match active bucket files with SHA-256 equality, and left `C:\hades\Hecton8` untouched.
- [x] Root artifact cleanup stayed clean.
  - Evidence: runtime/smoke images were moved under `artifacts\runtime-smoke\images\verification-20260515`; root scan for `runtime_*`, `smoke_*`, `save.bin`, `.diff`, `.patch`, `.obj`, `.ppm`, `.png` returned no files.
- [x] Shared MSVC verification passed.
  - Evidence: `cmake --build build-msvc` linked `timaert.exe`; 13 test executables exited 0; app smoke `new_game,wait_boot_done,wait_visible,capture_frame,quit` exited 0 with `[smoke] PASS`.
- [x] Threshold 0.75 continuation verified.
  - Evidence: `build-msvc-integrator` full build passed; all 14 current `*test.exe` executables passed including `npc_spawn_contract_test`; seed 112 boot smoke passed with `[smoke] PASS`.
- [x] Feature coordinate wrap hardened.
  - Evidence: `FeatureLayer::wrap_coord()` replaces `((x % width) + width) % width` inside `at()`/`set()` so malformed huge extents cannot overflow while failing closed outside backing storage; `feature_layer_parity_test` covers `INT_MIN` and `INT_MAX - 1` cases.
- [x] Current agent C++ breakage fixed.
  - Evidence: `renderer_3d.cpp` needed `glDisableVertexAttribArray`; the Win32 GL loader now declares, loads, and maps it. Targeted build, full build, 14 tests, and seed 113 smoke pass.
- [x] Boot dirt-road land mask boundary tightened.
  - Evidence: `boot_world()` now passes `app.terrain.rgba.size()` into `trace_dirt_roads()` with the RGBA pointer, so the dirt-road input mask can validate byte coverage instead of trusting a raw pointer-only call.

STATUS: VERIFIED.
