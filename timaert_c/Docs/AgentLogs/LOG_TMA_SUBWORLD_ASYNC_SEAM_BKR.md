# TMA_SUBWORLD_ASYNC_SEAM_BKR

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

TS files read:
- `C:\Timaert\src\game\subworld\seamless-manager.ts`
- `C:\Timaert\src\game\subworld\gen-worker.ts`
- `C:\Timaert\src\screens\SubworldScreen.svelte`
- `C:\Timaert\src\game\subworld\map-factory.ts`

C++ files changed:
- `src/sub/seamless_manager.h`
- `src/sub/seamless_manager.cpp`
- `src/sub/engine.cpp`
- `src/sub/engine.h` (minimal compatibility with concurrent `leave(bool)` / `status_line()` header edit)
- `tests/subworld_async_seam_test.cpp`
- `Docs/Tasks/Status_TMA_SUBWORLD_ASYNC_SEAM_BKR.md`

What was wrong:
- `SeamlessSubworldManager::check_boundary()` generated freed 3x3 slots synchronously through `generate_one()` -> `dispatch_generate()`.
- `check_boundary()` also snapshotted outgoing cells synchronously, quantizing full 1024x1024 heightmaps on the seam frame.
- `blit_into_composite()` ran full 3072x3072 `smooth_road_heights()` on the boundary path.
- Engine upload timing was not separated, so generation, smoothing, 2D upload, and 3D upload could not be attributed.

What was done:
- Added explicit `std::jthread` worker owned by `SeamlessSubworldManager`; no `std::async`.
- Worker job input is a captured `(absoluteCx, absoluteCy, CellContext, 9-neighbor height/biome/feature snapshot, saved snapshot copy)` and output is `SubworldMapData`.
- No GL calls and no composite writes occur on the worker.
- Boundary shift now moves surviving cells, inserts metadata-only flat grass/traversable placeholders into freed slots, queues 3 axis or 5 diagonal jobs, and returns.
- Completed worker jobs are drained one per frame and stitched into `composite_tiles_`, `composite_height_`, and `composite_struct_` on the main thread.
- Full composite road smoothing is retained for initial load only; seam path reports `smooth=0.000ms`.
- Outgoing real cells are moved into `pendingSaveCells_` and snapshotted outside the seam frame; `snapshot_all_to_cache()` flushes pending saves and skips placeholders deterministically.
- Engine timing logs gated by `TIMAERT_SEAM_TRACE`: `[seam-cross] gen=Xms smooth=Yms upload3d=Zms upload2d=Wms total=Tms`.

Deliberate divergences from TS:
- Native path does not port Web `Worker`, `ImageBitmap`, or `OffscreenCanvas`; it uses one `std::jthread` and main-thread GL/composite stitching.
- Placeholder cells are metadata-only plus direct composite pixels, not full in-memory generated `SubworldMapData`, to avoid seam-frame heap and heightmap fills.
- Deferred save queue mirrors TS pending save behavior but stores native `LoadedCell` values until drained.

Tests and proof:
- Direct MSVC compile passed:
  - `cl ... /c src\sub\seamless_manager.cpp`
  - `cl ... /c src\sub\engine.cpp`
- Manager seam smoke compiled and passed:
  - Debug-style compile output: `subworld_async_seam_test: ok stitched=3 gen=32.140ms smooth=0.000ms total=32.141ms`
  - Optimized `/O2` output: `subworld_async_seam_test: ok stitched=3 gen=6.499ms smooth=0.000ms total=6.500ms`
- Existing binaries passed:
  - `quest_lifecycle_test`: OK
  - `save_roundtrip_test`: OK
  - `pathfinding_parity_test`: OK

Blocked verification:
- Full `cmake --build build-msvc` cannot regenerate before compilation because concurrent unrelated files added `src/assets/character_paperdoll*.cpp` and `src/macro/audio.cpp`, and `CMakeLists.txt` now hard-fails when `SDL2_mixer` is missing.
- Because the app target did not link, no executable-level `TIMAERT_SEAM_TRACE` seam-cross log was produced from `timaert.exe`.
- No reliable before/after app seam timing exists in this run; before instrumentation did not exist and the full app build is blocked.

Remaining blockers in domain:
- Full app build and real 3D renderer upload timing must be rerun after the SDL2_mixer/CMake blocker is cleared.
- `Renderer3D::upload(mgr)` still rebuilds full mesh/road mask/tree VBO on dirty composites; timing instrumentation now exposes it, but this pass did not split renderer uploads across frames.

STATUS: PARTIAL

---

# Final Report: 2026-05-15 Verified Async Seam and Import Closure

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

TS files read:
- `C:\Timaert\src\game\subworld\seamless-manager.ts`
- `C:\Timaert\src\game\subworld\gen-worker.ts`
- `C:\Timaert\src\screens\SubworldScreen.svelte`
- `C:\Timaert\src\game\subworld\map-factory.ts`

C++ files changed:
- `src/sub/seamless_manager.h`
- `src/sub/seamless_manager.cpp`
- `src/sub/base_generator.h`
- `src/sub/base_generator.cpp`
- `src/sub/map_factory.h`
- `src/sub/map_factory.cpp`
- `src/sub/engine.cpp`
- `tests/subworld_async_seam_test.cpp`
- `CMakeLists.txt`

Exact parity gap closed:
- Seam crossings no longer synchronously generate the newly exposed 3 axis cells or 5 diagonal cells on the boundary frame.
- The manager now installs deterministic flat traversable placeholders, uses explicit `std::jthread` workers for CPU generation, and stitches completed real cells into the composite on the main thread.
- Expensive full-composite road smoothing is deferred off the boundary path and only queued when real road/square tiles exist.
- Outgoing real cells are saved through worker snapshot jobs; placeholders are skipped deterministically.
- Saved subworld restore uses immutable shared snapshots instead of copying cache payloads onto the seam frame.
- Engine seam timing is gated by `TIMAERT_SEAM_TRACE` and reports `[seam-cross] gen=Xms smooth=Yms upload3d=Zms upload2d=Wms total=Tms`.

Deliberate divergences from TS:
- Native C++ uses a persistent two-lane `std::jthread` pool instead of browser workers.
- Placeholder terrain is a deterministic visual/control fallback; it is not serialized as player-edited terrain.
- App smoke does not claim seam movement coverage because the current smoke DSL has no movement command; seam crossing is proven by the registered native seam test.

Cinematic cheats / performance actions:
- Placeholder cells buy immediate crossing response while real generation resolves asynchronously.
- Sparse road-index smoothing avoids copying/scanning the full composite tile grid for async smoothing.
- No-road composites skip smoothing entirely.
- Stale generation work from abandoned crossings is pruned before it can delay current-cell publication.
- Main-thread GL/composite uploads remain main-thread only; workers never write renderer-visible composite buffers.

Exact timings observed:
- CMake seam test latest run: road `27668us`, plain `151664us` in debug build under current machine load, diagonal `55926us`, rapid reversal `43103us`; crossing-path smoothing `0us`.
- Latest optimized focused seam proof from the direct optimized binary: road `24717us`, plain `10874us`, diagonal `79029us`, rapid reversal `13423us`; crossing-path smoothing `0us`.
- No fake microsecond saving is claimed for allocator reserve or import work; those are correctness/hitch-risk reductions.

Tests and smokes run:
- `cmake --build build-msvc --target timaert --parallel 1`: passed.
- `cmake --build build-msvc --target subworld_async_seam_test --parallel 1`: passed / no work after target registration.
- `build-msvc\subworld_async_seam_test.exe`: passed with road axis, plain axis, diagonal, rapid reversal, async smoothing publish, and pending snapshot proof.
- `build-msvc\quest_lifecycle_test.exe`: passed.
- `build-msvc\save_roundtrip_test.exe`: passed.
- `build-msvc\pathfinding_parity_test.exe`: passed.
- `build-msvc\subworld_generator_parity_test.exe`: passed.
- Runtime smoke `new_game,wait_boot_done,subworld_time,quit`: passed; output ended with `[smoke] PASS`.
- `git diff --check`: passed with CRLF normalization warnings only.

