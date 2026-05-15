# Rationale: TMA_SUBWORLD_ASYNC_SEAM_BKR

## 2026-05-15 - Async Seam Generation

Problem: Seam crossing synchronously generated 3 axis cells or 5 diagonal cells, then rebuilt and smoothed a 3072x3072 composite on the main thread.
Solution: Boundary crossing now shifts surviving composite data, installs deterministic flat grass placeholders, queues explicit `std::jthread` generation jobs, and stitches completed cells on the main thread. The DOD practice is phase separation: worker owns CPU-only generation, main thread owns composite and renderer-visible state.
Rejected Alternatives: `std::async` was rejected because launch policy and lifetime are opaque. GL upload from worker was rejected because renderer ownership is main-thread-only. Full synchronous regeneration was rejected because it preserves the stall.
Scalability potential: Low uses placeholders and delayed smoothing; Middle uses two worker lanes; High/Ultra can spend the saved frame time on richer generated cells and renderer uploads after the seam has already responded.
Hardware Impact: On i3/MX350-class hardware the crossing path avoids the worst generator and smoother spikes; measured focused smoke has held seam manager critical timing in the ~6-35 ms range depending on scheduler noise, with road smoothing reported as 0.000 ms on the crossing path.

Problem: Outgoing modified cells could still pay snapshot quantization on the seam frame.
Solution: Leaving real cells are moved into bounded storage and queued as worker save jobs after generation jobs. Completed snapshots commit by move through `store_saved_subworld(SavedSubworld&&)`.
Rejected Alternatives: Snapshotting all nine cells every boundary was rejected as unnecessary cache churn and precision churn. Dropping placeholder snapshots was accepted because placeholders contain no player edits and are deterministic temporary state.
Scalability potential: Low avoids hitching while preserving edits. Middle/High/Ultra can tolerate more rapid seam crossings because save work no longer monopolizes the frame.
Hardware Impact: Removes vector-heavy height quantization from the boundary thread for 3-5 outgoing cells on cheap CPUs.

Problem: Composite road smoothing is needed for visual continuity but full composite smoothing is too expensive for the seam path.
Solution: Initial load can still smooth synchronously when roads exist. Seam crossings defer full-composite smoothing to the worker after all placeholders are replaced, and publish the smoothed height map as a later dirty composite.
Rejected Alternatives: Removing smoothing entirely was rejected because roads crossing cell boundaries become visibly kinked. Smoothing immediately after placeholder install was rejected because it reintroduces a main-thread stall and smooths temporary data.
Scalability potential: Low gets immediate traversable placeholder terrain; Middle receives smoothed roads asynchronously; High/Ultra can use the same deferred budget for denser road visuals without blocking crossing response.
Hardware Impact: Keeps measured seam `smooth` cost at 0.000 ms; later smoothing still costs CPU but no longer blocks the crossing frame.

Problem: Worker shutdown could finish save snapshots and then clear completed saves without committing them.
Solution: Shutdown now drops pending generation/smoothing work first, drains worker completion, commits completed saves, then clears queues. Active CPU-only jobs are allowed to finish because `std::jthread` cannot safely abort generated vector ownership mid-call.
Rejected Alternatives: Hard cancellation was rejected because generator and snapshot functions are not interruptible and partial vector ownership would be unsafe. Waiting for all stale generation jobs before shutdown was rejected because it stalls scene transitions without preserving player data.
Scalability potential: Low preserves edits during leave/re-enter; Middle/High/Ultra avoid wasting transition time on stale non-save jobs.
Hardware Impact: Prevents lost saved modified cells while reducing shutdown wait from queued stale generation/smoothing work.

Problem: Async smoothing can still waste memory bandwidth on composites with no road/square tiles.
Solution: Each real loaded cell now stores a road-tile count computed after generation/restore. Composite smoothing is skipped when all counts are zero.
Rejected Alternatives: Scanning the full 3072x3072 composite each time was rejected because it spends the bandwidth the optimization is trying to save. Blindly queueing smoothing was rejected because no-road cells have no visual benefit from the pass.
Scalability potential: Low skips useless jobs; Middle keeps workers free for generation/save; High/Ultra spends those cycles on visible content rather than no-op smoothing.
Hardware Impact: Avoids a 9 MiB tile copy, 36 MiB height copy, and full smoother pass for no-road composites.

