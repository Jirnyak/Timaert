# Rationale: TMA_ROAD_RIVER_TERRAIN_BKR

## Decision 1: Preserve Native Road A*

Problem: TS has corridor-guided road stamping, but native docs classify current terrain-cost A* roads as the confirmed baseline.
Solution: Keep `trace_roads` and add invariants for rejected-water pruning instead of replacing the algorithm.
Rejected Alternatives: Re-porting TS Bresenham corridors would overwrite a documented native baseline without same-seed A/B evidence.
Scalability potential: Low keeps bounded A* with simple masks; Middle/High can spend saved budget on denser visual road overlays; Ultra can add richer materials without changing topology.
Hardware Impact: Avoids broad topology churn and preserves fixed small-path behavior on i3/MX350 class devices.

## Decision 2: CPU River Mask As Terrain Post-Pass

Problem: Native terrain lacked TS `riverData`, so trees and macro rendering could not honor river corridors.
Solution: Add `TerrainData::riverData`, generate a byte river mask after height/moisture/temp generation, carve river cells below sea threshold, then upload `riverTexture`.
Rejected Alternatives: Shader-only rivers would look present but would not affect spawning or feature masks. Full hydraulic simulation was rejected as frame-budget waste.
Scalability potential: Low uses generated mask as cheap lookup; Middle/High/Ultra can increase visual shading and wetland detail from the same mask.
Hardware Impact: Runtime cost is one R8 sample in renderer and one byte lookup in spawners; generation cost is offline during map build.

## Decision 3: River-Aware Tree Exclusion

Problem: TS tree spawning excludes river corridors with a two-cell buffer, but native trees could stamp into river lanes.
Solution: Build a compact byte exclusion mask from `riverData` in `spawn_trees` and reject buffered cells.
Rejected Alternatives: Checking Manhattan neighborhoods per candidate would multiply random candidate cost; mutating terrain alpha would conflate water and vegetation policy.
Scalability potential: Low keeps deterministic cheap rejection; Middle/High/Ultra can use the same mask for reeds, wet banks, and visual overkill.
Hardware Impact: One linear prepass and O(1) candidate lookup; expected gain over per-candidate neighborhood scan on i3/MX350.

## Decision 4: Renderer River Overlay From Native Texture

Problem: Generated river data must be visible in native macro rendering without changing established road/feature order.
Solution: Bind `riverTexture` as `u_riverMap` and blend a TS-style blue overlay on above-sea river cells before road/feature overlays.
Rejected Alternatives: Baking river color into the master texture would remove independent control and make later LOD/material upgrades harder.
Scalability potential: Low uses a single sample/blend; Middle/High/Ultra can add flow shimmer, bank foam, or biome-specific wet edges.
Hardware Impact: One extra R8 texture sample in macro fragment path; negligible versus preserving data-driven visual control.

## Decision 5: Isolated MSVC Verification Directory

Problem: Shared `build-msvc` was locked by concurrent `timaert.exe` and other agent `cmake/ninja` processes, producing `LNK1168` without compile evidence.
Solution: Configure `build-msvc-roadriver` with the same MSVC/Ninja toolchain plus explicit `SDL2_DIR` and `SDL2_mixer_DIR`, then run full build, focused tests, and seed smokes there.
Rejected Alternatives: Killing unrelated build processes was rejected after the lock repeated; claiming the shared build failure as code failure was also rejected because compilation had already reached the link lock.
Scalability potential: Low-end verification remains deterministic and isolated; high-end/parallel agent work can continue without corrupting a shared build tree.
Hardware Impact: No runtime impact; saves integration time by avoiding repeated link-lock stalls.

## Decision 6: Historical Road A* Acceleration (Superseded By Decision 9)

Problem: Runtime smoke proved the preserved native road A* baseline was correct but expensive; seed 2 spent about 43.4s in the road section after only component pre-prune.
Solution: This pass added 8-connected land-component pre-prune for impossible island pairs and reused generation-tagged A* scratch buffers instead of clearing/reallocating 1024x1024 arrays per edge. Decision 9 supersedes the current runtime contract: large maps now use a step-capped A* budget.
Rejected Alternatives: Direct-line fallback was rejected because earlier evidence showed seed topology drift. TS corridor snapping was still rejected for the original visual/topology reasons.
Scalability potential: Low keeps correctness and avoids wasted island searches; Middle/High/Ultra can spend the recovered boot budget on denser road/river visuals without changing network topology.
Hardware Impact: Seed 2 debug road section improved from about 43.4s to about 14.6s with identical kept/pruned stats; no per-frame cost added.

