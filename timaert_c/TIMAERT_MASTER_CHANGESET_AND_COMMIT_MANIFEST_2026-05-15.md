# TIMAERT MASTER CHANGESET AND COMMIT MANIFEST - 2026-05-15

Root master manifest. If you need the shortest entrypoint, open
`TIMAERT_START_HERE_MAIN_DOCUMENTS_2026-05-15.md` first. Keep both files near
the Timaert root so the current work cycle is not buried under `Docs/Reports`.

Workspace: `C:\Timaert\timaert_c`
Boundary: Timaert/Samosbor only. Do not write Timaert docs into Hecton.

## Current Snapshot

Latest observed dirty-tree facts before this root manifest:

```text
git status --short lines : 101
tracked modified files   : 77
tracked diff size        : 12,835 insertions / 2,033 deletions
untracked top-level Docs : yes
untracked source/tests   : yes
```

The diff continued to move after the first reports. Treat this root file as
the latest index and treat the detailed reports as supporting evidence.

Latest important late changes not fully reflected in the earliest report:

- Road/zone domain restored TS-style water contribution to zone generation:
  `boot_world()` now passes terrain RGBA pointer and byte count into
  `generate_zones`, and `pathfinding_parity_test` covers valid water-mask
  boost plus short-mask fallback.
- Spell domain added TS spell identity metadata:
  `SpellDef::sourceIcon` for all eight spells, native fallback icons, rarity
  and cast-time assertions.
- Spell domain replaced compact placeholder descriptions with richer
  TS-equivalent descriptions for all eight spells.
- `translation.md` was touched heavily by agents and contains mojibake in
  some existing status rows. Do not use it as polished prose until reviewed;
  use it as a parity ledger.

## Primary Documents

Root document:

```text
TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md
```

Detailed reports:

```text
Docs/Reports/2026-05-15_PRECOMMIT_CYCLE_DOCUMENTATION.md
Docs/Reports/2026-05-15_CHANGESET_INVENTORY.md
Docs/Reports/2026-05-15_AGENT_VERIFICATION_MATRIX.md
```

Agent status/log/rationale evidence:

```text
Docs/Tasks/Status_TMA_*.md
Docs/AgentLogs/LOG_TMA_*.md
Docs/AgentLogs/Rationale_TMA_*.md
Docs/AgentLogs/integrator_*after_dirt_landmask*.log
Docs/AgentLogs/integrator_full_build_after_dirt_landmask_count.log
Docs/AgentLogs/integrator_smoke_boot_dirt_landmask_count_seed114.log
```

Imported Hecton-origin reference material must be narrow and explicitly
quarantined. The broad `Docs/Imported/` mirror and `Imported_Hecton8` buckets
were removed from the first push because they contained unrelated Hecton
material and would pollute Timaert history.

Do not describe imported Hecton material as a live mirror. Several agent logs
explicitly state Hecton was still changing during sync attempts. It is a
Timaert-side snapshot.

## Verification Baseline

Latest broad evidence set:

- Full build log: `Docs/AgentLogs/integrator_full_build_after_dirt_landmask_count.log`.
- Boot smoke: `Docs/AgentLogs/integrator_smoke_boot_dirt_landmask_count_seed114.log`.
- Test sweep: `Docs/AgentLogs/integrator_alltests_after_dirt_landmask_*.log`.

Current 14-test surface:

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

Seed 114 smoke reached:

```text
terrain -> politik -> landmarks -> trees -> roads -> dirt roads
-> features/tree grid -> zones -> renderer upload -> path cost
-> macro NPCs -> settlement lookup -> subworld init -> PASS
```

## Honesty Rules For Commit Messages

Use these labels:

- `verified`: focused tests and at least one relevant smoke/build exist.
- `partial`: schema/docs/code exists but live producer/consumer or visual proof is incomplete.
- `environment-sensitive`: code/test path passed, but local dependency or toolchain setup can block a fresh build.

Do not write:

- "Full TS parity" for the whole repo.
- "Renderer.ts fully ported" while `translation.md` still records skipped/native-replaced Canvas2D/WebGL sprite paths.
- "Event system fully complete" while extended TS event tags lack normal gameplay producers/consumers.
- "Hecton import fully mirrored" while logs say Hecton kept changing.

Allowed precise statements:

- `features.ts` core byte-grid domain is transferred and hardened in C++.
- Road/river generation is verified as the native A* baseline with explicit TS divergence.
- Zone water-mask boost is restored with byte-counted terrain mask handling.
- Spells have native static metadata, source icon bytes, descriptions, sustained drain, projectile/effect runtime, and focused tests.
- Subworld generator/seam paths are covered by focused tests and smokes, but still need performance/visual review.

## Commit Split Plan

Do not commit this tree as one blob. Use domain commits. Several files must be
hunk-staged because multiple domains touched them.

### Commit 1 - Root Docs And Current Cycle Reports

Stage:

```text
TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md
Docs/Reports/2026-05-15_PRECOMMIT_CYCLE_DOCUMENTATION.md
Docs/Reports/2026-05-15_CHANGESET_INVENTORY.md
Docs/Reports/2026-05-15_AGENT_VERIFICATION_MATRIX.md
Docs/Tasks/Status_TMA_*.md
Docs/AgentLogs/LOG_TMA_*.md
Docs/AgentLogs/Rationale_TMA_*.md
```

Optional separate commit:

```text
none for this push
```

Reason: imported report material is separated from code review. Removed Hecton
import buckets must not be reintroduced without a file-by-file Timaert/Samosbor
relevance filter.

Tests:

```text
git diff --check
```

Message language: "document Timaert cycle evidence and commit plan".

### Commit 2 - Build, Ignore, GL Loader, Small Utility

Stage:

```text
.gitignore
AGENTS.md
CMakeLists.txt
src/gl/gl.h
src/gl/helpers.cpp
src/core/small_function.h
```

Review:

- SDL2_mixer discovery and required native dependency.
- FetchContent disconnected update policy.
- Test target graph.
- Win32 `glDisableVertexAttribArray`.
- `SmallFunction` inline storage constraints.

Tests:

```text
cmake --build build-msvc --target timaert -- -j1
git diff --check
```

Message language: "harden native build/test infrastructure".

### Commit 3 - Feature Layer, Terrain, Roads, Rivers, Zones

Stage:

```text
src/macro/features.h
src/macro/spawners.cpp
src/macro/spawners.h
src/macro/map_generator.cpp
src/macro/map_generator.h
src/macro/pathfinding.cpp
src/macro/movement_cost.h
src/macro/zones.cpp
src/macro/zones.h
src/macro/politik.cpp
src/macro/state.cpp
src/macro/state.h
src/macro/world_tick.cpp
tests/feature_layer_parity_test.cpp
tests/pathfinding_parity_test.cpp
tests/road_river_generation_test.cpp
```

Hunk-stage from shared files:

```text
src/app/main.cpp              # boot sea-level, dirt-road byte count, zone water-mask plumbing only
src/macro/macro_renderer.cpp  # feature/zone/landmark upload and river/feature painter pieces only
src/macro/macro_renderer.h
ARCHITECTURE.md
MERGE_PLAN.md
README.md
matwej.md
translation.md
```

Do not overclaim:

- Road algorithm is verified native A* baseline with intentional TS corridor
  divergence.
- `features.ts` byte-grid/core consumers are verified.

Tests:

```text
feature_layer_parity_test
pathfinding_parity_test
road_river_generation_test
boot smoke seed 49 or 114
```

Message language: "port and harden feature/road/zone generation".

### Commit 4 - Audio

Stage:

```text
src/macro/audio.cpp
src/macro/audio.h
tests/audio_contract_test.cpp
tests/audio_runtime_test.cpp
```

Hunk-stage from shared files:

```text
CMakeLists.txt       # SDL2_mixer/audio test target parts
src/app/main.cpp     # audio boot/sync/music selection only
AGENTS.md            # SDL2_mixer native requirement if not included in build commit
```

