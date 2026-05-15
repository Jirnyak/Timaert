# LOG: TMA_INTEGRATION_SENTINEL

What was wrong:
- `build-msvc` could not reliably link because other agents/users had `timaert.exe` running.
- MSVC compile failed on `main.cpp` because `findSmokeHostile` returned `entt::entity` in one branch and `entt::null_t` in another.
- MSVC compile failed on `SubworldEngine::resolve_subworld_deaths` because the implementation did not match the header signature.
- Road tracing had a seed-level hang/stall risk from full-map A* across impossible island pairs.
- Concurrent sessions repeatedly rewrote `spawners.cpp` / `spawners.h`, invalidating the first bounded road optimization attempt.

What was done:
- Created and used `build-msvc-integrator` to avoid executable locks.
- Fixed `findSmokeHostile` with explicit `-> entt::entity`.
- Fixed `resolve_subworld_deaths(bool drainAll)` and made `leave()` drain dead subworld entities in bounded batches.
- Kept land-component pruning in road tracing and exposed `componentPrunedEdges`.
- Reapplied bounded large-map road tracing: direct land-line proof first, then capped A* using a 512..4096 expansion budget, then prune if no route is proven.
- Ported native Codex from a raw unlocked-id list to the TS category/article content table, added the missing pause-menu Codex action, and made Escape close Codex before falling through to Pause.
- Verified current stable tree with focused test sweep and runtime smoke.

Cinematic cheats used:
- Road island mismatch is rejected by a land-component precheck instead of asking A* to prove impossibility over the full map.
- Runtime boot tracing is gated behind `TIMAERT_BOOT_TRACE` so normal runs do not pay log cost.

Exact microseconds saved:
- Not honestly measurable from this integration pass. Evidence is expansion count, not microseconds.
- Seed 1: old observed `expansions=3544426`; current bounded smoke `expansions=242873`.
- Seed 103: old observed `expansions=2743145`; current bounded smoke `expansions=307345`.
- Seed 12636737: current bounded smoke `expansions=270752`.
- Codex UI: no microseconds claimed; parity/quality fix with static constexpr tables.

Verification:
- `road_river_generation_test.exe`: PASS.
- Full focused suite from `build-msvc-integrator`: PASS.
- `TIMAERT_SMOKE_SEED=1`, `new_game,wait_boot_done,quit`: PASS with bounded road search.
- Gameplay smoke after bounded road reapply: spells, story, battle/subworld, and settlement UI capture all PASS.
- Focused suite after bounded road reapply: PASS.
- `cmake --build build-msvc-integrator --target timaert`: PASS after Codex/Escape changes.
- `TIMAERT_SMOKE_SEED=106`, `new_game,wait_boot_done,open_codex,capture_frame,quit`: PASS after Codex/Escape changes, captured `smoke_03_ui.ppm`.

---

What was wrong:
- TradeOverlay parity had native implementation evidence from the owning agent, but integration lacked direct runtime smoke actions for settlement Trade and NPC Trade.
- Re-running trade smoke exposed a road performance regression: seed 107 logged `[roads] ... expansions=1928000`.
- A broader direct-line + capped-A* road patch was repeatedly overwritten by concurrent work, so the tree could not honestly be reported as retaining that exact optimization.

What was done:
- Added `open_settlement_trade` and `open_npc_trade` smoke actions in `src/app/main.cpp`.
- Added `sm::ui::open_npc_trade_panel(entt::entity)` and routed the proximity-panel Trade button through it, keeping smoke and UI on the same popup path.
- Reapplied a narrower road guard inside `src/macro/spawners.cpp`: maps up to 65,536 cells keep full A*, while larger maps cap each route at 4096 expansions.
- Updated `tests/road_river_generation_test.cpp` so the large same-component detour test now asserts budget pruning instead of full-map detour discovery.
- Corrected `translation.md`: TradeOverlay behaviour is covered by native settlement/NPC trade surfaces, and the intro row now names the real TS source (`plot/intro.ts` + `StoryOverlay.svelte`), not nonexistent `IntroOverlay.svelte`.

