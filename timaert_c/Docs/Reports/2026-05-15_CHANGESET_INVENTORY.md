# Timaert Changeset Inventory

Date: 2026-05-15
Workspace: `C:\Timaert\timaert_c`
Purpose: current factual inventory for the dirty tree before commit slicing.

Root entrypoint: `TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md`
now lives at the Timaert repo root. Use it first; this inventory remains the
detailed dirty-tree index.

This document complements `Docs/Reports/2026-05-15_PRECOMMIT_CYCLE_DOCUMENTATION.md`.
The previous report is a domain narrative. This file is a current inventory:
what changed, where it changed, which agent status files exist, and what still
requires human/domain review before commits.

Agent verification matrix: `Docs/Reports/2026-05-15_AGENT_VERIFICATION_MATRIX.md`
maps each TMA agent to status/log/rationale evidence, focused tests, and
remaining risk language for commit preparation.

## Current Git Snapshot

Observed commands:

- `git status --short`
- `git diff --name-status`
- `git diff --numstat`
- `git diff --stat`
- `rg --files -g "*.ts" -g "*.tsx"`

Facts from the latest refresh:

- `git status --short`: 101 lines.
- Tracked modified files: 77.
- Tracked diff size: 12,597 insertions and 1,848 deletions.
- No `.ts` or `.tsx` files exist inside `timaert_c`.
- External TS authority remains the separate source tree referenced by `translation.md`.
- `Docs/` is still untracked as a top-level entry, but it contains active Timaert reports, tasks, agent logs, and narrow imported reference material.

Recursive Timaert docs counts at refresh time:

```text
Docs/AgentLogs : active TMA/integrator logs only; Imported_Hecton8 excluded from first push
Docs/Imported  : removed before push; broad mirror was unrelated Hecton carryover
Docs/Reports   : active Timaert reports only; Imported_Hecton8 excluded from first push
Docs/Tasks     : active TMA task/status files only; Imported_Hecton8 excluded from first push
Docs total     : refreshed after staging filter before commit
```

Important: the first push intentionally excludes broad Hecton import buckets.
Only current Timaert/TMA reports, tasks, logs, and root handoff documents belong
in the commit history.

## Agent Status Files

Observed `Docs/Tasks/Status_TMA_*.md` files:

```text
Status_TMA_AUDIO_SDL_MIXER_PORTER.md        -> Domain: AUDIO_SYSTEMS_PORTER; final state: VERIFIED_WITH_EXTERNAL_BUILD_BLOCKER
Status_TMA_CHARACTER_PAPERDOLL_ATLAS_BKR.md -> Domain: CHARACTER_RENDERING_PORTER; status: VERIFIED
Status_TMA_COMBAT_NPC_SOLDIER_BKR.md        -> Domain: COMBAT_STATE_ARCHITECT; task count: 8
Status_TMA_EVENT_QUEST_SAVE_LEDGER_BKR.md   -> Domain: event/quest/save parity
Status_TMA_FEATURE_LAYER_SENTINEL.md        -> Domain: features.ts -> C++ FeatureLayer; status: VERIFIED
Status_TMA_INTEGRATION_SENTINEL.md          -> Domain: integration/build/runtime smoke; status: INTEGRATED
Status_TMA_ROAD_RIVER_TERRAIN_BKR.md        -> Domain: macro terrain/roads/rivers/features; status: VERIFIED
Status_TMA_SETTLEMENT_NPC_ACTIONS_BKR.md    -> Domain: Settlement Build tab and NPC proximity actions
Status_TMA_SPELL_CASTING_EFFECTS_BKR.md     -> Domain: spell system casting/effects/SpellOverlay
Status_TMA_STORY_DIALOG_UI_BKR.md           -> Domain: UI_STORY_PORTER; final state: VERIFIED
Status_TMA_SUBWORLD_ASYNC_SEAM_BKR.md       -> Domain: SUBWORLD_PERFORMANCE_ARCHITECT; status: VERIFIED
Status_TMA_SUBWORLD_GENERATOR_PARITY_BKR.md -> Domain: TS per-mode subworld generator C++ parity; status: VERIFIED
```

Interpretation:

- Most named TMA agent domains report verified/integrated state.
- `TMA_AUDIO_SDL_MIXER_PORTER` explicitly reports an external build blocker state, not a clean unconditional verified state.
- Status files are evidence, not proof by themselves. The actual proof is the build/test/smoke log set in `Docs/AgentLogs`.

## Latest Verification Evidence

The current dirty tree has recent evidence from:

- `Docs/AgentLogs/integrator_full_build_after_dirt_landmask_count.log`
- `Docs/AgentLogs/integrator_smoke_boot_dirt_landmask_count_seed114.log`
- `Docs/AgentLogs/integrator_alltests_after_dirt_landmask_*.log`

Observed seed 114 boot facts:

```text
[boot] terrain generated
[boot] politik generated
[boot] landmarks populated
[boot] trees spawned
[roads] cities=68 attempted=157 kept=26 pruned=131 componentPruned=44 expansions=394130
[boot] roads traced
[boot] dirt roads traced
[boot] features and tree grid built
[boot] zones generated
[boot] macro renderer initialized
[boot] features uploaded
[boot] zones uploaded
[boot] landmarks uploaded
[boot] path cost built
[boot] macro npcs spawned
[boot] subworld init done
[smoke] PASS
```

Observed current test surface:

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

## Domain Inventory

### 1. Root Docs and Build Hygiene

Files:

- `.gitignore`
- `AGENTS.md`
- `ARCHITECTURE.md`
- `CMakeLists.txt`
- `MERGE_PLAN.md`
- `README.md`
- `matwej.md`
- `translation.md`

Observed changes:

- `.gitignore` now ignores `artifacts/`, runtime/smoke `.ppm`, root `.diff`, `.patch`, `.rej`, `.orig`, and root object artifacts.
- `AGENTS.md` now includes `src/assets` in the source glob guidance.
- `AGENTS.md` now records road generation as audited, while still requiring same-seed A/B evidence for future rewrites.
- `AGENTS.md` now documents SDL2_mixer/MP3 support as a native build requirement.
- `CMakeLists.txt` forces disconnected FetchContent updates for incremental build reliability.
- `CMakeLists.txt` adds SDL2 include discovery and SDL2_mixer target discovery/fatal error for native audio.
- `CMakeLists.txt` links SDL2_mixer into the app when available.
- `CMakeLists.txt` adds many focused test executables.
- Project docs were refreshed to reflect current parity status and known divergences.

Risk:

- Build configuration changed significantly. Before final commits, run a clean configure/build in the intended MSVC profile.
- Line-ending warnings are present. They are not `git diff --check` errors, but they should not be hidden in commit review.

### 2. Feature Layer, Terrain, Roads, Rivers, and Zones

Files:

- `src/macro/features.h`
- `src/macro/spawners.cpp`
- `src/macro/spawners.h`
- `src/macro/pathfinding.cpp`
- `src/macro/movement_cost.h`
- `src/macro/zones.cpp`
- `src/macro/zones.h`
- `src/macro/map_generator.cpp`
- `src/macro/map_generator.h`
- `src/macro/politik.cpp`
- `src/macro/state.cpp`
- `src/macro/state.h`
- `src/macro/world_tick.cpp`
- `src/macro/macro_renderer.cpp`
- `src/macro/macro_renderer.h`
- `tests/feature_layer_parity_test.cpp`
- `tests/pathfinding_parity_test.cpp`
- `tests/road_river_generation_test.cpp`

Observed changes:

- `FeatureLayer` now has `decode`, `cell_count_for`, `cell_count`, `has_complete_storage`, `has_invalid_cell_bytes`, sanitized copy, and safe upload view helpers.
- `FeatureLayer::at()` and `FeatureLayer::set()` now fail closed on malformed dimensions/storage and sanitize invalid bytes.
- Feature byte values are statically guarded against TS enum drift.
- `build_feature_layer()` now receives active sea level and validates terrain/mask storage before reading.
- Feature pass order is documented and tested: mountain, tree, dirt road, road.
- Road tracing now accepts sea level, component-prunes cross-island pairs, blocks water cells, tracks `componentPrunedEdges`, and keeps the native A* divergence explicit.
- Dirt road tracing validates optional land-mask byte coverage.
- Tree spawning uses active sea level and river exclusion where river data exists.
- `TerrainData` gained storage helper semantics used by trees, roads, path cost, and subworld entry.
- Native river data/texture integration exists, with renderer sampling through `u_riverMap`.
- Pathfinding cost-grid and zone generation ignore malformed feature storage instead of indexing it directly.
- Zone lookup/upload fail closed on invalid dimensions and invalid zone bytes.
- Macro renderer sanitizes feature/zone/landmark upload inputs.
- Macro renderer shader source was split/concatenated to avoid MSVC string literal limits while preserving GLSL.