Problem: Re-entering a saved/edited cell could deep-copy the saved quantized heightmap into the generation job on the seam frame.
Solution: The map-factory cache now stores immutable shared saved snapshots behind a mutex. Generation jobs capture a `shared_ptr<const SavedSubworld>` and restore from it on the worker after fresh generation.
Rejected Alternatives: Passing a raw pointer into the worker was rejected because cache clear/overwrite would create lifetime risk. Copying the saved payload into every job was rejected because it moves megabytes back to the seam frame.
Scalability potential: Low avoids a cache-copy hitch when revisiting edited cells; Middle/High/Ultra can preserve richer cell snapshots without increasing boundary-copy cost.
Hardware Impact: On i3/MX350-class hardware this removes one quantized heightmap vector copy plus structure copy per saved cell queued for generation.

Problem: Cosmetic async smoothing could be selected before pending save snapshots after generation drained.
Solution: Worker priority is now generation first, save snapshots second, smoothing third. This keeps seam responsiveness while ensuring outgoing modified cells reach the cache before optional road-height polish.
Rejected Alternatives: Keeping smoothing ahead of saves was rejected because a 36 MiB height smoothing job can delay player edit persistence. Running saves before generation was rejected because it would put seam response behind snapshot quantization.
Scalability potential: Low commits player edits sooner and still sees placeholders immediately. Middle keeps two worker lanes useful. High/Ultra can keep richer smoothing because it no longer outranks persistence.
Hardware Impact: On i3/MX350-class hardware this avoids one full-composite smoothing delay before save commit when crossings happen with pending edited outgoing cells.

Problem: Composite structure rebuilds could grow `composite_struct_` during seam publish or completed-cell publish.
Solution: Rebuild/blit now pre-counts non-placeholder structure totals and reserves exact capacity before push_back.
Rejected Alternatives: Leaving amortized growth was rejected because structure-heavy cells can copy existing entries during a publish. Shrinking capacity was rejected because it would add churn instead of removing it.
Scalability potential: Low avoids allocator spikes during seam publish. Middle/High/Ultra can carry denser structure visuals without a proportional allocator penalty at stitch time.
Hardware Impact: Gain is content-dependent; it removes realloc/copy risk for composite structures, not a constant microsecond value.

Problem: The full app verification channel became unstable from unrelated concurrent app/UI and macro-road changes.
Solution: Domain verification was kept to direct MSVC seam builds, focused seam crossing smoke, and existing regression binaries. The external full-target/app-smoke blockers are recorded separately instead of hidden.
Rejected Alternatives: Editing `src/app/main.cpp`, `src/ui/overlays.*`, or `src/macro/spawners.*` was rejected because those files are outside the seam prompt ownership and already dirty from other agents.
Scalability potential: Keeps this patch domain-bounded and mergeable. Low/Middle/High/Ultra behavior of the seam system is proven independently of unrelated macro/UI churn.
Hardware Impact: No runtime gain claimed; this is verification hygiene.

Problem: Rapid seam crossings can leave stale pending or completed generation jobs for cells that no longer belong to the 3x3 active window.
Solution: After a boundary shift, pending and completed generation queues are pruned against the current placeholder `(absolute cx, absolute cy, generation)` set. Completed drains also discard stale front entries without spending the one-current-publish budget.
Rejected Alternatives: Waiting for stale jobs to drain one per frame was rejected because old work can delay the current visible cell stitch. Hard-aborting active worker calls was rejected because `dispatch_generate` owns large vectors and has no safe interruption point.
Scalability potential: Low avoids visible placeholder persistence after quick movement. Middle keeps worker queues focused on current cells. High/Ultra can tolerate faster traversal and richer cells because stale work is purged before it blocks publication.
Hardware Impact: On i3/MX350-class hardware this saves one frame of stitch latency per stale completed front entry and avoids queued stale generation CPU where the job had not yet started.

Problem: The original focused smoke proved only the 3-cell axis seam path, not the 5-cell diagonal path required by the prompt.
Solution: `subworld_async_seam_test` now includes a no-road diagonal crossing that must recenter both axes, publish exactly five stitched cells, and never publish an async smoothing dirty event.
Rejected Alternatives: Assuming diagonal behavior from the axis case was rejected because the leaving/fresh-cell mask differs and is the failure-prone branch.
Scalability potential: Low/Middle/High/Ultra all use the same deterministic diagonal mask; this test protects the cheap placeholder path and the high-end deferred smoothing path from regressing.
Hardware Impact: No direct runtime gain; it prevents a class of diagonal seam regressions that would strand placeholders or waste smoothing work.