## Decision 7: Hecton Documentation Import Layout

Problem: User requested all Timaert/Samosbor docs, tasks, and logs from the Hecton folder moved into the Timaert folder, but a literal Hecton documentation search for `Timaert`, `Samosbor`, and the Cyrillic spelling of Samosbor returned no labeled hits.
Solution: Preserve the complete Hecton documentation/task/log/report corpus as a non-destructive import under `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs`, with source-relative paths and a generated `MANIFEST.tsv`.
Rejected Alternatives: Moving files out of Hecton was rejected because it would damage source provenance and break other active agents. Filtering only by literal labels was rejected because the labels are absent and would copy nothing.
Scalability potential: Low keeps imported records isolated from active C++ parity docs; Middle/High/Ultra agents can mine the archive later without contaminating current Timaert authority files.
Hardware Impact: No runtime impact. Import size is about 88 MB of documentation-class files and does not enter app build or frame paths.

## Decision 8: Remove Reintroduced Direct/Fallback Road Path

Problem: A continuation audit found `trace_roads()` had direct-line, bounded-window, fallback, and cap-hit road branches reintroduced, contradicting the recorded exact-topology road baseline and the status line that those paths were absent.
Solution: Remove the direct-line/fallback branches and their stats/log fields. Decision 9 records the current implementation: component pre-prune followed by generation-tagged A* with a large-map step cap.
Rejected Alternatives: Leaving bounded/fallback branches with zero counters was rejected as false evidence. Reintroducing TS corridor snapping was again rejected because the project authority keeps native terrain-cost A* until same-seed A/B visual proof says otherwise.
Scalability potential: Low keeps deterministic road topology and avoids hidden path drops; Middle/High/Ultra can improve visuals above the same road mask without changing connectivity.
Hardware Impact: No per-frame cost. Boot-time road tracing may do more work than bounded A*, but it preserves the verified topology and still avoids impossible cross-island searches through component pre-prune.

## Decision 9: Current Road Bound Clarification

Problem: Active docs drifted in both directions. Some still described obsolete direct-line fallback behaviour; others described an obsolete unbounded A* contract. Current source has neither direct-line fallback nor full unbounded A* on large maps.
Solution: Document the live implementation: component pre-prune, generation-tagged terrain-cost A*, large-map step cap, and pruning for routes not proven inside budget.
Rejected Alternatives: Reverting source to the old full-map wording was rejected because `find_road_path()` currently enforces `kRoadSearchLargeMapMaxSteps = 4096`. Reintroducing direct-line fallback was rejected because prior evidence showed topology drift.
Scalability potential: Low-end devices avoid unbounded boot stalls; higher tiers can later spend saved budget on road visuals without changing the topology contract.
Hardware Impact: No per-frame cost. Boot work remains bounded per edge after component pruning.

## Decision 10: Feature Layer Safety And Parity Lock

Problem: `features.ts` was ported structurally, but native `FeatureLayer::at/set` could divide by zero on an empty layer and `build_feature_layer()` trusted mask sizes. There was also no focused test locking TS feature priority.
Solution: Guard empty `FeatureLayer` dimensions, validate terrain RGBA and optional mask sizes before reads, preserve TS pass order `Mountain -> Tree -> DirtRoad -> Road`, preserve TS flattened-index tree placement, filter alpha-zero/below-sea cells, and add `feature_layer_parity_test`.
Rejected Alternatives: Leaving the behavior to caller discipline was rejected because feature data feeds pathfinding, zones, macro rendering, and subworld context. Adding a separate feature-builder abstraction was rejected as unnecessary churn for a small hardened surface.
Scalability potential: Low uses the same byte grid and O(cells) generation pass; Middle/High/Ultra can add richer feature rendering from the same stable `FeatureType` map without new runtime branching.
Hardware Impact: No per-frame cost. Generation adds only size checks and preserves contiguous byte storage; malformed inputs now fail closed instead of risking undefined reads.

## Decision 11: Hecton Import As Live Snapshot