Evidence:

- `feature_layer_parity_test` covers priority, water guard, torus lookup, malformed storage, invalid feature bytes, sanitized upload, short masks, threshold behavior, and tree index semantics.
- `pathfinding_parity_test` covers feature movement costs, malformed feature storage, mismatched dimensions, malformed terrain storage, and zone fallback.
- `road_river_generation_test` covers water-only pruning, dry detours, A* use, active sea level, over-budget detour pruning, malformed terrain handling, and river/tree/road invariants.

Risk:

- Road generation is not exact TS corridor snapping. It is a documented native A* baseline. Do not rename it to exact parity.
- `map_generator.cpp` is a 601-line insertion and should be reviewed as its own commit slice.

### 3. Macro Renderer and GL

Files:

- `src/macro/macro_renderer.cpp`
- `src/macro/macro_renderer.h`
- `src/gl/gl.h`
- `src/gl/helpers.cpp`
- `src/assets/sprite_atlas.cpp`

Observed changes:

- Renderer upload paths now use sanitized feature/zone/landmark data.
- River texture sampling is integrated into macro terrain rendering.
- Macro decoration painter follows the current TS-style 3x3 cell order documented in status files.
- `glDisableVertexAttribArray` was added to the Win32 GL proc table.
- One line was removed from `sprite_atlas.cpp`; inspect before staging with asset changes.

Risk:

- `translation.md` still records `game/renderer.ts` as partial because TS sprite batching/entity rendering is not fully represented by the native macro renderer.
- Rendering correctness needs screenshots or visual smoke beyond compile/test evidence.

### 4. Audio

Files:

- `src/macro/audio.cpp`
- `src/macro/audio.h`
- `CMakeLists.txt`
- `tests/audio_contract_test.cpp`
- `tests/audio_runtime_test.cpp`

Observed changes:

- New `AudioSystem` wraps SDL2_mixer with music/SFX IDs, volume controls, mute state, fade defaults, load status, and error storage.
- Native app has audio boot/sync paths and music selection for macro/subworld state.
- CMake requires SDL2_mixer for native builds and defines `TIMAERT_HAS_SDL_MIXER`.
- Boot smoke loaded music and SFX assets.

Evidence:

- `audio_contract_test` and `audio_runtime_test` are part of the current test surface.
- Seed 114 smoke loaded `explore`, `empire_theme`, `subworld`, and `witch` audio assets.

Risk:

- Audio status file names an external build blocker state, so final release docs should separate code correctness from machine dependency availability.

### 5. Character Paperdoll and Assets

Files:

- `src/assets/character_paperdoll.cpp`
- `src/assets/character_paperdoll.h`
- `src/assets/character_paperdoll_gl.cpp`
- `src/assets/character_paperdoll_gl.h`
- `tests/character_paperdoll_test.cpp`
- `tests/character_paperdoll_gl_smoke_test.cpp`

Observed changes:

- New character atlas data model with categories, directions, animation types, appearance presets, palette slots, atlas entries, and render layers.
- Atlas loader maps sheet names to ordinals and provides category/sprite entry lookup.
- GL path has a smoke test target.

Evidence:

- `character_paperdoll_test` and `character_paperdoll_gl_smoke_test` are part of the current test surface.
- Seed 114 smoke loaded a 964x964 paper-doll atlas with 286 sheets and 45,760 entries.

Risk:

- Test pass proves loader/GL smoke coverage; it does not prove final visual style or animation polish.

### 6. Spells, Runtime Effects, and Spell UI

Files:

- `src/content/spells/registry.cpp`
- `src/content/spells/spell_book.cpp`
- `src/content/spells/spell_book.h`
- `src/content/spells/spell_types.cpp`
- `src/content/spells/spell_types.h`
- `src/sub/spell_effects.cpp`
- `src/sub/spell_effects.h`
- `src/macro/spell_book_state.h`
- `src/sub/engine.cpp`
- `src/sub/renderer_3d.cpp`
- `src/sub/renderer_3d.h`
- `src/ui/overlays.cpp`
- `tests/spell_casting_effects_test.cpp`