Problem: Full-target verification collided with another active build in the same `build-msvc` directory.
Solution: The failed full-target attempt is recorded as external build-state contention: `dispatch.cpp.obj` was permission-denied while another `cmake --build build-msvc --target timaert ...` process was compiling/linking the same target.
Rejected Alternatives: Killing the other build or deleting shared build artifacts was rejected because other agents are active in this workspace.
Scalability potential: Keeps verification honest and avoids corrupting shared build state.
Hardware Impact: No runtime gain claimed; this protects the workspace from destructive build interference.

Problem: The save/snapshot guarantee needed proof for the worst timing: leaving immediately after a seam crossing while fresh cells are still placeholders.
Solution: `subworld_async_seam_test` now clears the saved cache, crosses east, immediately calls `snapshot_all_to_cache()`, and asserts that both an outgoing real cell and a surviving real cell are present in the saved-subworld cache.
Rejected Alternatives: Waiting until all generation jobs completed was rejected because it does not prove placeholder skip/save flush behavior. Inspecting private manager cells from the test was rejected because the public cache contract is the actual persistence boundary.
Scalability potential: Low devices can leave during pending generation without losing existing real cells. Middle/High/Ultra keep richer async generation because cache correctness no longer depends on jobs completing before exit.
Hardware Impact: No speedup claimed; this closes a correctness risk for rapid enter/exit and seam-cross flows.

Problem: The user requested transfer of Timaert/Samosbor docs, tasks, and logs from Hecton into the Timaert folder.
Solution: Hecton docs/tasks/logs/reports/artifact folders were searched by path and content for exact Timaert/Samosbor/TMA markers. The Timaert import audit was updated with the checked scopes and result.
Rejected Alternatives: Copying Hecton `MACRO_*` or unrelated agent logs into Timaert was rejected because no file contained Timaert/Samosbor ownership markers and cross-project contamination would reduce documentation trust.
Scalability potential: Keeps the Timaert docs tree clean while preserving a deterministic import location and audit trail for future recovered artifacts.
Hardware Impact: No runtime impact; documentation hygiene only.

Problem: Stale-prune behavior needed a dynamic proof, not just code inspection.
Solution: `subworld_async_seam_test` now performs an east crossing immediately followed by a west crossing, then requires exactly three current-cell stitch publishes and no abandoned seam/smoothing dirty event.
Rejected Alternatives: Relying on the diagonal test was rejected because it does not exercise stale jobs from an abandoned seam. Publishing every completed job and filtering visually was rejected because it can leak dirty events and texture uploads.
Scalability potential: Low devices avoid lingering placeholders after quick direction changes. Middle/High/Ultra can keep async generation aggressive without allowing abandoned work to reach renderer-visible state.
Hardware Impact: On i3/MX350-class hardware this avoids stale dirty uploads and one-frame publish delays when a player reverses across a seam before previous jobs finish.

Problem: Full target verification reached link but could not write `timaert.exe`.
Solution: The exact linker blocker was recorded: `LNK1168` with `timaert.exe` still running from `build-msvc` as PID `51040`.
Rejected Alternatives: Killing the running executable was rejected because the user did not authorize terminating another process. Deleting/replacing the exe was rejected because the linker failure is an external lock, not a code defect.
Scalability potential: Keeps build verification honest without damaging another active runtime session.
Hardware Impact: No runtime impact; verification hygiene only.

Problem: The async seam proof existed as a side-built executable and one stale binary in `build-msvc\codex-check` still had old coverage.
Solution: Registered `subworld_async_seam_test` in CMake with the same C++23, `/GR-`, `/EHs-c-`, `_HAS_EXCEPTIONS=0`, and `ENTT_NOEXCEPTION` rules used by the other native tests. The build tree now owns the current seam proof.
Rejected Alternatives: Leaving the test as a manual `cl` command was rejected because it is easy to run a stale artifact. Folding this into the full app smoke was rejected because the current smoke DSL has no movement command and editing app UI/smoke orchestration is outside this prompt's owned seam files.
Scalability potential: Low/Middle/High/Ultra seam behavior is protected by a repeatable native target: axis crossings, diagonal crossings, rapid reversals, deferred smoothing, and pending snapshot flush stay covered as generation richness grows.
Hardware Impact: No runtime impact; this is build-graph verification. It prevents regressions that would reintroduce placeholder stalls or stale uploads on cheap CPUs.

Problem: Hecton documentation import had drifted after the previous snapshot; seven new Hecton `Docs\AgentLogs` artifacts were missing from the quarantined Timaert import during this pass.
Solution: Copied only the missing `.log`/`.json` files into `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs\Docs\AgentLogs`, preserved relative paths, wrote a delta manifest, and rechecked selected-scope missing count to zero.
Rejected Alternatives: Flattening imported Hecton logs into active Timaert `Docs\AgentLogs` was rejected because that would mix foreign project reports with live Timaert agent state. Recursively re-copying the entire tree with overwrite was rejected because it could erase provenance timestamps and collide with concurrent imports.
Scalability potential: The import tree remains a quarantined, repeatable evidence store. Future recovered docs can be added by delta without polluting active Timaert task state.
Hardware Impact: No runtime impact; documentation transfer only.

