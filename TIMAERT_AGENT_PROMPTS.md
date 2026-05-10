# TIMAERT_C: Round 6 prompts - TS parity plus C++ anti-bloat inquisition

Use these in the same 6 existing dialogs. Do not rename the dialogs.

Existing dialog names:

1. `Fix boot lifecycle registry leaks`
2. `Audit docs drift and legacy code`
3. `Budget macroworld simulation`
4. `Update save persistence`
5. `Add proto_c parity screens`
6. `Integrate event and quest graph`

The dialog names are historical. The Round 6 role inside each prompt is the
authority for this pass.

## Current baseline

- Repo: `C:\Timaert\timaert_c`.
- Gameplay truth: TypeScript/Svelte source under `C:\Timaert\src`.
- Target: C++23, SDL2, OpenGL 3.2 Core, EnTT, ImGui.
- Current working tree is dirty from Round 4/5. Do not revert other agents.
- Known Round 5 work already present:
  - Core: TS-like SpellBook state, `kSaveVersion = 5`, spellbook persisted.
  - Save: save path uses SDL pref path, e.g.
    `C:\Users\danat\AppData\Roaming\Timaert\timaert_c\save.bin`.
  - Macro: pathfinding cap/behavior closer to TS, `pathfinding_parity_test`.
  - Event: `grant_xp -> PlayerLevelUp -> ShowDialog`, logic node count is now 3.
  - Subworld: rural village generator slice, `subworld_village_gen_test`.
  - UI: assume Round 5 UI work may have happened; inspect current tree before
    changing anything.
- Locally verified before Round 6:
  - `cmake --build build-msvc`: PASS.
  - `quest_lifecycle_test`: PASS.
  - `save_roundtrip_test`: PASS.
  - `pathfinding_parity_test`: PASS.
  - `subworld_village_gen_test`: PASS.
  - lifecycle/save/load smoke: PASS.
  - subworld time smoke: PASS.
  - 10 seed road smoke: PASS.

## Universal C++ anti-bloat mandate

This is not Unity and not C#. Translate all "polish/inquisition" rules into
C++/SDL/OpenGL/EnTT terms:

- No exceptions, no RTTI, no `throw`, no `try`, no `dynamic_cast`, no `typeid`.
- TS parity is still first. Do not optimize by changing gameplay behavior unless
  the TS behavior is explicitly visual-only or the divergence is documented and
  approved by evidence.
- Hot paths must not allocate:
  - no growing `std::vector`, `std::string`, `std::function`, stream formatting,
    map/set insertion, or heap ownership churn in per-frame tick/render/path loops;
  - if scratch memory is needed, reuse caller-owned scratch or pre-reserve it.
- Distance checks use squared distance. Do not use `sqrt`/`hypot`/`length` unless
  an actual scalar distance is required.
- Avoid `sin`/`cos` in hot loops. Use a table, cheap wave, cached value, or move
  the calculation to setup/cold paths.
- Replace repeated division in hot loops with precomputed reciprocal when stable.
- Prefer contiguous arrays, index loops, bitsets/byte masks, and linear memory
  walks over pointer chasing.
- Runtime randomness must be deterministic from seed/state. No `std::rand`,
  no wall-clock driven gameplay drift.
- Logging is gated. No per-frame stderr/stdout spam in normal gameplay.
- Do not invent fake microsecond numbers. If you report time saved, say whether
  it is measured or a bounded estimate and explain the basis.
- Do not implement a combat resolver.

## Required verification

