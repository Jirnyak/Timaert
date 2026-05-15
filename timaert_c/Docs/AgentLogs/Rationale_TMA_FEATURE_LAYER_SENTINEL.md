# Rationale: TMA_FEATURE_LAYER_SENTINEL

Problem: `features.ts` has only five valid feature IDs, but native complete feature storage could still carry arbitrary bytes if data was malformed or produced by future tools. `FeatureLayer::at()` and core consumers decoded raw bytes directly.

Solution: Added `FeatureLayer::decode()` and routed lookup, pathfinding cost-grid generation, and zone generation through it. Unknown values now become `FT_None`, preserving TS gameplay semantics instead of inventing a hidden sixth feature type.

Rejected Alternatives: Sanitising the entire feature vector on every upload was rejected because the current builder already produces valid bytes and upload is not the only consumer. Letting each consumer handle its own default was rejected because it spreads policy across pathfinding, zones, UI, and subworld routing.

Scalability potential: Low tier gets predictable fail-closed data with no per-frame scan. High/Ultra keeps the same feature byte map and can layer richer visuals in renderer code without changing gameplay consumers.

Hardware Impact: No microseconds claimed. The extra decode is a small switch only when reading feature bytes for generation-time systems or hover/subworld lookup; it is not a new frame-wide simulation pass.

Problem: `build_feature_layer` compared `td.rgba.size()` against `total * 4` without an explicit multiplication overflow guard.

Solution: Validated `FeatureLayer::cell_count_for()` and `total <= max_size / 4` before the RGBA byte-count check.

Rejected Alternatives: Trusting terrain dimensions was rejected because feature generation is a boundary between generated terrain and multiple runtime consumers. Adding a hard arbitrary max map size was rejected because existing project docs do not define one.

Scalability potential: Low-tier or malformed-load paths fail closed instead of entering undefined reads. High-tier maps keep the same path when dimensions are valid.

Hardware Impact: No runtime frame cost. The guard runs once during feature-layer construction.

Problem: Hecton and Timaert/Samosbor docs were historically mixed by agent imports, and the user explicitly repeated the separation rule.

Solution: Created a dedicated exact-match quarantine under `Docs\Imported\Hecton8_Timaert_Samosbor\2026-05-15_exact_matches` and copied only files matching Timaert/Samosbor/TMA by path or content. Hecton remained read-only.

Rejected Alternatives: Writing reports back into Hecton was rejected because it violates the two-games boundary. Dumping exact-match files into active `Docs\Tasks` or `Docs\AgentLogs` was rejected because imported Hecton-origin material is evidence, not active Timaert task state.

Scalability potential: Future agents can inspect the exact-match quarantine without scanning the whole Hecton Unity project.

Hardware Impact: 0 runtime impact; documentation-only import.

Problem: `FeatureLayer::at()` and downstream consumers rejected unknown feature bytes, but `FeatureLayer::set()` still accepted an invalid `FeatureType` cast and wrote the raw byte. That left a local API path able to create data that the rest of the system had to clean up later.

Solution: Locked the five TS enum byte values with `static_assert` and routed `FeatureLayer::set()` through `decode()` before writing. Invalid enum casts now become `FT_None` immediately.

Rejected Alternatives: Leaving setter validation to callers was rejected because this is the central byte-grid API. Scanning the whole feature buffer after every write was rejected as unnecessary; single-cell sanitize is enough.

Scalability potential: Low tier avoids invalid feature state at source. High/Ultra renderer paths can trust the byte contract and spend budget on richer drawing, not defensive gameplay interpretation.

Hardware Impact: No measurable frame cost. Setter is generation/setup code, and the extra single-byte switch is cheaper than propagating invalid state into pathfinding, zones, or GPU upload.

Problem: A full MSVC build after the agent changes re-ran CMake and failed before compiling project code because FetchContent tried to update `stb` from GitHub. One transient network reset made the entire local C++ verification fail.

Solution: Set `FETCHCONTENT_UPDATES_DISCONNECTED` to `ON` in the CMake cache before dependency declarations. Existing populated `_deps` are reused during incremental builds, while first-time population still has a normal download path.