Problem: Prior full-target blockers were external process/build-state issues, but the current domain needed a final clean verification pass after those blockers cleared.
Solution: Reran the full `timaert` target, CMake seam test, regression tests, generator parity test, and runtime subworld smoke. All passed. The smoke proves app boot/enter/tick/leave; the seam test proves actual seam crossing because the smoke script cannot move the player across a seam.
Rejected Alternatives: Claiming app-smoke seam coverage was rejected because no `[seam-cross]` line is emitted without crossing movement. Adding a new app smoke command was rejected as out-of-domain churn in `src/app/main.cpp`.
Scalability potential: Verification is now split correctly: app runtime stays broad and stable, seam target stays deterministic and focused on A11/A12 behavior.
Hardware Impact: No new runtime claim; final measured seam proof still shows smoothing at `0.000ms` on the crossing path, with async work published later.

Problem: The seam smoke proved dirty publishes, but did not directly assert that the first visible response in freed slots was the intended deterministic placeholder.
Solution: Added placeholder sampling to `subworld_async_seam_test` after axis and diagonal crossings. The exposed slot must be `TILE_GRASS` with height `WATER_LEVEL + kLandMargin` before any worker result is stitched.
Rejected Alternatives: Inferring placeholder correctness from later stitch counts was rejected because a broken placeholder fill could still be hidden by completed generation a few frames later. Adding renderer pixel assertions was rejected because this is a manager/composite invariant and does not need GL.
Scalability potential: Low devices get a predictable cheap traversable fallback immediately; High/Ultra devices can still replace it with rich generated cells later without changing the first-frame contract.
Hardware Impact: No runtime cost outside the test. It prevents regressions that would expose stale shifted terrain or water holes while async generation is pending.

Problem: Worker job structs still carried stale instrumentation fields that no live code consumed.
Solution: Removed `GenerationJob::targetIdx` and `CompletedJob::genMs`, and removed the worker-side timer used only for the dead field. Seam timing remains on the boundary path where it is actually reported.
Rejected Alternatives: Keeping unused fields for possible future diagnostics was rejected because they add misleading state and make the async contract harder to audit.
Scalability potential: Small structural cleanup; future worker telemetry should be explicit and consumed, not hidden in dead fields.
Hardware Impact: Tiny per-job state reduction and one less worker timer call per generated cell; no fixed microsecond claim.

Problem: Shared `build-msvc` was not a stable full-link verification target because another runtime held `build-msvc\timaert.exe` and other agents were compiling in the same tree.
Solution: Created a private verification tree `build-msvc-subworld-check`, configured it with explicit SDL package paths and existing populated FetchContent source dirs, built the full `timaert` target, and ran the runtime subworld smoke from that private executable.
Rejected Alternatives: Killing PID `66344` or cleaning shared `build-msvc` was rejected because those actions would interfere with other active agents. Retrying a network FetchContent pull after the `stb` TLS failure was rejected because local populated deps were already available.
Scalability potential: Private verification isolates seam-domain proof from shared build contention while still compiling and linking the full app.
Hardware Impact: No runtime gain claimed; this is verification isolation. The private smoke passed app boot, subworld enter, 1000 frames, leave, and quit.

Problem: Snapshot saving was proven, but async worker restoration from a saved snapshot was not directly asserted.
Solution: Added `worker_restore_saved` to `subworld_async_seam_test`. It stores a quantized saved heightmap for the east fresh cell, crosses a seam, verifies the exposed slot starts as the flat placeholder, then waits for the async stitched cell to show the restored saved height.
Rejected Alternatives: Relying on `find_saved_subworld` presence was rejected because cache presence does not prove the generation worker consumed it. Testing only initial synchronous load was rejected because the prompt's risk is the worker-backed seam path.
Scalability potential: Low devices can revisit edited cells through async seams without losing terrain edits; High/Ultra can support richer saved-cell payloads behind the same immutable shared snapshot path.
Hardware Impact: No new runtime cost. This is a correctness proof for the existing no-copy shared snapshot path.