Hecton/Timaert docs import:
- Verified quarantined import root: `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`.
- Found and copied three newly-created missing Hecton AgentLogs into the quarantine.
- Added `MANIFEST_DELTA_2026-05-15_TMA_SUBWORLD_ASYNC_SEAM_BKR.tsv`.
- Selected Hecton docs/tasks/logs import scope now reports `Missing=0`.
- Import tree file count after this delta: `1707`.

Remaining blockers in this domain:
- None for the async seam domain.
- Runtime app smoke still does not physically cross a seam because no smoke-script movement command exists; this is a smoke DSL limitation, not a seam-manager blocker.

STATUS: VERIFIED

# Verification Append: 2026-05-15 Shared Build Retest

What was wrong:
- Previous current-code proof used a private build because shared `build-msvc\timaert.exe` was locked by a running process.
- Docs needed the later shared test timing, not only the private 13-test timing.

What was done:
- Re-ran shared `cmake --build build-msvc` through `VsDevCmd.bat`; it linked `timaert.exe`.
- Ran all 13 native test executables from `build-msvc`.
- Ran app smoke `new_game,wait_boot_done,wait_visible,capture_frame,quit`.
- Moved smoke image output from the repo root to `artifacts\runtime-smoke\images\verification-20260515`.

Cinematic/performance cheats used:
- None new in this retest. Existing placeholder seam cells, async stitching, and off-boundary smoothing remain unchanged.

Exact microseconds saved:
- Not claimed. This is verification. Latest shared seam-generation timings were `22451us`, `94605us`, `35042us`, and `54911us`; seam-path smoothing stayed `0us`.

Verification:
- 13-test suite: PASS.
- `subworld_async_seam_test`: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `snapshot_pending ok`, `worker_restore_saved ok`.
- App smoke: PASS, macro renderer initialized and visible framebuffer captured.
- Root artifact scan: clean after moving smoke captures.

STATUS: VERIFIED

## Documentation Audit Addendum - 2026-05-15

What was wrong -> Active README/translation timing text still cited an older `subworld_async_seam_test` timing range.

What was done -> Updated active docs to the then-current shared `build-msvc` run. This timing note is superseded by the later 2026-05-15 shared `build-msvc` rerun appended at the bottom of this file.

Cinematic Cheats used -> None. Documentation-only timing refresh.

Exact Microseconds saved -> 0 us runtime from this audit. The measured generation slices remain 23258-49168 us in this debug MSVC run.

Verification -> `build-msvc\subworld_async_seam_test.exe` exited 0 during the documentation audit pass.

---

# Continued Hardening Report: 2026-05-15

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Rapid consecutive seam crossings could leave stale pending/completed generation work for cells no longer in the active 3x3 window.
- A stale completed job at the front of the queue could consume the one-per-frame generation drain and delay a current stitch by another frame.
- Focused seam proof covered the 3-cell axis path but not the required 5-cell diagonal path.
- Full target build verification collided with another active build in `build-msvc`.

What was done:
- Added stale generation-work pruning after boundary shifts. Pending and completed generation entries must now match a current placeholder `(absolute cx, absolute cy, generation)` to stay queued.
- Changed completed generation and smoothing drains so stale front entries are discarded without counting against the current publish budget.
- Added a no-road diagonal focused smoke case. It verifies both-axis recentering, exactly five stitched cell publishes, and no async smoothing publish.
- Recompiled focused debug and optimized seam binaries into `build-msvc/codex-check`.
- Recompiled `src/sub/engine.cpp` directly with SDL/EnTT include paths.

Cinematic cheats used:
- Deterministic flat placeholders remain the immediate visual response.
- Stale queued work is discarded instead of simulated or smoothed.
- Diagonal no-road case proves the cheapest path does not queue cosmetic smoothing.

Exact microseconds:
- Latest optimized focused seam smoke: road `9271us`, plain `14157us`, diagonal `10283us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `20674us`, plain `23475us`, diagonal `44175us`, crossing-path smoothing `0us`.
- Stale-prune savings are event-dependent: one frame of current stitch latency per stale completed front entry, plus any stale pending generation CPU avoided before worker pickup.

Verification:
- Debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=20.673ms plainGen=23.474ms diagonalGen=44.174ms`.
- Optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=9.271ms plainGen=14.156ms diagonalGen=10.282ms`.
- `src/sub/engine.cpp` direct MSVC compile passed.
- `quest_lifecycle_test`, `save_roundtrip_test`, and `pathfinding_parity_test` passed.
- Anti-bloat scan on touched seam files found no `std::async`, exception use, RTTI casts, or `std::rand`.
- `git diff --check` passed with CRLF warnings only; no root object artifacts remain.

Remaining blockers:
- Full target build attempt failed from shared build-directory contention: `dispatch.cpp.obj: Permission denied` while another agent's `cmake --build build-msvc --target timaert ...` process was compiling/linking the same target.
- Runtime app seam trace remains blocked by broader app/macro build/runtime state outside this prompt domain.

STATUS: PARTIAL

---

# Verification Refresh Report: 2026-05-15

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Save snapshots were safely off the seam frame, but cosmetic smoothing still had worker priority over pending saves once generation drained.
- Composite structure rebuilds used push_back without an exact reserve during publish/stitch operations.
- The smoother public comment still described the old local stencil instead of the sparse indexed worker path.
- Workspace verification changed under concurrent agents: full target build passed once, then a later full build failed in unrelated app/UI code; app smoke also failed before subworld entry in macro road tracing.

What was done:
- Worker queue priority is now generation -> save -> smooth. This preserves seam response and moves modified outgoing cells toward cache before optional road polish.
- `blit_into_composite` and `rebuild_composite_structures` now reserve exact structure capacity before appending translated structures.
- `base_generator.h` and `base_generator.cpp` comments were cleaned to match the sparse indexed smoother.
- Focused seam smoke now covers road and no-road cases: road cells must publish 3 stitched cells plus 1 deferred smooth; no-road cells must publish only 3 stitched cells and must not queue a no-op smoother.
- Reports were updated in `Docs/Tasks/Status_TMA_SUBWORLD_ASYNC_SEAM_BKR.md` and `Docs/AgentLogs/Rationale_TMA_SUBWORLD_ASYNC_SEAM_BKR.md`.

Cinematic cheats used:
- Flat traversable placeholder cells for instant seam response.
- Worker-backed real generation with main-thread composite stitching.
- Deferred road smoothing only after placeholders are gone.
- Sparse road-index smoothing to avoid copying/rescanning the 9 MiB composite tile grid.
- No-road smoothing skip.

Exact microseconds:
- Latest optimized focused seam smoke: road `9882us`, plain `30326us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `23630us`, plain `38932us`, crossing-path smoothing `0us`.
- Full target build previously linked `timaert.exe` through `VsDevCmd.bat`; no runtime seam trace from `timaert.exe` is claimed.
- Structure reserve and save-before-smooth are hardening paths; savings depend on structure count and pending save/smooth overlap, so no fabricated constant delta is recorded.

Verification:
- `cmake --build build-msvc --target timaert --parallel 1` passed once through `VsDevCmd.bat` and linked `timaert.exe`.
- Latest direct MSVC debug and optimized focused builds passed into `build-msvc/codex-check`.
- Latest debug seam smoke passed: `roadDirty=4 plainDirty=3 roadGen=23.629ms plainGen=38.931ms`.
- Latest optimized seam smoke passed: `roadDirty=4 plainDirty=3 roadGen=9.882ms plainGen=30.325ms`.
- Existing regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- Polish scans passed after manual inspection; `git diff --check` passed with CRLF warnings only.

