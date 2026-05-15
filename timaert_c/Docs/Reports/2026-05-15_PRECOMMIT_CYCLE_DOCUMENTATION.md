# Timaert Pre-Commit Cycle Documentation

Date: 2026-05-15
Workspace: `C:\Timaert\timaert_c`
Scope rule: Timaert/Samosbor documentation and logs belong under this tree. Hecton is a separate game and was not a write target for this report.

Root entrypoint: `TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md`
now lives at the Timaert repo root. Use that file first before commits; this
report is supporting detail.

## Executive State

This is a pre-commit documentation snapshot for the current multi-agent cycle.

Follow-up inventory: `Docs/Reports/2026-05-15_CHANGESET_INVENTORY.md`
contains the later current-file inventory, agent status index, refreshed
numstat, and high-risk review list. Treat this document as the narrative
snapshot and the inventory file as the current dirty-tree index.

Agent verification matrix: `Docs/Reports/2026-05-15_AGENT_VERIFICATION_MATRIX.md`
links each TMA agent domain to status files, logs, rationale, focused tests,
and remaining risks. Use it before commit messages so partial domains are not
misrepresented as fully complete.

Current working tree facts before this report was added:

- `git status --short`: 101 status lines.
- Tracked modified files: 77.
- Tracked diff size: 12,550 insertions and 1,843 deletions across 77 files.
- Top-level untracked entries before this report: 24.
- `rg --files -g "*.ts" -g "*.tsx"` inside `timaert_c`: no TypeScript files found.
- Gameplay reference is still the external TS tree described by `translation.md` as `../src/`; absence of `.ts` files inside `timaert_c` does not by itself prove full TS parity.

Honest status:

- The `features.ts` domain and its C++ consumers are strongly verified in this cycle.
- Several broader TS domains are now represented natively and covered by tests/smokes.
- Full repository-wide TS parity cannot be claimed without completing the explicit `translation.md` final export audit. `translation.md` still records partial or intentionally skipped renderer surfaces and some event producer/consumer gaps.

## Verification Evidence

Latest verification logs in `Docs/AgentLogs`:

- Full build: `integrator_full_build_after_dirt_landmask_count.log`.
  - Tail shows `timaert.exe` linked successfully.
  - High-risk translation units compiled during that build include `src/app/main.cpp`, `src/sub/renderer_3d.cpp`, `src/sub/gens/dispatch.cpp`, `src/sub/seamless_manager.cpp`, `src/macro/pathfinding.cpp`, and `src/macro/zones.cpp`.
- Boot smoke: `integrator_smoke_boot_dirt_landmask_count_seed114.log`.
  - Seed: 114.
  - Map: 1024x1024.
  - Roads: `cities=68 attempted=157 kept=26 pruned=131 componentPruned=44 expansions=394130`.
  - Boot reached terrain, politik, landmarks, trees, roads, dirt roads, features, zones, renderer upload, path cost, macro NPCs, subworld init.
  - Final line: `[smoke] PASS`.
- Full test sweep: `integrator_alltests_after_dirt_landmask_*.log`.
  - Latest observed final test log: `integrator_alltests_after_dirt_landmask_subworld_generator_parity_test.log`.
  - Subworld generator parity output includes forest glades, tree structures, spire scorch rocks, ruin wall tiles, city/village houses, road bridge count, grassland trees, and swamp trees.
- `git diff --check` was run before this report and returned exit code 0 with LF-to-CRLF warnings only.
- No active `cl`, `link`, `ninja`, `cmake`, or `timaert` processes were found during the cycle check.

Tests covered in the latest 14-test sweep:

```text
audio_contract_test
audio_runtime_test
character_paperdoll_gl_smoke_test
character_paperdoll_test
combat_squad_test
feature_layer_parity_test
npc_spawn_contract_test
pathfinding_parity_test
quest_lifecycle_test
road_river_generation_test
save_roundtrip_test
spell_casting_effects_test
subworld_async_seam_test
subworld_generator_parity_test
```

## Primary TS-to-C++ Transfer: Feature Layer

The strongest completed slice in this cycle is the native `features.ts` transfer and hardening.

Main files:

- `src/macro/features.h`
- `src/macro/spawners.cpp`
- `src/macro/spawners.h`
- `src/macro/macro_renderer.cpp`
- `src/macro/macro_renderer.h`
- `src/macro/pathfinding.cpp`
- `src/macro/zones.cpp`
- `src/macro/zones.h`
- `src/macro/movement_cost.h`
- `src/sub/gens/dispatch.cpp`
- `src/sub/base_generator.cpp`
- `src/app/main.cpp`
- `tests/feature_layer_parity_test.cpp`
- `tests/pathfinding_parity_test.cpp`
- `tests/road_river_generation_test.cpp`
- `tests/subworld_generator_parity_test.cpp`

Implemented and verified behaviour:

- Native `FeatureType` enum preserves the five TS byte values through compile-time assertions.
- `FeatureLayer` now has fail-closed decoding for unknown feature bytes.
- `FeatureLayer::at()` and `FeatureLayer::set()` use overflow-safe torus coordinate wrapping.
- Huge or malformed dimensions fail closed instead of indexing undefined memory.
- `FeatureLayer::has_complete_storage()` validates storage before consumers trust the cell grid.
- `FeatureLayer::has_invalid_cell_bytes()` catches invalid enum bytes.
- `FeatureLayer::copy_sanitized_cells()` and `complete_cells_or_sanitized()` preserve zero-copy upload for valid grids and use scratch sanitization for corrupted complete grids.
- `build_feature_layer()` now accepts active map sea level and applies a land/water guard.
- Mountain pass uses `kDefaultFeatureMountainThreshold = 0.75f`, matching TS `snowLevel 0.80 - 0.05`.
- Writer order matches TS: `Mountain -> Tree -> DirtRoad -> Road`.
- Tree writes use TS flattened index semantics.
- Short road and dirt masks apply only their valid prefix bytes, matching typed-array style safe reads.
- Road and dirt road masks cannot stamp features into cells filtered as water by active sea level.
- Movement cost, zones, subworld entry, base generation, and renderer upload decode or sanitize feature bytes at module boundaries.
- Macro feature texture upload remains zero-copy for valid grids and blanks or sanitizes invalid data instead of trusting garbage.
- Zone and landmark texture uploads now also sanitize invalid bytes and blank invalid dimensions/data pointers.

Evidence:

- `feature_layer_parity_test` passed.
- `pathfinding_parity_test` passed.
- `road_river_generation_test` passed.
- Seed 114 boot smoke reached feature upload, zones upload, landmark upload, path cost build, macro NPC spawn, and subworld init.
- Full build and 14-test sweep passed after the feature dirt/land-mask count change.

## Terrain, Roads, Trees, Zones, and Path Cost

Main files:

- `src/macro/map_generator.cpp`
- `src/macro/map_generator.h`
- `src/macro/spawners.cpp`
- `src/macro/spawners.h`
- `src/macro/pathfinding.cpp`
- `src/macro/movement_cost.h`
- `src/macro/politik.cpp`
- `src/macro/zones.cpp`
- `src/macro/zones.h`
- `src/macro/state.cpp`
- `src/macro/state.h`

Current state:

- `spawn_trees()` has an overload that receives active `seaLevel`.
- `trace_roads()` receives active `seaLevel`.
- `trace_dirt_roads()` has an optional land mask byte count.
- `boot_world()` passes `lp.seaLevel` through trees, roads, features, and path cost.
- Dirt-road generation receives the terrain RGBA byte count so malformed terrain cannot silently read beyond valid data.
- Road generation remains a documented intentional divergence from TS corridor-guided Bresenham: native terrain-cost A* is kept, with water blocking and component pruning.
- Cross-island routes are pruned, same-island routes use bounded terrain-cost A*, and routes not proven inside budget are not stamped.
- The latest smoke road stats prove pruning is active on seed 114.

Risk:

- This is not byte-identical TS road generation. It is documented as a native divergence in `ARCHITECTURE.md` and `translation.md`.
- Before commit, keep the divergence explicit. Do not rename it as "exact TS parity".

## Renderer and GL Reliability

Main files:

- `src/macro/macro_renderer.cpp`
- `src/macro/macro_renderer.h`
- `src/sub/renderer_2d.cpp`
- `src/sub/renderer_3d.cpp`
- `src/sub/renderer_3d.h`
- `src/gl/gl.h`
- `src/gl/helpers.cpp`

Current state:

- Macro renderer consumes sanitized feature, zone, and landmark textures.
- Macro feature decoration painter follows the TS-style 3x3 order for trees, mountains, and landmarks.
- Active map sea level is threaded into the renderer path where needed.
- `glDisableVertexAttribArray` was added to the Win32 GL proc table to repair `renderer_3d` compilation/link reliability.
- 3D spell visuals and paper-doll related native paths are represented in C++.

