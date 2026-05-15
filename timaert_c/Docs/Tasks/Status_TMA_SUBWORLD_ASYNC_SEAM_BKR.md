# Status: TMA_SUBWORLD_ASYNC_SEAM_BKR

Prompt ID: TMA_SUBWORLD_ASYNC_SEAM_BKR
Domain: SUBWORLD_PERFORMANCE_ARCHITECT

- [x] Read batch header, own prompt, Timaert AGENTS, matwej A11/A12, translation, merge plan, architecture.
- [x] Read TS authority: seamless-manager.ts, gen-worker.ts, SubworldScreen.svelte, map-factory.ts.
- [x] Read C++ seam path: seamless_manager, dispatch, base_generator, map_factory, engine, renderer_2d, renderer_3d.
- [x] Patch worker-backed boundary generation: manager queues jthread jobs, installs placeholders, and stitches completed cells on the main thread.
- [x] Patch seam timing: engine measures `gen`, `smooth`, `upload3d`, `upload2d`, and `total` behind `TIMAERT_SEAM_TRACE`.
- [x] Compile touched translation units with MSVC `cl`: `seamless_manager.cpp` and `engine.cpp` pass `/W3 /GR- /EHs-c-`.
- [x] Run manager seam smoke: `subworld_async_seam_test` passes; optimized seam manager timing `gen=6.499ms smooth=0.000ms total=6.500ms`.
- [x] Run existing test binaries: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test` pass.
- [x] Superseded: full `cmake --build build-msvc --target timaert --parallel 1` later linked cleanly after the unrelated audio/CMake state settled.
- [x] Append final report to Docs/AgentLogs.

## 2026-05-15 continuation

- [x] Re-read status/log and prompt block before continuing.
- [x] Rechecked seam manager, engine seam timing path, map factory cache, renderer upload caller signatures, and async seam test.
- [x] Upgraded deferred outgoing-cell snapshots: heightmap quantization now runs on the manager worker, not on the main thread after a crossing.
- [x] Added rvalue `store_saved_subworld(SavedSubworld&&)` so completed worker snapshots commit to cache by move instead of copying vectors.
- [x] Added async full-composite road smoothing job after all placeholders are replaced; seam path still reports `smooth=0.000ms`, and completed smoothing publishes a later dirty composite.
- [x] Updated `subworld_async_seam_test` to require 3 stitched cell updates plus 1 async smoothing publish.
- [x] Fixed worker shutdown edge case: stop now exits only after queues are empty, so pending save jobs are not dropped after an in-flight generation job.
- [x] Direct MSVC compile passed for `seamless_manager.cpp`, `map_factory.cpp`, and `engine.cpp`.
- [x] Optimized seam smoke passed after shutdown fix: `subworld_async_seam_test: ok dirty=4 gen=10.002ms smooth=0.000ms total=10.002ms`.
- [x] Existing binaries passed again: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.

## 2026-05-15 final hardening pass

- [x] Re-read status, prompt block, and created missing rationale file before further edits.
- [x] Fixed shutdown preservation: pending generation/smoothing jobs are dropped first, completed save snapshots are committed before queue cleanup. DOD: save data wins over stale CPU work; rejected waiting for stale non-save queues; estimate saved on transition is queue-dependent, worst case avoids several cell generations.
- [x] Added per-cell road-tile accounting so no-road composites skip async full-composite smoothing and its 9 MiB tile copy plus 36 MiB height copy. DOD: avoid no-op work; rejected scanning 3072x3072 every publish; estimated saved when no roads: one 45 MiB snapshot copy plus smoother scan.
- [x] Converted saved-subworld cache lookup for async generation from deep copy to immutable shared snapshot reference. DOD: saved edits are preserved without copying quantized height vectors on seam frame; rejected worker access to unlocked raw cache pointer; estimated saved per revisited edited cell: one heightmap vector copy plus structures copy.
- [x] Direct MSVC compile passed again for `map_factory.cpp`, `seamless_manager.cpp`, and `engine.cpp` using BuildTools `cl` with `/W3 /GR- /EHs-c-`.
- [x] Debug focused seam smoke rebuilt and passed: `subworld_async_seam_test: ok dirty=4 gen=66.176ms smooth=0.000ms total=66.177ms` under heavy concurrent build load.
- [x] Optimized focused seam smoke rebuilt and passed: `subworld_async_seam_test: ok dirty=4 gen=11.295ms smooth=0.000ms total=11.296ms`.
- [x] Existing regression binaries passed again: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- [x] Polish scans run. Touched-file hits were legitimate: test name strings, template syntax, an unrelated `attempt` loop in `engine.cpp`, and a `prefix` comment in `map_factory.h`.
- [x] `git diff --check` passed for touched/report files; only pre-existing CRLF normalization warnings were reported.
- [x] Superseded: full target build was rerun through BuildTools CMake and passed without requiring dotnet or destructive cleanup.

## 2026-05-15 verification refresh after polish mandate

- [x] Re-read status, rationale, own prompt block, and `<POLISH_MANDATE>` after core seam tasks were implemented.
- [x] Cleaned sparse road smoother comments and public header description so the documented behavior matches the indexed worker path.
- [x] Reprioritized worker queue order to generation -> save snapshots -> cosmetic smoothing. DOD: preserve modified outgoing cells before optional visual smoothing; rejected smoothing ahead of save because one 36 MiB smooth job can delay snapshot persistence on cheap CPUs; estimate saved for edit-heavy crossings is one smoothing-job delay before save commit.
- [x] Reserved exact composite structure capacity before full structure rebuild/blit. DOD: avoid vector growth churn during seam/job publish; rejected shrink/reallocate patterns; estimate saved is structure-count dependent, with no fake fixed microsecond claim.
- [x] Full target MSVC build passed once through `VsDevCmd.bat`: `cmake --build build-msvc --target timaert --parallel 1` rebuilt subworld objects and linked `timaert.exe`.
- [x] Latest direct MSVC focused build passed for `subworld_async_seam_test` debug and optimized into `build-msvc/codex-check`.
- [x] Latest debug seam smoke passed: `roadDirty=4 plainDirty=3 roadGen=23.629ms plainGen=38.931ms`, both with `smooth=0.000ms` on the crossing path.
- [x] Latest optimized seam smoke passed: `roadDirty=4 plainDirty=3 roadGen=9.882ms plainGen=30.325ms`, both with `smooth=0.000ms` on the crossing path.
- [x] Existing regression binaries passed again: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- [x] Polish scans rerun. Relevant touched-file hits were inspected: gated seam `fprintf`, test-only `fprintf`, vector reserves/pushes on seam publish, an unrelated `attempt` loop in `engine.cpp`, and `prefix` documentation in `map_factory.h`.
- [x] `git diff --check` passed for touched/report files; Git only emitted CRLF normalization warnings.
- [x] Superseded: latest full target build now passes; the unrelated app/UI signature drift was resolved by other work.
- [x] Superseded: runtime app smoke `new_game,wait_boot_done,subworld_time,quit` now passes through boot, subworld enter, 1000 subworld frames, leave, and quit.

## 2026-05-15 continued hardening

- [x] Re-read status, rationale, and own prompt block before extending the seam domain.
- [x] Added stale generation-work pruning after boundary shifts. DOD: pending/completed jobs must match a current placeholder `(cx, cy, generation)` before they can consume future drain time; rejected letting old rapid-crossing jobs sit ahead of live cells; estimated saving is discarded stale cell generations/results on rapid crossings.
- [x] Changed completed generation/smoothing drains to discard stale front entries without counting them against the per-frame publish budget. DOD: one stale completed job cannot delay the next current stitch/smooth publish by a frame; rejected unbounded current-cell publishing; estimate saved is one frame per stale front entry.
- [x] Added focused diagonal seam proof: no-road diagonal crossing must publish exactly 5 stitched cells and no smoothing publish.
- [x] Latest debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=20.673ms plainGen=23.474ms diagonalGen=44.174ms`, all crossing-path smoothing `0.000ms`.
- [x] Latest optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=9.271ms plainGen=14.156ms diagonalGen=10.282ms`, all crossing-path smoothing `0.000ms`.
- [x] Direct MSVC compile passed for `src/sub/engine.cpp` with SDL/EnTT include paths.
- [x] Existing regression binaries passed again: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- [x] Anti-bloat scan on touched seam files found no `std::async`, exceptions, RTTI casts, or `std::rand`.
- [x] `git diff --check` passed for touched/report files; Git only emitted CRLF normalization warnings. No root `.obj` artifacts remain.
- [x] Superseded: full target build was retried after shared build-directory contention cleared and passed.

## 2026-05-15 pending snapshot proof

- [x] Added focused pending-snapshot cache proof. DOD: immediate `snapshot_all_to_cache()` after a seam crossing must save an outgoing real cell and a surviving real cell while fresh generation jobs are still pending; rejected relying on later leave/idle frames; estimate saved is correctness, not frame time.
- [x] Latest debug focused seam smoke passed with snapshot proof: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=69.346ms plainGen=69.815ms diagonalGen=74.353ms`, `snapshot_pending ok`, all crossing-path smoothing `0.000ms`.
- [x] Latest optimized focused seam smoke passed with snapshot proof: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=25.351ms plainGen=8.823ms diagonalGen=38.133ms`, `snapshot_pending ok`, all crossing-path smoothing `0.000ms`.
- [x] Existing regression binaries passed again after the snapshot proof update: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.

## 2026-05-15 Hecton import request

- [x] Re-read status/rationale before handling the requested Hecton-to-Timaert docs transfer.
- [x] Rechecked `C:\hades\Hecton8` docs/tasks/logs/reports/artifact folders for Timaert/Samosbor/TMA markers by path and content.
- [x] Updated `Docs/Imported/Hecton8_Timaert_Samosbor_Import_Audit.md` with the recheck scope, false positives, and result.
- [x] No Hecton files were copied because no positive Timaert/Samosbor/TMA artifacts were found; rejected importing unrelated Hecton `MACRO_*` task/log files because that would contaminate Timaert docs.

## 2026-05-15 rapid reversal proof and build retry

- [x] Added focused rapid reversal seam proof. DOD: east crossing immediately followed by west crossing must stitch exactly the current 3 cells and publish no abandoned seam/smoothing dirty event; rejected assuming stale-prune correctness from static queue inspection.
- [x] Latest debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3 roadGen=47.236ms plainGen=16.123ms diagonalGen=73.043ms reversalGen=24.275ms`, `snapshot_pending ok`, all crossing-path smoothing `0.000ms`.
- [x] Latest optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3 roadGen=10.946ms plainGen=12.694ms diagonalGen=13.752ms reversalGen=23.174ms`, `snapshot_pending ok`, all crossing-path smoothing `0.000ms`.
- [x] Existing regression binaries passed again: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- [x] Superseded: full target build later linked cleanly; the remaining live `timaert.exe` process was from `build-msvc-integrator`, not the verified `build-msvc` target.

## 2026-05-15 final verification and import closure

- [x] Re-read status, rationale, batch header, and exact prompt before the final verification pass.
- [x] Registered `tests/subworld_async_seam_test.cpp` as a normal CMake target, using the same C++23, no-RTTI, no-exceptions MSVC discipline as the other native tests.
- [x] Full app target passed: `cmake --build build-msvc --target timaert --parallel 1`.
- [x] CMake-registered seam proof passed: `build-msvc\subworld_async_seam_test.exe` reported road axis, plain axis, diagonal, rapid reversal, and pending snapshot success, with boundary smoothing still `0.000ms`.
- [x] 2026-05-15 documentation audit rerun passed with `snapshot_pending ok` and `worker_restore_saved ok`; its older timing values are superseded by the following shared `build-msvc` verification rerun.
- [x] 2026-05-15 shared `build-msvc` verification rerun passed: `build-msvc\subworld_async_seam_test.exe` reported `roadGen=39.142ms`, `plainGen=9.768ms`, `diagonalGen=41.913ms`, `reversalGen=27.749ms`, `snapshot_pending ok`, and `worker_restore_saved ok`.
- [x] Existing regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`.
- [x] Runtime smoke passed: `TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,subworld_time,quit` booted, entered subworld, ticked 1000 frames, left, and printed `[smoke] PASS`.
- [x] Re-verified the Hecton documentation import quarantine and copied seven newly-created missing Hecton AgentLogs artifacts into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`.
- [x] Selected Hecton docs/tasks/logs import scope was brought back to `Missing=0` at the last copy; import tree file count is `1711`.
- [x] `git diff --check` passed for touched code/test/report files; Git only reported CRLF normalization warnings.
- [x] Final domain status: `VERIFIED`.

## 2026-05-15 post-verified hardening pass

- [x] Re-read status, rationale, and exact prompt before continuing.
- [x] Removed dead async-job fields: `GenerationJob::targetIdx` and `CompletedJob::genMs`; rejected keeping stale instrumentation state that was not read anywhere.
- [x] Strengthened `subworld_async_seam_test` with direct placeholder assertions: newly exposed axis and diagonal slots must be `TILE_GRASS` at `WATER_LEVEL + kLandMargin` before worker stitches.
- [x] Rebuilt CMake seam target: `cmake --build build-msvc --target subworld_async_seam_test --parallel 1`.
- [x] CMake seam test passed after the stronger placeholder proof: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `snapshot_pending ok`, all crossing-path smoothing `0.000ms`.
- [x] Regression binaries passed again: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`.
- [x] Shared `build-msvc` full target was externally blocked by concurrent build/runtime state: first link could not open `npc_spawn.cpp.obj`, then `build-msvc\timaert.exe` PID `66344` remained running and locking the target exe.
- [x] Created private verification build `build-msvc-subworld-check`; first configure needed explicit SDL package paths and cached FetchContent source dirs to avoid a transient GitHub `stb` TLS fetch failure.
- [x] Private full app target passed: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`.
- [x] Private runtime smoke passed: `build-msvc-subworld-check\timaert.exe` with `TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,subworld_time,quit` ended with `[smoke] PASS`.
- [x] Hecton import quarantine refreshed again. Prompt delta manifest rows: `13`; Hecton `Docs` selected files: `1879`; Hecton root selected files: `23`; imported docs/root counts match; missing selected files: `0`; import tree total files: `1913`.
- [x] Anti-bloat scan on touched seam/test files found no `std::async`, exceptions, RTTI casts, `std::rand`, stale job fields, TODO, or FIXME.
- [x] `git diff --check` passed for touched code/test/report files; Git only reported CRLF normalization warnings.
- [x] Final domain status remains `VERIFIED`.

## 2026-05-15 saved-restore proof and latest private verification

- [x] Re-read status, rationale, and exact prompt before continuing.
- [x] Added `worker_restore_saved` to `subworld_async_seam_test`: a saved quantized heightmap is stored for a fresh seam cell, the seam crosses east, the freed slot is first verified as placeholder, then the async worker stitch must restore the saved height.
- [x] Shared `build-msvc` seam target was externally blocked by object permission contention, so the focused proof was rebuilt in private `build-msvc-subworld-check`.
- [x] Private seam test passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `snapshot_pending ok`, `worker_restore_saved ok`, all crossing-path smoothing `0.000ms`.
- [x] Private full app target passed again: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`.
- [x] Private runtime smoke passed again with `new_game,wait_boot_done,subworld_time,quit` and ended with `[smoke] PASS`.
- [x] Shared `build-msvc` and `build-msvc-roadriver` executables remained running externally, so they were not killed or overwritten.
- [x] Latest Hecton import refresh copied 9 more fresh artifacts into the quarantine. Prompt delta manifest rows: `22`; Hecton `Docs` selected files: `1913`; selected missing files: `0`; Hecton root selected files: `23`; root missing files: `0`; import tree total files: `2301`.
- [x] Anti-bloat scan stayed clean for touched seam/test files.
- [x] `git diff --check` stayed clean for touched code/test/report files except CRLF normalization warnings.
- [x] Final domain status remains `VERIFIED`.

