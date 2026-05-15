# Rationale: TMA_INTEGRATION_SENTINEL

Problem: Parallel agents left the workspace dirty and partially conflicting. `build-msvc` was repeatedly locked by running executables, and source files were modified while integration was running.

Solution: Created `build-msvc-integrator` as an isolated proof build. Fixed compile errors locally instead of killing other agents' processes. Used focused tests and runtime smoke logs as evidence. Reapplied road generation as land-component pruning plus a 4096-expansion cap inside the existing large-map A* path.

Rejected Alternatives: Killing `timaert.exe` and other build processes would have invalidated other agents' work. Keeping a broken bounded road rewrite would have produced a non-compiling tree.

Scalability potential: Low tier now gets bounded route proof instead of full-map searches. High/Ultra can later spend saved worldgen budget on prettier road overlays, bridges, or settlement approach decals without changing the movement graph contract.

Hardware Impact: Component pruning avoids impossible cross-island searches. Bounded large-map road search reduced observed expansions from multi-million ranges to hundreds-of-thousands in sampled 1024x1024 smoke seeds, which is the correct direction for i3/MX350-class machines.

Problem: Native Codex was weaker than TS: pause menu could not open it, and the overlay only displayed unlocked ids.

Solution: Ported the TS CodexOverlay category/article data into constexpr C++ tables and routed pause `Codex` through the shell result into the normal overlay path.

Rejected Alternatives: Loading a JSON/text file at runtime would add I/O and parsing for eight fixed starter articles. A dynamic article vector per frame would add avoidable UI churn.

Scalability potential: Low tier pays only static table reads and the already-open ImGui frame. High/Ultra can later spend UI budget on richer Codex art without changing the unlock contract.

Hardware Impact: No measurable frame gain claimed; this is a parity and quality fix. Runtime proof is `open_codex` smoke seed 106, not a performance benchmark.

Problem: Trade parity had code-level claims but no runtime smoke entrypoint proving both settlement and NPC trade surfaces could open from a generated world. The same smoke exposed a road regression: seed 107 reported 1,928,000 road expansions after a parallel overwrite restored unbounded full-map A*.

Solution: Added smoke-only actions `open_settlement_trade` and `open_npc_trade`, plus `ui::open_npc_trade_panel(entt::entity)` so the smoke can open the same native popup the proximity panel uses. Reapplied road protection as a minimal cap inside `find_road_path`: small maps retain full search; large maps stop at 4096 expansions and prune unproven same-component detours.

Rejected Alternatives: A separate full-screen TradeOverlay clone would duplicate native panel UX without adding gameplay parity. Reintroducing the direct-line proof patch was rejected for this pass because it was repeatedly overwritten by concurrent sessions; the narrower internal cap changes fewer call sites and survived rebuild/smoke verification.

Scalability potential: Low tier avoids pathological boot-time route searches. High/Ultra can later spend saved boot budget on nicer road visuals or bridge decals, but topology remains deterministic because over-budget routes are pruned rather than half-stamped.

Hardware Impact: Seed 107 road expansions dropped from 1,928,000 to 344,885 in the trade-surface smoke. No microseconds are claimed; expansion count is the measured evidence.

Problem: During the post-agent audit, external builds/agents modified key C++ files while integration was running. `spawners.cpp` was repeatedly reverted to unbounded road A*, and an older `subworld_async_seam_test.exe` still emitted unconditional `debug shutdown` lines even though the current `seamless_manager.cpp` no longer contained those strings.

Solution: Waited for `cl`/`cmake`/`ninja`/`timaert` processes to go quiet, rechecked current source markers, rebuilt current `timaert`, `road_river_generation_test`, `subworld_async_seam_test`, `save_roundtrip_test`, and `quest_lifecycle_test`, then reran focused tests and smoke from the freshly linked executables.

Rejected Alternatives: Trusting a stale executable would have hidden whether source and binary matched. Marking `subworld/seamless-manager.ts` as complete would be false: native worker placeholders are verified, but TS-style preloaded outer ring is not fully ported.

