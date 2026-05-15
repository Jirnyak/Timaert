# Rationale: TMA_SUBWORLD_GENERATOR_PARITY_BKR

## 2026-05-15

Problem: TS generator dispatch has explicit `grassland` as the default wilderness mode, while native no-feature land cells resolved to an internal `Open` mode.
Solution: Resolve default no-feature land to `SubworldMode::Grassland` and keep the existing lightweight open generator implementation behind that mode.
Rejected Alternatives: Adding a separate large grassland generator would duplicate the same lay-base/scatter behavior and increase synchronous generation surface without a TS-visible difference.
Scalability Potential: Low-end devices keep the same bounded cell work; high-end devices still get denser TS-faithful vegetation from the shared scatter path.
Hardware Impact: No new loops beyond existing generation. Mode-key parity improves save/cache correctness without frame cost.

Problem: Native `BiomeConfig` values drifted from TS `base-generator.ts`, and generated `waterLevel` used per-biome values instead of TS `WATER_LEVEL=0.40`.
Solution: Align native density/step values to TS and force `SubworldMapData::waterLevel` to the shared `WATER_LEVEL` constant.
Rejected Alternatives: Keeping per-biome water planes would make water/swamp/desert visual planes diverge from TS and from native heightmap sea-level math.
Scalability Potential: Low-end devices pay the same scatter algorithm with TS densities; high-end devices get visibly richer biome identity, especially taiga/tropics/swamp.
Hardware Impact: Vegetation counts rise where TS is denser. The focused test keeps this observable; no per-frame allocation was added.

Problem: TS forest glades were absent in native forest cells, leaving forests uniformly dense.
Solution: Port TS glade scan constants and global-coordinate placement, then compact tree structures out of cleared circles.
Rejected Alternatives: Local RNG clearings were rejected because they would not stitch across cell boundaries.
Scalability Potential: Low-end devices get cheap circle carving on a coarse 48-tile grid; high-end devices benefit from stronger readable forest silhouettes.
Hardware Impact: Generation-only cost. Tree structure count is reduced inside glades, so render-side billboard count does not increase.

Problem: TS spire landmarks existed in macro state but native subworld dispatch had no landmark discriminator for spires.
Solution: Add `CellLandmarkKind`, route spires from the engine resolver, and generate the TS scorch/crater/tower data slice.
Rejected Alternatives: Overloading `landmarkSettlementId` population rules would keep spires unreachable and confuse city/village routing.
Scalability Potential: Cheap data discriminator on low-end; high-end can later render richer structure geometry without changing generator routing.
Hardware Impact: One small landmark scan over `gs.spires` during cell resolution; no per-frame work added.

Problem: Native ruins were not porting TS ring/square rules.
Solution: Add bounded-array ring wall generation with TS radii, segment range, roughness, difficulty rings, and cracked center square.
Rejected Alternatives: Porting the full TS wall-ring/gate structure stack was rejected for this slice because the renderer does not consume those richer fields yet and synchronous generator growth is capped.
Scalability Potential: Low-end gets blocked wall/tile evidence cheaply; high-end can later map the same data into richer wall meshes.
Hardware Impact: Bounded ring segments, no unbounded temporary vectors in tile loops.

Problem: TS water-road cells generate bridge structures with deck height `3`, but native road generation only carved road tiles.
Solution: Add `Structure::Bridge`, update restore grouping, and emit bridge segments for water road cells based on the same connectivity cases as TS.
Rejected Alternatives: Rendering-only bridges were rejected because the generator data must exist first for save/restore and tests.
Scalability Potential: Low-end stores one/few bridge records per water road; high-end renderer can consume the same records for full bridge meshes later.
Hardware Impact: O(connection count) structure writes per water road cell; no per-frame work added in this slice.

Problem: Full app smoke cannot directly request seam crossing through the current smoke script vocabulary.
Solution: Use the available direct `subworld_async_seam_test.cpp` to verify boundary recentering and async composite publish behavior, and run the existing `subworld_time` app smoke for runtime subworld boot/tick proof.
Rejected Alternatives: Adding new smoke commands in `app/main.cpp` is outside this generator prompt and would touch the app shell.
Scalability Potential: Seam proof remains isolated and deterministic.
Hardware Impact: Verification-only; no runtime code change.

Problem: City/village generators still carried lightweight native placeholder behavior after the first parity slice.
Solution: Port the TS-visible settlement features into bounded native passes: city wall rings, organic main roads, deterministic branch roads, central square, keep, roadside block houses, farm fields, farm-road connectors, and tree-clear radius; village main roads, central square, roadside 2-3 tile houses, palisade, fields, farm-road connectors, and tree-clear radius. Focused tests now assert city/village mode routing plus houses, walls, fields, square, and roads.
Rejected Alternatives: A line-by-line TS mycelium queue clone was rejected because `matwej.md` documents a prior city/village expansion causing seam freeze and rollback. The native solution keeps the player-visible structure classes and population scaling but hard-caps branch, field, house, and wall work.
Scalability Potential: Low-end devices get stable bounded generator time with visible settlement silhouettes, gates, blocks, roads, and farms. Mid devices can raise structure rendering fidelity from the same data. High/Ultra devices can later consume the existing wall/bridge/house structure records for mesh overkill without changing generator ownership.
Hardware Impact: No per-frame work added. Generation cost remains bounded by fixed caps: city branch roads <=72, city fields <=80, city houses <=380, village fields <=40, village houses <=120, wall node arrays <=48. On i3/MX350-class hardware this prevents the prior seam-freeze pattern and buys visual detail with generation-only cost.