Remaining blockers:
- Latest full target build is externally blocked by unrelated app/UI drift: `src/app/main.cpp(2743)` calls `sm::ui::draw_show_dialog` with 3 arguments while `src/ui/overlays.h` no longer matches.
- Runtime app smoke is externally blocked before subworld entry: it exits after `[boot] trees spawned`, during macro road tracing, with dirty macro/app files outside this prompt domain.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Continued Hardening

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Rapid consecutive seam crossings could leave stale pending/completed generation work for cells no longer in the active 3x3 window.
- A stale completed job at the front of the queue could consume the one-per-frame generation drain and delay a current stitch by another frame.
- Focused seam proof covered the 3-cell axis path but not the required 5-cell diagonal path.
- Full target build verification collided with another active build in `build-msvc`.

What was done:
- Added stale generation-work pruning after boundary shifts. Pending and completed generation entries must now match a current placeholder `(absolute cx, absolute cy, generation)` to stay queued.
- Changed completed generation and smoothing drains so stale front entries are discarded without counting against the current publish budget.
- Added a no-road diagonal focused smoke case. It verifies both-axis recentering, exactly five stitched cell publishes, and no async smoothing publish.
- Recompiled focused debug and optimized seam binaries into `build-msvc/codex-check`.
- Recompiled `src/sub/engine.cpp` directly with SDL/EnTT include paths.

Cinematic cheats used:
- Deterministic flat placeholders remain the immediate visual response.
- Stale queued work is discarded instead of simulated or smoothed.
- Diagonal no-road case proves the cheapest path does not queue cosmetic smoothing.

Exact microseconds:
- Latest optimized focused seam smoke: road `9271us`, plain `14157us`, diagonal `10283us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `20674us`, plain `23475us`, diagonal `44175us`, crossing-path smoothing `0us`.
- Stale-prune savings are event-dependent: one frame of current stitch latency per stale completed front entry, plus any stale pending generation CPU avoided before worker pickup.

Verification:
- Debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=20.673ms plainGen=23.474ms diagonalGen=44.174ms`.
- Optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=9.271ms plainGen=14.156ms diagonalGen=10.282ms`.
- `src/sub/engine.cpp` direct MSVC compile passed.
- `quest_lifecycle_test`, `save_roundtrip_test`, and `pathfinding_parity_test` passed.
- Anti-bloat scan on touched seam files found no `std::async`, exception use, RTTI casts, or `std::rand`.
- `git diff --check` passed with CRLF warnings only; no root object artifacts remain.

Remaining blockers:
- Full target build attempt failed from shared build-directory contention: `dispatch.cpp.obj: Permission denied` while another agent's `cmake --build build-msvc --target timaert ...` process was compiling/linking the same target.
- Runtime app seam trace remains blocked by broader app/macro build/runtime state outside this prompt domain.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Pending Snapshot Proof

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- The focused tests proved seam stitching and smoothing, but not the worst cache timing: immediate snapshot/leave after a seam crossing while fresh generation jobs are still pending.

What was done:
- Added `snapshot_pending` to `subworld_async_seam_test`.
- The test clears the saved-subworld cache, crosses east, immediately calls `snapshot_all_to_cache()`, then asserts the outgoing real cell and a surviving real cell were saved.
- The test uses the actual public cache contract (`find_saved_subworld`) rather than private manager state.

Cinematic cheats used:
- Placeholder cells remain deterministic and skipped during snapshot.
- Real outgoing/surviving cells are persisted while generation work stays asynchronous.

Exact microseconds:
- Latest optimized focused seam smoke with snapshot proof: road `25351us`, plain `8823us`, diagonal `38133us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke with snapshot proof: road `69347us`, plain `69816us`, diagonal `74354us`, crossing-path smoothing `0us`.
- Snapshot proof is a correctness check; no speedup is claimed.

Verification:
- Debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5`, `snapshot_pending ok`.
- Optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5`, `snapshot_pending ok`.
- Existing regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.

Remaining blockers:
- Full target/runtime app seam trace remains blocked by shared build/runtime state outside this prompt domain.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Latest Combined

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was done in this continuation:
- Rechecked Hecton docs/tasks/logs/reports/artifacts for Timaert/Samosbor/TMA markers and updated `Docs/Imported/Hecton8_Timaert_Samosbor_Import_Audit.md`.
- No Hecton files were copied because no positive Timaert/Samosbor/TMA artifacts were found; broad `samos` hits were false positives.
- Added focused rapid reversal seam proof: east crossing immediately followed by west crossing must publish exactly three current-cell stitches and no stale/smoothing dirty event.
- Rebuilt and reran focused debug/optimized seam smoke.

Exact microseconds:
- Latest optimized focused seam smoke: road `10946us`, plain `12694us`, diagonal `13753us`, rapid reversal `23174us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `47236us`, plain `16124us`, diagonal `73044us`, rapid reversal `24276us`, crossing-path smoothing `0us`.

Verification:
- Debug focused seam smoke passed with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3` and `snapshot_pending ok`.
- Optimized focused seam smoke passed with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3` and `snapshot_pending ok`.
- `quest_lifecycle_test`, `save_roundtrip_test`, and `pathfinding_parity_test` passed.
- `git diff --check` passed with CRLF warnings only.
- No root `.obj` artifacts remain.

Remaining blockers:
- Full target build reached link and failed externally: `LINK : fatal error LNK1168: cannot open timaert.exe for writing`.
- Process list showed `build-msvc\timaert.exe` running as PID `51040`; it was not terminated.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Verified Async Seam and Import Closure

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was done:
- Registered `tests/subworld_async_seam_test.cpp` as a normal CMake target.
- Verified worker-backed seam generation, deterministic placeholders, main-thread stitching, stale-job pruning, deferred smoothing, and pending snapshot persistence.
- Re-ran the full native app target and runtime subworld smoke after earlier external build/runtime blockers cleared.
- Verified the Hecton docs/tasks/logs import quarantine, copied three newly-created missing Hecton AgentLogs, and recorded the delta manifest.

Exact timings:
- CMake seam test latest run: road `27668us`, plain `151664us` in debug build under current machine load, diagonal `55926us`, rapid reversal `43103us`, crossing-path smoothing `0us`.
- Latest optimized focused seam proof: road `24717us`, plain `10874us`, diagonal `79029us`, rapid reversal `13423us`, crossing-path smoothing `0us`.

Verification:
- `cmake --build build-msvc --target timaert --parallel 1`: passed.
- `cmake --build build-msvc --target subworld_async_seam_test --parallel 1`: passed / no work after registration.
- `build-msvc\subworld_async_seam_test.exe`: passed with road axis, plain axis, diagonal, rapid reversal, async smoothing publish, and pending snapshot proof.
- `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, and `subworld_generator_parity_test`: passed.
- Runtime smoke `new_game,wait_boot_done,subworld_time,quit`: passed with `[smoke] PASS`.
- Hecton import selected-scope missing files: `0`; import tree file count: `1707`.
- `git diff --check`: passed with CRLF normalization warnings only.

Remaining blockers:
- None in the async seam domain.
- App smoke still does not physically cross a seam because the current smoke DSL has no movement command; seam crossing is covered by the registered native seam test.

STATUS: VERIFIED

---

# Verification Append: 2026-05-15 Saved-Cache Lifetime Fix

What was wrong -> Repeated `subworld_async_seam_test` runs could exit with `-1` after `snapshot_pending ok` or `worker_restore_saved ok`. The failure reproduced in the async seam test lifecycle, not in feature-layer generation. The pending-snapshot and worker-restore test cases cleared the global saved-subworld cache while a local `SeamlessSubworldManager` instance was still alive.