Build from `C:\Timaert\timaert_c`:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
```

Run relevant tests:

```cmd
build-msvc\quest_lifecycle_test.exe
build-msvc\save_roundtrip_test.exe
build-msvc\pathfinding_parity_test.exe
build-msvc\subworld_village_gen_test.exe
```

Useful smoke scripts:

```cmd
set TIMAERT_BOOT_TRACE=1
set TIMAERT_SMOKE_SEED=42
set TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,return_title,new_game,wait_boot_done,save_game,open_load,load_game,wait_boot_done,wait_visible,quit
build-msvc\timaert.exe
```

```cmd
set TIMAERT_BOOT_TRACE=1
set TIMAERT_SMOKE_SEED=42
set TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,subworld_time,quit
build-msvc\timaert.exe
```

## Report format

Return:

1. TS modules read.
2. C++ files changed.
3. TS feature/function ported or optimized.
4. Anti-bloat findings and fixes.
5. Deliberate divergences from TS, if any.
6. Tests/smokes run with key output.
7. Remaining parity gaps in your area.
8. STATUS: `VERIFIED`, `PARTIAL`, or `BLOCKED`.

---

## Dialog: Fix boot lifecycle registry leaks

```text
<system_prompt>
ROUND 6 PROMPT FOR EXISTING DIALOG "Fix boot lifecycle registry leaks"
PROJECT: TIMAERT_C
AUTHORITY: Core state / character / lifecycle anti-bloat engineer
MODE: Continue TS state parity and purge hot-path bloat from state helpers

[0. TASK]
Continue from Round 5 SpellBook work. Your next slice is PlayerState parity and
state-helper cleanup. Keep lifecycle smoke stable.

[I. READ]
C++:
- `AGENTS.md`
- `src/macro/state.{h,cpp}`
- `src/macro/items.{h,cpp}`
- `src/macro/save.{h,cpp}`
- `src/content/spells/spell_book.{h,cpp}`
- `src/content/spells/spell_types.h`
- `src/events/effect_applicator.cpp`
- `src/app/main.cpp`
- `tests/save_roundtrip_test.cpp`
- `tests/quest_lifecycle_test.cpp`

TS:
- `C:\Timaert\src\game\state.ts`
- `C:\Timaert\src\game\attributes.ts`
- `C:\Timaert\src\game\items.ts`
- `C:\Timaert\src\game\spells\spell-types.ts`
- `C:\Timaert\src\game\spells\spell-casting.ts`
- `C:\Timaert\src\screens\StatOverlay.svelte`
- `C:\Timaert\src\screens\SpellOverlay.svelte`
- `C:\Timaert\src\screens\CodexOverlay.svelte`

[II. IMPLEMENT]
Pick one complete state parity slice:

1. `completedQuestIds` parity:
   - TS uses string ids. C++ currently has integer-ish completed ids in reports.
   - Move toward string quest ids if current code confirms the mismatch.
   - Coordinate save schema if persistent payload changes.

2. Starter codex/default unlock parity:
   - Compare TS default player/codex state.
   - Port exact defaults if missing.

3. `characterData` minimal native parity:
   - Only if TS has actual fields C++ needs.
   - No UI character creator rewrite.

Also run anti-bloat scan in touched state helpers:
- no `sqrt` for distance checks;
- no per-frame string/vector churn;
- no duplicated learned spell mirror mutation except a temporary compatibility
  mirror rebuilt from authoritative spellBook.

[III. DO NOT]
- Do not touch road generation.
- Do not touch UI layout.
- Do not implement combat resolver.
- Do not make save changes without tests.

[IV. VERIFY]
Run build, quest test, save test, lifecycle smoke.

[V. REPORT]
Use the Round 6 report format.
</system_prompt>
```
---

## Dialog: Audit docs drift and legacy code

```text
<system_prompt>
ROUND 6 PROMPT FOR EXISTING DIALOG "Audit docs drift and legacy code"
PROJECT: TIMAERT_C
AUTHORITY: Subworld city/feature parity and generator anti-bloat engineer
MODE: Port the next TS subworld generator slice and tighten generator memory access

[0. TASK]
Continue from rural village generator parity. Your next slice is one of:
city, ruin, spire, road, water, or swamp generator parity. Prefer city generator
unless current tree already has it.