## 2026-05-15 runtime seam upload proof and Hecton refresh

- [x] Re-read status/rationale before continuing after user continuation.
- [x] Added saved-structure restoration proof to `subworld_async_seam_test`: worker-restored saved cells now prove both quantized heightmap restore and saved `Structure::Bridge` merge into the shifted composite.
- [x] Removed stray production `debug smooth roadIdx` stderr from `SeamlessSubworldManager::queue_composite_smooth`; normal gameplay seam smoothing is silent unless `TIMAERT_SEAM_TRACE` is enabled.
- [x] Added `subworld_seam` smoke action in `src/app/main.cpp` to cross a real runtime seam and exercise the existing `[seam-cross]` upload timing path.
- [x] Fixed seam-frame double upload in `SubworldEngine`: a dirty composite now uploads only the active renderer on the seam frame and defers the inactive renderer until view switch.
- [x] Optimized 3D upload path: no-road composites upload a 1x1 zero road mask instead of a full 3072x3072 zero mask, and the terrain index buffer is built once in `Renderer3D::init()` instead of per seam upload.
- [x] Rejected and reverted the attempted 2D R16 height upload after measurement showed worse Debug smoke time (`upload2d=2628.803ms` vs previous active 2D path); kept the codebase on the known-correct R32F 2D height upload.
- [x] Private full app build passed: `cmake --build build-msvc-subworld-check --target timaert subworld_async_seam_test --parallel 1`.
- [x] Private runtime 3D seam smoke passed: `subworld_seam` emitted `[seam-cross] gen=32.052ms smooth=0.000ms upload3d=176.381ms upload2d=0.000ms total=208.577ms` and `[smoke] PASS`.
- [x] Private `subworld_time` smoke still passed after upload deferral: 1000 subworld frames, exact 1440 minute advance, leave/player invariant OK, `[smoke] PASS`.
- [x] Focused seam test passed after saved-structure proof: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `snapshot_pending ok`, `worker_restore_saved ok`.
- [x] Regression tests passed in private tree: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- [x] Updated Timaert root docs only: `translation.md`, `README.md`, and `matwej.md` now record seam-manager transfer, runtime seam smoke evidence, and residual active 3D upload target.
- [x] Refreshed Hecton import quarantine under Timaert only. Selected Hecton `Docs` files: `2020`; root selected files: `26`; missing copied: `0`; updated copied: `31`; missing after copy: `0`; import tree total files: `3063`.
- [x] Anti-bloat scan found no forbidden `std::async`, exceptions, RTTI casts, `std::rand`, debug seam spam, TODO, or FIXME in touched seam/runtime/test files.
- [x] `git diff --check` passed for touched code/test/report/doc files except LF-to-CRLF normalization warnings.
- [x] Final domain status remains `VERIFIED`; residual performance target is active 3D terrain/instance upload, not worker generation/smoothing.