Risk:

- `translation.md` still marks `game/renderer.ts` as partial because original TS sprite batching/entity rendering is not fully ported. Native macro actors use ImGui/GL/paper-doll overlays instead of the original WebGL sprite batch.
- TS Canvas2D companion renderers are intentionally skipped or replaced by native 3D/diagnostic paths. Keep this explicit.

## Subworld and Seam Work

Main files:

- `src/sub/engine.cpp`
- `src/sub/engine.h`
- `src/sub/gens/dispatch.cpp`
- `src/sub/base_generator.cpp`
- `src/sub/base_generator.h`
- `src/sub/map_factory.cpp`
- `src/sub/map_factory.h`
- `src/sub/map_data.h`
- `src/sub/seamless_manager.cpp`
- `src/sub/seamless_manager.h`
- `src/sub/spawn.cpp`
- `src/sub/spawn.h`
- `src/sub/ai.cpp`
- `src/sub/ai.h`
- `tests/subworld_async_seam_test.cpp`
- `tests/subworld_generator_parity_test.cpp`

Current state:

- Subworld generator dispatch has broad native coverage for forest, grassland, swamp, mountain, water, road, ruin, spire, city, and village generation.
- Seam manager has heavy changes and is now covered by `subworld_async_seam_test`.
- Generator parity test proves populated outputs for multiple biome/cell families.
- Feature and terrain data boundaries fail closed when malformed.

Risk:

- `src/sub/gens/dispatch.cpp` and `src/sub/seamless_manager.cpp` are large-change files. They compile and tests pass, but they should be reviewed as their own commit slice.
- Bounded native generator logic intentionally differs from unbounded TS worker-style expansion where `matwej.md` warns against unsafe queues. Keep the performance reason documented.

## Spells, Combat, and Runtime Effects

Main files:

- `src/content/spells/registry.cpp`
- `src/content/spells/spell_book.cpp`
- `src/content/spells/spell_book.h`
- `src/content/spells/spell_types.cpp`
- `src/content/spells/spell_types.h`
- `src/sub/spell_effects.cpp`
- `src/sub/spell_effects.h`
- `src/sub/engine.cpp`
- `src/macro/spell_book_state.h`
- `src/macro/army.h`
- `tests/spell_casting_effects_test.cpp`
- `tests/combat_squad_test.cpp`

Current state:

- Spell registry/book/types now carry more TS-shaped data: learned/active state, cooldowns, sustained effects, tags, status metadata, macro effect data, and flavour data.
- Runtime spell effects are isolated under `src/sub/spell_effects.*`.
- Projectile, AoE, chain, beam, haste, flight, and Armageddon-style runtime cases have focused test coverage.
- Combat/squad changes are covered by `combat_squad_test`.

Risk:

- Spell and combat files are broad and should not be mixed into the feature-layer commit.
- World-map macro spell effects remain intentionally honest: unsupported macro damage should not claim success.

## Quests, Events, Plot, and UI

Main files:

- `src/events/event_bus.cpp`
- `src/events/event_bus.h`
- `src/events/event_types.h`
- `src/events/effect_applicator.cpp`
- `src/events/logic_nodes.cpp`
- `src/events/logic_nodes.h`
- `src/events/node_registry.cpp`
- `src/events/quests/quest_engine.cpp`
- `src/content/quests/procedural.cpp`
- `src/content/quests/procedural.h`
- `src/content/plot/encounters.cpp`
- `src/content/plot/intro.cpp`
- `src/content/plot/chapter_1.h`
- `src/ui/overlays.cpp`
- `src/ui/overlays.h`
- `src/ui/macro_overlay.cpp`
- `src/ui/macro_overlay.h`
- `src/ui/screens.cpp`
- `src/ui/screens.h`
- `tests/quest_lifecycle_test.cpp`
- `tests/save_roundtrip_test.cpp`

Current state:

- Event bus gained broader TS-shaped query/history/listener behaviour.
- Quest lifecycle tests cover accept/update/complete/fail style paths.
- Procedural quest generation expanded.
- Encounter and intro/chapter content is now represented natively.
- Overlay files changed heavily and compile through the app.
- Save round-trip covers expanded event/schema state.

Risk:

- `translation.md` still marks `event-types.ts` as schema/save parity with some normal gameplay producers/consumers incomplete.
- `ui/overlays.cpp` and `src/app/main.cpp` are high-churn files. Build and tests pass, but commit review must inspect actual UI logic rather than trusting size.

