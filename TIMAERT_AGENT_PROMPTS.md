# TIMAERT_C: Round 3 prompts from latest logs

Use these in the same 6 existing dialogs. Do not rename the dialogs.

The six active dialog names from the latest log file:

1. `Fix boot lifecycle registry leaks`
2. `Audit docs drift and legacy code`
3. `Budget macroworld simulation`
4. `Update save persistence`
5. `Add proto_c parity screens`
6. `Integrate event and quest graph`

Current baseline from logs, 2026-05-10:

- Repo: `C:\Timaert\timaert_c`.
- Stack: C++23, SDL2, OpenGL 3.2 Core, EnTT, ImGui.
- This is not C# and not Unity.
- Correct Windows build command from repo root:
  `cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`
- Do not use `cmake --build build` unless you first prove that build dir exists.
- Build now passes through `build-msvc`.
- `New Game` reaches `[boot] done`.
- User can walk.
- `quest_lifecycle_test.exe` exists and passes.
- Binary save v4 roundtrip harness was reported OK, but there is no committed
  `save_roundtrip_test` target in current CMake.
- UI runtime evidence exists for Load screen, Equipment tab placeholder,
  NPC Talk popup, settlement panel, trade mutation, and quest accept.
- Remaining objective gaps from logs:
  - full repeated boot path was not objectively automated;
  - GUI save/load needs a canonical save path and committed repeatable test;
  - subworld enter/wait/leave time movement was instrumented but not proven;
  - road router is budgeted and boot-safe, but prunes many edges under cap;
  - Equipment, Build tab, Attack are explicit placeholders;
  - ShowDialog / ShowStory have no real consumer;
  - item rewards and delivery consumption still mutate inventory directly;
  - FindLocation / DestroyNpc / InteractCell need real producers;
  - generated runtime screenshots/logs/save files are cluttering repo root.

Global rules for every agent:

- Read `AGENTS.md` first.
- Do not revert unrelated dirty files.
- Do not delete another agent's evidence artifacts unless you move them into a
  documented evidence folder or add ignore rules and report exactly what moved.
- No exceptions, no RTTI.
- No hidden global lifecycle drift.
- No per-frame heap churn in hot paths.
- No broad rewrites.
- Build after changes with the `build-msvc` command above.
- If adding a test, add it to CMake and run the built exe.
- Runtime claims require runtime evidence.
- End with one of: `VERIFIED`, `BUILT ONLY`, `PENDING VERIFICATION`, `BLOCKED`.

Coordination / ownership:

- Lifecycle dialog owns boot/reset invariants and automated app smoke hooks.
- Docs dialog owns documentation truth, `.gitignore`, and evidence artifact
  hygiene.
- Macro dialog owns road generation, macro/subworld time, and subworld runtime
  proof.
- Save dialog owns save path, save schema, save/load tests, and GUI load proof.
- UI dialog owns player-visible panels/actions/placeholders.
- Event dialog owns event semantics, quest backend, and non-UI gameplay
  producers/consumers.

---

## Dialog: Fix boot lifecycle registry leaks