Rejected Alternatives: Treating the failure as "network only" was rejected because it blocks reproducible local verification. Vendoring new dependency copies was rejected because the dependencies are already populated in this build tree and broad dependency reshuffles are unnecessary here.

Scalability potential: More agents can rebuild locally without forcing remote updates during every regenerate, reducing CI/developer noise while preserving the current dependency graph.

Hardware Impact: 0 runtime impact. Build-time reliability only.

Problem: Native macro rendering drew tree, mountain, and landmark overlays as separate whole-map passes, but TS `renderer.ts` paints nearby decorations in one 3x3 per-cell painter loop. Separate passes can put a farther landmark over a nearer tree/mountain, which is a visible parity error even when the feature byte grid is correct.

Solution: Added a single `decorationOverlay()` in the macro fragment shader. It iterates the 3x3 candidate cells far-to-near and draws tree -> mountain -> landmark inside each cell, matching the TS painter contract while keeping road/dirt overlays earlier.

Rejected Alternatives: Adding z-buffered quads for macro features was rejected because macro features are procedural shader decorations, not world meshes. Keeping the old global overlay order was rejected because it was visibly order-dependent and not TS-faithful.

Scalability potential: Low tier keeps one fullscreen shader pass and a small fixed 3x3 loop. High/Ultra can enrich the per-cell draw functions without changing gameplay feature storage or draw-order semantics.

Hardware Impact: No microseconds claimed. The loop grows from independent 2x2-style decoration passes to one 3x3 painter pass in the existing fullscreen shader; app smoke verified shader link/runtime visibility.

Problem: The Hecton import quarantine was polluted by a failed refresh that used an unavailable PowerShell relative-path API and copied thousands of source files directly into the quarantine top level.