What was done -> Scoped those local managers so `clear_saved_subworlds()` runs only after manager destruction joins its `std::jthread` workers. Production manager behavior is unchanged; the test now respects the worker/cache lifetime contract.

Cinematic Cheats used -> None. This is deterministic worker/cache lifecycle hygiene.

Exact Microseconds saved -> Not claimed. Verification evidence: focused target rebuild passed; 5/5 focused stability loop runs exited 0; private current-code 13-test suite exited 0. Previous private 13-test slices were road 28.886 ms, plain 25.606 ms, diagonal 50.717 ms, rapid reversal 56.236 ms, with `snapshot_pending ok` and `worker_restore_saved ok`; this is superseded by the later shared build retest append.

STATUS: VERIFIED

---

# Final Bottom Report: 2026-05-15 Hecton Import Request

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was requested:
- Transfer all Timaert/Samosbor docs, tasks, and logs from the Hecton folder into the Timaert folder.

What was done:
- Rechecked Hecton docs/tasks/logs/reports/artifact folders by path and content.
- Search scope included `C:\hades\Hecton8\Docs\Tasks`, `Docs\AgentLogs`, `Docs\Reports`, `Logs`, `CodexArtifacts`, and `.codex-artifacts`.
- Updated `C:\Timaert\timaert_c\Docs\Imported\Hecton8_Timaert_Samosbor_Import_Audit.md`.

Result:
- No positive Timaert/Samosbor/TMA artifacts were found in Hecton.
- Broad `samos` content hits were false positives from transliterated Russian words such as `samostoyatelno`.
- No files were copied because importing unrelated Hecton logs would contaminate the Timaert documentation set.

Destination reserved:
- `C:\Timaert\timaert_c\Docs\Imported`

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Rapid Reversal Proof

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Stale-prune behavior was implemented and partially covered, but there was no dynamic proof for a player reversing across a seam before previous generation work completed.

What was done:
- Added `rapid_reversal` to `subworld_async_seam_test`.
- The test crosses east, immediately crosses west, then requires exactly three current-cell stitch publishes and no abandoned seam/smoothing dirty event.
- Rebuilt debug and optimized focused seam binaries into `build-msvc/codex-check`.

Exact microseconds:
- Latest optimized focused seam smoke: road `10946us`, plain `12694us`, diagonal `13753us`, rapid reversal `23174us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `47236us`, plain `16124us`, diagonal `73044us`, rapid reversal `24276us`, crossing-path smoothing `0us`.

Verification:
- Debug focused seam smoke passed with `reversalDirty=3` and `snapshot_pending ok`.
- Optimized focused seam smoke passed with `reversalDirty=3` and `snapshot_pending ok`.
- Existing regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- Full target build compiled to link, then failed externally with `LINK : fatal error LNK1168: cannot open timaert.exe for writing`; `timaert.exe` PID `51040` was running from `build-msvc`.

STATUS: PARTIAL

---

# Final Hardening Pass: 2026-05-15

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Shutdown could let workers finish save snapshots and then clear `completedSaveJobs_` without committing them to the map-factory cache.
- Async composite smoothing was queued even when the 3x3 composite had no `TILE_ROAD` or `TILE_SQUARE`, wasting a 9 MiB tile copy, 36 MiB height copy, and smoother scan.
- Async generation still deep-copied a saved edited-cell snapshot into `GenerationJob` on the seam frame when a cached saved subworld existed.

What was done:
- `shutdown_worker()` now drops stale pending generation/smooth work first, lets active CPU-only jobs finish, commits completed save snapshots, then clears queues.
- `LoadedCell` and `CompletedJob` now carry `roadTiles`. Counts are computed after generation/restore, placeholders report zero, and composite smoothing is skipped when all loaded real cells have zero road/square tiles.
- `map_factory` now stores saved snapshots as immutable `std::shared_ptr<const SavedSubworld>` behind a mutex. `queue_generation()` captures the shared snapshot reference instead of copying the saved heightmap/structure payload.
- Added `find_saved_subworld_ref()` while preserving the old raw-pointer API with a thread-local pin for compatibility.
- Created `Docs/AgentLogs/Rationale_TMA_SUBWORLD_ASYNC_SEAM_BKR.md` and documented the decisions.

Cinematic cheats used:
- Flat deterministic grass placeholders buy immediate seam response while real cells generate off-thread.
- Full road smoothing is delayed until real cells replace placeholders, so the crossing frame never pays road smoothing.
- No-road composites skip the smoothing illusion completely because there is no road visual to improve.

Measured proof:
- Direct MSVC compile passed after the hardening changes:
  - `map_factory.cpp`
  - `seamless_manager.cpp`
  - `engine.cpp`
- Debug focused seam smoke rebuilt and passed:
  - `subworld_async_seam_test: ok dirty=4 gen=66.176ms smooth=0.000ms total=66.177ms`
  - This run was contaminated by active concurrent CMake/MSVC builds.
- Optimized focused seam smoke rebuilt and passed:
  - `subworld_async_seam_test: ok dirty=4 gen=11.295ms smooth=0.000ms total=11.296ms`
- Existing binaries passed:
  - `quest_lifecycle_test` OK
  - `save_roundtrip_test` OK
  - `pathfinding_parity_test` OK
- Polish scans were run. Touched-file hits were legitimate test strings, template syntax, an unrelated `attempt` loop in `engine.cpp`, and an existing `prefix` comment.
- `git diff --check` passed for touched/report files; only CRLF normalization warnings were emitted.

Exact microseconds:
- Current optimized focused seam path: `11296us` total, `0us` smoothing on the crossing path.
- Current debug focused seam path under concurrent build load: `66177us` total, `0us` smoothing.
- New no-road smoothing skip did not trigger in the focused smoke because that test intentionally uses road cells; measured savings in that scenario are `0us`.
- New shared saved-snapshot reference removes a saved-heightmap deep copy only when revisiting cached edited cells; this path was compiled but not separately microbenchmarked, so no fabricated microsecond claim is made.

Blocked verification:
- Full `cmake --build build-msvc` remains an unreliable proof channel in this workspace because concurrent agents are actively building and the known unrelated SDL/audio state can block or mutate the app target. I did not run a final full app rebuild.
- No executable-level `TIMAERT_SEAM_TRACE` runtime log from `timaert.exe` was produced in this pass.

STATUS: PARTIAL

---

# Continuation: 2026-05-15

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

Additional review:
- Re-read `Docs/Tasks/Status_TMA_SUBWORLD_ASYNC_SEAM_BKR.md`.
- Re-read this log.
- Re-extracted own `<AGENT_PROMPT>` from `TIMAERT BATCH.md`.
- Rechecked `src/sub/seamless_manager.*`, `src/sub/map_factory.*`, `src/sub/engine.*`, `src/sub/renderer_3d.h`, and `tests/subworld_async_seam_test.cpp`.

What was still weak:
- Outgoing cells were moved off the seam frame, but their heightmap quantization still ran later on the main thread. That could produce a delayed hitch after crossing.
- Full-composite road smoothing was removed from the boundary path but not yet restored asynchronously. That risked visual road-height kinks after placeholder replacement.
- The test proved worker stitching, but not the later async smoothing publish.

What was done:
- Added worker-side save jobs in `SeamlessSubworldManager`. Outgoing `LoadedCell` data is moved into `SaveJob`; the worker runs `snapshot_subworld`; the main thread only commits completed snapshots.
- Added `store_saved_subworld(SavedSubworld&&)` in `map_factory` to move completed snapshots into the cache instead of copying vector payloads.
- Added async full-composite smoothing jobs. Once all placeholders are replaced, the manager queues a `SmoothJob` with composite copies; the worker runs `smooth_road_heights`; main thread swaps the completed heightmap and marks the composite dirty.
- Crossing a new seam cancels stale pending smooth jobs by generation id. Completed stale smooth jobs are ignored.
- `snapshot_all_to_cache()` cancels stale smoothing, skips placeholders deterministically, queues real cells for worker snapshot, and flushes save jobs before return.
- `subworld_async_seam_test` now requires four dirty publishes after the initial seam: three cell stitches plus one async composite smoothing publish.
- Worker shutdown now exits only when queues are empty. This prevents pending save jobs from being dropped if shutdown is requested immediately after an in-flight generation job completes.

Measured proof:
- Optimized smoke:
  - `subworld_async_seam_test: ok dirty=4 gen=10.002ms smooth=0.000ms total=10.002ms`
- Direct MSVC compile:
  - `cl ... /c src\sub\seamless_manager.cpp` OK
  - `cl ... /c src\sub\map_factory.cpp` OK
  - `cl ... /c src\sub\engine.cpp` OK
- Existing binary tests:
  - `quest_lifecycle_test` OK
  - `save_roundtrip_test` OK
  - `pathfinding_parity_test` OK

Remaining blocker:
- Full app build is still not a valid verification channel until the unrelated `SDL2_mixer` CMake blocker from concurrent audio work is cleared. No `timaert.exe` seam-cross runtime log was produced in this continuation.

STATUS: PARTIAL

---

# Canonical Bottom Report: 2026-05-15

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Worker shutdown could clear completed save snapshots without committing them.
- Async road smoothing could still queue for composites with no roads or squares.
- Re-entering edited cells could deep-copy saved subworld snapshots into generation jobs on the seam frame.

What was done:
- Shutdown now drops stale generation/smooth queues, commits completed saves, then clears queues.
- Added per-cell road tile counts and skip async smoothing when no loaded real cell has `TILE_ROAD` or `TILE_SQUARE`.
- Saved subworld cache now stores immutable shared snapshots under a mutex. Generation jobs capture a `shared_ptr<const SavedSubworld>` instead of copying height/structure vectors.
- Rationale file was created and updated.
- Status file was updated.

Cinematic cheats used:
- Deterministic flat grass placeholders for immediate seam response.
- Worker-backed real-cell generation and later main-thread stitching.
- Deferred road smoothing after placeholders are gone.
- No-road smoothing skip.

Proof:
- Direct MSVC compile passed for `map_factory.cpp`, `seamless_manager.cpp`, and `engine.cpp`.
- Debug focused seam smoke rebuilt and passed: `dirty=4 gen=66.176ms smooth=0.000ms total=66.177ms`.
- Optimized focused seam smoke rebuilt and passed: `dirty=4 gen=11.295ms smooth=0.000ms total=11.296ms`.
- `quest_lifecycle_test`, `save_roundtrip_test`, and `pathfinding_parity_test` passed.
- Polish scans and `git diff --check` passed; only CRLF warnings from Git were emitted.

Exact microseconds:
- Optimized focused seam path: `11296us` total, `0us` crossing-path smoothing.
- Debug focused seam path under concurrent build load: `66177us` total, `0us` crossing-path smoothing.
- No-road smoothing skip and saved-snapshot reference are compiled hardening paths; the focused road-cell smoke does not exercise their savings, so no fake microsecond delta is claimed.

Blocked:
- Full app CMake build and runtime `timaert.exe` seam trace remain unverified because the workspace is under concurrent builds and unrelated SDL/audio build state is still a blocker/noise source.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Verification Refresh

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Save snapshots were safely off the seam frame, but cosmetic smoothing still had worker priority over pending saves once generation drained.
- Composite structure rebuilds used push_back without an exact reserve during publish/stitch operations.
- The smoother public comment still described the old local stencil instead of the sparse indexed worker path.
- Workspace verification changed under concurrent agents: full target build passed once, then a later full build failed in unrelated app/UI code; app smoke also failed before subworld entry in macro road tracing.

What was done:
- Worker queue priority is now generation -> save -> smooth. This preserves seam response and moves modified outgoing cells toward cache before optional road polish.
- `blit_into_composite` and `rebuild_composite_structures` now reserve exact structure capacity before appending translated structures.
- `base_generator.h` and `base_generator.cpp` comments were cleaned to match the sparse indexed smoother.
- Focused seam smoke covers road and no-road cases: road cells must publish 3 stitched cells plus 1 deferred smooth; no-road cells must publish only 3 stitched cells and must not queue a no-op smoother.
- Reports were updated in `Docs/Tasks/Status_TMA_SUBWORLD_ASYNC_SEAM_BKR.md` and `Docs/AgentLogs/Rationale_TMA_SUBWORLD_ASYNC_SEAM_BKR.md`.

Cinematic cheats used:
- Flat traversable placeholder cells for instant seam response.
- Worker-backed real generation with main-thread composite stitching.
- Deferred road smoothing only after placeholders are gone.
- Sparse road-index smoothing to avoid copying/rescanning the 9 MiB composite tile grid.
- No-road smoothing skip.

Exact microseconds:
- Latest optimized focused seam smoke: road `9882us`, plain `30326us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `23630us`, plain `38932us`, crossing-path smoothing `0us`.
- Full target build previously linked `timaert.exe` through `VsDevCmd.bat`; no runtime seam trace from `timaert.exe` is claimed.
- Structure reserve and save-before-smooth are hardening paths; savings depend on structure count and pending save/smooth overlap, so no fabricated constant delta is recorded.