Problem: Farm-road carving initially erased the village central square.
Solution: Restamp the village square after farm-road carving and before palisade/tree scatter, matching the TS rule that the square is a persistent settlement anchor.
Rejected Alternatives: Weakening the test assertion was rejected; generator data must preserve the square tile.
Scalability Potential: Deterministic tile precedence keeps settlement landmarks legible at all LODs.
Hardware Impact: One tiny rectangle stamp per generated village; below measurement noise and 0 us per frame.

Problem: Full CMake link intermittently failed with `LNK1168` on `timaert.exe`.
Solution: Identify and stop the stale `timaert.exe` process, verify the executable path is inside `build-msvc`, move the locked binary aside, and relink.
Rejected Alternatives: Treating the link lock as a code failure or deleting recursively in the build tree.
Scalability Potential: No runtime effect; preserves repeatable verification.
Hardware Impact: Verification-only.

Problem: The city keep was attempted through the normal roadside-house clearance path, so the existing vertical main road could reject it while the generator still counted it as placed.
Solution: Add a dedicated landmark-house stamp for the keep and guard it with a focused invariant requiring exactly one high central keep above the square.
Rejected Alternatives: Allowing the missing keep and relying on loose house-count thresholds.
Scalability Potential: Low-end and high-end both retain the same readable city landmark anchor; high/Ultra renderers can later use the existing high house structure as a castle/keep mesh seed.
Hardware Impact: One bounded rectangle stamp per generated city, 0 us per frame.

Problem: City/village main roads ignored neighbour road connectivity and always generated the fallback four-way pattern.
Solution: Add a fixed-size `RoadAxisSet` collected from the 3x3 `nbFeature` array. If at least two road/dirt-road neighbours exist, settlement main roads/gates target those directions using `edge_target`; otherwise they fall back to four cardinal roads like TS.
Rejected Alternatives: Keeping always-four-way settlement roads, which makes gates disagree with macro roads and wastes visual complexity on dead edges.
Scalability Potential: Low-end devices draw fewer unnecessary settlement gates when macro connectivity is sparse; high-end devices get road layouts that line up with macro intent and can support richer gate meshes later.
Hardware Impact: Eight feature checks per generated settlement, no heap allocation, 0 us per frame.

Problem: Comments around road carving were stale and misplaced above the spire generator, including a false claim that the raster was straight-line.
Solution: Move the road comment to `carve_organic_road` and describe the actual endpoint-damped organic curve.
Rejected Alternatives: Leaving misleading documentation that would invite seam-breaking future edits.
Scalability Potential: No runtime effect; reduces maintenance risk.
Hardware Impact: 0 us.

Problem: Runtime app smoke failed once during macro boot before roads/subworld execution.
Solution: Reran immediately after confirming no stale `timaert` process; the second run passed through boot, subworld time, and quit. Treated as transient invocation/runtime instability outside this generator path, not as verified generator failure.
Rejected Alternatives: Hiding the first failure or marking generator work verified without a passing rerun.
Scalability Potential: No runtime code change.
Hardware Impact: Verification-only.

Problem: Wall gates were tile-correct but not structure-correct. A future wall renderer consuming `Structure::Wall` records could still draw wall segments across road gates.
Solution: Make `stamp_settlement_wall` track whether a segment intersects road/square tiles and skip the matching `Structure::Wall` record when a gate is opened. Reordered city generation so main/branch roads exist before wall rings are stamped, matching TS gate-detection intent.
Rejected Alternatives: Keeping tile-only gates and relying on current renderer ignorance of wall structures.
Scalability Potential: Low-end devices keep cheap tile gates; high/Ultra renderers can consume wall structures later without drawing blockers through entrances.
Hardware Impact: One boolean branch per wall segment during generation; no per-frame cost.

Problem: Manual direct compiles had left root-level `.obj` files in the repository root.
Solution: Verified every object path stayed under `C:\Timaert\timaert_c` and removed only root `*.obj` artifacts; manual compile outputs remain under `build-msvc\manual-*`.
Rejected Alternatives: Recursive deletion or leaving ignored binary clutter in the source root.
Scalability Potential: No runtime effect; cleaner workspace for multi-agent work.
Hardware Impact: 0 us.