Solution: Removed only verified top-level duplicate copies inside `Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, reran the import with explicit `Substring` relative paths, mapped Hecton root files into `Root\`, and refreshed active exact-match buckets with SHA-256 proof.

Rejected Alternatives: Writing cleanup notes or moved docs back into Hecton was rejected because the user explicitly separated the two games. Leaving top-level duplicates was rejected because it would make future scans treat quarantine artifacts as root-owned active docs.

Scalability potential: Future agents can re-run exact or broad import scans without manually untangling malformed top-level copies.

Hardware Impact: 0 runtime impact. Disk cleanup removed `284854493` duplicate bytes from the import quarantine.

Problem: The TS gameplay path builds features with `gState.mapParams.snowLevel - 0.05`; TS default `snowLevel` is `0.80`, so the default mountain feature threshold is `0.75`. Native boot and macro shader upload still used `0.78`, causing fewer mountain feature cells and a renderer threshold that did not match the TS default.

Solution: Added `kDefaultFeatureMountainThreshold = 0.75f` to the feature contract and used it for both `build_feature_layer()` in boot and the macro renderer `u_mtnThreshold` uniform. Updated the boundary test to prove byte 191 stays below threshold and byte 192 enters the mountain pass.

Rejected Alternatives: Keeping `0.78` was rejected because no root doc or TS source justifies it for `features.ts`; it was a silent native drift. Adding a new map-parameter field in this pass was rejected because that touches save/UI schema and belongs to a separate `LayerParameters` parity task.

Scalability potential: Low tier and high tier now consume the same mountain feature mask. Visual overkill can enrich mountain drawing without drifting gameplay cell classification.

Hardware Impact: No measurable frame cost. This is a constant change and removes a parity mismatch, not a new loop.

Problem: `FeatureLayer::at()` and `FeatureLayer::set()` still used the TS-looking wrap formula `((x % width) + width) % width`. In C++ that can overflow `int` when a malformed layer has a pathological positive width and a positive remainder near `INT_MAX`, even though malformed storage is supposed to fail closed.

Solution: Added `FeatureLayer::wrap_coord()` with a single modulo plus conditional add. `at()` and `set()` now use it before computing the backing index.

Rejected Alternatives: Keeping the TS expression verbatim was rejected because JavaScript number arithmetic does not map to C++ signed-overflow rules. Adding wider arithmetic at every call site was rejected because the central `FeatureLayer` API owns the torus lookup contract.

Scalability potential: Normal maps keep the same torus behavior. Malformed or tool-generated layers fail closed instead of creating undefined behavior in debug or release builds.

Hardware Impact: No meaningful frame cost. One modulo remains; the second modulo is removed.

Problem: Full native build exposed a concurrent agent regression: `renderer_3d.cpp` called `glDisableVertexAttribArray`, but the Win32 OpenGL loader only declared/loaded `glEnableVertexAttribArray`. Windows builds therefore failed outside the feature domain.

Solution: Added `DisableVertexAttribArray` to `glp`, loaded it through `SDL_GL_GetProcAddress`, and mapped `glDisableVertexAttribArray` in `gl.h`.

Rejected Alternatives: Removing the renderer call was rejected because the terrain VAO path intentionally disables stale attribute 1 after rebinding attribute 0. Depending on platform header prototypes was rejected because this project uses a local Win32 GL proc table.

Scalability potential: Renderer agents can use the expected GL attribute API without creating platform-specific compile failures.

Hardware Impact: 0 runtime simulation cost. One GL proc pointer is loaded at startup.

Problem: `FETCHCONTENT_UPDATES_DISCONNECTED` was added without `FORCE`, so an existing CMake cache could keep dependency updates connected and still try a live `stb` GitHub fetch during regenerate.

Solution: Set the normal variable and force the cache entry to `ON`. Incremental builds now reuse populated `_deps` instead of depending on the network.

Rejected Alternatives: Passing `-DFETCHCONTENT_UPDATES_DISCONNECTED=ON` manually was rejected because agents need the repo build command to be reliable without hidden local flags.

Scalability potential: Parallel agents can rebuild repeatedly without random network outages masquerading as C++ regressions.

Hardware Impact: 0 runtime impact. Build reliability only.

Problem: `boot_world()` passed `app.terrain.rgba.data()` to `trace_dirt_roads()` without the matching byte count, even after the dirt-road API gained `landMaskByteCount`. That left the boot path relying on pointer trust instead of the new malformed-buffer guard.

Solution: Pass `app.terrain.rgba.size()` with the RGBA pointer. The dirt-road tracer can now reject too-short non-zero land masks when callers provide a count.

Rejected Alternatives: Leaving the pointer-only call was rejected because it bypasses the documented boundary check. Reworking `trace_dirt_roads()` to reject every pointer with zero count was rejected because existing tests and optional API usage rely on `nullptr`/default-count behavior.

Scalability potential: Custom map and future streaming paths can carry explicit buffer sizes through feature-adjacent road generation without changing normal road/dirt visuals.

Hardware Impact: 0 runtime cost beyond passing one `std::size_t` argument.

Problem: Feature-adjacent native systems still mixed the active terrain sea level with hard-coded `0.40f` assumptions. Custom maps could generate terrain with one waterline, then classify tree spawns, road components, feature stamping, path-cost water, or macro shader water against another.

Solution: Threaded the active `LayerParameters::seaLevel` through app boot into `spawn_trees()`, `trace_roads()`, `build_feature_layer()`, and `build_cost_grid()`, and through frame rendering into `MacroRenderer::draw()`. The headers keep `0.40f` defaults for existing tools/tests, but the normal runtime path now uses the current map parameter.

Rejected Alternatives: Keeping byte sea-level tests only was rejected because `height / 255 < seaLevel` is the TS gameplay comparison in movement cost and avoids byte-rounding drift. Trusting alpha only was rejected because malformed/custom terrain buffers can disagree with the red-channel waterline. Hard-coding `0.40f` in the shader was rejected because it makes custom-game sea level visually lie.

Scalability potential: Low-tier machines keep cheap scalar comparisons and the same single fullscreen macro shader pass. Middle/High/Ultra can spend saved coherence on richer water/shore visuals without gameplay, roads, and features disagreeing about where land begins.

Hardware Impact: No measured microsecond saving is claimed. The change moves an existing scalar from a constant/default to an active parameter and prevents misclassified feature/road work; all added tests are offline verification, not frame cost.
