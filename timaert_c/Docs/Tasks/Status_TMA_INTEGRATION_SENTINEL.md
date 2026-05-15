# Status: TMA_INTEGRATION_SENTINEL

Domain: cross-agent integration, build stability, runtime smoke verification

## Checklist

- [x] Agent output inspected.
  - Evidence: all ten `Docs/Tasks/Status_TMA_*.md` files and agent logs were reviewed at integration level.
- [x] Isolated integration build created.
  - Evidence: `build-msvc-integrator` configured with MSVC/Ninja and SDL2/SDL2_mixer paths to avoid locks from agents running `build-msvc\timaert.exe`.
- [x] MSVC compile blockers fixed.
  - Evidence: `findSmokeHostile` now has explicit `-> entt::entity`; `SubworldEngine::resolve_subworld_deaths(bool drainAll)` implementation matches the header and drains on leave.
- [x] Road generation hang risk reduced without breaking invariants.
  - Evidence: land-component pruning is active in `trace_roads`; large-map road budget regression passes; seeds 1, 3, 101, 103, and 12636737 smoke pass with bounded expansions.
- [x] Full focused test sweep passed.
  - Evidence: audio, paperdoll, combat squad, pathfinding, quest lifecycle, road/river, save, spell effects, subworld generator, and async seam tests all returned exit 0 from `build-msvc-integrator`.
- [x] Runtime smoke coverage passed.
  - Evidence: boot, spells, story overlay, battle/subworld, settlement UI capture, and seeds 1..10 passed earlier on the isolated executable.
- [x] Road bounded-A* optimization retained in current source.
  - Evidence: `find_road_path` now keeps small maps full-search but caps large-map searches at 4096 expansions inside the existing signature; `road_river_generation_test` passes; seed 107 trade smoke reports `[roads] ... expansions=344885` instead of the earlier 1,928,000 unbounded regression.
- [x] Codex pause/UI parity tightened.
  - Evidence: pause menu now opens Codex like TS `PauseOverlay`; native Codex renders static TS article/category content instead of raw ids; Escape closes Codex before entering Pause; `open_codex` smoke passed on seed 106.
- [x] Trade surfaces runtime smoke-covered.
  - Evidence: smoke actions `open_settlement_trade` and `open_npc_trade` open the native settlement Trade tab and NPC Trade popup from the runtime world, logging stock/player inventory/gold and capturing frames on seed 107.
- [x] Translation ledger corrected for external UI status.
  - Evidence: `translation.md` now marks TradeOverlay behaviour covered by native surfaces and corrects the nonexistent `IntroOverlay.svelte` row to `plot/intro.ts` + `StoryOverlay.svelte`.
- [x] Post-agent C++ regression audit performed.
  - Evidence: inspected deletion-heavy/high-risk diffs (`army.h`, `event_bus`, `logic_nodes`, `seamless_manager`, `sub/engine`, `renderer_3d`, `spawners`, `save`); found real concurrent overwrite risk in roads and stale async-seam executable output; waited for external builds, rebuilt current sources, and reran focused gates.
- [x] Current post-agent verification passed.
  - Evidence: `road_river_generation_test`, `save_roundtrip_test`, `quest_lifecycle_test`, `subworld_async_seam_test`, boot smoke seed 108, and trade smoke seed 107 all pass from `build-msvc-integrator` after the latest external file changes.
- [x] Late-agent marker audit repeated after fresh external builds.
  - Evidence: road cap markers, event save boundary, feature parity target, async seam upload-defer markers, and `subworld_seam` smoke action are present in current source after external `cmake/cl/ninja` processes went quiet.
- [x] Seam resolver heap-capable wrapper removed.
  - Evidence: `CellResolver` now uses `sm::SmallFunction<CellContext(int,int)>`; `SmallFunction` was fixed to accept lvalue callables; integrator `timaert` and `subworld_async_seam_test` rebuild passed.
- [x] Subworld snapshot quantization made cheaper without format change.
  - Evidence: `snapshot_subworld` now rounds clamped positive height samples with `x + 0.5f` instead of `std::lround`; save round-trip and async saved-restore seam tests pass.
- [x] Final runtime smoke gates passed.
  - Evidence: boot smoke seed 108, trade/NPC smoke seed 107, and app-level `subworld_seam` smoke seed 109 all pass from `build-msvc-integrator`. Seed 109 seam trace reports `upload2d=0.000ms`, proving inactive 2D upload suppression is live.

## Current Status

STATUS: INTEGRATED.

The current tree builds in the isolated MSVC integrator target. Targeted road, feature, save, quest, pathfinding, async seam, boot, trade/NPC, and app seam smoke evidence exists. Road tracing no longer performs unbounded full-map A* on large maps; uncertain same-component routes are pruned after a bounded proof attempt. Codex UX is no longer a raw-id debug list, TradeOverlay behaviour is covered by native settlement/NPC surfaces, and the most recent external-agent edits did not break the focused gate.

Remaining honest gaps: C++ seamless subworld is worker-backed and app-smoke verified, but still should not be called perfect TS preloaded-ring parity. Debug app seam trace still shows a visible 3D upload cost (`448.893ms` on seed 109 in `build-msvc-integrator`), so the path is improved, not finished.