Problem: The gate-safe wall fix still left a second stale-structure risk: city farm-road connectors and fields are produced near the outer ring, so a future wall renderer could consume `Structure::Wall` records whose midpoint sat on road, square, house, or field tiles.
Solution: Reordered city wall stamping after every city road/field connector pass, expanded `stamp_settlement_wall` suppression to all protected settlement tiles, and added a focused parity helper that rejects wall records on protected tiles for normal and neighbour-aligned city/village outputs. The first strict run proved the stale-record count was zero but exposed a thinner city wall (`cityWalls=38`), so city wall tessellation was raised to `22 + ring * 8` while still clamped by the existing 48-node fixed buffer.
Rejected Alternatives: Weakening the wall-count invariant, keeping tile-only correctness, or porting the unbounded TS mycelium/gate queue after `matwej.md` documented seam-freeze risk.
Scalability Potential: Low gets deterministic cheap tile gates/fields with no wall blockers; Middle gets denser city silhouettes from the same data; High/Ultra can later render wall structures as meshes without drawing blockers through roads, fields, houses, or squares.
Hardware Impact: 0 us per frame. Generation-only cost rises by about 18 city wall segment nodes for the 6000-pop parity seed and remains capped by 48 nodes per ring; the fresh seam proof stayed bounded (`roadGen=47.218ms`, `plainGen=34.773ms`, `diagonalGen=36.168ms`) and the app smoke passed.

Problem: Native road and settlement gates still targeted fixed edge midpoints, while TS uses `computeEdgeAnchors`: deterministic shared-edge anchors placed 35-65% along orthogonal edges from symmetric cell-coordinate seeds.
Solution: Added bounded native edge-anchor math and routed road cells plus neighbour-connected city/village main roads through it. Bridge structures now use the same anchored endpoints as the road raster. The native single-neighbour through-road enhancement is kept for visual continuity, but its water bridge now spans the same through segment instead of only edge-to-center.
Rejected Alternatives: Keeping fixed midpoint gates, which loses TS visual variation and makes all roads/gates look mechanically centered; reverting the single-neighbour through-road to the TS center dead-end, which would reintroduce the native seam artifact documented in code.
Scalability Potential: Low gets deterministic anchor variation with one xorshift per road edge; Middle gets less repetitive settlements and roads; High/Ultra can consume anchored bridge/wall/gate data for richer meshes without needing new generator fields.
Hardware Impact: 0 us per frame. Generation-only work is O(connected road edges), max eight anchors per generated cell. Seam rerun passed after one transient diagonal timing failure unrelated to road anchors (`roadGen=66.763ms`, `plainGen=131.214ms`, `diagonalGen=90.187ms`, `reversalGen=65.927ms`).

Problem: The user requested transfer of all Timaert/Samosbor docs/tasks/logs from the Hecton folder, but exact Hecton content search for `Timaert`, `TMA_`, `Samosbor`, and `Самосбор` found no directly labeled files.
Solution: Performed a copy-only import of the current operational Hecton documentation surface into isolated Timaert buckets: imported Hecton Tasks, AgentLogs, Reports, Docs root authority files, and root markdown docs. Created a Markdown and CSV manifest under `Docs\Imported\Hecton8`. After the first import pass, corrected a PowerShell `-Include` scope bug by removing 163 non-doc root files from the Timaert import bucket after verifying every removal target was inside `Docs\Imported\Hecton8\ProjectRoot`.
Rejected Alternatives: Moving/deleting Hecton source files; importing deprecated/archive dumps without an exact Timaert/Samosbor signal; leaving `.csproj`, `.lscache`, `.zip`, `.json`, `.log`, and other non-doc root artifacts in Timaert docs.
Scalability Potential: Low-end and high-end runtime unchanged. Project organization improves because imported evidence is searchable from Timaert without contaminating active status/log namespaces.
Hardware Impact: 0 us per frame; documentation-only. Retained import is 468 files / 55,368,551 bytes, all hash-verified against Hecton sources.

Problem: Ruin landmarks were still missing the TS road-stitch contract. In TS, `RuinGenerator.carveRuinedPaths()` uses `computeEdgeAnchors()` for landmark-to-road neighbours, so a ruin next to a macro road gets a deterministic broken path to the shared edge. Native ruin generation built rings and a cracked square but ignored `nbFeature`, leaving adjacent roads visually disconnected.
Solution: Added a bounded road-neighbour collector and routed `gen_ruin` through TS-style deterministic edge anchors before wall stamping. Ruin wall stamping now skips road/square/house/field tiles and suppresses stale wall structure records on protected crossings. The focused parity test asserts the expected east edge anchor and rejects protected-tile wall records in the road-connected ruin case.
Rejected Alternatives: Treating ruin roads as renderer-only decoration; using fixed edge midpoints; or porting the TS wall segment/gate geometry stack in full despite the current native `Structure` schema not carrying ring node/tag metadata.
Scalability Potential: Low gets cheap readable ruin entrances aligned to macro roads. Middle gets deterministic broken paths without new runtime work. High/Ultra can later render richer ruin gate meshes from the same gate-safe structure stream.
Hardware Impact: 0 us per frame. Generation-only cost is bounded by at most eight road-neighbour anchor carves and one protected-tile branch per ruin wall sample.