Observed changes:

- Spell definitions now carry rarity, delivery shape, tags, status data, macro effect metadata, projectile radius/life, beam length, chain fields, and pros/cons flavor arrays.
- Spell book API now exposes richer cast checks and spell scaling helpers.
- Registry now spawns native projectile/beam/chain/AoE/sustained spell descriptors.
- Armageddon uses deterministic bounded meteor count instead of one fake broad effect.
- Haste and Flight are sustained effects integrated with macro and subworld movement/camera paths.
- `sub/spell_effects.*` isolates projectile/effect ticking for subworld ECS.
- UI exposes learned spells, active spell, cooldowns, metadata, pros/cons, and sustained state.

Evidence:

- `spell_casting_effects_test` is part of the current test surface.
- App smoke and status files record SpellOverlay open/cast/toggle paths.

Risk:

- Unsupported world-map macro spell effects must remain honest and should not report successful production effects.
- Spell visuals should be inspected separately from simulation tests.

### 7. Combat, NPCs, Army, and Settlement/NPC Actions

Files:

- `src/macro/army.h`
- `src/macro/npc.h`
- `src/macro/npc_spawn.cpp`
- `src/macro/npc_spawn.h`
- `src/sub/ai.cpp`
- `src/sub/ai.h`
- `src/sub/spawn.cpp`
- `src/sub/spawn.h`
- `src/ecs/components.h`
- `src/ecs/systems.cpp`
- `tests/combat_squad_test.cpp`
- `tests/npc_spawn_contract_test.cpp`

Observed changes:

- Army/combat structures changed heavily and are covered by a dedicated combat squad test.
- NPC type/state structures expanded.
- Macro NPC spawning now has fallback behavior for invalid/mismatched terrain and fails closed on invalid map dimensions.
- Subworld spawn/AI files changed to support current combat and subworld runtime behavior.

Evidence:

- `combat_squad_test` is part of the current test surface.
- `npc_spawn_contract_test` verifies invalid terrain does not suppress all NPC spawns, fallback positions stay in map bounds, mismatched terrain is treated as absent terrain, and invalid map dimensions spawn nothing.

Risk:

- Combat/army changes are broad and should not be committed with road/feature work.

### 8. Events, Logic Nodes, Quests, Plot, and Save

Files:

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
- `src/macro/save.cpp`
- `tests/quest_lifecycle_test.cpp`
- `tests/save_roundtrip_test.cpp`

Observed changes:

- Event bus reserves tick/history buffers and now has query/history helpers.
- `flush()` promotes tick events into last-tick semantics used by logic nodes.
- Event tags now include TS-shaped quest aliases and extended serializable tags: settlement entry/leave/mood, NPC HP, player stat, battle end, magic surge, faction relation, dialog start, camera move.
- Static assertions preserve serialized tag values for important quest and extended tags.
- Dialog choice payload support exists.
- Node registry/event applicator/quest engine changed to route more TS-shaped story/quest effects.
- Procedural quest generation expanded.
- Encounters and intro/chapter content are represented natively.
- Save round-trip coverage expanded to newer event/schema state.

Evidence:

- `quest_lifecycle_test` is the largest focused test and is part of the current sweep.
- `save_roundtrip_test` is part of the current sweep.

Risk:

- `translation.md` still states some event producer/consumer coverage is partial. Schema/save parity is stronger than live gameplay parity.

### 9. Story, Dialog, Settlement UI, and App Integration

Files:

- `src/app/main.cpp`
- `src/ui/overlays.cpp`
- `src/ui/overlays.h`
- `src/ui/macro_overlay.cpp`
- `src/ui/macro_overlay.h`
- `src/ui/screens.cpp`
- `src/ui/screens.h`
- `src/content/plot/intro.cpp`
- `src/content/plot/chapter_1.h`

Observed changes:

- App includes audio boot/sync, expanded smoke actions, spell casting actions, story completion smoke, frame capture, modal clearing, macro NPC attack routing, and subworld/audio/spell smoke tokens.
- `boot_world()` passes active sea level into roads/features/path-related systems.
- Story overlay now has bounded value/completion helpers and app-layer result routing.
- Dialog overlay has count-only/missing-payload handling and node activation through the app/logic layer.
- Settlement trade/build/NPC action surfaces expanded.
- Character panel gained spell and army details.
- Codex/story/dialog content and modal sizing were expanded.

Evidence:

- Status files record story/dialog smokes: count-only dialog, level dialog, story overlay, and story completion.
- Current broad test sweep includes quest/save/spell/combat paths touched by app/UI.

Risk:

- `src/app/main.cpp` has 1,867 insertions / 117 deletions in numstat.
- `src/ui/overlays.cpp` has 1,259 insertions / 56 deletions in numstat.
- These files require manual UI-flow review. Compile/test pass is not enough to prove every modal and settlement branch.

### 10. Subworld Generation, Seam, Renderer, and Runtime

Files:

- `src/sub/base_generator.cpp`
- `src/sub/base_generator.h`
- `src/sub/engine.cpp`
- `src/sub/engine.h`
- `src/sub/gens/dispatch.cpp`
- `src/sub/map_data.h`
- `src/sub/map_factory.cpp`
- `src/sub/map_factory.h`
- `src/sub/renderer_2d.cpp`
- `src/sub/renderer_3d.cpp`
- `src/sub/renderer_3d.h`
- `src/sub/seamless_manager.cpp`
- `src/sub/seamless_manager.h`
- `tests/subworld_async_seam_test.cpp`
- `tests/subworld_generator_parity_test.cpp`

Observed changes:

- Subworld generator dispatch gained explicit routing for city, village, ruin, spire, road, forest, grassland, swamp, mountain, and water modes.
- Forest glades, ruin rings, spire scorch/crater/tower, bridge structures, city gates/walls/roads/houses/fields, and village roads/houses/palisade/fields are now represented by bounded native generation logic.
- Settlement roads align to neighbouring macro road/dirt-road features where possible.
- Wall structure records are suppressed where gate/road/square/house/field tiles would conflict.
- Seamless manager now uses worker threads, job queues, placeholder data, composite blits, buffer shifts, save/smooth jobs, and completed job queues.
- Subworld engine integrates more spell, combat, flight, terrain storage, and seamless entry behavior.
- 3D renderer gained spell visual support and GL attribute disable support.

Evidence:

- `subworld_generator_parity_test` reports non-zero outputs for forest glades, tree structures, spire scorch rock, ruin wall tiles, city/village houses, road bridge count, grassland trees, and swamp trees.
- `subworld_async_seam_test` is part of the current test surface.

Risk:

- `src/sub/gens/dispatch.cpp` and `src/sub/seamless_manager.cpp` are high-risk due to size and architectural reach.
- Bounded native generation intentionally differs from unbounded TS worker-style expansion where docs record seam/performance risks.

## Tracked Modified Files With Numstat