Problem: Hecton import verification drifted again while concurrent agents were writing fresh documentation/logs.
Solution: Ran another bounded delta copy into the quarantined import tree. The prompt delta manifest reached 22 rows, Hecton selected `Docs` missing count returned to zero, root selected missing count stayed zero, and the import tree reached 2301 files.
Rejected Alternatives: Moving imported logs into active Timaert `Docs\AgentLogs` was still rejected because it would mix Hecton reports into live Timaert state. Chasing the live Hecton log tree indefinitely was rejected; each pass records a checked instant.
Scalability potential: The import remains append-only and source-relative, so future deltas remain auditable without polluting active task state.
Hardware Impact: No runtime impact; documentation transfer only.

Problem: Runtime seam timing had only manager-level proof; renderer upload timing was instrumented but not exercised by an app smoke that actually crossed a seam.
Solution: Added the `subworld_seam` smoke action. It enters the real `SubworldEngine`, crosses one east seam in default 3D view, asserts center/timing invariants, lets worker stitches settle, leaves, and prints smoke evidence. The existing `TIMAERT_SEAM_TRACE` line now reports real app upload costs.
Rejected Alternatives: Claiming `subworld_time` as seam coverage was rejected because it enters/leaves but does not cross a cell boundary. Adding a renderer-only synthetic test was rejected because it would miss `SubworldEngine::tick()` upload behavior.
Scalability potential: Low devices get a reproducible hitch diagnostic path. Middle/High/Ultra can compare active renderer upload cost before adding richer generator content.
Hardware Impact: No direct runtime gain from the smoke hook; it exposed the real double-upload cost and protects the fix.

Problem: The first real 3D seam smoke showed the seam frame uploaded both renderers: `upload3d=1949.772ms`, `upload2d=1091.408ms`, total `3308.131ms` in the private Debug MSVC build.
Solution: `SubworldEngine` now marks both renderers dirty when the composite changes, uploads only the active renderer on the seam frame, and keeps the inactive renderer dirty until view switch/render. This keeps F-toggle correctness without buying invisible pixels on the seam frame.
Rejected Alternatives: Uploading both renderers eagerly was rejected because one view is invisible. Dropping inactive dirty state was rejected because switching views after a seam would render stale terrain.
Scalability potential: Low devices avoid guaranteed double GL upload. High/Ultra still get immediate upload for the visible renderer and deferred correctness for the alternate view.
Hardware Impact: In private Debug smoke, the measured seam frame moved from total `3308.131ms` with double upload to `208.577ms` in the latest 3D seam smoke (`upload2d=0.000ms`, `upload3d=176.381ms`). This is a Debug measurement, not a release claim.

Problem: `Renderer3D::upload()` rebuilt/uploaded constant topology and always pushed a full 3072x3072 road mask even when the composite had no road tiles.
Solution: Terrain indices are now built once in `Renderer3D::init()`. Road mask upload now uses a 1x1 zero texture for no-road composites, and expands to the full R8 mask only when a road tile exists.
Rejected Alternatives: Removing the road mask was rejected because road cells need the visual overlay. Full zero-mask upload was rejected because it spends 9 MiB of upload bandwidth for identical blank data.
Scalability potential: Low/no-road wilderness seams avoid useless mask upload; road-heavy high-end scenes retain the full mask path.
Hardware Impact: The latest private Debug 3D seam smoke reports `upload3d=176.381ms`; remaining target is active 3D terrain/instance upload.

Problem: A trial 2D height texture compression path looked attractive on paper but could degrade actual driver timing.
Solution: Tested R16-normalized 2D height upload, measured worse smoke (`upload2d=2628.803ms`), and reverted it to the prior R32F path.
Rejected Alternatives: Keeping a numerically smaller upload despite worse measured runtime was rejected. Shipping an unmeasured visual downgrade was rejected.
Scalability potential: Prevents false optimization churn; future 2D upload work should use partial updates or texture-coordinate recentering, not CPU conversion without evidence.
Hardware Impact: Avoided a regression; no speedup claimed.

Problem: Hecton documentation/log imports drift while parallel agents append logs.
Solution: Refreshed the Timaert quarantine mirror only: `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`. Selected scope is 2020 Hecton Docs files plus 26 root files; 31 changed files were updated, 0 selected files are missing after copy, import tree has 3063 files.
Rejected Alternatives: Writing Timaert logs into Hecton was rejected. Flattening Hecton logs into active Timaert `Docs\AgentLogs` was rejected because that would contaminate live Timaert reports.
Scalability potential: Quarantined, relative-path import keeps projects separated and future deltas auditable.
Hardware Impact: No runtime impact; documentation transfer only.