## Audio and Character Paperdoll

Main files:

- `src/macro/audio.cpp`
- `src/macro/audio.h`
- `src/assets/character_paperdoll.cpp`
- `src/assets/character_paperdoll.h`
- `src/assets/character_paperdoll_gl.cpp`
- `src/assets/character_paperdoll_gl.h`
- `src/assets/sprite_atlas.cpp`
- `tests/audio_contract_test.cpp`
- `tests/audio_runtime_test.cpp`
- `tests/character_paperdoll_test.cpp`
- `tests/character_paperdoll_gl_smoke_test.cpp`

Current state:

- Native audio layer exists and is covered by contract/runtime tests.
- Seed 114 boot smoke loaded music and SFX assets.
- Character paper-doll atlas loader and GL smoke coverage exist.
- Boot smoke loaded a 964x964 paper-doll atlas with 286 sheets and 45,760 entries.

Risk:

- Audio depends on SDL2_mixer availability in the local build environment.
- Asset loading proof exists in smoke logs, but visual inspection is still separate from the automated pass.

## Build and Test Infrastructure

Main files:

- `CMakeLists.txt`
- `.gitignore`
- `src/gl/gl.h`
- `src/gl/helpers.cpp`
- `tests/*.cpp`

Current state:

- `CMakeLists.txt` has broad target/test additions.
- `FETCHCONTENT_UPDATES_DISCONNECTED` is forced to reduce repeated network fetch risk for already-populated dependencies during incremental builds.
- GL proc table fix keeps the 3D renderer buildable on Win32.
- Test surface expanded to audio, paperdoll, combat, features, NPC spawn, roads/rivers, spells, subworld seam/generator, pathfinding, quests, and save.

Risk:

- CMake changes are broad and should be reviewed once with a clean configure/build if time allows.
- CRLF warnings exist across many files. They are not `git diff --check` errors, but the repo should decide whether to normalize line endings before final commits.

## Hecton/Timaert Documentation Separation

Current Timaert-owned documentation/import locations:

- `Docs/AgentLogs/`
- `Docs/Tasks/`
- `Docs/Reports/`

Policy:

- Do not write Timaert/Samosbor docs into Hecton.
- The broad `Docs/Imported/...` mirror was removed before push because it mixed in unrelated Hecton material and was not a valid Timaert working document set.
- `Imported_Hecton8` quarantine buckets were also excluded from the first push; do not reintroduce them without file-by-file Timaert/Samosbor relevance filtering.
- Hecton is read-only for this Timaert cycle unless the user explicitly changes that.

## Current Tracked Modified File List

```text
.gitignore
AGENTS.md
ARCHITECTURE.md
CMakeLists.txt
MERGE_PLAN.md
README.md
matwej.md
src/app/main.cpp
src/assets/sprite_atlas.cpp
src/content/plot/encounters.cpp
src/content/plot/intro.cpp
src/content/quests/procedural.cpp
src/content/quests/procedural.h
src/content/spells/registry.cpp
src/content/spells/spell_book.cpp
src/content/spells/spell_book.h
src/content/spells/spell_types.cpp
src/content/spells/spell_types.h
src/ecs/components.h
src/ecs/systems.cpp
src/events/effect_applicator.cpp
src/events/event_bus.cpp
src/events/event_bus.h
src/events/event_types.h
src/events/logic_nodes.cpp
src/events/logic_nodes.h
src/events/node_registry.cpp
src/events/quests/quest_engine.cpp
src/gl/gl.h
src/gl/helpers.cpp
src/macro/army.h
src/macro/features.h
src/macro/macro_renderer.cpp
src/macro/macro_renderer.h
src/macro/map_generator.cpp
src/macro/map_generator.h
src/macro/movement_cost.h
src/macro/npc.h
src/macro/npc_spawn.cpp
src/macro/npc_spawn.h
src/macro/pathfinding.cpp
src/macro/politik.cpp
src/macro/save.cpp
src/macro/spawners.cpp
src/macro/spawners.h
src/macro/spell_book_state.h
src/macro/state.cpp
src/macro/state.h
src/macro/world_tick.cpp
src/macro/zones.cpp
src/macro/zones.h
src/sub/ai.cpp
src/sub/ai.h
src/sub/base_generator.cpp
src/sub/base_generator.h
src/sub/engine.cpp
src/sub/engine.h
src/sub/gens/dispatch.cpp
src/sub/map_data.h
src/sub/map_factory.cpp
src/sub/map_factory.h
src/sub/renderer_2d.cpp
src/sub/renderer_3d.cpp
src/sub/renderer_3d.h
src/sub/seamless_manager.cpp
src/sub/seamless_manager.h
src/sub/spawn.cpp
src/sub/spawn.h
src/ui/macro_overlay.cpp
src/ui/macro_overlay.h
src/ui/overlays.cpp
src/ui/overlays.h
src/ui/screens.cpp
src/ui/screens.h
tests/pathfinding_parity_test.cpp
tests/quest_lifecycle_test.cpp
tests/save_roundtrip_test.cpp
translation.md
```