Problem: The user requested all Timaert/Samosbor docs/tasks/logs from Hecton copied into Timaert, but Hecton was actively emitting new logs during sync.
Solution: Keep the non-destructive import under `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs`, refresh changed/missing files into Timaert only, and write `DELTA_SYNC_*.tsv` manifests for each catch-up pass.
Rejected Alternatives: Writing Timaert docs into Hecton was explicitly rejected by the user and was not done. Moving files out of Hecton was rejected because it would damage the separate Hecton game workspace.
Scalability potential: Low keeps imported records isolated from active Timaert authority docs; higher-tier agents can mine the snapshot without polluting root Timaert docs.
Hardware Impact: No runtime impact. Import tree is documentation-only and outside CMake build inputs.

## Decision 12: Hecton Import Stable Boundary

Problem: A single import pass could still miss files if concurrent Hecton agents appended logs while the copy was running.
Solution: Run repeated Timaert-side delta syncs until the selected Hecton source set reached two consecutive zero-change rounds. The final stable pass selected 2048 source files, copied/refreshed 21 files in round 1, then copied/refreshed 0 files in rounds 2 and 3, leaving remaining missing/stale-by-size counts at 0/0.
Rejected Alternatives: Treating the first no-error copy as final was rejected because Hecton was demonstrably live. Writing a shared marker or report into Hecton was rejected because the user explicitly separated Timaert and Hecton as two games.
Scalability potential: Low keeps imported evidence quarantined and source-relative. Middle/High/Ultra agents can mine the complete snapshot without contaminating active Timaert plans, tasks, or logs.
Hardware Impact: No runtime impact. The final import tree is 3084 files / 305885792 bytes, all documentation/artifact class data outside the C++ frame path.

## Decision 13: Feature Consumer Storage Validation

Problem: `FeatureLayer::at/set` were safe, but `build_cost_grid`, `generate_zones`, and `MacroRenderer::upload_features` still trusted `features.data` size directly. A malformed layer could bypass the safe accessors and read past backing storage.
Solution: Add reusable `FeatureLayer` storage validation helpers, validate feature dimensions/storage once per consumer call, and fail closed: pathfinding/zones treat malformed layers as `FT_None`, while renderer uploads a 1x1 blank R8 feature map.
Rejected Alternatives: Calling `FeatureLayer::at()` inside every cell loop was rejected because it adds modulo and bounds work to hot generation loops. Leaving direct indexing was rejected because it contradicts the hardened feature transfer contract.
Scalability potential: Low keeps O(cells) loops pointer-fast after one validation branch; Middle/High/Ultra can still add richer visual feature layers without changing the validated byte-grid contract.
Hardware Impact: No per-frame cost in pathfinding/zones; upload fallback is only used for malformed data. On i3/MX350 class devices the common path remains a raw pointer loop with no per-cell bounds branch.

## Decision 14: Continued Hecton Import Boundary

Problem: After the previous stable import boundary, Hecton emitted more selected docs/logs while the user repeated the transfer order.
Solution: Run another Timaert-only delta loop, then a separate verification pass. The loop selected 2707 then 2709 source files, copied/refreshed 18 and 2 files, then reached copied/refreshed 0; the verification round selected 2709, copied/refreshed 0, and left missing/stale counts at 0/0.
Rejected Alternatives: Writing any coordination marker into Hecton was rejected because Hecton and Timaert are separate games. Stopping after the first zero-change loop round was rejected because prior passes showed the source could change between rounds.
Scalability potential: Low keeps imported Hecton evidence quarantined under Timaert. Middle/High/Ultra agents can mine the imported corpus without polluting active Timaert root docs.
Hardware Impact: No runtime impact. Latest import tree is 3111 files / 309483263 bytes and remains outside CMake/runtime inputs.

## Decision 15: FeatureLayer Malformed Storage Fails Closed

Problem: `features.ts` uses typed arrays whose bounds behavior is deterministic, but native callers can construct a `FeatureLayer` with non-empty dimensions and a short backing `data` vector. The old `at()` / `set()` implementation trusted `width * height` shape after the empty-dimension guard.
Solution: Add overflow rejection in `resize()` and bounds-check the computed flattened index against `data.size()` in both `at()` and `set()`. Valid prefix reads still work; out-of-backing cells return `FT_None` or ignore writes.
Rejected Alternatives: Forcing all callers through a factory was rejected because `FeatureLayer` is intentionally a compact POD-like byte grid used by tests, renderer upload code, and generation code. Throwing exceptions was rejected because the project uses fail-closed runtime guards, not exception control flow.
Scalability potential: Low keeps the same one-byte feature grid and zero per-frame cost; Middle/High/Ultra can still layer richer rendering from the same stable byte ids.
Hardware Impact: 0 us per frame. Two generation/runtime accessor comparisons prevent undefined memory access on malformed data without changing the hot renderer path.