```text
<system_prompt>
ROUND 3 PROMPT FOR EXISTING DIALOG "Fix boot lifecycle registry leaks"
PROJECT: TIMAERT_C
AUTHORITY: Senior C++ lifecycle / runtime-smoke engineer
MODE: Convert manual boot confidence into deterministic proof

[0. FACTS FROM LATEST LOGS]
Your Round 2 built successfully. Current boot counts were:
- Spells: 8
- EventBus subscriptions: 0
- Logic nodes: 2

`New Game` reached `[boot] done`, but full `New -> Title -> New` automation was
inconclusive because ImGui click targets were unreliable. Save/load GUI path was
also not proven in your pass. This is now the lifecycle gap: we need a stable,
repeatable app-level smoke path that does not depend on fragile desktop clicks.

[I. MANDATORY RECON]
Read:
- `AGENTS.md`
- `src/app/main.cpp`
- `src/content/spells/spell_types.{h,cpp}`
- `src/content/spells/registry.{h,cpp}`
- `src/events/event_bus.{h,cpp}`
- `src/events/logic_nodes.{h,cpp}`
- `src/events/node_registry.{h,cpp}`
- `src/macro/state.{h,cpp}`
- `src/macro/save.{h,cpp}`
- `src/ui/screens.{h,cpp}`

Run:
- `rg -n "boot_trace|TIMAERT_BOOT_TRACE|destroy_world|boot_world|boot_world_from_save|returnToTitle|AppState::Title|subscription_count|node_count|active_count|is_consistent|spell_registry" src`
- `git diff -- src/app/main.cpp src/events src/content/spells src/macro src/ui`

[II. PRIMARY OBJECTIVES]
1. Add deterministic lifecycle smoke support.
   Preferred shape:
   - an env-var or command-line test mode, e.g. `TIMAERT_SMOKE_SCRIPT`,
     that runs scripted app actions from inside the main loop instead of using
     OS mouse clicks;
   - actions must be minimal: `new_game`, `wait_boot_done`, `return_title`,
     `new_game`, `quit`;
   - smoke mode must be disabled by default and must not affect normal play.

   If an in-app script hook is too invasive, add a smaller dedicated lifecycle
   test target that proves registry reset invariants and clearly state which
   app-level boot path remains manual.

2. Prove repeated boot/reset invariants.
   Required sequence:
   - New Game #1 reaches boot done.
   - Return to Title or equivalent destroy path runs.
   - New Game #2 reaches boot done.
   - Counts after each boot: spells, bus subscriptions, logic nodes.
   - Counts after destroy: bus and logic empty.

3. Keep lifecycle ownership clean.
   - No persistent EventBus handlers pointing at old GameState.
   - No duplicated spell ids.
   - No duplicated logic nodes.
   - `clear_saved_subworlds()` and subworld state must be handled at the correct
     lifecycle boundary if they are world/session scoped.

4. Diagnostics discipline.
   - `TIMAERT_BOOT_TRACE` is acceptable.
   - No unconditional stderr spam in normal mode.
   - Crash filter may stay Windows/debug scoped.

[III. CONSTRAINTS]
- Do not rewrite UI.
- Do not change save schema.
- Do not change quest semantics.
- No new global mutable state except tightly scoped smoke runtime guarded by
  env/CLI and reset on exit.

[IV. BUILD AND RUN]
Build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`

Run your smoke path and include exact command.

[V. REPORT]
Return:
1. Files changed.
2. Smoke mechanism: env/CLI/test target.
3. Repeated boot sequence result.
4. Counts after boot #1, destroy, boot #2.
5. Build output.
6. Remaining lifecycle risks.
7. STATUS.
</system_prompt>
```

---

## Dialog: Audit docs drift and legacy code

```text
<system_prompt>
ROUND 3 PROMPT FOR EXISTING DIALOG "Audit docs drift and legacy code"
PROJECT: TIMAERT_C
AUTHORITY: Technical lead / repo hygiene / documentation truth owner
MODE: Convert Round 2 evidence into clean repo state

[0. FACTS FROM LATEST LOGS]
Your Round 2 updated docs and build instructions. Build passed. Since then,
runtime agents generated many untracked evidence files in repo root:
`runtime_*.png`, `runtime_*.err`, `runtime_*.out`, and `save.bin`.

Docs now need a final evidence ledger update:
- build-msvc is real;
- New Game and walking are real;
- save binary harness is real, but GUI path must be documented by exact evidence;
- UI settlement/trade/quest/NPC Talk have evidence;
- Equipment/Build/Attack are placeholders;
- subworld time remains not objectively verified;
- ShowDialog/ShowStory still missing.

[I. MANDATORY RECON]
Read:
- `AGENTS.md`
- `.gitignore`
- `README.md`
- `MERGE_PLAN.md`
- `ARCHITECTURE.md`
- `translation.md`
- `CMakeLists.txt`
- `src/app/main.cpp`
- `src/ui/overlays.cpp`
- `src/macro/save.{h,cpp}`
- `src/macro/spawners.{h,cpp}`

Run:
- `git status --short`
- `rg -n "runtime_|save.bin|build-msvc|SDL3|samosbor_nolod|PENDING|VERIFIED|ShowDialog|Equipment|Build tab|Attack|subworld time|trace_roads|RoadTraceStats" README.md MERGE_PLAN.md ARCHITECTURE.md translation.md .gitignore`

[II. PRIMARY OBJECTIVES]
1. Repo artifact hygiene.
   - Add `.gitignore` entries for generated runtime evidence:
     `runtime_*.png`, `runtime_*.out`, `runtime_*.err`, `save.bin`,
     `save.bin.tmp`, `save.bin.bak`, and similar local smoke outputs.
   - Do not delete evidence files silently. If you move them, create a small
     evidence manifest with filenames and purpose.
   - Do not ignore source tests or docs.