Cinematic cheats used:
- Large same-island road detours are not proven with full-map A*. They are treated as over-budget and pruned, preserving deterministic topology and boot time.
- TradeOverlay is not cloned as a standalone full-screen wrapper; the native panel surfaces execute the same player-visible trade behaviour where the player actually interacts with a settlement/NPC.

Exact microseconds saved:
- Not honestly measurable from this pass.
- Seed 107 road expansions before guard: 1,928,000.
- Seed 107 road expansions after guard: 344,885.

Verification:
- `cmake --build build-msvc-integrator --target road_river_generation_test timaert -- -j1`: PASS (`Docs/AgentLogs/integrator_rebuild_road_cap_trade_hooks.log`).
- `build-msvc-integrator/road_river_generation_test.exe`: PASS (`Docs/AgentLogs/integrator_road_test_road_cap_trade_hooks.log`).
- `TIMAERT_SMOKE_SEED=107`, `new_game,wait_boot_done,open_settlement_trade,wait_visible,capture_frame,open_npc_trade,wait_visible,capture_frame,quit`: PASS (`Docs/AgentLogs/integrator_smoke_trade_surfaces_road_cap_clean.log`).
- `git diff --check`: exit 0; CRLF warnings only.

---

What was wrong:
- The user's concern was valid: external agents/builds were changing important C++ files while integration was running.
- `src/macro/spawners.cpp` was repeatedly overwritten back to unbounded full-map A*, which produced seed 108 boot smoke `[roads] ... expansions=7535787` before the cap was restored.
- A stale `subworld_async_seam_test.exe` emitted unconditional `debug shutdown` lines even though the current `src/sub/seamless_manager.cpp` source no longer contained them.
- The translation ledger overstated some areas by age: seamless subworld is improved and verified, but it is not yet TS preloaded-ring parity.

What was done:
- Waited until external `cl`/`cmake`/`ninja`/`timaert` processes went quiet before trusting source or binaries.
- Rechecked source markers: road cap still present, road budget test still expects over-budget prune, and current `seamless_manager` has no `debug shutdown` string.
- Rebuilt current `timaert`, `road_river_generation_test`, `subworld_async_seam_test`, `save_roundtrip_test`, and `quest_lifecycle_test` in `build-msvc-integrator`.
- Audited high-risk diffs. `army.h` deletion is intentional and correct: it removes forbidden UnitType/RPS army schema in favor of NPC-as-soldier records. `EventBus`/`LogicNodes` changes are improvements: `SmallFunction`, dispatch-safe subscription mutation, and explicit node activation. `seamless_manager` is an improvement over single-thread generation, but not full TS preload parity.
- Added native schema values for the remaining TS event tags and save round-trip proof for them: `NpcHpChange`, `SettlementMoodChange`, `PlayerStatChange`, `BattleEnd`, `MagicSurge`, `FactionRelationChange`, `DialogStart`, and `CameraMove`.

Cinematic cheats used:
- Road search still uses a hard large-map proof budget instead of proving every huge same-island detour.
- Seamless subworld currently uses deterministic flat placeholders while worker jobs finish, instead of blocking the frame on a synchronous full cell generation.

Exact microseconds saved:
- Not honestly measured.
- Seed 108 road expansions after restoring cap: 423,890.
- Seed 107 trade smoke road expansions after restoring cap: 344,885.
- Current async seam focused test reports generation slices: road 41.266 ms, plain 31.427 ms, diagonal 73.608 ms, rapid reversal 24.220 ms.

Verification:
- `cmake --build build-msvc-integrator --target timaert road_river_generation_test subworld_async_seam_test save_roundtrip_test quest_lifecycle_test -- -j1`: PASS (`Docs/AgentLogs/integrator_rebuild_current_after_external_agents.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_current_after_external_agents.log`).
- `save_roundtrip_test`: PASS, 2126-byte v8 fixture with extended event tags (`Docs/AgentLogs/integrator_save_current_after_external_agents.log`).
- `quest_lifecycle_test`: PASS (`Docs/AgentLogs/integrator_quest_current_after_external_agents.log`).
- `subworld_async_seam_test`: PASS, no `debug shutdown` spam (`Docs/AgentLogs/integrator_subworld_async_current_after_external_agents.log`).
- Boot smoke seed 108: PASS with `[roads] ... expansions=423890` (`Docs/AgentLogs/integrator_smoke_boot_current_after_external_agents.log`).
- Trade smoke seed 107: PASS with settlement and NPC trade frame captures (`Docs/AgentLogs/integrator_smoke_trade_current_after_external_agents.log`).