Verification:
- `cmake --build build-msvc --target timaert --parallel 1` passed once through `VsDevCmd.bat` and linked `timaert.exe`.
- Latest direct MSVC debug and optimized focused builds passed into `build-msvc/codex-check`.
- Latest debug seam smoke passed: `roadDirty=4 plainDirty=3 roadGen=23.629ms plainGen=38.931ms`.
- Latest optimized seam smoke passed: `roadDirty=4 plainDirty=3 roadGen=9.882ms plainGen=30.325ms`.
- Existing regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.
- Polish scans passed after manual inspection; `git diff --check` passed with CRLF warnings only.

Remaining blockers:
- Latest full target build is externally blocked by unrelated app/UI drift: `src/app/main.cpp(2743)` calls `sm::ui::draw_show_dialog` with 3 arguments while `src/ui/overlays.h` no longer matches.
- Runtime app smoke is externally blocked before subworld entry: it exits after `[boot] trees spawned`, during macro road tracing, with dirty macro/app files outside this prompt domain.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Continued Hardening

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- Rapid consecutive seam crossings could leave stale pending/completed generation work for cells no longer in the active 3x3 window.
- A stale completed job at the front of the queue could consume the one-per-frame generation drain and delay a current stitch by another frame.
- Focused seam proof covered the 3-cell axis path but not the required 5-cell diagonal path.
- Full target build verification collided with another active build in `build-msvc`.

What was done:
- Added stale generation-work pruning after boundary shifts. Pending and completed generation entries must now match a current placeholder `(absolute cx, absolute cy, generation)` to stay queued.
- Changed completed generation and smoothing drains so stale front entries are discarded without counting against the current publish budget.
- Added a no-road diagonal focused smoke case. It verifies both-axis recentering, exactly five stitched cell publishes, and no async smoothing publish.
- Recompiled focused debug and optimized seam binaries into `build-msvc/codex-check`.
- Recompiled `src/sub/engine.cpp` directly with SDL/EnTT include paths.

Cinematic cheats used:
- Deterministic flat placeholders remain the immediate visual response.
- Stale queued work is discarded instead of simulated or smoothed.
- Diagonal no-road case proves the cheapest path does not queue cosmetic smoothing.