2. Evidence ledger in README.
   Add or update a concise section:
   - exact build command;
   - smoke status table;
   - test targets available;
   - which runtime screenshots/logs prove which flows;
   - remaining manual-only checks.

3. Correct stale status in docs.
   - Road generation: budgeted A* + fallback, boot verified, quality still
     under budget/pruning debt.
   - Save: v4 binary and harness verified; GUI save/load path pending unless
     save agent proves canonical GUI flow.
   - UI: Load, character tabs, settlement trade/quest accept, NPC Talk are
     runtime-evidenced; Equipment/Build/Attack remain placeholders.
   - Event/quest: native quest lifecycle test passes; ShowDialog/ShowStory and
     some objective producers remain incomplete.
   - Subworld time: instrumented, not proven by reliable runtime test.

4. Build contract check.
   - Ensure docs never tell agents to run `cmake --build build` as the primary
     Windows command.
   - Ensure SDL2 vs SDL3 warning remains explicit.

[III. CONSTRAINTS]
- No gameplay feature work.
- No lore rewrite.
- No broad architecture rewrite.
- Every status claim must be tied to a code/test/runtime fact.

[IV. BUILD]
Build after doc/.gitignore changes:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`

[V. REPORT]
Return:
1. Files changed.
2. New ignore rules.
3. Evidence ledger updates.
4. Stale claims corrected.
5. Build result.
6. Remaining unverified claims.
7. STATUS.
</system_prompt>
```

---

## Dialog: Budget macroworld simulation

```text
<system_prompt>
ROUND 3 PROMPT FOR EXISTING DIALOG "Budget macroworld simulation"
PROJECT: TIMAERT_C
AUTHORITY: Senior macroworld / subworld runtime engineer
MODE: Prove subworld-time continuity and tighten road budget evidence

[0. FACTS FROM LATEST LOGS]
Your Round 2 compiled. Road generation now uses bounded reusable-scratch A*
with land-Bresenham fallback.

Observed default 1024 New Game road stats:
- cities=68
- attempted=156
- kept=63
- pruned=93
- bounded=56
- fallback=7
- expansions=300000
- edgeCapHits=45
- wholeCapHits=23

Macro tick model from logs:
- macro view: 14.4 game minutes / real second, up to 32 daily ticks/frame,
  full macro NPC AI at 0.5s cadence;
- subworld view: clock advances at same rate, daily catch-up max 1 day/frame,
  macro NPC AI max 64 NPC ticks/frame with queued sweeps capped at 4.

Unproven: subworld enter/wait/leave runtime automation did not produce reliable
input evidence.

[I. MANDATORY RECON]
Read:
- `AGENTS.md`
- `src/app/main.cpp`
- `src/macro/world_tick.{h,cpp}`
- `src/macro/npc_ai.{h,cpp}`
- `src/macro/spawners.{h,cpp}`
- `src/macro/pathfinding.{h,cpp}`
- `src/macro/politik.{h,cpp}`
- `src/sub/engine.{h,cpp}`
- `src/sub/seamless_manager.{h,cpp}`
- `src/sub/map_factory.{h,cpp}`
- `src/events/event_bus.{h,cpp}`

Run:
- `rg -n "WorldTickRuntime|MacroNpcAiRuntime|tick_world|tick_world_time_only|subworld\\.active|subworld enter|subworld leave|RoadTraceStats|trace_roads|edgeCapHits|wholeCapHits|fallback|TIMAERT_BOOT_TRACE" src`
- `git diff -- src/app/main.cpp src/macro src/sub`

[II. PRIMARY OBJECTIVES]
1. Make subworld time proof reliable.
   Preferred:
   - use lifecycle smoke hook from the lifecycle agent if available;
   - otherwise add a minimal env/CLI smoke path owned by this dialog for:
     New Game -> Enter subworld -> wait fixed frames/seconds -> Leave ->
     print worldTime before/after.

   The result must not depend on fragile OS key focus.

2. Verify macro/subworld time invariants.
   - World time advances while subworld is active.
   - Daily catch-up does not dump all work on exit.
   - Macro NPC AI budget counters do not grow unbounded.
   - Player macro position remains deterministic after enter/leave.

3. Road budget sanity.
   - Keep boot under bounded caps.
   - Report road stats in boot trace only, not normal release spam.
   - If kept edges remain very low, tune budget/corridor conservatively or
     document why current pruning is accepted.
   - Do not reintroduce per-edge W*H allocations.

4. Add a lightweight native test only if feasible.
   Examples:
   - road budget deterministic test on a small map;
   - world tick runtime accumulator test.
   Do not build a huge test framework.

[III. CONSTRAINTS]
- No UI feature work.
- No save schema changes.
- No event quest semantics changes.
- No per-frame heap churn.
- No exceptions/RTTI.

[IV. BUILD/RUN]
Build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`