---

What was wrong:
- Agents kept producing late changes after the previous audit. The new logs claimed feature parity and async seam runtime upload fixes, but the workspace had already shown that logs can lag source or be invalidated by overwrites.
- `build-msvc-integrator` did not yet contain the newly added `feature_layer_parity_test.exe`, so a naive run returned command-not-found instead of parity evidence.
- `SeamlessSubworldManager` still used `std::function` for the resolver even though the project had a no-heap `SmallFunction` helper.
- `snapshot_subworld` used `std::lround` for every clamped positive height sample; that is heavier than needed for the existing 16-bit save format.

What was done:
- Waited for external `cmake`/`cl`/`ninja` activity to finish, then re-ran source marker checks.
- Verified current source still contains the road 4096 large-map cap, event save `LastSerializable` boundary, feature parity target/tests, async seam upload dirty flags, `TIMAERT_SEAM_TRACE`, and `subworld_seam` smoke action.
- Rebuilt `feature_layer_parity_test` in `build-msvc-integrator` and ran it.
- Replaced seam `CellResolver` storage with `sm::SmallFunction<CellContext(int,int)>`.
- Fixed `SmallFunction::emplace` so lvalue callables copy into inline storage correctly.
- Replaced subworld snapshot height quantization `std::lround` with positive clamped `x + 0.5f` rounding.

Cinematic cheats used:
- No new simulation was added. Road generation remains component-pruned plus capped A*.
- Seam crossing remains a worker-backed placeholder strategy; flat placeholders hide generation latency instead of blocking the frame.
- Inactive renderer upload is skipped on the app seam path; current seed 109 trace proves `upload2d=0.000ms`.

Exact microseconds saved:
- Not honestly claimed for the resolver or quantization changes.
- App seam smoke seed 109 measured: `gen=111.422ms`, `smooth=0.000ms`, `upload3d=448.893ms`, `upload2d=0.000ms`, `total=562.822ms`.
- This proves the inactive 2D upload cost is removed on that path, but the remaining 3D upload cost is still not final AAA-grade performance.

Verification:
- `cmake --build build-msvc-integrator --target subworld_async_seam_test timaert save_roundtrip_test -- -j1`: PASS.
- `subworld_async_seam_test`: PASS after resolver/quantize changes (`Docs/AgentLogs/integrator_subworld_async_after_fast_quantize.log` and final marker check).
- `feature_layer_parity_test`: PASS after building the target in the integrator directory (`Docs/AgentLogs/integrator_feature_layer_final_marker_check.log`).
- `road_river_generation_test`: PASS (`Docs/AgentLogs/integrator_road_after_fast_quantize.log`).
- `save_roundtrip_test`: PASS (`Docs/AgentLogs/integrator_save_after_fast_quantize.log`).
- `quest_lifecycle_test`: PASS (`Docs/AgentLogs/integrator_quest_after_fast_quantize.log`).
- `pathfinding_parity_test`: PASS (`Docs/AgentLogs/integrator_pathfinding_final_marker_check.log`).
- Boot smoke seed 108: PASS with `[roads] ... expansions=423890` (`Docs/AgentLogs/integrator_smoke_boot_after_fast_quantize.log`).
- Trade/NPC smoke seed 107: PASS with settlement and NPC trade surfaces (`Docs/AgentLogs/integrator_smoke_trade_after_fast_quantize.log`).
- App seam smoke seed 109: PASS with `upload2d=0.000ms` and remaining `upload3d=448.893ms` (`Docs/AgentLogs/integrator_smoke_subworld_seam_final.log`).
- `git diff --check`: exit 0; CRLF warnings only.