## 2026-05-15 saved-cache lifetime hardening

- [x] Reproduced flaky repeated `subworld_async_seam_test` process exits after `snapshot_pending ok` / `worker_restore_saved ok`.
- [x] Fixed the test lifetime contract: `clear_saved_subworlds()` now runs after the local `SeamlessSubworldManager` leaves scope in the pending-snapshot and worker-restore test cases, so worker shutdown completes before global saved-cache cleanup.
- [x] Focused target rebuilt: `cmake --build build-msvc --target subworld_async_seam_test -- -j1`.
- [x] Focused stability loop passed 5/5 runs after the fix.
- [x] Private current-code build `build-msvc-verify` linked `timaert.exe` and all test executables because shared `build-msvc\timaert.exe` was locked by a running process.
- [x] Private 13-test MSVC run passed. Previous private seam slices: `roadGen=28.886ms`, `plainGen=25.606ms`, `diagonalGen=50.717ms`, `reversalGen=56.236ms`, `snapshot_pending ok`, `worker_restore_saved ok`; this is superseded by the later shared build retest below.
- [x] Private app smoke passed with `new_game,wait_boot_done,wait_visible,capture_frame,quit`; macro renderer initialized, feature/zone/landmark textures uploaded, framebuffer visibility passed, frame captured.