Exact microseconds:
- Latest optimized focused seam smoke: road `9271us`, plain `14157us`, diagonal `10283us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `20674us`, plain `23475us`, diagonal `44175us`, crossing-path smoothing `0us`.
- Stale-prune savings are event-dependent: one frame of current stitch latency per stale completed front entry, plus any stale pending generation CPU avoided before worker pickup.

Verification:
- Debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=20.673ms plainGen=23.474ms diagonalGen=44.174ms`.
- Optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5 roadGen=9.271ms plainGen=14.156ms diagonalGen=10.282ms`.
- `src/sub/engine.cpp` direct MSVC compile passed.
- `quest_lifecycle_test`, `save_roundtrip_test`, and `pathfinding_parity_test` passed.
- Anti-bloat scan on touched seam files found no `std::async`, exception use, RTTI casts, or `std::rand`.
- `git diff --check` passed with CRLF warnings only; no root object artifacts remain.

Remaining blockers:
- Full target build attempt failed from shared build-directory contention: `dispatch.cpp.obj: Permission denied` while another agent's `cmake --build build-msvc --target timaert ...` process was compiling/linking the same target.
- Runtime app seam trace remains blocked by broader app/macro build/runtime state outside this prompt domain.

STATUS: PARTIAL
---

# Final Bottom Report: 2026-05-15 Pending Snapshot Proof

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was wrong:
- The focused tests proved seam stitching and smoothing, but not the worst cache timing: immediate snapshot/leave after a seam crossing while fresh generation jobs are still pending.

What was done:
- Added `snapshot_pending` to `subworld_async_seam_test`.
- The test clears the saved-subworld cache, crosses east, immediately calls `snapshot_all_to_cache()`, then asserts the outgoing real cell and a surviving real cell were saved.
- The test uses the actual public cache contract (`find_saved_subworld`) rather than private manager state.

Cinematic cheats used:
- Placeholder cells remain deterministic and skipped during snapshot.
- Real outgoing/surviving cells are persisted while generation work stays asynchronous.

Exact microseconds:
- Latest optimized focused seam smoke with snapshot proof: road `25351us`, plain `8823us`, diagonal `38133us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke with snapshot proof: road `69347us`, plain `69816us`, diagonal `74354us`, crossing-path smoothing `0us`.
- Snapshot proof is a correctness check; no speedup is claimed.

Verification:
- Debug focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5`, `snapshot_pending ok`.
- Optimized focused seam smoke passed: `roadDirty=4 plainDirty=3 diagonalDirty=5`, `snapshot_pending ok`.
- Existing regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`.

Remaining blockers:
- Full target/runtime app seam trace remains blocked by shared build/runtime state outside this prompt domain.

STATUS: PARTIAL

---

# Final Bottom Report: 2026-05-15 Latest Combined Physical Append

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was done in this continuation:
- Rechecked Hecton docs/tasks/logs/reports/artifacts for Timaert/Samosbor/TMA markers and updated `Docs/Imported/Hecton8_Timaert_Samosbor_Import_Audit.md`.
- No Hecton files were copied because no positive Timaert/Samosbor/TMA artifacts were found; broad `samos` hits were false positives.
- Added focused rapid reversal seam proof: east crossing immediately followed by west crossing must publish exactly three current-cell stitches and no stale/smoothing dirty event.
- Rebuilt and reran focused debug/optimized seam smoke.

Exact microseconds:
- Latest optimized focused seam smoke: road `10946us`, plain `12694us`, diagonal `13753us`, rapid reversal `23174us`, crossing-path smoothing `0us`.
- Latest debug focused seam smoke: road `47236us`, plain `16124us`, diagonal `73044us`, rapid reversal `24276us`, crossing-path smoothing `0us`.

Verification:
- Debug focused seam smoke passed with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3` and `snapshot_pending ok`.
- Optimized focused seam smoke passed with `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3` and `snapshot_pending ok`.
- `quest_lifecycle_test`, `save_roundtrip_test`, and `pathfinding_parity_test` passed.
- `git diff --check` passed with CRLF warnings only.
- No root `.obj` artifacts remain.

Remaining blockers:
- Full target build reached link and failed externally: `LINK : fatal error LNK1168: cannot open timaert.exe for writing`.
- Process list showed `build-msvc\timaert.exe` running as PID `51040`; it was not terminated.

STATUS: PARTIAL
---

# Final Physical Append: 2026-05-15 Verified Async Seam and Import Closure

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

Summary:
- Full native target passed: `cmake --build build-msvc --target timaert --parallel 1`.
- CMake seam target is registered and `build-msvc\subworld_async_seam_test.exe` passed road axis, plain axis, diagonal, rapid reversal, async smoothing publish, and pending snapshot proof.
- Regression tests passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- Runtime smoke passed: `new_game,wait_boot_done,subworld_time,quit` ended with `[smoke] PASS`.
- Hecton import quarantine was refreshed with seven missing AgentLogs artifacts; selected-scope missing files were brought back to `0` at last copy, import tree file count is `1711`.

Remaining domain blockers:
- None for async seam generation.
- App smoke DSL still cannot physically cross a seam; seam crossing is covered by the registered native seam test.

STATUS: VERIFIED

---

# Final Physical Append: 2026-05-15 Post-Verified Hardening Pass

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was upgraded:
- Removed stale worker-job fields `GenerationJob::targetIdx` and `CompletedJob::genMs`.
- Added direct placeholder assertions to `subworld_async_seam_test`: newly exposed axis and diagonal slots must be flat grass at `WATER_LEVEL + kLandMargin` before async generation stitches real cells.
- Refreshed the quarantined Hecton import again; this prompt's delta manifest now has `13` copied artifacts and the selected Hecton docs/root scope was missing `0` files at verification.

Verification:
- Rebuilt CMake seam target: passed.
- `build-msvc\subworld_async_seam_test.exe`: passed with road axis, plain axis, diagonal, rapid reversal, async smoothing publish, pending snapshot proof, and placeholder proof.
- `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`: passed.
- Shared `build-msvc` full link was externally blocked by active runtime PID `66344`, so full app verification moved to private tree.
- Private full app build passed: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`.
- Private runtime smoke passed: `build-msvc-subworld-check\timaert.exe` with `new_game,wait_boot_done,subworld_time,quit` ended with `[smoke] PASS`.
- Anti-bloat scan and `git diff --check`: clean except CRLF normalization warnings.

Import state:
- Hecton `Docs` selected source files: `1879`; imported docs files: `1879`.
- Hecton root selected source files: `23`; imported root files: `23`.
- Import tree total files: `1913`.

Remaining blockers:
- None in the async seam domain.
- Shared `build-msvc\timaert.exe` was still running externally during this pass; private build/smoke was used to avoid interfering with it.

STATUS: VERIFIED
---

# Final Physical Append: 2026-05-15 Saved-Restore Seam Proof

Prompt ID and domain:
TMA_SUBWORLD_ASYNC_SEAM_BKR / SUBWORLD_PERFORMANCE_ARCHITECT.

What was upgraded:
- Added `worker_restore_saved` to `subworld_async_seam_test`.
- The test stores a saved quantized heightmap for a newly exposed seam cell, crosses east, verifies the freed slot starts as the deterministic flat placeholder, then requires the async worker stitch to restore the saved height.
- This closes the worker restore proof, not just the pending snapshot save proof.