```text
7     0    .gitignore
10    6    AGENTS.md
166   113  ARCHITECTURE.md
318   1    CMakeLists.txt
37    22   MERGE_PLAN.md
70    99   README.md
147   117  matwej.md
1867  117  src/app/main.cpp
0     1    src/assets/sprite_atlas.cpp
1     1    src/content/plot/encounters.cpp
3     0    src/content/plot/intro.cpp
449   47   src/content/quests/procedural.cpp
3     0    src/content/quests/procedural.h
246   63   src/content/spells/registry.cpp
161   22   src/content/spells/spell_book.cpp
38    4    src/content/spells/spell_book.h
84    3    src/content/spells/spell_types.cpp
74    9    src/content/spells/spell_types.h
41    1    src/ecs/components.h
4     2    src/ecs/systems.cpp
11    16   src/events/effect_applicator.cpp
74    3    src/events/event_bus.cpp
28    13   src/events/event_bus.h
62    5    src/events/event_types.h
22    9    src/events/logic_nodes.cpp
9     6    src/events/logic_nodes.h
106   24   src/events/node_registry.cpp
8     5    src/events/quests/quest_engine.cpp
4     0    src/gl/gl.h
4     0    src/gl/helpers.cpp
89    123  src/macro/army.h
131   8    src/macro/features.h
171   86   src/macro/macro_renderer.cpp
11    4    src/macro/macro_renderer.h
601   0    src/macro/map_generator.cpp
33    2    src/macro/map_generator.h
5     1    src/macro/movement_cost.h
108   8    src/macro/npc.h
30    6    src/macro/npc_spawn.cpp
2     3    src/macro/npc_spawn.h
15    12   src/macro/pathfinding.cpp
29    11   src/macro/politik.cpp
58    15   src/macro/save.cpp
480   69   src/macro/spawners.cpp
20    15   src/macro/spawners.h
2     0    src/macro/spell_book_state.h
6     4    src/macro/state.cpp
7     9    src/macro/state.h
5     4    src/macro/world_tick.cpp
34    17   src/macro/zones.cpp
36    4    src/macro/zones.h
10    11   src/sub/ai.cpp
3     3    src/sub/ai.h
101   67   src/sub/base_generator.cpp
12    8    src/sub/base_generator.h
667   21   src/sub/engine.cpp
32    1    src/sub/engine.h
864   87   src/sub/gens/dispatch.cpp
7     2    src/sub/map_data.h
33    9    src/sub/map_factory.cpp
4     0    src/sub/map_factory.h
600   73   src/sub/renderer_3d.cpp
38    1    src/sub/renderer_3d.h
713   47   src/sub/seamless_manager.cpp
126   9    src/sub/seamless_manager.h
123   7    src/sub/spawn.cpp
18    0    src/sub/spawn.h
598   188  src/ui/macro_overlay.cpp
13    1    src/ui/macro_overlay.h
1259  56   src/ui/overlays.cpp
37    1    src/ui/overlays.h
2     1    src/ui/screens.cpp
1     0    src/ui/screens.h
177   0    tests/pathfinding_parity_test.cpp
938   24   tests/quest_lifecycle_test.cpp
154   24   tests/save_roundtrip_test.cpp
140   97   translation.md
```

## Untracked Source/Test Additions

```text
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

Notable untracked source additions:

- `src/core/small_function.h`: inline-storage callable wrapper, useful where small callbacks should avoid heap churn.
- `src/macro/audio.*`: SDL2_mixer-backed native audio.
- `src/assets/character_paperdoll*`: paper-doll atlas and GL support.
- `src/sub/spell_effects.*`: native spell projectile/effect ticking.
- `src/content/plot/chapter_1.h`: native placeholder/registration surface for chapter content.

## Commit Readiness Notes

Commit slicing should stay domain-based:

1. Docs/import/status/report updates.
2. Build system and GL loader fixes.
3. Feature layer plus terrain/road/river hardening and tests.
4. Audio.
5. Character paperdoll.
6. Spell system and runtime effects.
7. Events/quests/save/plot.
8. UI/app integration.
9. Subworld generator/seam/runtime.
10. Combat/NPC/army.

Do not commit all dirty files together. The current tree compiles/tests in the
observed logs, but a single blob commit would make regression isolation poor.

## High-Risk Review List

Manual review before staging:

- `src/app/main.cpp`: very high churn; owns app-loop routing, smoke actions, audio sync, story/spell paths.
- `src/ui/overlays.cpp`: very high churn; owns story/dialog/spell/settlement presentation.
- `src/sub/gens/dispatch.cpp`: generator architecture and mode parity.
- `src/sub/seamless_manager.cpp`: threading/job queues/composite data.
- `src/sub/engine.cpp`: runtime integration surface.
- `src/macro/spawners.cpp`: roads/trees/features/dirt roads and water handling.
- `CMakeLists.txt`: dependency and test target graph.
- `translation.md`: do not let status rows overclaim exact TS parity.

## Honest Remaining Gaps

- Full TS parity is not proven by absence of `.ts` files inside `timaert_c`.
- `translation.md` still records partial/skipped surfaces, especially native replacements for original TS Canvas2D/WebGL rendering paths and partial event producer/consumer coverage.
- Current evidence is strong for tested domains, especially feature layer, road/river invariants, pathfinding, save, quest lifecycle, spells, subworld seam/generator, audio, paperdoll, combat, and NPC spawn.
- Visual quality still needs direct screenshot/playtest review. Compile and smoke logs do not prove final art direction.
- Hecton-origin imports are documentation evidence only. They must remain labelled as imports inside Timaert and must not be treated as live Hecton project files.