## 2026-05-15 sparse road-mask upload hardening

- [x] Re-read status, rationale, and exact prompt before continuing after the user continuation.
- [x] Corrected worker priority to match the documented contract: exposed-cell generation first, save snapshots second, cosmetic smoothing third. Rejected smoothing-before-save because it can delay persistence behind a full-composite visual job.
- [x] Added exact sparse road-mask metadata to `SeamlessSubworldManager`: road/square smoothing indices and visual `TILE_ROAD` mask indices are collected in one worker tile pass, not two scans.
- [x] Updated `Renderer3D::upload` to build the road mask from sparse manager indices instead of scanning the full 3072x3072 tile buffer; no-road composites still upload a 1x1 zero mask.
- [x] Reused CPU scratch buffers for terrain vertices, road-mask bytes, sparse road indices, and billboard instances across 3D uploads.
- [x] Measured and rejected a speculative GL buffer/texture sub-update trial after it regressed the same private Debug seam smoke to `upload3d=331.363ms`.
- [x] Strengthened `subworld_async_seam_test` with sparse road-mask correctness: road cases must expose only `TILE_ROAD` indices and plain cases must expose none.
- [x] Private build passed: `cmake --build build-msvc-subworld-check --target subworld_async_seam_test --target timaert --parallel 1` through `VsDevCmd.bat`; no dotnet rebuilds were used.
- [x] Focused seam test passed: road/plain/diagonal/rapid-reversal coverage, `snapshot_pending ok`, `worker_restore_saved ok`, road-mask proof; latest logged generation slices were `97.725ms`, `40.816ms`, `36.966ms`, and `54.870ms`, all with seam-path smoothing `0.000ms`.
- [x] Runtime 3D seam smoke passed: `[seam-cross] gen=53.097ms smooth=0.000ms upload3d=111.769ms upload2d=0.000ms total=165.049ms`, `[smoke] PASS`.
- [x] Runtime `subworld_time` smoke passed on seed 42: 1000 subworld frames, exact 1440-minute advance, `[smoke] PASS`.
- [x] Regression binaries passed in the private tree: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`.
- [x] Updated Timaert docs only: `matwej.md`, `translation.md`, and `README.md` record the latest seam and upload evidence.
- [x] Refreshed the Hecton import quarantine under Timaert only. Selected Hecton `Docs` files: `2097`; root selected files: `30`; copied missing: `0`; updated changed: `0`; missing after copy: `0`; import tree files: `3168`.
- [x] Anti-bloat scan found no forbidden `std::async`, exceptions, RTTI casts, `std::rand`, debug seam spam, TODO, or FIXME in touched seam/runtime/test files.
- [x] `git diff --check` passed for touched code/test/report/doc files except LF-to-CRLF normalization warnings.
- [x] Final domain status remains `VERIFIED`; residual target is deeper active 3D upload architecture, not async generation/smoothing.

## 2026-05-15 conservative 1536 road-mask upload pass

- [x] Re-read status, rationale, exact prompt, and current root docs before continuing after the user continuation.
- [x] Confirmed road raster width is a 5-tile footprint in native `carve_organic_road`, so a 2:1 road-mask downsample keeps roughly 2-3 mask pixels across a road while reducing upload bandwidth.
- [x] Changed `Renderer3D` road composites from a 3072x3072 R8 road mask to a conservative 1536x1536 R8 road mask built from sparse manager road indices; no-road composites still upload a 1x1 zero mask.
- [x] Private app target rebuilt through `VsDevCmd.bat`: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`; no dotnet rebuilds were used.
- [x] Runtime 3D seam smoke passed with the accepted downsample path: `[seam-cross] gen=23.032ms smooth=0.000ms upload3d=69.636ms upload2d=0.000ms total=92.813ms`, `[smoke] PASS`.
- [x] Focused seam test still passed: road/plain/diagonal/rapid-reversal coverage, `snapshot_pending ok`, `worker_restore_saved ok`, all seam-path smoothing `0.000ms`.
- [x] Runtime `subworld_time` smoke still passed on seed 42: 1000 subworld frames, exact 1440-minute advance, `[smoke] PASS`.
- [x] Regression binaries passed in the private tree: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`.
- [x] Updated Timaert root docs only: `matwej.md`, `translation.md`, and `README.md` now record the 1536 road-mask upload evidence.
- [x] Refreshed the Hecton import quarantine under Timaert only. Selected Hecton `Docs` files: `2182`; root selected files: `3`; copied missing: `0`; updated changed: `0`; missing after copy: `0`; import tree files: `3279`.
- [x] Anti-bloat scan found no forbidden `std::async`, exceptions, RTTI casts, `std::rand`, debug seam spam, TODO, or FIXME in touched seam/runtime/test files.
- [x] `git diff --check` passed for touched code/test/report/doc files except LF-to-CRLF normalization warnings.
- [x] Final domain status remains `VERIFIED`; residual target is deeper active 3D upload architecture and possibly frame-sliced renderer refresh, not async generation/smoothing.

## 2026-05-15 shared build retest after saved-cache lifetime fix

- [x] Shared `build-msvc` was no longer locked and rebuilt `timaert.exe` successfully through `VsDevCmd.bat`.
- [x] Full 13-test native suite exited 0. `subworld_async_seam_test` reported `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `snapshot_pending ok`, and `worker_restore_saved ok`.
- [x] Latest shared seam generation slices: `roadGen=22.451ms`, `plainGen=94.605ms`, `diagonalGen=35.042ms`, `reversalGen=54.911ms`; boundary smoothing stayed `0.000ms`.
- [x] App smoke `new_game,wait_boot_done,wait_visible,capture_frame,quit` exited 0 with `[smoke] PASS`.
- [x] Root smoke image artifacts were moved to `artifacts\runtime-smoke\images\verification-20260515`; root garbage scan returned no loose runtime images/diffs/patches/PPMs.