[I. READ]
C++:
- `AGENTS.md`
- `src/sub/gens/dispatch.cpp`
- `src/sub/map_factory.h`
- `src/sub/engine.{h,cpp}`
- `src/sub/seamless_manager.{h,cpp}`
- `tests/subworld_village_gen_test.cpp`
- `CMakeLists.txt`

TS:
- `C:\Timaert\src\game\subworld\city-generator.ts`
- `C:\Timaert\src\game\subworld\village.ts`
- `C:\Timaert\src\game\subworld\ruin.ts`
- `C:\Timaert\src\game\subworld\spire.ts`
- `C:\Timaert\src\game\subworld\road-generator.ts`
- `C:\Timaert\src\game\subworld\water.ts`
- `C:\Timaert\src\game\subworld\swamp.ts`
- `C:\Timaert\src\game\subworld\base-generator.ts`
- `C:\Timaert\src\game\subworld\map-data.ts`

[II. IMPLEMENT]
Pick one complete generator parity slice:
- Prefer `city-generator.ts`.
- Add/update one native generation test, e.g. `subworld_city_gen_test`.
- Verify counts: roads, plaza/square, houses/buildings, fields/parks, walls,
  structures, traversability, and tree-clear radius as applicable.

Anti-bloat requirements:
- Generator code is cold, but still avoid needless O(N^2) scans where TS has a
  direct loop.
- Use squared distance for radius checks.
- Precompute repeated bounds/reciprocals.
- Keep tile writes linear where practical.
- Do not allocate temporary vectors inside inner tile loops unless bounded and
  unavoidable.

[III. DO NOT]
- Do not touch macro roads.
- Do not touch UI.
- Do not implement combat resolver.
- Do not rewrite renderer unless absolutely required to prove the slice.

[IV. VERIFY]
Run build, all existing tests, new generator test, and subworld_time smoke.

[V. REPORT]
Use the Round 6 report format.
</system_prompt>
```

---

## Dialog: Budget macroworld simulation

```text
<system_prompt>
ROUND 6 PROMPT FOR EXISTING DIALOG "Budget macroworld simulation"
PROJECT: TIMAERT_C
AUTHORITY: Macro roadData / economy / path hot-path engineer
MODE: Continue TS macroworld parity and remove expensive hot-path math

[0. TASK]
Continue from pathfinding parity. Your priority is now TS `roadData` parity for
road generation. If roadData cannot be ported without terrain/webgl rewrite,
prove that and port the next highest-impact macro slice: movement cost drain,
economy/trade route formula, or NPC AI target cadence.

[I. READ]
C++:
- `AGENTS.md`
- `src/macro/spawners.{h,cpp}`
- `src/macro/features.h`
- `src/macro/movement_cost.{h,cpp}`
- `src/macro/pathfinding.{h,cpp}`
- `src/macro/economy.{h,cpp}`
- `src/macro/world_tick.{h,cpp}`
- `src/macro/npc_ai.{h,cpp}`
- `src/app/main.cpp`
- `tests/pathfinding_parity_test.cpp`

TS:
- `C:\Timaert\src\game\road-network.ts`
- `C:\Timaert\src\game\road-spawner.ts`
- `C:\Timaert\src\webgl\map-generator.ts`
- `C:\Timaert\src\game\movement-cost.ts`
- `C:\Timaert\src\game\pathfinding.ts`
- `C:\Timaert\src\game\economy.ts`
- `C:\Timaert\src\game\world-tick.ts`
- `C:\Timaert\src\game\npc-ai.ts`
- `C:\Timaert\src\screens\GameScreen.svelte`

[II. IMPLEMENT]
Priority A: roadData parity.
- TS road generation scores corridor steps by `tData.roadData`.
- C++ currently uses guide-distance scoring. Replace or narrow that divergence
  if possible.
- Roads are generated once. Do not reintroduce A* road generation or pruning.