Problem: Worker scheduling code drifted from the documented priority and selected cosmetic smoothing before pending save snapshots once generation drained.
Solution: Reordered the worker selection path to generation -> save -> smoothing while keeping `saveJobsPaused_` active during pending exposed-cell generation. This preserves seam responsiveness and moves player-edited outgoing cells to cache before optional visual polish.
Rejected Alternatives: Leaving smoothing ahead of saves was rejected because a full-composite smoothing job can delay persistence. Running saves ahead of exposed-cell generation was rejected because it can keep placeholders visible longer.
Scalability potential: Low devices preserve edits without paying a smoothing delay; High/Ultra can retain richer smoothing because it no longer outranks persistence.
Hardware Impact: Saves up to one smoothing-job delay before snapshot commit on i3/MX350-class hardware in edit-heavy seam crossings; no fixed microsecond claim because it is content dependent.

Problem: `Renderer3D::upload` still scanned the full 3072x3072 tile buffer to find road pixels even though the seam manager already tracks road metadata per generated cell.
Solution: Added exact visual road-mask metadata (`TILE_ROAD` only) beside the existing smoothing road/square indices, collected both in one worker tile pass, and exposed sparse composite road-mask indices to the renderer. The renderer now fills the R8 mask from sparse indices and keeps the 1x1 zero texture path for no-road composites.
Rejected Alternatives: Reusing smoothing indices directly was rejected because those include `TILE_SQUARE` and would change visuals. Keeping the full tile scan was rejected because it spends 9.4M byte reads per upload for information already known. Building road-mask indices in a second worker scan was rejected and replaced with a single combined scan.
Scalability potential: Low/no-road and sparse-road seams avoid useless CPU scanning; High/Ultra keep exact full-resolution road visuals when roads exist.
Hardware Impact: Latest accepted private Debug smoke improved from prior accepted `upload3d=176.381ms` to `upload3d=111.769ms` on the same `subworld_seam` path; total seam smoke improved from `208.577ms` to `165.049ms`. This is local Debug evidence, not a release guarantee.

Problem: A plausible GL optimization could regress driver behavior even if it reduced apparent reallocations.
Solution: Tested stable GL buffer/texture storage with `glBufferSubData`/`glTexSubImage2D`, measured the same seam smoke at `upload3d=331.363ms`, and reverted the trial. The accepted version keeps original GL upload semantics plus sparse CPU preparation.
Rejected Alternatives: Keeping the sub-update path on theoretical grounds was rejected because the measured smoke got worse. Guessing at driver behavior was rejected; the timing proof decides.
Scalability potential: Prevents false optimization churn and preserves the measured faster path while leaving deeper renderer architecture as the next target.
Hardware Impact: Avoided a measured Debug regression of roughly `219594us` versus the accepted `111.769ms` upload path.

Problem: Hecton documentation/log imports can drift while parallel agents write logs, and the user explicitly requires Timaert/Samosbor docs to stay out of Hecton.
Solution: Refreshed the Timaert-only quarantine mirror under `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, preserved source-relative paths, wrote a delta manifest, and appended an audit entry. The refresh selected 2097 Hecton Docs files plus 30 root files; nothing was missing after copy and no Hecton files were written.
Rejected Alternatives: Flattening Hecton logs into active Timaert `Docs\AgentLogs` was rejected as project contamination. Writing any Timaert reports to Hecton was rejected by explicit user instruction.
Scalability potential: Keeps project documentation separated while allowing deterministic future deltas.
Hardware Impact: No runtime impact; documentation hygiene only.

Problem: Road composites still uploaded a full 3072x3072 R8 road mask after the sparse-index pass, so the seam frame spent bandwidth on 9 MiB of binary overlay data whenever any road was visible.
Solution: Kept the sparse manager index path but downsampled the road mask to 1536x1536. Native generated roads have a 5-tile footprint, so the 2:1 mask still leaves roughly 2-3 pixels across a road with GL linear filtering while cutting mask upload storage to 2.25 MiB.
Rejected Alternatives: A 1024x1024 mask was rejected as a larger visual risk because 5-tile roads can collapse to roughly 1-2 pixels. Keeping 3072x3072 was rejected because the measured upload remained the residual hitch. Reusing the rejected GL sub-update path was rejected because it already measured worse.
Scalability potential: Low devices avoid 6.75 MiB of road-mask upload bandwidth per road seam. Middle keeps readable roads. High/Ultra retain the exact same road geometry and can spend saved time on richer terrain/instances later.
Hardware Impact: Latest private Debug `subworld_seam` smoke moved from the previous accepted `upload3d=111.769ms` / `total=165.049ms` to `upload3d=69.636ms` / `total=92.813ms`; that is `42133us` less active 3D upload and `72236us` less total measured seam time on this path.

Problem: Documentation import can drift while external Hecton files change, and active Timaert reports must not be written to Hecton.
Solution: Refreshed only the Timaert quarantine mirror under `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, with source-relative paths and a manifest. Selected Hecton Docs count is 2182, selected root count is 3, and missing-after-copy is 0.
Rejected Alternatives: Writing Timaert status/logs into Hecton was rejected by explicit user instruction. Flattening imported files into live Timaert task/log folders was rejected because it would contaminate active project state.
Scalability potential: Repeatable quarantine import keeps project documentation separated and auditable.
Hardware Impact: No runtime impact; documentation hygiene only.