STATUS: VERIFIED.

## 2026-05-15 finish pass: water-plane isolation, compile fixes, rejected terrain-payload trial

- [x] Re-read status, rationale, and exact `TMA_SUBWORLD_ASYNC_SEAM_BKR` prompt before final continuation.
- [x] Measured a terrain-payload shader-grid trial in `Renderer3D` (height+normal VBO, X/Z reconstructed in shader), proved it worse (`upload3d=63.248ms`, `total=93.941ms`), and reverted it to the accepted 6-float terrain vertex path.
- [x] Fixed a native app link blocker in `src/macro/spawners.{h,cpp}` by adding ABI-compatible wrappers for the old 3-arg `spawn_trees` and 5-arg `build_feature_layer` call shapes, forwarding to the sea-level-aware implementations with `seaLevel=0.40f`.
- [x] Fixed the `gen_spire` compile path in `src/sub/gens/dispatch.cpp` by threading the sanitized 3x3 neighbor feature array into the generator instead of referencing an undeclared `nbFeature`.
- [x] Hardened `subworld_async_seam_test` water-plane proof by clearing the saved-cell cache around the invariant case so stale saved cells cannot contaminate the 3x3 scan.
- [x] Private native rebuild passed through BuildTools 18: `cmake --build build-msvc-subworld-check --target timaert subworld_async_seam_test --parallel 1`; no dotnet rebuilds were used. One earlier retry through the wrong VS environment failed before project code on missing standard header `cstddef`, then the correct BuildTools environment built cleanly.
- [x] Focused seam test passed on freshly rebuilt executable: `roadGen=31.578ms`, `plainGen=23.261ms`, `diagonalGen=29.785ms`, `reversalGen=24.892ms`, `snapshot_pending ok`, `worker_restore_saved ok`, water-plane `water=3145728`, `land=6291456`, `badWater=0`, `badLand=0`, `maxWater=0.40000`, `minLand=0.42000`.
- [x] Runtime `subworld_time` smoke passed after rebuild: 1000 subworld frames, exact 1440-minute advance, `[smoke] PASS`.
- [x] Runtime `subworld_seam` smoke passed after rebuild: `[seam-cross] gen=38.989ms smooth=0.000ms upload3d=118.795ms upload2d=0.000ms total=157.938ms`, `[smoke] PASS`. Prior best accepted 1024-mask timing remains `upload3d=51.785ms`, `total=74.603ms`.
- [x] Regression binaries rebuilt and passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`.
- [x] Refreshed the Hecton import quarantine under Timaert only. Selected Hecton `Docs` files: `2372`; root selected files: `3`; copied missing: `21`; updated changed: `11`; unchanged: `2343`; missing after copy: `0`; copy errors: `0`; import tree files: `3990`.
- [x] Updated Timaert docs only: `README.md`, `matwej.md`, `translation.md`, and `Docs/Imported/Hecton8_Timaert_Samosbor_Import_Audit.md`. No Timaert docs/logs were written to Hecton.
- [x] Final anti-bloat scan found no forbidden `std::async`, exceptions, RTTI casts, `std::rand`, debug seam spam, TODO, or FIXME in touched seam/runtime/test files.
- [x] Final renderer trial-revert scan found no leftover `aHeightNrm`, `uMeshStep`, `uGridWidth`, or disabled normal attribute code in `renderer_3d`.
- [x] Final `git diff --check` passed for touched code/test/report/doc files except LF-to-CRLF normalization warnings.
- [x] Final process check found no leftover `timaert` or `subworld_async_seam_test` processes.
- [x] Final report appended to `Docs/AgentLogs/LOG_TMA_SUBWORLD_ASYNC_SEAM_BKR.md` at the bottom; old log entries were not overwritten.

STATUS: VERIFIED.

## 2026-05-15 1024 road-mask dilation pass

- [x] Re-read status, rationale, and exact prompt before continuing after the user continuation.
- [x] Changed `Renderer3D` road composites from the accepted 1536x1536 R8 mask to a 1024x1024 R8 mask with one-pixel dilation fed by sparse manager road indices; no-road composites still upload a 1x1 zero mask.
- [x] Private app target rebuilt through `VsDevCmd.bat`: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`; no dotnet rebuilds were used.
- [x] Runtime 3D seam smoke passed on the final rebuilt executable: `[seam-cross] gen=22.695ms smooth=0.000ms upload3d=51.785ms upload2d=0.000ms total=74.603ms`, `[smoke] PASS`.
- [x] Focused seam target rebuilt and passed: road/plain/diagonal/rapid-reversal coverage, `snapshot_pending ok`, `worker_restore_saved ok`, sparse road-mask proof, all seam-path smoothing `0.000ms`.
- [x] Runtime `subworld_time` smoke passed on seed 42: 1000 subworld frames, exact 1440-minute advance, `[smoke] PASS`.
- [x] Regression binaries passed in the private tree: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`.
- [x] Updated Timaert root docs only: `README.md`, `matwej.md`, and `translation.md` now record the 1024 road-mask timing evidence.
- [x] Refreshed the Hecton import quarantine under Timaert only. Selected Hecton `Docs` files: `2228`; root selected files: `3`; copied missing: `4`; updated changed: `1`; missing after copy: `0`; import tree files: `3335`.
- [x] Live Hecton log caveat handled: `Docs\AgentLogs\AUP_assembly_build_loop53.log` was locked during source hashing, then recopied successfully after size stabilization at `939966` bytes.
- [x] Anti-bloat scan found no forbidden `std::async`, exceptions, RTTI casts, `std::rand`, debug seam spam, TODO, or FIXME in touched seam/runtime/test files.
- [x] `git diff --check` passed for touched code/test/report/doc files except LF-to-CRLF normalization warnings.
- [x] Final domain status remains `VERIFIED`; residual target is deeper active 3D terrain/instance upload architecture, not async generation/smoothing.

STATUS: VERIFIED.

## 2026-05-15 final status marker

- [x] Latest finish-pass section above is the authoritative current-source verification: native rebuild through BuildTools 18, focused seam test, `subworld_seam`, `subworld_time`, anti-bloat scan, diff check, process check, and final log append all completed.
- [x] Current rebuilt runtime seam evidence: `[seam-cross] gen=38.989ms smooth=0.000ms upload3d=118.795ms upload2d=0.000ms total=157.938ms`.
- [x] Current rebuilt focused seam evidence: `roadGen=31.578ms`, `plainGen=23.261ms`, `diagonalGen=29.785ms`, `reversalGen=24.892ms`, water-plane `badWater=0`, `badLand=0`.

STATUS: VERIFIED.