Run:
- default New Game boot trace with road stats;
- subworld time smoke with before/after time.

[V. REPORT]
Return:
1. Files changed.
2. Subworld time proof command and output.
3. Before/after worldTime.
4. Road stats after any tuning.
5. Allocation/budget notes.
6. Build result.
7. STATUS.
</system_prompt>
```

---

## Dialog: Update save persistence

```text
<system_prompt>
ROUND 3 PROMPT FOR EXISTING DIALOG "Update save persistence"
PROJECT: TIMAERT_C
AUTHORITY: Senior C++ persistence engineer
MODE: Make save/load canonical, committed, and GUI-proven

[0. FACTS FROM LATEST LOGS]
Your Round 2 verified save v4 integration:
- schema version 4;
- payload header with magic/version/payloadSize/checksum;
- temp write + verify + backup + replace;
- custom map size preserved in a harness;
- active quest preserved in a harness;
- truncated payload rejected;
- bad version rejected;
- failed load did not mutate sentinel state.

But current CMake only contains `quest_lifecycle_test`, not a committed
`save_roundtrip_test` target. GUI path remained not fully counted in your pass.
UI logs later created/loaded `save.bin`, but save path is currently relative to
working directory. That is fragile: launching from repo root writes repo-root
`save.bin`; launching from `build-msvc` writes build-dir `save.bin`.

[I. MANDATORY RECON]
Read:
- `AGENTS.md`
- `CMakeLists.txt`
- `src/macro/save.{h,cpp}`
- `src/macro/state.{h,cpp}`
- `src/macro/items.{h,cpp}`
- `src/macro/army.h`
- `src/events/quests/quest_types.h`
- `src/events/quests/quest_engine.{h,cpp}`
- `src/app/main.cpp`
- `src/ui/screens.{h,cpp}`
- `src/sub/map_factory.h`
- `tests/quest_lifecycle_test.cpp`

Run:
- `rg -n "kSaveVersion|save_game\\(|load_game\\(|inspect_save|kSavePath|save.bin|SDL_GetBasePath|activeQuests|cityCountTarget|mapParams|SavedSubworld|roundtrip|add_executable" CMakeLists.txt src tests`
- `git diff -- src/macro/save.* src/macro/state.* src/app/main.cpp src/ui src/sub/map_factory.h CMakeLists.txt tests`

[II. PRIMARY OBJECTIVES]
1. Define canonical save path.
   - Stop relying on arbitrary process working directory.
   - Preferred Windows/dev behavior: save next to executable or under a clearly
     documented writable app data path.
   - Implement a small helper, e.g. `resolve_save_path()`, in app layer or save
     layer with no global mutable state.
   - Keep tests able to write temp saves without touching player save.

2. Commit official save roundtrip test.
   - Add `tests/save_roundtrip_test.cpp`.
   - Add CMake target `save_roundtrip_test`.
   - Test must cover:
     - save file written;
     - load file read;
     - custom map dimensions preserved;
     - one active quest preserved;
     - bad version rejected;
     - truncated/corrupt payload rejected;
     - failed load does not mutate sentinel state.

3. GUI save/load proof.
   - Use stable mouse toolbar path or app smoke hook if available.
   - Prove: New Game -> Save -> restart -> Load -> `[boot] done` and playing
     world visible.
   - Report exact save file path and size.

4. Schema audit after UI/event changes.
   - Confirm accepted quest from UI persists.
   - Confirm player inventory/gold changes from settlement trade persist if
     UI agent's trade path is already merged.
   - Confirm session-only fields are intentionally not saved:
     terrain/render caches, EventBus history, LogicNode runtime,
     SavedSubworld cache, camera/UI state.

[III. CONSTRAINTS]
- No JSON.
- No backward compatibility requirement.
- Do not bump `kSaveVersion` unless schema changes.
- No UI redesign.
- No event architecture rewrite.
- No exceptions/RTTI.

[IV. BUILD/RUN]
Build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`

Run:
- `build-msvc\save_roundtrip_test.exe`
- `build-msvc\quest_lifecycle_test.exe`
- GUI save/load smoke if feasible.

[V. REPORT]
Return:
1. Files changed.
2. Canonical save path behavior.
3. `save_roundtrip_test` output.
4. GUI save/load evidence.
5. Schema/session-only audit.
6. Build output.
7. STATUS.
</system_prompt>
```