## Decision 16: Macro Shader Literal Split For MSVC

Problem: MSVC rejected the large adjacent GLSL fragment literal in `macro_renderer.cpp` with `C2026: string too big`, blocking a clean targeted build after the feature hardening.
Solution: Split the fragment shader into independent static chunks and concatenate them into a `std::string` before `gl_link()`. Shader source order and GLSL content remain unchanged.
Rejected Alternatives: Trimming shader features was rejected because it would regress the already-ported biome/river/feature renderer. Moving to runtime file IO was rejected because the project currently embeds shaders and boot smoke depends on self-contained binaries.
Scalability potential: Low keeps the same compiled shader and no runtime feature loss; Middle/High/Ultra retain the existing visual surface for future overkill shader branches.
Hardware Impact: 0 us per frame. One startup string concatenation replaces a compiler-limited source literal and preserves shader runtime cost.

## Decision 17: Final Hecton Import Refresh As Timaert-Only Snapshot

Problem: The earlier stable import was correct at that timestamp, but the user repeated the requirement to transfer all Hecton docs/tasks/logs into Timaert and never write Timaert records back into Hecton.
Solution: Run a final selected-file refresh from `C:\hades\Hecton8` into `C:\Timaert\timaert_c\Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, copy only missing selected files, and write a Timaert-side delta manifest plus import-index note.
Rejected Alternatives: Deleting or moving source Hecton records was rejected because Hecton and Timaert are separate games. Writing status/log/rationale files into Hecton was rejected by explicit user instruction.
Scalability potential: Low keeps imported records quarantined from C++ build inputs; Middle/High/Ultra agents can mine the archive without contaminating active Timaert authority docs.
Hardware Impact: 0 us runtime. Latest boundary sync selected 2711 Hecton source files, copied 2 newly missing files across the last catch-up loop, reported 0 copy errors and 0 selected files missing after two consecutive zero-change rounds. Hecton is a live workspace, so this is a snapshot boundary, not a continuous mirror.

## Decision 18: Root Plan Road/River Closure

Problem: `matwej.md` still listed road audit and river generation as Tier A open work, including the stale statement that C++ had no rivers.
Solution: Mark A1/A2 done with the current native contracts: road generation is the verified terrain-cost A* divergence, and river generation is present through `riverData`, `riverTexture`, tree exclusion, and renderer overlay.
Rejected Alternatives: Leaving the stale root plan untouched was rejected because future agents would waste time redoing completed domain work. Claiming final visual river polish was also rejected; the note says first native integration is done and future work is polish.
Scalability potential: Low keeps root plans aligned with verified systems; Middle/High/Ultra agents can focus on visible upgrades instead of rediscovering completed parity.
Hardware Impact: 0 us runtime. Documentation correction only; it prevents integration churn, not frame cost.

## Decision 19: Terrain Storage Boundary Guards

Problem: Feature storage was fail-closed, but tree spawning, road tracing, path-cost generation, and subworld entry still assumed the macro terrain RGBA backing vector matched `width * height * 4`.
Solution: Add `TerrainData` storage helpers and gate terrain-consuming entry points before hot loops or subworld resolver math. Malformed terrain now returns empty trees/roads/cost grids or refuses subworld entry instead of indexing invalid memory.
Rejected Alternatives: Adding bounds checks inside every terrain cell access was rejected because valid runtime data should stay raw-loop fast. Leaving the assumptions implicit was rejected because malformed macro terrain would crash downstream systems outside the original generator.
Scalability potential: Low keeps valid path loops contiguous and branch-light; Middle/High/Ultra can add richer terrain/feature consumers while reusing the same boundary contract.
Hardware Impact: 0 us per frame. Valid data pays one boundary validation per generation/entry call; i3/MX350-class devices keep the same inner-loop memory pattern.

## Decision 20: Continued Hecton Boundary 2

Problem: Hecton emitted more selected docs/log artifacts after the previous snapshot boundary while the user repeated the transfer requirement.
Solution: Run another read-only Hecton to Timaert-only sync until two consecutive zero-change rounds. The loop copied/refreshed 3 files, then 3 more, then reached two rounds of 0 copied/refreshed with missing/stale counts at 0/0.
Rejected Alternatives: Writing a coordination marker into Hecton was rejected by explicit user instruction. Claiming a permanent mirror was rejected because Hecton is a live workspace.
Scalability potential: Low keeps provenance isolated in the import quarantine; Middle/High/Ultra agents can mine the latest snapshot without mixing Hecton and Timaert active docs.
Hardware Impact: 0 us runtime. Latest import tree is 3143 files / 313081010 bytes outside CMake/runtime inputs.

## Decision 21: Politik Terrain Boundary Guard

Problem: Road tracing and feature consumers were fail-closed on malformed terrain storage, but `generate_politik`, `snap_cities_to_land`, and `finalize_politik` still accepted any non-empty `rgba` vector and indexed it as if it matched map dimensions.
Solution: Validate map dimensions before `cellOwner` allocation, use terrain only when `TerrainData` dimensions match the requested map and RGBA storage is complete, and return early from land snapping/finalization on malformed terrain.
Rejected Alternatives: Adding per-cell bounds checks inside every Politik land predicate was rejected because valid terrain should remain a raw contiguous lookup. Failing all Politik generation on malformed terrain was rejected because the existing API explicitly supports no-terrain fallback placement.
Scalability potential: Low keeps current city placement cost and avoids crash paths; Middle/High/Ultra can add richer road/kingdom visual layers without changing the validated terrain boundary.
Hardware Impact: 0 us per frame. Valid generation pays one boundary check and keeps the same raw terrain loop; i3/MX350-class boot avoids undefined reads on malformed input.

## Decision 22: Zone And Landmark Upload Boundary

Problem: `ZoneLayer::at()` was safe, but renderer upload is a separate boundary and must not pass short zone storage, invalid zone bytes, or invalid landmark maps into GL texture creation.
Solution: Add zone data/field storage helpers; use data-storage validation for `upload_zones`; sanitize invalid zone bytes to zone `0`; keep landmark upload on a 1x1 blank fallback for invalid input and sanitize unknown landmark ids.
Rejected Alternatives: Requiring every producer to be perfect was rejected because renderer upload is the last line before GPU-visible corruption. Per-pixel shader validation was rejected because byte-map sanitization is cheaper and deterministic.
Scalability potential: Low keeps one R8 byte map for zones/landmarks; Middle/High/Ultra can add richer overlays while invalid data still collapses to a blank/safe texture.
Hardware Impact: 0 us per frame. Sanitization runs only during upload and only copies when invalid bytes are present; valid uploads remain direct pointer submissions.

## Decision 23: Continued Hecton Boundary 3

Problem: Hecton continued producing selected docs/log artifacts after the previous Timaert snapshot while the user repeated the import order.
Solution: Run two more read-only Hecton to Timaert-only syncs. `CONTINUE3` selected 2793 files and stabilized after one copy plus two zero-change rounds. `CONTINUE4` selected 2813 files and stabilized after two zero-change rounds with no missing/stale files.
Rejected Alternatives: Writing Timaert markers into Hecton was rejected by explicit user instruction. Treating the import as a permanent live mirror was rejected because Hecton is an active separate game workspace.
Scalability potential: Low keeps imported Hecton evidence quarantined under Timaert; Middle/High/Ultra agents can mine the latest snapshot without polluting active Timaert root docs/tasks/logs.
Hardware Impact: 0 us runtime. Latest import tree is 3280 files / 331498308 bytes outside CMake/runtime inputs.

## Decision 24: Continued Hecton Boundary 4 Under Live Emission

Problem: A final audit found Hecton had emitted more selected files after `CONTINUE4`, then continued emitting files during the verification loop.
Solution: Run a bounded copy/refresh loop plus a verification loop, both read-only against Hecton and Timaert-only for output. The latest verification boundary selected 2832 files, copied/refreshed all missing/stale files, and ended with missing/stale counts at 0/0.
Rejected Alternatives: Chasing a permanent mirror indefinitely was rejected because Hecton is live and separate. Writing any stop marker or report into Hecton was rejected by explicit user instruction.
Scalability potential: Low keeps the snapshot quarantined under Timaert. Middle/High/Ultra agents can mine the latest imported evidence without mixing active Hecton and Timaert docs.
Hardware Impact: 0 us runtime. Latest import tree is 3304 files / 338201505 bytes outside CMake/runtime inputs.

## Decision 25: Renderer Upload Boundary As Final Sanitizer

Problem: Safe feature/zone accessors do not protect the GPU boundary if a malformed byte grid is uploaded directly to GL.
Solution: Treat renderer upload as the final sanitizer. Feature, zone, and landmark maps now validate dimensions/storage before texture creation, upload a 1x1 blank R8 map for invalid inputs, and copy/sanitize only when invalid ids are present.
Rejected Alternatives: Trusting producers was rejected because renderer upload is a cross-system boundary. Shader-side validation was rejected because it spends frame cost on a problem that can be solved once during upload.
Scalability potential: Low keeps compact R8 maps and deterministic blank fallbacks; Middle/High/Ultra can add richer overlays from the same sanitized masks without exposing invalid ids to shader code.
Hardware Impact: 0 us per frame. Valid uploads remain direct pointer submissions; malformed or dirty maps pay a one-time upload scratch copy.

## Decision 26: Landmark Population Terrain Guard

Problem: Landmark projection from Politik still read terrain moisture/temperature after only assuming the macro terrain backing existed.
Solution: Gate `populate_landmarks_from_politik` on `TerrainData::has_rgba_storage()` before city/village projection. Malformed terrain now clears generated landmarks and exits instead of indexing invalid terrain buffers.
Rejected Alternatives: Per-cell bounds checks inside every landmark terrain query were rejected because valid generation should stay raw-loop fast. Continuing with partial corrupted landmarks was rejected because settlements/villages become long-lived game state.
Scalability potential: Low avoids corrupted POIs on weak devices; Middle/High/Ultra can spend landmark budget on richer village/settlement visuals once the terrain boundary is proven valid.
Hardware Impact: 0 us per frame. One generation-time boundary check replaces undefined memory access on malformed terrain.

## Decision 27: Dirt Road Boundary Guard

Problem: `trace_dirt_roads` still trusted map dimensions, complete road-mask storage, equal village coordinate arrays, and in-range village coordinates before indexing.
Solution: Validate map dimensions with the same `FeatureLayer::cell_count_for` helper, reject short road masks and mismatched village arrays, and wrap village coordinates before the first road-mask lookup.
Rejected Alternatives: Relying on caller discipline was rejected because dirt roads are generated between road tracing and feature-layer build, so a malformed road mask could corrupt the entire feature pass. Per-cell exception handling was rejected because the native project disables exceptions and expects fail-closed guards.
Scalability potential: Low keeps the valid TS path as a single preflight branch and contiguous byte-mask write; Middle/High/Ultra can add richer dirt-road visuals without changing the validated mask contract.
Hardware Impact: 0 us per frame. Valid generation pays one boundary validation and keeps the same spiral search/torus trace loops.

## Decision 28: Continued Hecton Boundary 6

Problem: Hecton emitted more selected docs/log artifacts after prior import boundaries while the user repeated that Timaert and Hecton must remain separate.
Solution: Run another read-only Hecton to Timaert-only sync until two consecutive zero-change rounds. The loop copied 14 newly selected files and refreshed 5 stale-by-size files, then reached two zero-change rounds with missing/stale counts at 0/0.
Rejected Alternatives: Writing a marker into Hecton was rejected by explicit user instruction. Claiming a permanent mirror was rejected because Hecton is live and independent from Timaert.
Scalability potential: Low keeps imported Hecton evidence quarantined under Timaert; Middle/High/Ultra agents can mine the current snapshot without polluting active Timaert docs/tasks/logs.
Hardware Impact: 0 us runtime. Latest import tree is 3348 files / 338890653 bytes outside CMake/runtime inputs.

## Decision 29: Final Hecton Boundary 7

Problem: A last verification pass after code/test/report work found Hecton had emitted additional selected artifacts after boundary 6.
Solution: Run one final read-only Hecton to Timaert-only sync. The loop copied 2 files and refreshed 5 stale-by-size files, then reached two zero-change rounds with missing/stale counts at 0/0.
Rejected Alternatives: Chasing live Hecton indefinitely was rejected because it is a separate active workspace. Writing any Timaert report or sync marker into Hecton remained forbidden and was not done.
Scalability potential: Low keeps the imported evidence as a quarantined snapshot; Middle/High/Ultra agents can mine it without mixing Timaert and Hecton authority docs.
Hardware Impact: 0 us runtime. Latest import tree is 3361 files / 341977403 bytes outside CMake/runtime inputs.

## Decision 30: Dirt Road Land-Mask Byte Count

Problem: `trace_dirt_roads` rejected malformed road masks and village arrays, but a non-null terrain RGBA pointer still had no byte-count contract before alpha-channel reads.
Solution: Extend the dirt-road API with an optional land-mask byte count. If a non-zero byte count is supplied, it must cover `width * height * 4`; otherwise the function returns an empty dirt mask before reading alpha bytes.
Runtime Caller: `boot_world` passes `app.terrain.rgba.size()` together with `app.terrain.rgba.data()`, so the production macro generation path exercises the byte-count contract.
Rejected Alternatives: Trusting raw pointers indefinitely was rejected because dirt-road generation sits directly before feature-layer construction. Changing the function to own `TerrainData` was rejected as broader churn; the current call sites only need a byte-count guard.
Scalability potential: Low keeps the valid TS dirt-road path unchanged and branch-light; Middle/High/Ultra can add richer dirt overlays while retaining the same validated mask contract.
Hardware Impact: 0 us per frame. Valid generation pays one arithmetic guard before the existing spiral search and torus trace.

## Decision 31: Final Hecton Boundary 8

Problem: A final sync after dirt-road verification found another late Hecton artifact batch, including a log that changed size while being copied.
Solution: Run a read-only Hecton to Timaert-only sync until two consecutive zero-change rounds. The transient size mismatch resolved on later rounds, ending with missing/stale counts at 0/0.
Rejected Alternatives: Writing a lock or stop marker into Hecton was rejected by explicit user instruction. Claiming a permanent mirror was rejected because Hecton remains an active separate game workspace.
Scalability potential: Low keeps all imported evidence quarantined under Timaert; Middle/High/Ultra agents can mine the snapshot without mixing game authority.
Hardware Impact: 0 us runtime. Latest import tree is 3909 files / 523915969 bytes outside CMake/runtime inputs.

## Decision 32: Macro NPC Terrain Boundary Guard

Problem: `spawn_macro_npcs` used macro terrain alpha through `is_land`, but the helper wrapped against `TerrainData::width/height` before proving those dimensions and RGBA storage matched the active map. The same path also assumed `GameState::mapW/mapH` were positive before calling `wrapi`.
Solution: Treat terrain as usable only when dimensions equal `GameState::mapW/mapH` and `TerrainData::has_rgba_storage()` is true. Invalid or mismatched terrain is treated as absent terrain, while invalid map dimensions fail closed before any spawn search.
Rejected Alternatives: Failing all NPC spawns on malformed terrain was rejected because absent terrain is a valid fallback mode for this API. Trying to wrap zero-sized maps was rejected as undefined math; returning no spawns is the only valid fail-closed behavior. Per-attempt bounds checks in the search loop were rejected because valid generation should stay branch-light after a single boundary gate.
Scalability potential: Low keeps NPC spawn robust on corrupted or partial macro bootstrap data; Middle/High/Ultra can add richer faction and density rules while reusing the same terrain contract.
Hardware Impact: 0 us per frame. Valid generation pays one cheap boundary predicate before contiguous alpha reads; i3/MX350-class devices avoid undefined reads without adding runtime-frame cost.

## Decision 33: Continued Hecton Boundary 8 With Expanded Selector

Problem: The previous import snapshot covered the earlier selected docs/tasks/logs set, but the user repeated the requirement to transfer all Timaert/Samosbor-relevant Hecton docs, tasks, and logs while keeping the two games separate.
Solution: Run a read-only Hecton to Timaert-only sync using an expanded selector: `Docs`, `Logs`, `CodexArtifacts`, `.codex-artifacts`, and root doc/log/index files. The loop reached two consecutive zero-change rounds with selected `3772` and missing/stale `0/0`.
Rejected Alternatives: Writing reports or stop markers into Hecton was rejected by explicit instruction. Copying Hecton runtime/code trees wholesale into active Timaert source was rejected because the task is documentation/task/log transfer, not game-code mixing.
Scalability potential: Low keeps imported evidence quarantined under `Docs/Imported/Hecton8`; Middle/High/Ultra agents can mine the larger snapshot without polluting Timaert's authoritative root docs.
Hardware Impact: 0 us runtime. Latest import tree is 3889 files / 523232925 bytes outside CMake/runtime inputs.

## Decision 34: Final Hecton Boundary 9 After Verification

Problem: After local NPC verification and baseline reruns, the live Hecton workspace had emitted another late artifact batch.
Solution: Run one final read-only Hecton to Timaert-only sync with the expanded selector. The loop copied/refreshed the late files and then reached two consecutive zero-change rounds with selected `3803` and missing/stale `0/0`.
Rejected Alternatives: Writing any Timaert status, log, rationale, or sync marker into Hecton was rejected by explicit user instruction. Chasing a permanent mirror was rejected because Hecton is live and independent.
Scalability potential: Low keeps all imported evidence quarantined under `Docs/Imported/Hecton8`; Middle/High/Ultra agents can mine the latest snapshot without making Hecton docs authoritative for Timaert.
Hardware Impact: 0 us runtime. Latest import tree is 3922 files / 524582898 bytes outside CMake/runtime inputs.

## Decision 35: Final Current-Workspace Verification And Boundary 9 Verify

Problem: The dirt-road byte-count guard needed proof in the active runtime call path, and Hecton continued changing after the previous import boundary while the final build/test/smoke checks ran.
Solution: Verify that `boot_world` passes `app.terrain.rgba.size()` with the terrain pointer, rebuild focused native targets, rerun focused/baseline tests, run seed `49` smoke, then run another read-only Hecton-to-Timaert import continuation. The first continuation loop ended stale, so a verification loop was run until two zero-change rounds and missing/stale `0/0`.
Rejected Alternatives: Claiming the earlier import as final was rejected after the stale-by-size result. Writing a lock or sync marker into Hecton was rejected by explicit user instruction. Forcing every test caller to own `TerrainData` was rejected because optional pointer/count keeps the API narrow and backward-compatible.
Scalability potential: Low keeps dirt-road generation branch-light and quarantines imported Hecton evidence outside active Timaert source; Middle/High/Ultra can add richer dirt overlays or mine imported docs without mixing game authorities.
Hardware Impact: 0 us per frame. Valid runtime dirt-road generation pays one byte-count arithmetic guard; latest import tree is 3934 files / 556140914 bytes outside CMake/runtime inputs.

## Decision 36: Zone Water-Boost Runtime Parity

Problem: TS `GameScreen.svelte` passes an `isWater` predicate into `generateZones`, but native `boot_world` called `generate_zones` without the terrain water mask. Water cells therefore missed the TS `WATER_BOOST` in the runtime zone layer.
Solution: Extend `generate_zones` with an optional RGBA byte count and pass `app.terrain.rgba.data()` plus `app.terrain.rgba.size()` from `boot_world`. The generator applies `WATER_BOOST` only when the supplied buffer covers `width * height * 4`; short masks are ignored.
Rejected Alternatives: Leaving water boost out was rejected because it is a visible TS zone parity drift. Requiring `TerrainData` ownership in `generate_zones` was rejected because zones only need the optional water predicate equivalent, and the byte-counted pointer keeps the API narrow.
Scalability potential: Low restores correct zone danger contouring around water without frame cost; Middle/High/Ultra can layer richer water-adjacent spawn and spire gating rules on a zone field that matches TS inputs.
Hardware Impact: 0 us per frame. Valid maps pay one generation-time byte-count check and one alpha byte read per zone cell, matching existing load-time zone work.

## Decision 37: Final Hecton Boundary 10 After Zone Parity

Problem: Hecton emitted another late docs/log batch while the zone water-boost fix and verification ran.
Solution: Run a read-only Hecton to Timaert-only sync and reject the first dirty loop because it did not reach a zero-change streak. Accept the follow-up verification loop only after two zero-change rounds with selected `3844` and missing/stale `0/0`.
Rejected Alternatives: Claiming the dirty loop as final was rejected because it copied/refreshed late files. Writing any sync marker into Hecton was rejected by explicit user instruction.
Scalability potential: Low keeps Hecton evidence quarantined under Timaert; Middle/High/Ultra agents can mine the latest snapshot without making Hecton docs authoritative for Timaert.
Hardware Impact: 0 us runtime. Latest import tree is 3968 files / 560310159 bytes outside CMake/runtime inputs.