Problem: The Hecton documentation import became stale while this agent was working because other Hecton agents continued appending active task/log/report files. A single copy pass could honestly reach zero missing files, then become stale seconds later.
Solution: Kept the import quarantined under `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, refreshed missing/stale selected docs repeatedly, wrote prompt-owned manifests for every refresh, and recorded a final capture with 1918 selected Hecton source files. The settle loop reached a zero-missing / zero-stale state, then a later recheck proved Hecton was still live, so the final capture is documented with an active-writer caveat instead of a false static claim.
Rejected Alternatives: Moving/deleting Hecton source files; merging Hecton logs into active Timaert `Docs\AgentLogs`; pretending a live concurrently written log folder can be permanently verified by one snapshot.
Scalability Potential: Runtime unchanged. Organization improves because the imported corpus is searchable from Timaert while active Timaert task/log namespaces remain clean.
Hardware Impact: 0 us per frame; documentation-only. Disk footprint of the final captured import tree is 2,282 files / 133,732,143 bytes.

Problem: A new Hecton audit pass was required after the user explicitly warned not to write Timaert docs to Hecton. Filename search found no Hecton paths with Timaert/Samosbor/TMA labels, while content search found only three Hecton compute-logistics audit files that mention the Timaert import boundary.
Solution: Treated Hecton as read-only and recorded the result in Timaert-only reports. The three Hecton hits remain Hecton-domain audit references, not misplaced Timaert docs. No Hecton file was written, moved, or deleted in this pass.
Rejected Alternatives: Deleting Hecton audit files because they mention Timaert; copying false-positive Hecton audit records into active Timaert agent logs; writing status/log updates under `C:\hades\Hecton8`.
Scalability Potential: Runtime unchanged. Project separation improves because Hecton stays a read-only external source and Timaert stores its own reports under `C:\Timaert\timaert_c\Docs`.
Hardware Impact: 0 us per frame; documentation-only.

Problem: Feature texture upload already sanitized invalid bytes, but the rule lived inside `MacroRenderer::upload_features` instead of the feature-layer contract. That made CPU consumers and GL upload depend on separate implementations of the same corrupt-byte policy.
Solution: Added `FeatureLayer::has_invalid_cell_bytes()` and `FeatureLayer::copy_sanitized_cells()`, then switched the renderer to call those helpers. The helper scans only declared cells, requires complete backing storage for a full sanitized copy, and decodes unknown ids through the same `FeatureLayer::decode()` used by CPU systems.
Rejected Alternatives: Leaving the renderer-local loop in place; unconditionally copying every valid feature grid before upload; treating invalid complete-grid bytes as shader-visible decorations.
Scalability Potential: Low keeps direct R8 upload for valid data and fails corrupt data closed. Middle keeps one byte grid and no schema expansion. High/Ultra can add richer feature ids later without letting unknown ids leak into current shaders.
Hardware Impact: 0 us per frame. Normal complete valid upload remains direct. Corrupt complete upload performs the same O(cell count) upload-time scan class as before and only allocates/copies when invalid bytes exist.

Problem: The focused `road_river_generation_test` build failed at link time because the test now calls Politik generation/finalization APIs but the target did not list `src/macro/politik.cpp` or its language dependency.
Solution: Added `src/macro/politik.cpp` and `src/macro/language.cpp` to the `road_river_generation_test` target. This matches the APIs already present in the test source and keeps malformed-terrain Politik coverage buildable in isolation.
Rejected Alternatives: Removing the Politik malformed-terrain test; relying on the full application target to catch it indirectly; ignoring the focused target failure.
Scalability Potential: Runtime unchanged. Verification improves because the road/river target can now be built independently by future agents without hidden full-app linkage.
Hardware Impact: 0 us per frame; build-system correctness only.

Problem: The Hecton read-only scan changed again: it now found six Hecton compute/accounting/audit files mentioning Timaert/Samosbor/TMA terms, including an additional cooldown-check report.
Solution: Copied the six exact hit files into the Timaert quarantine import tree and wrote `MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP18.tsv` with SHA-256 hashes. Hecton was used only as a read source.
Rejected Alternatives: Writing any Timaert report into Hecton; moving/deleting Hecton files; merging Hecton compute/accounting records into active Timaert task/log namespaces.
Scalability Potential: Runtime unchanged. Project separation stays explicit while the external references remain searchable from the Timaert repo.
Hardware Impact: 0 us per frame; documentation-only.

Problem: `subworld_async_seam_test` exposed worker starvation: road smoothing could be queued after save snapshots had already occupied the seam worker pool, so the visual smooth publish missed the test window under Debug load.
Solution: Added active generation accounting and a save-job pause gate in `SeamlessSubworldManager`. During a seam crossing, generation jobs stay first, saves remain paused while placeholders exist, and road smoothing gets priority once all exposed cells are installed. Saves resume only after the smoothing opportunity is queued or skipped.
Rejected Alternatives: Weakening the test, increasing only the timeout, or running smoothing synchronously on `check_boundary`, which would reintroduce the seam hitch that `matwej.md` explicitly warns against.
Scalability Potential: Low-end machines get deterministic visible cell replacement before background saves. Middle machines avoid transient unsmoothed road composites. High/Ultra machines can spend the same worker path on heavier visual smoothing later without changing the seam contract.
Hardware Impact: 0 us per frame. Background worker ordering only. Latest Debug seam run exits 0 with generation slices `road=10.258ms`, `plain=32.342ms`, `diagonal=28.184ms`, `rapid_reversal=55.566ms`.

Problem: Async composite road smoothing still used per-road-tile 25x25 sampling and an unordered shoulder accumulator. It preserved visuals but was too expensive and variable when combined with Debug builds and full 3072x3072 composites.
Solution: Kept the same visual math but changed the implementation to an integral-image average for the 25x25 pass, 80 sparse Laplacian iterations over road indices, and deterministic sorted sparse shoulder accumulation. The worker receives pre-collected sorted road/square indices instead of rescanning the whole tile grid for every smooth job.
Rejected Alternatives: Reducing the smoothing radius or iteration count, which would change road visuals; keeping `unordered_map` shoulder accumulation; or moving the smooth pass back onto the seam path.
Scalability Potential: Low gets the same visual smoothing with less worker variance. Middle gets reliable async publish under load. High/Ultra can increase future road visual quality from a stable indexed worker path.
Hardware Impact: 0 us per frame. Async smoothing average pass changes from O(roadTiles * 625 samples) to O(fullComposite + roadTiles); for the measured road seam footprint this removes millions of repeated samples from the worker job.

Problem: `features.ts` writes tree features by flattened index (`y * width + x`) and only the read helper wraps toroidally. Native `build_feature_layer` computed the flattened index for validation, then called `FeatureLayer::set(t.x, t.y, FT_Tree)`, which could torus-wrap malformed tree coordinates to a different cell than TS.
Solution: Change the tree pass to range-check the flattened index with 64-bit arithmetic, keep the native water filter, and write `fl.data[i] = FT_Tree` directly. Add a regression proving `x == width` and negative-x flattened cases land on the TS cells and do not wrap through torus coordinates.
Rejected Alternatives: Clipping malformed tree coordinates before write was rejected because TS does not clip x/y before flattening. Keeping `FeatureLayer::set()` inside the builder was rejected because it makes the writer semantics differ from TS while hiding behind the lookup API's torus behavior.
Scalability Potential: Low keeps the same one-byte feature layer and O(tree count) generation pass. Middle/High/Ultra get safer feature data for macro render, road/dirt/settlement routing, and future overkill renderers without changing the feature schema.
Hardware Impact: 0 us per frame. Generation-only cost is unchanged in big-O; each tree now pays one 64-bit multiply/add and one direct byte store instead of a modulo-wrapping setter. Focused feature target, macro tests, full build, subworld tests, and app smoke all passed after the change.

Problem: The user repeated the Hecton/Timaert boundary requirement after the feature-layer audit. A fresh Hecton search was needed to avoid relying on stale import assumptions.
Solution: Re-ran read-only filename and content scans under `C:\hades\Hecton8`. Filename scan found no Timaert/Samosbor/TMA-labeled paths. Content scan found only the same Hecton compute-logistics audit references about the boundary/import process. Timaert reports stayed under `C:\Timaert\timaert_c\Docs`.
Rejected Alternatives: Writing any Timaert report to Hecton, deleting Hecton audit files because they mention Timaert, or copying false-positive Hecton audit logs into active Timaert `Docs\AgentLogs`.
Scalability Potential: Runtime unchanged. Project separation remains explicit: Hecton is only an external read-only source, while Timaert owns this C++ port's status, rationale, and logs.
Hardware Impact: 0 us per frame; documentation-only.

Problem: `features.ts` treats short road/dirt masks as valid prefix data because typed-array reads inside bounds return bytes and out-of-bounds reads evaluate false. Native `build_feature_layer` required mask size >= total and discarded the entire road or dirt pass when a mask was short, losing TS prefix semantics.
Solution: Replace all-or-nothing road/dirt mask validation with bounded prefix loops: `min(mask.size(), total)` for road and dirt. Full masks are unchanged; short masks stamp the valid prefix and stop safely.
Rejected Alternatives: Keeping all-or-nothing malformed-mask behavior was rejected because it disagreed with the TS source. Reading to `total` unguarded was rejected because native vectors cannot safely emulate out-of-range typed-array reads.
Scalability Potential: Low keeps the same single R8 feature grid and no heap growth. Middle/High/Ultra get deterministic feature byte semantics for future macro overlays and pathing consumers without extra schema.
Hardware Impact: 0 us per frame. Generation cost remains O(mask prefix length), with no allocation and fewer iterations when a mask is short. Full-build, macro, subworld, event/save/spell/combat, and smoke verification passed.

Problem: The architecture/translation docs only said malformed-mask safety, which was too vague after tightening TS prefix behavior.
Solution: Update `translation.md` and `ARCHITECTURE.md` to record TS flattened tree writes and short road/dirt prefix reads as the authoritative feature-layer contract.
Rejected Alternatives: Leaving stale docs that imply native may ignore short masks wholesale.
Scalability Potential: Runtime unchanged. Maintainers now have the exact byte-grid contract and tests to prevent future drift.
Hardware Impact: 0 us per frame; documentation-only.

Problem: After finding two feature-layer edge drifts in one pass, single-case tests were not enough evidence that the native byte-grid builder would stay aligned with `features.ts` under small maps, water filtering, malformed trees, and mask length variation.
Solution: Add a deterministic reference matrix test to `feature_layer_parity_test`. It builds a compact TS-contract reference implementation with the native water guard, runs varied sizes and generated input patterns, and byte-compares production output.
Rejected Alternatives: Only keeping hand-picked tests, or moving this into a slow app smoke where a single byte-grid mismatch would be hard to isolate.
Scalability Potential: Runtime unchanged. Test coverage now guards the feature byte contract that macro render, movement/pathing, zones, fauna, and subworld dispatch consume.
Hardware Impact: 0 us per frame; test-only. The focused target still builds/runs quickly, and full MSVC build plus runtime smoke passed after adding it.

Problem: The final verification sweep left `build-msvc` test/app processes alive after emitting output, which can lock executables and poison later build/link attempts.
Solution: Path-filtered the cleanup to `C:\Timaert\timaert_c\build-msvc\subworld_async_seam_test.exe` and `C:\Timaert\timaert_c\build-msvc\timaert.exe`, then stopped only those stale verification processes.
Rejected Alternatives: Leaving stale executable locks, or killing unrelated processes outside the current build output path.
Scalability Potential: Runtime unchanged. Build reproducibility improves for follow-up agents and local rebuilds.
Hardware Impact: 0 us per frame; verification hygiene only.

Problem: The feature byte grid feeds movement cost, zones, GL upload, subworld mode, fauna, and UI. A malformed or short `FeatureLayer` should not let downstream systems read beyond backing storage or reinterpret stale bytes.
Solution: Keep `FeatureLayer` API torus-safe with prefix reads, but require complete storage at raw-consumer boundaries. `build_cost_grid` and `generate_zones` ignore malformed feature storage as `FT_None`; `MacroRenderer::upload_features` uploads a 1x1 blank R8 feature texture when storage is incomplete. `pathfinding_parity_test` proves valid features apply, short/mismatched feature storage is ignored by cost/zone builders, and malformed terrain fails closed.
Rejected Alternatives: Revalidating every feature lookup inside hot loops, or trusting all callers to provide complete storage and letting malformed buffers reach raw pointer reads.
Scalability Potential: Low gets a single branch at subsystem boundary and no per-cell penalty beyond the existing feature loop. Middle/High/Ultra keep raw contiguous byte reads for valid feature layers while malformed data fails closed.
Hardware Impact: 0 us per frame on valid storage; one boundary branch before movement cost, zones, or feature texture upload. Prevents undefined reads without adding heap allocation.

Problem: The latest Hecton read-only content scan found two additional Hecton compute/accounting markdown reports mentioning Timaert prompt names, in addition to the known compute-logistics audit files. They are not active Timaert docs, but the user requested any Timaert/Samosbor material in Hecton be findable from Timaert.
Solution: Refreshed the exact five hit files into the quarantined import tree under `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs` and wrote a hash manifest at `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP16.tsv`. Source files under `C:\hades\Hecton8` were only read.
Rejected Alternatives: Writing Timaert reports into Hecton; moving/deleting Hecton compute docs; importing Hecton accounting/audit files into active Timaert task/log namespaces.
Scalability Potential: Runtime unchanged. Process separation improves because current Hecton references are searchable from Timaert without polluting active Timaert agent state.
Hardware Impact: 0 us per frame; documentation-only.

Problem: Complete feature storage can still contain corrupt or future feature byte ids. TS comparisons only recognize the known enum values, so unknown bytes behave like no matched feature. Native CPU consumers needed an explicit shared decoder to prevent invalid enum bytes from leaking through movement and zone logic.
Solution: Use `FeatureLayer::decode` for `FeatureLayer::at`, movement-cost feature reads, and zone feature reads. Unknown bytes map to `FT_None`; known bytes remain zero-copy raw byte storage. Focused tests now cover invalid-byte lookup, movement fallback to biome cost, preservation of following valid bytes, and invalid-byte no-op behavior in zones.
Rejected Alternatives: Letting arbitrary `FeatureType(value)` values flow through CPU systems, or sanitizing the whole feature grid into a new buffer before every consumer.
Scalability Potential: Low keeps a single byte grid and no heap allocation. Middle/High/Ultra keep contiguous feature storage and get deterministic handling for corrupt or future ids.
Hardware Impact: 0 us per frame in normal gameplay; generation/boot consumers pay one tiny switch per read when using raw feature bytes.

Problem: Hecton compute/accounting references to Timaert are active and can change between passes.
Solution: Refreshed the same five exact Hecton hit files into the Timaert quarantine again and wrote `Docs\Imported\Hecton8\MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP17.tsv` with matching SHA-256 hashes.
Rejected Alternatives: Treating the previous manifest as permanent while active Hecton logs move, writing to Hecton, or importing Hecton accounting files into active Timaert logs.
Scalability Potential: Runtime unchanged. Documentation provenance remains current without crossing game/project boundaries.
Hardware Impact: 0 us per frame; documentation-only.

Problem: Feature texture upload already sanitized invalid bytes, but the rule lived inside `MacroRenderer::upload_features` instead of the feature-layer contract. That made CPU consumers and GL upload depend on separate implementations of the same corrupt-byte policy.
Solution: Added `FeatureLayer::has_invalid_cell_bytes()` and `FeatureLayer::copy_sanitized_cells()`, then switched the renderer to call those helpers. The helper scans only declared cells, requires complete backing storage for a full sanitized copy, and decodes unknown ids through the same `FeatureLayer::decode()` used by CPU systems.
Rejected Alternatives: Leaving the renderer-local loop in place; unconditionally copying every valid feature grid before upload; treating invalid complete-grid bytes as shader-visible decorations.
Scalability Potential: Low keeps direct R8 upload for valid data and fails corrupt data closed. Middle keeps one byte grid and no schema expansion. High/Ultra can add richer feature ids later without letting unknown ids leak into current shaders.
Hardware Impact: 0 us per frame. Normal complete valid upload remains direct. Corrupt complete upload performs the same O(cell count) upload-time scan class as before and only allocates/copies when invalid bytes exist.

Problem: The focused `road_river_generation_test` build failed at link time because the test now calls Politik generation/finalization APIs but the target did not list `src/macro/politik.cpp` or its language dependency.
Solution: Added `src/macro/politik.cpp` and `src/macro/language.cpp` to the `road_river_generation_test` target. This matches the APIs already present in the test source and keeps malformed-terrain Politik coverage buildable in isolation.
Rejected Alternatives: Removing the Politik malformed-terrain test; relying on the full application target to catch it indirectly; ignoring the focused target failure.
Scalability Potential: Runtime unchanged. Verification improves because the road/river target can now be built independently by future agents without hidden full-app linkage.
Hardware Impact: 0 us per frame; build-system correctness only.

Problem: The Hecton read-only scan changed again: it now found six Hecton compute/accounting/audit files mentioning Timaert/Samosbor/TMA terms, including an additional cooldown-check report.
Solution: Copied the six exact hit files into the Timaert quarantine import tree and wrote `MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP18.tsv` with SHA-256 hashes. Hecton was used only as a read source.
Rejected Alternatives: Writing any Timaert report into Hecton; moving/deleting Hecton files; merging Hecton compute/accounting records into active Timaert task/log namespaces.
Scalability Potential: Runtime unchanged. Project separation stays explicit while the external references remain searchable from the Timaert repo.
Hardware Impact: 0 us per frame; documentation-only.

Problem: The feature texture upload path scanned once to detect invalid bytes and then scanned/copied again through `copy_sanitized_cells()`. That was correct but wasteful for corrupt complete grids and left the renderer coordinating byte sanitation decisions.
Solution: Added `FeatureLayer::complete_cells_or_sanitized()`. It returns the original complete data pointer for valid grids, returns sanitized scratch for corrupt complete grids, and returns `nullptr` for incomplete storage. `MacroRenderer::upload_features()` now uses that single API.
Rejected Alternatives: Keeping the two-step renderer scan; unconditionally copying every feature grid; dropping the explicit `copy_sanitized_cells()` helper that remains useful for direct tests and non-upload callers.
Scalability Potential: Low-end keeps zero-copy uploads on valid maps and no per-frame cost. Middle/High/Ultra get a stricter feature-byte boundary that can absorb future feature ids without shader-visible corruption.
Hardware Impact: 0 us per frame. Valid map upload is direct. Corrupt complete grids avoid the previous detect-then-copy double pass and allocate/copy only when invalid bytes exist.

Problem: Full MSVC build failed in `npc_spawn_contract_test` because gameplay headers reached `gl/gl.h`, but the imported `SDL2::SDL2` target did not propagate include directories to that test target in this environment.
Solution: Derived `TIMAERT_SDL2_INCLUDE_DIRS` from `SDL2::SDL2` when available, otherwise from `SDL2_DIR`, and registered it once as a system include before targets are declared. Full build and `npc_spawn_contract_test` now pass.
Rejected Alternatives: Ignoring a full-build failure because it was outside the feature test; hard-coding only `C:\dev\SDL2...` into one target; removing the test's coverage.
Scalability Potential: Runtime unchanged. Build reproducibility improves for every target that includes `map_generator.h` or `gl/gl.h` through gameplay headers.
Hardware Impact: 0 us per frame; build configuration only.

Problem: Hecton audit/accounting references changed again during this pass.
Solution: Re-ran the Hecton scan read-only, copied the seven current hit files into the Timaert quarantine, and wrote `MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP19.tsv` with SHA-256 hashes.
Rejected Alternatives: Writing Timaert docs/logs to Hecton; moving/deleting Hecton files; treating Hecton audit docs as active Timaert task state.
Scalability Potential: Runtime unchanged. Timaert can search imported external references without crossing project boundaries.
Hardware Impact: 0 us per frame; documentation-only.

Problem: Feature coordinate wrapping is a shared low-level contract for torus-safe lookup/set calls. The existing callers were guarded, but the utility still did narrow signed modulo work directly on hostile inputs while the tests already document `INT_MIN` as a required safe case.
Solution: Move the modulo arithmetic inside `FeatureLayer::wrap_coord()` to `std::int64_t`, keep the positive-remainder normalization, and keep malformed dimension/storage cases fail-closed at the API boundary.
Rejected Alternatives: Duplicating wrap math in callers; clipping hostile coordinates before wrap; or changing the `features.ts`-compatible torus lookup contract.
Scalability Potential: Low keeps a single byte grid and no heap work. Middle/High/Ultra keep stable feature lookup behavior for renderer, movement, zones, fauna, and future richer feature consumers.
Hardware Impact: 0 us per frame. The cost is one 64-bit modulo inside explicit feature lookup/set calls, not inside the normal zero-copy texture upload path.

Problem: The renderer upload path had already moved to `FeatureLayer::complete_cells_or_sanitized()`, but a redundant boundary check could creep back in and split responsibility between renderer and feature-layer API.
Solution: Keep `MacroRenderer::upload_features()` as a narrow consumer of the feature-layer upload view: valid grids use the original pointer, corrupt complete grids use sanitized scratch, and incomplete grids upload a 1x1 blank R8 texture.
Rejected Alternatives: Reintroducing renderer-side storage validation before the shared API, or copying every grid unconditionally before upload.
Scalability Potential: Low gets zero-copy valid uploads and fail-closed malformed data. Middle/High/Ultra can add richer visual feature ids later while unknown current ids still collapse to `FT_None`.
Hardware Impact: 0 us per frame. Valid upload is direct. Corrupt upload remains upload-time only and copies only when invalid bytes are present.

Problem: A full-build failure was inherited for `MacroRenderer::draw`, but the current source declaration and definition had already converged on the `seaLevel` parameter after concurrent edits.
Solution: Audited the header, implementation, and call site, rejected a fake compatibility overload, and reran the full MSVC build. The build exited 0 and relinked `timaert.exe`.
Rejected Alternatives: Adding an unused overload to satisfy a stale diagnostic, or weakening the renderer API back to a hard-coded sea level.
Scalability Potential: Runtime unchanged. Keeping one renderer signature preserves the path for low-end fixed sea level and high-end richer water/river rendering.
Hardware Impact: 0 us per frame; verification/build hygiene only.

Problem: Hecton references to Timaert/Samosbor/TMA terms are still external audit/accounting files and can change while Hecton agents run.
Solution: Re-ran the Hecton scan read-only, copied the seven current hit files into the Timaert quarantine, and wrote `MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP20.tsv` with matching SHA-256 hashes.
Rejected Alternatives: Writing any Timaert report into Hecton; moving/deleting Hecton files; or importing Hecton audit/accounting files into active Timaert task/log namespaces.
Scalability Potential: Runtime unchanged. Timaert keeps searchable provenance while Hecton remains a separate game/project.
Hardware Impact: 0 us per frame; documentation-only.

Problem: TS grassland, forest, swamp, and mountain generators carve trails or passes toward `edgeAnchors` derived from neighbouring road cells, but native wilderness modes only scattered terrain/trees unless the current macro cell itself resolved to `Road`.
Solution: Pass the decoded 3x3 feature neighbourhood into grassland/open, forest, swamp, and mountain generators. Add `carve_wilderness_anchor_trails()` as a bounded helper that collects road/dirt-road neighbours, computes the same deterministic TS 35-65% edge anchors, and reuses the existing organic road raster from cell center to each anchor.
Rejected Alternatives: Promoting every road-adjacent wilderness cell to `SubworldMode::Road`, which would erase forest/swamp/mountain biome identity; adding a new unbounded TS mycelium-style path system; or leaving trails as renderer-only decoration with no tile/traversal data.
Scalability Potential: Low gets at most eight short generation-only trail rasters per affected cell and keeps seam crossing on the worker path. Middle/High/Ultra get visibly continuous roads through forests, swamps, grasslands, and mountain passes without changing renderer or save schema.
Hardware Impact: 0 us per frame. The measured real app seam smoke still reports seam-path `smooth=0.000ms`; Debug generation for the real seam was `33.678ms`, with the remaining `139.683ms` cost in 3D upload, not this generator slice.

Problem: The new wilderness trail parity needed isolated proof, not just a passed app smoke.
Solution: Added focused `subworld_generator_parity_test` invariants for grassland east/west trails, forest north/south paths, swamp single-neighbour muddy path, and mountain single-neighbour pass. The test checks the exact deterministic edge-anchor tiles and the center road tile.
Rejected Alternatives: Counting total road tiles only, which would miss wrong edge anchors; relying on city/village/road tests, which do not exercise wilderness modes.
Scalability Potential: Runtime unchanged. The test locks the edge-anchor contract so future generator changes cannot silently break macro-to-subworld continuity.
Hardware Impact: 0 us per frame; verification-only.

Problem: Hecton audit/accounting references changed again during this pass.
Solution: Re-ran the Hecton scan read-only, copied the ten current hit files into the Timaert quarantine, and wrote `MANIFEST_TMA_SUBWORLD_GENERATOR_PARITY_BKR_LOOP21.tsv` with matching SHA-256 hashes.
Rejected Alternatives: Writing Timaert docs/logs to Hecton; moving/deleting Hecton files; or importing Hecton audit/accounting files into active Timaert task/log namespaces.
Scalability Potential: Runtime unchanged. Timaert keeps searchable external provenance without crossing the two-game boundary.
Hardware Impact: 0 us per frame; documentation-only.