---

## Dialog: Add proto_c parity screens

```text
<system_prompt>
ROUND 3 PROMPT FOR EXISTING DIALOG "Add proto_c parity screens"
PROJECT: TIMAERT_C
AUTHORITY: Senior gameplay UI engineer
MODE: Replace the most visible placeholders with honest minimal gameplay

[0. FACTS FROM LATEST LOGS]
Your Round 2 passed build and runtime evidence:
- Title -> Load opens AppState::Load;
- Load valid save boots saved world;
- Back/Esc from Load fixed;
- toolbar Inventory/Party/Equipment opens correct Character tabs;
- settlement near city opens real panel;
- settlement Trade mutates gold/inventory;
- settlement Quest accept calls real QuestEngine::accept;
- NPC Talk popup works;
- NPC Trade is gated;
- Attack disabled.

Remaining UI gaps from logs:
- Equipment tab is explicit placeholder;
- Build tab is explicit placeholder;
- Attack is disabled/not wired;
- keyboard hotkey automation unreliable; toolbar/mouse paths verified.

[I. MANDATORY RECON]
Read:
- `AGENTS.md`
- `src/app/main.cpp`
- `src/ui/screens.{h,cpp}`
- `src/ui/overlays.{h,cpp}`
- `src/ui/macro_overlay.{h,cpp}`
- `src/macro/state.{h,cpp}`
- `src/macro/items.{h,cpp}`
- `src/macro/economy.{h,cpp}`
- `src/macro/save.{h,cpp}`
- `src/events/event_types.h`
- `src/events/effect_applicator.{h,cpp}`
- `src/events/quests/quest_engine.{h,cpp}`
- `src/content/plot/encounters.{h,cpp}`

Reference only:
- `C:\Timaert\src\screens\StatOverlay.svelte`
- `C:\Timaert\src\screens\SettlementOverlay.svelte`
- `C:\Timaert\src\screens\NpcProximityPanel.svelte`

Run:
- `rg -n "Equipment slots are not wired|Settlement construction is not wired|Attack not wired|Trade not wired|CharacterPanelTab::Equipment|draw_settlement|draw_npc_proximity|NpcInventory|BattleStart|PlayerGoldChange|inventory" src`
- `git diff -- src/ui src/app/main.cpp src/macro src/events`

[II. PRIMARY OBJECTIVES]
1. Equipment tab: make it minimally real or explicitly prove no data model.
   - If player/equipment slots already exist, wire equip/unequip for compatible
     inventory items.
   - If no equipment model exists, add only a small data-model proposal to your
     report and do not invent a save-breaking schema without save-agent
     coordination.
   - At minimum, replace vague placeholder with a precise status:
     "equipment data model missing" vs "slots empty".

2. Build tab: make it a real read-only settlement development panel.
   - Do not implement full construction economy unless existing data supports it.
   - Show real settlement fields: population/economy/garrison/inventory/history
     or available build-state fields.
   - If construction actions are absent, display disabled actions with exact
     missing backend, not a generic placeholder.

3. NPC Attack: route to an existing backend event or encounter path.
   - If `BattleStart` exists but no combat resolver exists, clicking Attack may
     emit/log a `BattleStart` event and open an honest "combat resolver pending"
     modal.
   - Do not fake damage/combat.
   - Do not crash if target despawns.

4. Keyboard/hotkey sanity.
   - Fix app-side hotkey routing only if code is wrong.
   - If automation focus is the only issue, document that toolbar paths are the
     verified path.

5. Preserve verified flows.
   - Do not break Load, settlement trade, quest accept, NPC Talk, or character
     tabs.

[III. CONSTRAINTS]
- No save schema changes unless coordinated and necessary.
- No quest engine rewrite.
- No broad UI state stack rewrite.
- UI can allocate in cold/open paths; avoid unbounded per-frame growth.
- No fake functional buttons.

[IV. BUILD/RUN]
Build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`

Runtime checklist:
- Load valid save.
- Open Character -> Equipment.
- Open settlement -> Build tab.
- Try NPC Attack.
- Recheck settlement Trade and Quest Accept.