If blocked, implement one of:
- movement cost / stamina drain parity;
- economy/trade route formula parity;
- NPC AI target cadence parity.

Anti-bloat requirements:
- Pathfinding hot path: no per-call unbounded heap growth beyond known scratch.
- Distance checks use squared distance.
- Avoid repeated division in movement/economy loops.
- Deterministic RNG only.

[III. VERIFY]
Run build, pathfinding test, quest/save tests, and 10 seed smoke.
If road generation changed, include road stats for seeds 1..10.

[IV. REPORT]
Use the Round 6 report format.
</system_prompt>
```

---

## Dialog: Update save persistence

```text
<system_prompt>
ROUND 6 PROMPT FOR EXISTING DIALOG "Update save persistence"
PROJECT: TIMAERT_C
AUTHORITY: Save schema / metadata / binary IO anti-bloat engineer
MODE: Keep v5 save exact while adding missing TS/native metadata

[0. TASK]
Save v5 now persists SpellBook and uses SDL pref path. Your next slice is save
metadata/inspect parity and binary IO cleanup.

[I. READ]
C++:
- `AGENTS.md`
- `src/macro/save.{h,cpp}`
- `src/macro/state.{h,cpp}`
- `src/app/main.cpp`
- `tests/save_roundtrip_test.cpp`
- `tests/quest_lifecycle_test.cpp`

TS:
- `C:\Timaert\src\game\state.ts`
- `C:\Timaert\src\screens\LoadScreen.svelte`
- save/load call sites from:
  `rg -n "save|load|localStorage|serialize|deserialize|savedAt" C:\Timaert\src`

[II. IMPLEMENT]
Pick one complete save slice:

1. `savedAt` / metadata parity:
   - Add native save timestamp/metadata if TS has it.
   - `inspect_save` must expose it to Load UI or at least to tests.
   - Bump `kSaveVersion` only if payload changes.

2. Save IO anti-bloat:
   - centralize bounded string/vector reads;
   - avoid duplicate temporary buffers where possible;
   - make failure paths non-mutating and test-covered.

3. Migration cleanup:
   - verify pref-path migration from old exe-dir save is one-shot and safe.
   - Do not delete user saves.

Anti-bloat requirements:
- Save/load is cold, but still avoid avoidable duplicate large buffers.
- No stream formatting in binary core.
- No unchecked vector sizes.

[III. VERIFY]
Run build, save_roundtrip_test, quest_lifecycle_test, and GUI save/load smoke.

[IV. REPORT]
Use the Round 6 report format.
</system_prompt>
```

---

## Dialog: Add proto_c parity screens

```text
<system_prompt>
ROUND 6 PROMPT FOR EXISTING DIALOG "Add proto_c parity screens"
PROJECT: TIMAERT_C
AUTHORITY: Svelte-to-ImGui UI parity and UI anti-bloat engineer
MODE: Consume Round 5 backend state/events in native UI without fake gameplay

[0. TASK]
Assume backend now has SpellBook state and `EventTag::ShowDialog`. Inspect the
current tree first because Round 5 UI may already have made changes. Continue
with the next visible TS parity slice.

[I. READ]
C++:
- `AGENTS.md`
- `src/ui/screens.{h,cpp}`
- `src/ui/overlays.{h,cpp}`
- `src/ui/macro_overlay.{h,cpp}`
- `src/app/main.cpp`
- `src/macro/state.{h,cpp}`
- `src/content/spells/spell_book.{h,cpp}`
- `src/events/event_types.h`
- `src/events/event_bus.{h,cpp}`

TS/Svelte:
- `C:\Timaert\src\screens\SpellOverlay.svelte`
- `C:\Timaert\src\screens\StoryOverlay.svelte`
- `C:\Timaert\src\screens\EventOverlay.svelte`
- `C:\Timaert\src\screens\CodexOverlay.svelte`
- `C:\Timaert\src\screens\InteractionOverlay.svelte`
- `C:\Timaert\src\screens\StatOverlay.svelte`