Problem: The accepted 1536x1536 road mask reduced upload cost but still spent 2.25 MiB of R8 texture upload on every road seam.
Solution: Changed the road-composite mask to 1024x1024 and added one-pixel dilation while still sourcing exact `TILE_ROAD` indices from `SeamlessSubworldManager`. Native road raster width is 5 tiles, so the mask keeps visible road coverage while reducing texture storage to 1 MiB.
Rejected Alternatives: Returning to the 3072x3072 mask was rejected because it reintroduces the 9 MiB upload. Keeping the 1536x1536 mask was rejected after the 1024x1024 path measured faster in the same private Debug seam smoke. A non-dilated 1024x1024 mask was rejected because 5-tile roads can collapse too aggressively at a 3:1 mask scale.
Scalability potential: Low devices buy back another 1.25 MiB of seam-frame road-mask bandwidth versus the prior accepted pass. Middle devices keep roads legible through dilation. High/Ultra retain the same road geometry and can spend saved upload time on richer billboard/terrain refresh later.
Hardware Impact: Previous accepted private Debug `subworld_seam` timing was `upload3d=69.636ms` / `total=92.813ms`. The final rebuilt executable measured `upload3d=51.785ms` / `total=74.603ms`, a measured reduction of `17851us` active 3D upload and `18210us` total seam time on this path.

Problem: Hecton documentation/log output continued to change while the user required Timaert/Samosbor docs to live under Timaert, not Hecton.
Solution: Refreshed the Timaert-only quarantine mirror from read-only `C:\hades\Hecton8`: 2228 selected Hecton `Docs` files plus 3 root docs were considered, 4 missing files were copied, 1 changed file was refreshed, and selected missing count returned to 0. The one live Hecton log that was locked during hashing was recopied on retry after size stabilization.
Rejected Alternatives: Writing any Timaert status/log back into Hecton was rejected by explicit user instruction. Flattening imported Hecton files into active Timaert `Docs\AgentLogs` or `Docs\Tasks` was rejected because it would contaminate current project state.
Scalability potential: The quarantined mirror remains source-relative and auditable while parallel Hecton agents continue writing.
Hardware Impact: No runtime impact; documentation hygiene only.

Problem: A shader-grid terrain payload looked cheaper on paper because it removed X/Z from each terrain vertex and reconstructed them in the vertex shader.
Solution: Implemented the trial locally, rebuilt, measured the same private Debug seam smoke, then reverted the trial after it regressed to `upload3d=63.248ms`, `total=93.941ms`. The accepted path remains the proven 6-float position+normal upload with the 1024 road-mask dilation work intact.
Rejected Alternatives: Keeping the theoretical byte reduction was rejected because measured driver/runtime cost got worse. Rewriting the whole terrain streaming architecture in the final pass was rejected because the prompt domain needs seam safety and measured proof, not speculative renderer churn.
Scalability potential: Low devices keep the measured faster path today; High/Ultra renderer work should move to a real cell-dirty or persistent-buffer architecture with fresh timing, not a shader trick that already failed.
Hardware Impact: Avoided a measured Debug regression of `11463us` active 3D upload and `19338us` total seam time versus the prior accepted `upload3d=51.785ms`, `total=74.603ms` smoke.

Problem: The focused water-plane invariant could fail when the global saved-cell cache retained stale saved data from earlier cases, which made the test measure cache contamination instead of generator/water correctness.
Solution: Cleared saved subworld state before and after the water-plane case. The generator-side post-pass remains the real invariant: after all mode edits and smoothing, water tiles clamp to `<= WATER_LEVEL`, and non-water tiles clamp to `>= WATER_LEVEL + kLandMargin`.
Rejected Alternatives: Weakening the test or accepting intermittent failure was rejected because the user asked for honest proof. Moving cache ownership into the test fixture was rejected as wider churn; bounded cache clearing around the invariant case proves the generator contract cleanly.
Scalability potential: Low devices and saved-game reloads keep deterministic terrain class boundaries; High/Ultra can add richer generator detail without violating the water/land visual plane.
Hardware Impact: No runtime cost. The final focused scan covered `3145728` water cells and `6291456` land cells with `badWater=0`, `badLand=0`, `maxWater=0.40000`, and `minLand=0.42000`.