## Top-Level Untracked Entries Before This Report

```text
Docs/
TIMAERT BATCH.md
src/assets/character_paperdoll.cpp
src/assets/character_paperdoll.h
src/assets/character_paperdoll_gl.cpp
src/assets/character_paperdoll_gl.h
src/content/plot/chapter_1.h
src/core/small_function.h
src/macro/audio.cpp
src/macro/audio.h
src/sub/spell_effects.cpp
src/sub/spell_effects.h
tests/audio_contract_test.cpp
tests/audio_runtime_test.cpp
tests/character_paperdoll_gl_smoke_test.cpp
tests/character_paperdoll_test.cpp
tests/combat_squad_test.cpp
tests/feature_layer_parity_test.cpp
tests/npc_spawn_contract_test.cpp
tests/road_river_generation_test.cpp
tests/spell_casting_effects_test.cpp
tests/subworld_async_seam_test.cpp
tests/subworld_generator_parity_test.cpp
```

This report itself adds:

```text
Docs/Reports/2026-05-15_PRECOMMIT_CYCLE_DOCUMENTATION.md
```

## Did Agents Remove Required C++ Logic?

Evidence-based answer: no active evidence of required C++ logic removal was found in the verified build path, but the change set is too large to claim that every UI/content branch is manually audited.

Reasons:

- The latest full build compiled and linked the app after the large changes.
- Seed 114 boot smoke exercised the app initialization path through terrain, politics, roads, dirt roads, features, zones, renderer uploads, path cost, NPC spawn, settlement lookup, and subworld init.
- The 14-test sweep covers feature parity, malformed feature handling, pathfinding, roads/rivers, quest lifecycle, save round-trip, spells, combat, audio, paperdoll, NPC spawn, and subworld generation/seam behaviour.
- Boundary hardening was added where removed/invalid data would previously leak into lookups or GPU uploads.

Residual risk:

- `src/app/main.cpp`, `src/ui/overlays.cpp`, `src/ui/macro_overlay.cpp`, `src/sub/gens/dispatch.cpp`, `src/sub/seamless_manager.cpp`, and `src/sub/engine.cpp` are very high-churn files. They need domain-by-domain review before commit.
- Tests prove covered invariants; they do not prove every player-visible UI branch.
- Any commit that mixes all domains together will be hard to review and unsafe to revert.

## Recommended Commit Split

Do not commit this as one blob. Recommended slices:

1. Documentation, batch files, import separation, and Timaert-only logs.
2. Build/GL/test infrastructure: CMake, GL proc table, test target plumbing.
3. Feature layer TS transfer and hardening: `features.h`, spawners, pathfinding, zones, renderer upload, focused tests.
4. Terrain/roads/rivers/politik/path-cost generation changes.
5. Subworld generation and seamless manager changes.
6. Spells, spell effects, combat, and related tests.
7. Quests, events, plot, save schema, and lifecycle tests.
8. UI overlays and app-loop integration.
9. Audio and character paperdoll assets/tests.

Each commit should have its own test evidence in the message. If a slice cannot pass its focused tests alone, it is not ready.

## Pre-Commit Gate

Before final commits:

- Re-run full build after this documentation file is added.
- Re-run all 14 focused tests.
- Re-run the smoke boot with the same seed 114 and at least one different seed.
- Re-run `git diff --check`.
- Review `translation.md` rows still marked partial or skipped; do not call them fully ported.
- Review high-churn files by domain before staging.
- Keep all Timaert docs under `C:\Timaert\timaert_c\Docs`.
- Do not write Timaert reports to Hecton.

## Known Non-Fatal Warnings

- Git emits LF-to-CRLF replacement warnings across many files.
- These warnings did not make `git diff --check` fail.
- They should be resolved by a repo line-ending policy decision, not by random per-file edits during feature migration.