Scalability potential: Current seam generation is no longer main-thread single-cell rebuild; it uses worker jobs, placeholder cells, stale-work pruning, async save jobs, and async composite smoothing. The remaining scalability target is ahead-of-boundary ring preload to avoid even short placeholder exposure.

Hardware Impact: Current `subworld_async_seam_test` after rebuild reports road/plain/diagonal/reversal generation in roughly 24-74 ms slices on this machine, with no unconditional debug shutdown spam. Exact frame cost is not claimed.

---

Problem: Late external agents kept building and editing after the first post-agent audit. Their reports claimed road/feature parity and async seam upload improvements, but reports alone do not prove the current source still contains those changes.

Solution: Waited for external `cmake`/`cl`/`ninja` activity to go quiet, then rechecked source markers directly. Verified current source still has the road 4096 large-map cap, feature parity test target, async seam `TIMAERT_SEAM_TRACE`/upload dirty flags, and `subworld_seam` smoke action. Rebuilt and ran the missing `feature_layer_parity_test` target in the integrator build.

Rejected Alternatives: Trusting `LOG_TMA_*.md` without current-source marker checks was rejected because previous agents had already overwritten `spawners.cpp` once. Treating the absent integrator `feature_layer_parity_test.exe` as a runtime failure was rejected after the log showed the executable was simply not built in that build directory yet.

Scalability potential: Low tier keeps bounded road generation and avoids inactive seam renderer uploads. High/Ultra can spend the saved frame budget on richer 3D presentation, but the current Debug trace still leaves 3D upload optimization work.

Hardware Impact: Seed 109 app seam smoke reports `[seam-cross] gen=111.422ms smooth=0.000ms upload3d=448.893ms upload2d=0.000ms total=562.822ms`. This proves the inactive 2D upload is gone on the 3D seam path, but the remaining 3D upload cost is still too high to call final.

Problem: `SeamlessSubworldManager` still stored its cell resolver in `std::function`, leaving heap-capable type erasure in a boundary-generation path even though the project already had `SmallFunction`.

Solution: Replaced `CellResolver` with `sm::SmallFunction<CellContext(int,int)>` and fixed `SmallFunction::emplace` to accept lvalue callables by value before inline move construction. This preserves resolver behavior and removes the remaining `std::function` from the seam manager.

Rejected Alternatives: Leaving `std::function` in place was rejected because EventBus had already moved to `SmallFunction` for the same no-heap direction. Hand-rolling a function pointer plus context pointer was rejected because the engine resolver captures multiple references and the existing `SmallFunction` already covers that safely.

Scalability potential: Low tier avoids allocator-capable resolver storage in the seam path. High/Ultra behavior is unchanged; this is a plumbing/robustness fix, not a visual feature.

Hardware Impact: No microseconds claimed. Verification is compile/test evidence: `timaert`, `subworld_async_seam_test`, and runtime smokes pass after the resolver replacement.

Problem: `snapshot_subworld` quantized every height sample through `std::lround`. Height values are clamped to `[0,1]` before quantization, so the full C math rounding path is unnecessary for millions of positive samples.

Solution: Replaced `std::lround(v * 65535.0f)` with `std::uint16_t(v * 65535.0f + 0.5f)`. For clamped non-negative values this preserves the save format result while avoiding the heavier libm path.

Rejected Alternatives: Changing the saved height format was rejected because save compatibility matters. Skipping quantization was rejected because the TS/native snapshot contract stores compact 16-bit height data.

Scalability potential: Low tier benefits during subworld snapshot/save bursts. High/Ultra can keep the same restore fidelity with less CPU wasted in quantization.

Hardware Impact: No honest wall-time win claimed from the current Debug test because `subworld_async_seam_test` still spends about 100 seconds in its full polling/teardown scenario. Correctness proof remains: `save_roundtrip_test` and `worker_restore_saved` pass after the change.