Tests:

```text
audio_contract_test
audio_runtime_test
subworld_audio smoke if available
```

Message language: "add SDL2_mixer native audio path".

Status language: `environment-sensitive` if the target machine lacks SDL2_mixer.

### Commit 5 - Character Paperdoll

Stage:

```text
src/assets/character_paperdoll.cpp
src/assets/character_paperdoll.h
src/assets/character_paperdoll_gl.cpp
src/assets/character_paperdoll_gl.h
tests/character_paperdoll_test.cpp
tests/character_paperdoll_gl_smoke_test.cpp
```

Hunk-stage from shared files:

```text
src/app/main.cpp
src/ui/macro_overlay.cpp
src/sub/renderer_3d.cpp
src/sub/renderer_3d.h
CMakeLists.txt
```

Tests:

```text
character_paperdoll_test
character_paperdoll_gl_smoke_test
boot smoke with paperdoll atlas load
```

Message language: "add native character paperdoll atlas path".

### Commit 6 - Spells And Runtime Effects

Stage:

```text
src/content/spells/registry.cpp
src/content/spells/spell_book.cpp
src/content/spells/spell_book.h
src/content/spells/spell_types.cpp
src/content/spells/spell_types.h
src/macro/spell_book_state.h
src/sub/spell_effects.cpp
src/sub/spell_effects.h
tests/spell_casting_effects_test.cpp
```

Hunk-stage from shared files:

```text
src/app/main.cpp       # cast/toggle/smoke/spell event pieces only
src/sub/engine.cpp     # spell effect ticking, Haste/Flight/flight height pieces only
src/sub/renderer_3d.cpp
src/sub/renderer_3d.h
src/ui/overlays.cpp    # SpellOverlay pieces only
src/ui/overlays.h
CMakeLists.txt
translation.md
```

Late included changes:

- `SpellDef::sourceIcon` for all eight TS spells.
- Native fallback icon plus source icon bytes.
- Full TS-equivalent spell descriptions.
- Macro type/power/duration and pros/cons metadata.
- Sustained Haste/Flight aura visuals.

Tests:

```text
spell_casting_effects_test
save_roundtrip_test
SpellOverlay smoke: open_spells, cast_spell, toggle_haste, toggle_flight
```

Message language: "port native spell casting metadata and effects".

### Commit 7 - Events, Quests, Plot, Save

Stage:

```text
src/events/event_bus.cpp
src/events/event_bus.h
src/events/event_types.h
src/events/effect_applicator.cpp
src/events/logic_nodes.cpp
src/events/logic_nodes.h
src/events/node_registry.cpp
src/events/quests/quest_engine.cpp
src/content/quests/procedural.cpp
src/content/quests/procedural.h
src/content/plot/encounters.cpp
src/content/plot/intro.cpp
src/content/plot/chapter_1.h
src/macro/save.cpp
tests/quest_lifecycle_test.cpp
tests/save_roundtrip_test.cpp
```

Hunk-stage from shared files:

```text
src/app/main.cpp
src/ui/overlays.cpp
src/ui/overlays.h
CMakeLists.txt
translation.md
```

Do not overclaim:

- Event schema/save coverage exists for extended tags.
- Several tags still lack normal gameplay producers/consumers; this is `partial`, not complete TS event parity.

Tests:

```text
quest_lifecycle_test
save_roundtrip_test
trigger_level_dialog smoke
story/dialog smokes if staged here
```

Message language: "expand quest/event/save parity coverage".

### Commit 8 - Combat, NPC, Army

Stage:

```text
src/macro/army.h
src/macro/npc.h
src/macro/npc_spawn.cpp
src/macro/npc_spawn.h
src/ecs/components.h
src/ecs/systems.cpp
src/sub/ai.cpp
src/sub/ai.h
src/sub/spawn.cpp
src/sub/spawn.h
tests/combat_squad_test.cpp
tests/npc_spawn_contract_test.cpp
```

Hunk-stage from shared files:

```text
src/app/main.cpp
src/sub/engine.cpp
src/ui/macro_overlay.cpp
src/ui/overlays.cpp
CMakeLists.txt
```

Tests:

```text
combat_squad_test
npc_spawn_contract_test
trigger_battle_start smoke
subworld_time smoke
```

Message language: "unify NPC soldier combat state".

### Commit 9 - Settlement/NPC UI And App Actions

Stage or hunk-stage:

```text
src/ui/macro_overlay.cpp
src/ui/macro_overlay.h
src/ui/overlays.cpp
src/ui/overlays.h
src/app/main.cpp
src/ui/screens.cpp
src/ui/screens.h
```

Only include UI/action hunks not already taken by spells/events/combat.

Evidence:

- Build/NPC panel/NPC trade/Attack smokes.
- PPM captures in `Docs/AgentLogs/TMA_SETTLEMENT_NPC_ACTIONS_BKR_*.ppm`.

Tests:

```text
trade/NPC/action smokes
quest_lifecycle_test
save_roundtrip_test
```

Message language: "wire settlement and NPC action UI".

### Commit 10 - Subworld Generator, Seam, Runtime

Stage:

```text
src/sub/base_generator.cpp
src/sub/base_generator.h
src/sub/gens/dispatch.cpp
src/sub/map_data.h
src/sub/map_factory.cpp
src/sub/map_factory.h
src/sub/seamless_manager.cpp
src/sub/seamless_manager.h
tests/subworld_async_seam_test.cpp
tests/subworld_generator_parity_test.cpp
```

Hunk-stage from shared files:

```text
src/sub/engine.cpp
src/sub/engine.h
src/sub/renderer_2d.cpp
src/sub/renderer_3d.cpp
src/sub/renderer_3d.h
src/app/main.cpp
CMakeLists.txt
translation.md
```

Do not overclaim:

- Bounded native generator is intentional where TS worker-style generation is unsafe for native seam performance.
- Seam path is improved and tested; performance still needs real profiling.

Tests:

```text
subworld_generator_parity_test
subworld_async_seam_test
subworld_time smoke
subworld_seam smoke
```

Message language: "expand subworld generation and async seam".

## Shared Files That Need Hunk Staging

These files are not cleanly owned by one commit:

```text
src/app/main.cpp
src/ui/overlays.cpp
src/ui/macro_overlay.cpp
src/sub/engine.cpp
src/sub/renderer_3d.cpp
CMakeLists.txt
ARCHITECTURE.md
README.md
matwej.md
translation.md
```

If these are committed whole in the wrong slice, the commit history will lie
about ownership. Use `git add -p` or split with a temporary branch workflow.

## Final Gate Before First Commit

Run after any further code edits:

```text
git diff --check
cmake --build build-msvc -- -j1
build-msvc\feature_layer_parity_test.exe
build-msvc\pathfinding_parity_test.exe
build-msvc\road_river_generation_test.exe
build-msvc\quest_lifecycle_test.exe
build-msvc\save_roundtrip_test.exe
build-msvc\spell_casting_effects_test.exe
build-msvc\combat_squad_test.exe
build-msvc\npc_spawn_contract_test.exe
build-msvc\subworld_async_seam_test.exe
build-msvc\subworld_generator_parity_test.exe
build-msvc\audio_contract_test.exe
build-msvc\audio_runtime_test.exe
build-msvc\character_paperdoll_test.exe
build-msvc\character_paperdoll_gl_smoke_test.exe
```

Then run at least:

```text
TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,wait_visible,quit
TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit
TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,trigger_story_overlay,complete_story_overlay,quit
```

Do not start commits while `timaert.exe`, `cl`, `link`, `ninja`, or `cmake`
processes are still running from another agent unless the commit only stages
documentation.

## Final Read

Current honest status:

- Large parts of the TS feature, road/river, spell, quest/save, paperdoll,
  audio, combat, subworld, and UI surfaces are now native and tested.
- The repo is not globally "100% TS parity" until the final TS export walk in
  `translation.md` is complete.
- This work is ready for careful commit slicing, not for a single bulk commit.