Verification:
- Shared `build-msvc` seam rebuild was externally blocked by object permission contention, so the focused proof was rebuilt in private `build-msvc-subworld-check`.
- `build-msvc-subworld-check\subworld_async_seam_test.exe`: passed with road axis, plain axis, diagonal, rapid reversal, async smoothing publish, pending snapshot proof, placeholder proof, and `worker_restore_saved ok`.
- Private full app target passed: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`.
- Private runtime smoke passed: `build-msvc-subworld-check\timaert.exe` with `new_game,wait_boot_done,subworld_time,quit` ended with `[smoke] PASS`.
- Anti-bloat scan and `git diff --check`: clean except CRLF normalization warnings.

Import state:
- Latest prompt delta copied 9 more fresh Hecton artifacts.
- Prompt delta manifest rows: `22`.
- Hecton `Docs` selected source files: `1913`; selected missing files: `0`.
- Hecton root selected source files: `23`; root missing files: `0`.
- Import tree total files: `2301`.

Remaining blockers:
- None in the async seam domain.
- Shared `build-msvc`/`build-msvc-roadriver` executables were running externally; private build/smoke was used to avoid interfering with them.

STATUS: VERIFIED

---

# Verification Append: 2026-05-15 Shared build-msvc Rerun

What was wrong -> Active docs still pointed at the previous async seam timing range after the latest shared `build-msvc` full test run.

What was done -> Re-ran `build-msvc\subworld_async_seam_test.exe` as part of the full 13-test suite. Current worker-generation slices: road axis 39.142 ms, plain axis 9.768 ms, diagonal 41.913 ms, rapid reversal 27.749 ms. `snapshot_pending ok` and `worker_restore_saved ok` both passed.

Cinematic Cheats used -> None; this is seam worker scheduling and saved-cell restoration proof.

Exact Microseconds saved -> Not claimed. Latest measured generation range is 9,768-41,913 microseconds for isolated worker slices on this machine.

STATUS: VERIFIED

# Final Physical Append: 2026-05-15 Runtime Seam Upload Proof

What was wrong:
- Manager-level async seam proof existed, but app runtime upload timing was not exercised by a real seam-crossing smoke.
- First app seam proof exposed a true double-upload hitch: active 3D and inactive 2D renderers both uploaded on the seam frame (`upload3d=1949.772ms`, `upload2d=1091.408ms`, `total=3308.131ms` in private Debug MSVC smoke).
- Saved snapshot worker restore proved heightmap restore but not structure merge into the shifted composite.
- Hecton import quarantine had drifted again while external Hecton logs changed.

What was done:
- Added `subworld_seam` smoke action and kept timing behind `TIMAERT_SEAM_TRACE`.
- Added saved `Structure::Bridge` restore proof to `subworld_async_seam_test`.
- Removed stray production `debug smooth roadIdx` stderr.
- Deferred inactive renderer upload in `SubworldEngine`; only the visible renderer uploads on the seam frame.
- Optimized 3D upload by using a 1x1 zero road mask for no-road composites and making terrain indices static after init.
- Reverted a worse 2D R16 upload experiment after measurement.
- Updated `translation.md`, `README.md`, and `matwej.md` with verified Timaert evidence only.
- Refreshed Hecton docs/tasks/logs quarantine in Timaert: `2020` Docs files + `26` root files selected, `31` updated, `0` missing after copy, `3063` import files total.

Cinematic/performance cheats used:
- Placeholder cells remain deterministic flat traversable grass so seam response is immediate while workers build rich cells.
- Cosmetic smoothing is worker-side and skipped for no-road composites.
- Inactive renderer upload is delayed until it can actually be seen.
- No-road 3D road mask collapses to a 1x1 zero texture instead of uploading a blank 3072x3072 mask.

Exact measured evidence:
- Latest private Debug 3D seam smoke: `[seam-cross] gen=32.052ms smooth=0.000ms upload3d=176.381ms upload2d=0.000ms total=208.577ms`, `[smoke] PASS`.
- Focused seam test: `roadDirty=4 plainDirty=3 diagonalDirty=5 reversalDirty=3`, `snapshot_pending ok`, `worker_restore_saved ok`.
- Regression tests passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- Runtime smoke passed: `subworld_time` with 1000 subworld frames and exact 1440-minute advance.

Microseconds saved:
- Debug smoke total seam frame reduced from `3308131us` to `208577us` against the first real app seam measurement: `3099554us` saved on that measured path.
- Inactive 2D upload on default 3D seam reduced from `1091408us` to `0us` on the seam frame.
- Boundary smoothing remains `0us` on the crossing path.

STATUS: VERIFIED

# Final Physical Append: 2026-05-15 Sparse Road-Mask Upload Hardening

What was wrong:
- Worker queue priority in code still allowed cosmetic smoothing to outrank save snapshots after generation drained.
- `Renderer3D::upload` avoided no-road full-mask upload, but road composites still scanned the full 3072x3072 tile field to rebuild an R8 road mask.
- A speculative GL storage/sub-update path looked reasonable but was not proven.

What was done:
- Reordered worker priority to generation -> save snapshots -> cosmetic smoothing.
- Added exact sparse `TILE_ROAD` mask metadata to `SeamlessSubworldManager`, collected in the same tile pass as smoothing road/square indices.
- Updated `Renderer3D` to build road masks from sparse manager indices, keep the 1x1 no-road mask path, and reuse CPU scratch buffers across uploads.
- Strengthened `subworld_async_seam_test` with sparse road-mask proof.
- Tested and reverted the GL sub-update/storage-retention trial because it regressed measured upload time.
- Refreshed the Hecton import quarantine under Timaert only; no files were written to Hecton.

Cinematic/performance cheats used:
- Placeholder flat traversable cells still buy immediate seam response while worker jobs finish.
- No-road composites use a 1x1 zero road mask instead of a 9 MiB blank texture.
- Sparse road-mask metadata avoids rereading the whole composite tile field for a binary overlay.
- Cosmetic road smoothing remains behind generation/save work and off the boundary path.

Verification:
- Private native build passed through `VsDevCmd.bat`: `subworld_async_seam_test` and `timaert`; no dotnet rebuilds.
- `subworld_async_seam_test` passed: road/plain/diagonal/reversal, placeholder, snapshot, saved height/structure restore, sparse road-mask proof; seam-path smoothing `0.000ms`.
- Runtime `subworld_seam` smoke passed with `[seam-cross] gen=53.097ms smooth=0.000ms upload3d=111.769ms upload2d=0.000ms total=165.049ms`.
- Runtime `subworld_time` smoke passed; regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- Anti-bloat scan clean; `git diff --check` clean except CRLF normalization warnings.

Exact microseconds saved:
- Versus previous accepted private Debug seam smoke: active 3D upload moved from `176381us` to `111769us`, saving `64612us` on that measured path.
- Total measured seam smoke moved from `208577us` to `165049us`, saving `43528us` despite generation timing noise.
- Versus the first double-upload fault, total measured seam smoke moved from `3308131us` to `165049us`, saving `3143082us` on the corrected 3D path.
- Rejected GL sub-update trial avoided a measured regression from `111769us` to `331363us` upload, i.e. `219594us` worse.

STATUS: VERIFIED

# Final Physical Append: 2026-05-15 Conservative 1536 Road-Mask Upload Pass

What was wrong:
- The sparse road-mask pass removed the full tile scan but still uploaded a 3072x3072 R8 mask on road composites.
- The measured residual seam cost was still active 3D upload, not generation or smoothing.

What was done:
- Confirmed native roads are stamped with a 5-tile footprint.
- Changed road composites to upload a 1536x1536 R8 road mask from sparse road indices.
- Kept no-road composites on the 1x1 zero road-mask path.
- Updated Timaert docs only and refreshed the Hecton quarantine import without writing to Hecton.

Cinematic/performance cheats used:
- 2:1 binary road-mask downsample: keeps road readability with linear filtering while buying bandwidth back for terrain/instance visuals.
- Placeholder seam cells and worker stitching remain unchanged.
- Async road smoothing remains off the seam frame.

Verification:
- Private MSVC app target rebuilt through `VsDevCmd.bat`; no dotnet rebuilds.
- Runtime `subworld_seam` smoke passed with `[seam-cross] gen=23.032ms smooth=0.000ms upload3d=69.636ms upload2d=0.000ms total=92.813ms`.
- `subworld_async_seam_test` passed: road/plain/diagonal/reversal, snapshot, saved restore, road-mask proof, smoothing `0.000ms`.
- Runtime `subworld_time` smoke passed.
- Regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- Anti-bloat scan clean; `git diff --check` clean except CRLF normalization warnings.

Exact microseconds saved:
- Versus previous accepted sparse-mask smoke: active 3D upload moved from `111769us` to `69636us`, saving `42133us`.
- Total measured seam smoke moved from `165049us` to `92813us`, saving `72236us`.
- Versus the first double-upload fault, total measured seam smoke moved from `3308131us` to `92813us`, saving `3215318us` on the corrected 3D path.

STATUS: VERIFIED

# Final Physical Append: 2026-05-15 1024 Road-Mask Dilation Pass

What was wrong:
- The prior accepted road-composite upload used a 1536x1536 R8 mask. It was much better than the original 3072x3072 mask, but it still uploaded 2.25 MiB of road overlay data on each road seam.

What was done:
- `Renderer3D::upload` now uses a 1024x1024 R8 road mask for road composites.
- Sparse `TILE_ROAD` indices still come from `SeamlessSubworldManager`; the renderer no longer scans the full 3072x3072 tile field.
- Each sparse road mask point writes a 3x3 mask neighborhood, preserving readability for the native 5-tile road footprint at a 3:1 mask scale.
- No-road composites still use the 1x1 zero road mask.
- Timaert root docs were updated only in `README.md`, `matwej.md`, and `translation.md`.
- Hecton docs/tasks/logs were refreshed only into the Timaert quarantine folder.

Cinematic cheats used:
- 3:1 road-mask downsample with one-pixel dilation. The road geometry remains native; the binary visual overlay spends fewer upload bytes and relies on filtered/dilated mask coverage.

Exact microseconds saved:
- Prior accepted Debug seam smoke: `upload3d=69.636ms`, `total=92.813ms`.
- Final rebuilt Debug seam smoke: `upload3d=51.785ms`, `total=74.603ms`.
- Saved active 3D upload: `17851us`.
- Saved total measured seam time: `18210us`.

Verification:
- Private MSVC app rebuild passed through `VsDevCmd.bat`: `cmake --build build-msvc-subworld-check --target timaert --parallel 1`.
- Final `subworld_seam` smoke passed: `[seam-cross] gen=22.695ms smooth=0.000ms upload3d=51.785ms upload2d=0.000ms total=74.603ms`.
- Focused `subworld_async_seam_test` passed after the 1024 path: road/plain/diagonal/reversal, `snapshot_pending ok`, `worker_restore_saved ok`, sparse road-mask proof, smoothing `0.000ms`.
- `subworld_time` smoke passed on seed 42.
- Regression binaries passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- Hecton quarantine refresh: selected `Docs=2228`, selected root docs `3`, copied missing `4`, updated changed `1`, selected missing after copy `0`, import tree files `3335`.
- Anti-bloat scan clean; `git diff --check` clean except LF-to-CRLF normalization warnings.

STATUS: VERIFIED

# Final Physical Append: 2026-05-15 Finish Pass After Continuation

STATUS: VERIFIED.

What was wrong:
- The final continuation had stale ledger numbers and needed a current-source rebuild proof. Source mtimes for `src/sub/gens/dispatch.cpp` and `tests/subworld_async_seam_test.cpp` were newer than the private executables, so old smoke/test timings could not be treated as final evidence.
- A terrain-payload shader-grid optimization looked cheaper but measured worse: `upload3d=63.248ms`, `total=93.941ms` versus the prior accepted 1024-mask best `upload3d=51.785ms`, `total=74.603ms`.
- The focused water-plane invariant could be contaminated by saved-cell cache state from earlier cases.
- Full app link/compile had two practical blockers: old macro spawner call shapes needed ABI wrappers, and `gen_spire` needed the sanitized neighbor feature array instead of an undeclared `nbFeature`.
- Hecton documentation/logs had drifted again and needed a Timaert-only quarantine refresh without writing Timaert reports to Hecton.

What was done:
- Reverted the measured-worse terrain-payload shader-grid trial and kept the accepted `Renderer3D` terrain vertex path with 1024 R8 road-mask dilation.
- Added ABI-compatible `spawn_trees` and `build_feature_layer` wrappers in `src/macro/spawners.{h,cpp}`, forwarding old call shapes to sea-level-aware implementations with `seaLevel=0.40f`.
- Threaded the sanitized 3x3 neighbor feature array into `gen_spire` in `src/sub/gens/dispatch.cpp`.
- Hardened `tests/subworld_async_seam_test.cpp` by clearing saved subworld cache around the water-plane invariant case.
- Updated `README.md`, `matwej.md`, and `translation.md` with fresh current-source timing and water-plane evidence.
- Refreshed the Hecton import quarantine under `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` only. Selected Hecton `Docs`: `2372`; root files: `3`; copied missing: `21`; updated changed: `11`; unchanged: `2343`; missing after copy: `0`; copy errors: `0`; import tree files: `3990`.

Cinematic cheats / performance decisions:
- Kept deterministic flat grass placeholders on the boundary path. Low-end devices get immediate traversable cells; high-end machines can spend worker time on richer generated cells after the seam frame.
- Kept full composite road smoothing off the seam boundary path. Current seam smokes prove `smooth=0.000ms`.
- Kept sparse road-mask metadata and 1024x1024 R8 road-mask dilation instead of scanning/uploading the full 3072x3072 tile field.
- Rejected shader-grid terrain reconstruction and GL sub-update/storage-retention because measured timings were worse than the accepted path.

Verification:
- Native rebuild passed with BuildTools 18: `cmake --build build-msvc-subworld-check --target timaert subworld_async_seam_test --parallel 1`. No dotnet rebuilds were used. An earlier retry through the wrong VS environment failed before project code on missing standard header `cstddef`; this was corrected by using the CMake-cache-matched BuildTools 18 environment.
- Focused seam test passed after rebuild: `roadGen=31.578ms`, `plainGen=23.261ms`, `diagonalGen=29.785ms`, `reversalGen=24.892ms`, `smooth=0.000ms`, `snapshot_pending ok`, `worker_restore_saved ok`.
- Water-plane invariant passed after rebuild: `water=3145728`, `land=6291456`, `badWater=0`, `badLand=0`, `maxWater=0.40000`, `minLand=0.42000`.
- Runtime real seam smoke passed after rebuild: `[seam-cross] gen=38.989ms smooth=0.000ms upload3d=118.795ms upload2d=0.000ms total=157.938ms`, `[smoke] PASS`.
- Runtime time smoke passed after rebuild: 1000 subworld frames, exact 1440-minute advance, `[smoke] PASS`.
- Earlier same-pass regression suite passed: `quest_lifecycle_test`, `save_roundtrip_test`, `pathfinding_parity_test`, `subworld_generator_parity_test`.
- Final anti-bloat scan found no forbidden `std::async`, exceptions, RTTI casts, `std::rand`, debug seam spam, TODO, or FIXME in touched seam/runtime/test files.
- Final trial-revert scan found no leftover `aHeightNrm`, `uMeshStep`, `uGridWidth`, or disabled normal-attribute code.
- Final `git diff --check` passed for touched files except LF-to-CRLF normalization warnings.
- Final process check found no leftover `timaert` or `subworld_async_seam_test` process.

Exact microseconds saved / avoided:
- Accepted 1024 road-mask best remains the best measured path: `upload3d=51.785ms`, `total=74.603ms`.
- Terrain-payload shader-grid trial was rejected, avoiding a measured regression of `11463us` active 3D upload and `19338us` total seam time versus the accepted 1024-mask best.
- Compared with the original real 3D seam double-upload fault (`total=3308.131ms`), the freshly rebuilt final smoke (`total=157.938ms`) is `3150193us` lower total Debug seam time and keeps inactive 2D upload at `0us` on the seam frame.
- Current rebuilt smoke still spends `118795us` in active 3D upload; this remains the honest residual target. Worker generation/smoothing are no longer the boundary-path blocker.

STATUS: VERIFIED