Problem: The private app build exposed an ABI mismatch: existing call sites still referenced the old `spawn_trees(TerrainData, seed, density)` and `build_feature_layer(..., waterMask, roadMask)` shapes while the implementation had moved to sea-level-aware overloads.
Solution: Added narrow wrapper overloads in `src/macro/spawners.{h,cpp}` that forward to the sea-level-aware implementations with `seaLevel=0.40f`.
Rejected Alternatives: Editing broad macro call sites was rejected as outside the seam domain. Reverting sea-level-aware spawner work was rejected because it belongs to feature/pathfinding correctness from another domain.
Scalability potential: Keeps macro feature generation ABI-stable while preserving the richer sea-level-aware path for systems that pass it explicitly.
Hardware Impact: No frame-time claim; this removed a link blocker without changing hot-loop behavior.

Problem: `gen_spire` referenced `nbFeature` without accepting it as a parameter, breaking the full app compile path.
Solution: Passed the sanitized 3x3 neighbor feature array through the dispatch switch into `gen_spire`.
Rejected Alternatives: Removing wilderness anchor trail generation from spires was rejected because it would silently change generator output. Using globals was rejected because it would weaken worker snapshot isolation.
Scalability potential: Keeps generator jobs self-contained and compatible with worker-backed `(absoluteCx, absoluteCy, seed/context snapshot)` execution.
Hardware Impact: No runtime gain claimed; compile correctness and deterministic road-anchor inputs are preserved.

Problem: Hecton documentation/log imports drifted again while the final verification pass ran, and the user explicitly required Timaert/Samosbor material to live under Timaert rather than Hecton.
Solution: Refreshed only the Timaert quarantine mirror under `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, preserving source-relative paths and writing a new manifest. Selected Hecton `Docs` files: 2372; selected root files: 3; copied missing: 21; updated changed: 11; missing after copy: 0; import tree files: 3990.
Rejected Alternatives: Writing active Timaert logs into Hecton was rejected by explicit user instruction. Flattening imported Hecton files into live Timaert `Docs\AgentLogs`/`Docs\Tasks` was rejected because it would contaminate active Timaert state.
Scalability potential: The quarantined mirror remains auditable and repeatable while both game projects stay separated.
Hardware Impact: No runtime impact; documentation hygiene only.

Problem: The first rebuild retry used the wrong VS developer environment and failed before project code with `fatal error C1083: Cannot open include file: 'cstddef'`.
Solution: Switched to the BuildTools 18 `VsDevCmd.bat` that matches the private CMake cache compiler, verified the `INCLUDE` path contained the MSVC and Windows SDK headers, and rebuilt `timaert` plus `subworld_async_seam_test` successfully.
Rejected Alternatives: Treating the missing standard header as a project compile failure was rejected because it was an environment setup issue. Running dotnet rebuilds was rejected by explicit user instruction.
Scalability potential: Keeps native verification reproducible in the private build tree without disturbing shared build directories used by other agents.
Hardware Impact: No runtime impact. It makes the final timings authoritative for current source instead of stale executable state.

Problem: The previous app seam timing was from an executable older than the latest `dispatch.cpp` and focused test source edits.
Solution: Rebuilt the native targets and reran focused seam, `subworld_seam`, and `subworld_time` smokes. Final current-source evidence: focused `roadGen=31.578ms`, `plainGen=23.261ms`, `diagonalGen=29.785ms`, `reversalGen=24.892ms`; app seam `[seam-cross] gen=38.989ms smooth=0.000ms upload3d=118.795ms upload2d=0.000ms total=157.938ms`; time smoke advanced exactly 1440 minutes over 1000 frames.
Rejected Alternatives: Reusing stale executable timings was rejected after source mtimes proved the binaries were older than source files. Claiming this pass beats the best 1024-mask timing was rejected because the prior best remains `upload3d=51.785ms`, `total=74.603ms`.
Scalability potential: The evidence now separates stable async seam correctness from remaining active 3D upload cost, which is the right target for future Low/Middle/High/Ultra renderer scaling.
Hardware Impact: Current rebuilt Debug smoke has `smooth=0.000ms` and `upload2d=0.000ms` on the seam path; active 3D upload remains the residual `118795us` cost in this run.