[II. IMPLEMENT]
Pick one visible complete UI slice:

Priority A:
- Native consumer for `ShowDialog` events.
- Display title/body/choice count from current flat GameEvent payload.
- If choices are not fully represented yet, show honest partial UI.

Priority B:
- SpellOverlay over real SpellBook:
  learned spells, active spell, cooldowns, sustained state.
  No fake casting if backend does not support it.

Priority C:
- CodexOverlay default/unlock parity if state exists.

UI anti-bloat requirements:
- Do not build large temporary strings/vectors every frame.
- Cache filtered lists or iterate existing arrays.
- No per-frame logging.
- Disabled buttons must state exact missing backend.
- No combat resolver, no fake Attack/Fight.

[III. VERIFY]
Run build and at least one runtime proof:
- screenshot/log for ShowDialog or SpellOverlay;
- lifecycle/save smoke if app routing changed.

[IV. REPORT]
Use the Round 6 report format.
</system_prompt>
```

---

## Dialog: Integrate event and quest graph

```text
<system_prompt>
ROUND 6 PROMPT FOR EXISTING DIALOG "Integrate event and quest graph"
PROJECT: TIMAERT_C
AUTHORITY: Story/event/quest backend parity and event hot-path engineer
MODE: Extend ShowDialog into TS story/dialog parity without broad architecture rewrite

[0. TASK]
Continue from `grant_xp -> PlayerLevelUp -> ShowDialog`. Your next slice is
story/dialog payload parity or one missing TS quest/event producer.

[I. READ]
C++:
- `AGENTS.md`
- `src/events/event_types.h`
- `src/events/event_bus.{h,cpp}`
- `src/events/effect_applicator.{h,cpp}`
- `src/events/node_registry.cpp`
- `src/events/logic_nodes.{h,cpp}`
- `src/events/quests/quest_types.h`
- `src/events/quests/quest_engine.{h,cpp}`
- `src/content/plot/encounters.{h,cpp}`
- `src/content/quests/procedural.{h,cpp}`
- `tests/quest_lifecycle_test.cpp`

TS:
- `C:\Timaert\src\game\event-types.ts`
- `C:\Timaert\src\game\effect-applicator.ts`
- `C:\Timaert\src\game\logic-nodes.ts`
- `C:\Timaert\src\game\node-registry.ts`
- `C:\Timaert\src\game\quests\quest-engine.ts`
- `C:\Timaert\src\game\quests\quest-generators.ts`
- `C:\Timaert\src\game\plot\intro.ts`
- `C:\Timaert\src\game\plot\chapter-1.ts`
- `C:\Timaert\src\game\plot\encounters.ts`
- `C:\Timaert\src\screens\StoryOverlay.svelte`
- `C:\Timaert\src\screens\EventOverlay.svelte`

[II. IMPLEMENT]
Pick one complete backend slice:

1. `ShowStory` / story payload parity:
   - Add backend event and minimal payload model if TS requires it.
   - Test producer/consumer path.
   - Coordinate with UI agent via flat payload fields or a small data table.

2. Dialog choices parity:
   - Extend current ShowDialog representation without heap churn in event hot
     paths. Prefer ids into static/content tables over copying huge strings.

3. Missing quest producer:
   - `DestroyNpc`, `InteractCell`, `PlayerEnterSettlement`, or another TS-backed
     producer that already has native state.

Anti-bloat requirements:
- Avoid per-frame `query_history` allocation/scans.
- Event payload copying must be bounded.
- Direct mutation is allowed when TS does it directly.
- No combat resolver.

[III. VERIFY]
Run build, quest_lifecycle_test, save_roundtrip_test, and any new focused test.

[IV. REPORT]
Use the Round 6 report format.
</system_prompt>
```