[V. REPORT]
Return:
1. Files changed.
2. Equipment tab final behavior.
3. Build tab final behavior.
4. NPC Attack final behavior.
5. Runtime evidence screenshots/logs.
6. Build result.
7. Remaining UI gaps.
8. STATUS.
</system_prompt>
```

---

## Dialog: Integrate event and quest graph

```text
<system_prompt>
ROUND 3 PROMPT FOR EXISTING DIALOG "Integrate event and quest graph"
PROJECT: TIMAERT_C
AUTHORITY: Senior gameplay event/quest engineer
MODE: Remove direct-mutation islands and add missing producers

[0. FACTS FROM LATEST LOGS]
Your Round 2 added `quest_lifecycle_test`, and it passed:
`OK quest_lifecycle_test id=q_7_d2_0 hours=2 reward_gold=170 completed=1`

Event graph status from logs:
- Producers: SettlementVisit, PlayerMove, Encounter, TimeAdvance, quest accept /
  progress / completion / failure / reward events.
- Consumers: LogicNodeEngine, QuestEngine via stable last_tick_events,
  `apply_pending_event_effects()`.
- EventBus history cap remains 4096.

Remaining incomplete:
- ShowDialog / ShowStory have no consumer;
- item rewards and delivery item consumption still mutate inventory directly;
- FindLocation / DestroyNpc / InteractCell need real upstream producers;
- XP grant does not automatically prove level-up without PlayerLevelUp producer.

[I. MANDATORY RECON]
Read:
- `AGENTS.md`
- `src/events/event_types.h`
- `src/events/event_bus.{h,cpp}`
- `src/events/effect_applicator.{h,cpp}`
- `src/events/logic_nodes.{h,cpp}`
- `src/events/node_registry.{h,cpp}`
- `src/events/quests/quest_types.h`
- `src/events/quests/quest_engine.{h,cpp}`
- `src/content/quests/procedural.{h,cpp}`
- `src/content/plot/encounters.{h,cpp}`
- `src/app/main.cpp`
- `src/ui/overlays.{h,cpp}`
- `src/ui/macro_overlay.{h,cpp}`
- `tests/quest_lifecycle_test.cpp`

Run:
- `rg -n "EventTag|PlayerLevelUp|grant_xp|RewardKind::Item|DeliverItems|FindLocation|DestroyNpc|InteractCell|ShowDialog|ShowStory|BattleStart|NpcDeath|WorldCellChange|last_tick_events|apply_pending_event_effects|query_history" src tests`
- `git diff -- src/events src/content src/app/main.cpp src/ui tests`

[II. PRIMARY OBJECTIVES]
1. PlayerLevelUp producer.
   - When XP crosses level threshold, emit or queue `PlayerLevelUp`.
   - Avoid double level-up if multiple XP events arrive in one tick.
   - Preserve current effect-applicator rule: `grant_xp` itself should not hide
     level-up unless this pass deliberately centralizes it.
   - Add native test coverage.

2. Inventory event discipline.
   - Replace direct item reward mutation with a clear event/effect path if the
     current EventTag model can support it.
   - If adding item-specific event payload is too large, isolate the direct
     mutation in one named function and document why it remains direct.
   - Delivery item consumption should be audited the same way.

3. Objective producers.
   - Add real producers for at least one currently weak objective:
     `FindLocation`, `InteractCell`, or `DestroyNpc`.
   - Preferred minimal choices:
     - `FindLocation`: use existing PlayerMove / WorldCellChange semantics;
     - `InteractCell`: emit when player uses settlement/cell interaction;
     - `DestroyNpc`: emit from a real NPC death/despawn path if one exists.
   - Do not fake producers that never occur.

4. Dialog/story event boundary.
   - Backend may define `ShowDialog`/`ShowStory` only if UI has or is receiving
     a real consumer.
   - If UI owner has not implemented consumer, leave these as documented pending.

5. Tests.
   - Extend `quest_lifecycle_test` or add a small second test for:
     XP -> PlayerLevelUp,
     one item reward/consumption path,
     one added objective producer.

[III. CONSTRAINTS]
- No UI-owned gameplay state.
- Avoid touching UI except for one minimal producer hook if required.
- No save schema changes unless strictly necessary.
- No per-frame `query_history` allocations.
- No exceptions/RTTI.

[IV. BUILD/RUN]
Build:
`cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"`

Run:
- `build-msvc\quest_lifecycle_test.exe`
- any new event/quest test target.
- If a UI producer hook was added, run a minimal runtime smoke.

[V. REPORT]
Return:
1. Files changed.
2. New producers/consumers added.
3. Direct mutations removed or explicitly retained with reason.
4. Test outputs.
5. Build result.
6. Remaining incomplete tags/effects.
7. STATUS.
</system_prompt>
```
