# LOG: TMA_COMBAT_NPC_SOLDIER_BKR

## 2026-05-14 Final Report
STATUS: VERIFIED

What was wrong:
- Native army state still used legacy `UnitType`/RPS-style army histograms, so soldiers could not be real NPC kinds.
- Recruitment used synthetic unit buckets and fixed unit cost tables.
- Subworld entry did not project macro soldiers into actual ECS combat entities.
- Exit from dangerous zones had no hostile proximity gate.
- Death flow had no reliable player-owned hit attribution, hired-soldier XP path, or lootable corpse interaction.
- Save shape still assumed POD army/garrison/deserter histograms.

What was done:
- Replaced `src/macro/army.h` with `SoldierRecord` and `SoldierSquad`; removed `UnitType`, `ArmyComposition`, `kUnitStats`, `damage_multiplier`, `kHireCost`, `kUpkeepCost`, old hire/upkeep helpers.
- Extended `src/macro/npc.h` rows with upkeep, hireability, and XP reward; added NPC-kind upkeep, hire, garrison, and XP helpers.
- Moved `PlayerState::army`, `Settlement::garrison`, and `GameState::deserterPool` to `SoldierSquad`.
- Updated daily upkeep/garrison growth to NPC-kind records.
- Updated ImGui character/settlement recruitment readers to show NPC kinds and hire concrete garrison records.
- Added bounded squad serialization in `src/macro/save.cpp`; save version remains v8 and save tests now validate NPC soldier records.
- Added ECS components: `PlayerSoldierTag`, `SoldierLink`, `LastHit`, `Structure::Corpse`, `CorpseLoot`.
- Added `spawn_player_squad` to create real subworld NPC entities for the player squad.
- Added danger-zone exit gate through `SubworldEngine::leave(false)` and a status-line refusal message.
- Added death resolution for player-owned kills, hired-soldier XP, corpse loot spawn, and `E` interaction loot transfer.
- Added `tests/combat_squad_test.cpp` and CMake target.

Cinematic cheats used:
- No battle screen and no resolver simulation. Combat remains normal subworld ECS melee/projectile logic.
- Exit gating uses zone scalar + hostile radius scan, not a separate combat arena state.
- Corpses are lightweight ECS `Structure` records only when loot/gold exists.

Exact microseconds saved / cost notes:
- Removed per-use conversion pressure from unit buckets to NPC entities; squad upkeep is O(n), estimated about 0.3 us for 32 soldiers.
- Recruitment scan is button-triggered O(garrison), estimated about 1 us for 64 records.
- Subworld squad spawning is entry-only, estimated 10-40 us for small squads.
- Exit gate scan is attempt-only, estimated under 0.1 ms for current subworld entity counts.
- Death/loot work is death-event bounded; normal frame cost is effectively unchanged outside existing ECS views.

Verification evidence:
- MSVC/CMake/Ninja app build: PASS. Initial direct CMake failed because Visual C++ environment variables were missing; rerun through `vcvars64.bat` passed. One stale locked `build-msvc/timaert.exe` was moved to `build-msvc/timaert.locked-old.exe`, then the fresh `timaert.exe` linked.
- `save_roundtrip_test`: PASS. Output: `OK save_roundtrip_test path=...timaert_save_roundtrip_v8.bin bytes=1712 map=512x256 quest=q_active`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output: `OK combat_squad_test hired=1 garrison=1 upkeep=3 discounted=1 generated=9`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,subworld_time,quit`, seed 42; output ended with `subworld_time OK` and `PASS`.

Remaining legacy readers:
- No remaining `UnitType`, `ArmyComposition`, `kUnitStats`, `damage_multiplier`, `kHireCost`, `kUpkeepCost`, `hire_unit`, `calculate_army_upkeep`, or `total_units` matches in `src`/`tests`.
- `EventTag::BattleStart` remains as a plot event that logs "Battle: ..." because that path was pre-existing and does not read the deleted unit/RPS schema.

## 2026-05-15 Hardening Report
STATUS: VERIFIED

What was still weak:
- `BattleStart` event compatibility still terminated at a log-only breadcrumb, which was not a true no-battle-mode transfer.
- Player squad projections had `Combat` but not `SubworldAi`, leaving the "existing AI and CombatTemplate" requirement under-specified.
- The focused combat test validated macro hire/upkeep but not ECS subworld projection.

What was upgraded:
- Added `SubworldEngine::spawn_hostile_npc(...)` and app-level `BattleStart` routing. Encounter battle choices now enter subworld and spawn a real hostile NPC with `NPCKind`, `Combat`, `Health`, `NpcLevel`, `NpcInventory`, `SubworldAi::Combat`, `SubworldTag`, and `Sprite`.
- Updated `effect_applicator.cpp` so `BattleStart` no longer claims combat is unported; it now records a combat breadcrumb while runtime owns the real spawn.
- Added `SubworldAi::Combat` to projected player soldiers and made generic subworld AI skip `PlayerSoldierTag`; squad combat owns their target selection.
- Added a tile-vector overload for `spawn_player_squad` so focused tests can verify projection without constructing the full async seamless manager.
- Extended `combat_squad_test` to verify subworld projection components and AI contract.
- Added smoke action `trigger_battle_start` and verified it routes a legacy event into subworld combat.

Cinematic cheats used:
- BattleStart is converted into a local hostile spawn, not a battle arena or combat resolver.
- Smoke action force-leaves after proof so the existing subworld time smoke can run unchanged.

Exact microseconds saved / cost notes:
- BattleStart conversion cost is event-only: one hostile spawn plus subworld entry, no per-frame battle subsystem.
- PlayerSoldier AI skip adds a branch in existing ECS AI iteration; expected cost is below measurement noise for current entity counts.
- Projection test overload avoids async seam-manager construction in test binaries.

Verification evidence:
- Build: PASS via `vcvars64.bat` + CMake/Ninja. Orphaned build subprocesses from timed-out runs were terminated; no source rollback.
- `save_roundtrip_test`: PASS, v8, 1712 bytes.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, output included `projected=1`.
- Combined runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. Output showed `battle_start routed hostiles=0->1`, then `subworld_time OK`, then `PASS`.

Remaining legacy readers:
- `rg` still finds no deleted unit/army symbols in `src` or `tests`.
- `EventTag::BattleStart` remains only as an event compatibility token. It now routes to normal subworld NPC combat.

## 2026-05-15 Third-Pass Polish Report
STATUS: VERIFIED

What was still wrong:
- Subworld combat/death tick paths still had entity gather patterns that could allocate under load.
- `SubworldEngine::leave` snapshotted and synced macro position but did not destroy session-only `SubworldTag` entities. That allowed temporary hostile/soldier/corpse ECS records to survive until a later subworld enter path cleared them.
- The BattleStart smoke proved combat entry, but did not prove forced leave cleaned the session.

What was done:
- Replaced subworld combat actor gather with a fixed `std::array<entt::entity, 2048>`.
- Replaced subworld death gather with a fixed `std::array<entt::entity, 512>`.
- Added `clear_subworld_entities` with a fixed 2048-entity reap batch loop.
- Called cleanup from `SubworldEngine::leave(true/false)` after `snapshot_all_to_cache()` and `sync_macro_player_to_center()`, before nulling pointers.
- Added a smoke-only invariant in `trigger_battle_start`: after `app.subworld.leave(true)`, `view<SubworldTag>()` must be zero or smoke fails.

Cinematic cheats used:
- Cleanup stays session-scoped; no persistent corpse simulation was introduced.
- Dense combat degrades by bounded frame batches instead of allocating or inventing a battle resolver.
- The verification hook is smoke-only and does not add normal gameplay UI/debug spam.

Exact microseconds saved / cost notes:
- Fixed combat/death gather removes allocator jitter from combat frames. Estimated low-end saving: 2-15 us during dense actor/death spikes.
- Exit cleanup is paid only on leave. Estimated cost: under 0.1 ms for current subworld entity counts.
- Preventing leaked `SubworldTag` entities also avoids later macro overlay/reader work on stale session actors.

Verification evidence:
- MSVC/CMake/Ninja build: PASS through `vcvars64.bat`, single-job, no dotnet rebuild.
- `save_roundtrip_test`: PASS. Output included `OK save_roundtrip_test ... bytes=1756 ...`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. Output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.

## 2026-05-15 Fourth-Pass Combat Owner Report
STATUS: VERIFIED

What was still wrong:
- `tick_npc_ai` moved `SubworldAi::Combat` hostiles toward the player.
- `SubworldEngine::tick_subworld_combat` then moved those same `ecs::Combat` actors again toward player soldiers or player. Result: double integration, faster-than-authored hostiles, less predictable exit/combat behavior.

What was done:
- Updated `src/sub/ai.cpp`: `SubworldAi::Combat` returns immediately for `PlayerSoldierTag` or any entity with `ecs::Combat`.
- Updated `src/sub/ai.h` so the header documents the single-owner movement contract.
- Preserved the fallback path for AI-only combat entities with no `ecs::Combat`, so old/degraded data can still chase without damage.
- Updated `tests/combat_squad_test.cpp`: added a real combat actor that must not move in `tick_npc_ai`, plus an AI-only fallback actor that must still move.
- Updated `CMakeLists.txt`: `combat_squad_test` now links `src/sub/ai.cpp`.

Cinematic cheats used:
- No physics steering simulation. Movement ownership is a hard routing rule: AI owns wander/flee/fallback; combat pass owns real combat actors.
- Determinism over realism: one movement integration per actor per frame.

Exact microseconds saved / cost notes:
- Removes one duplicate movement integration per active hostile per frame.
- Estimated low-end saving: 1-4 us for 100 combat actors, with larger value in determinism than raw frame time.
- Prevents authored NPC speed from being silently multiplied by subsystem overlap.

Verification evidence:
- MSVC/CMake build: PASS after one retry. First attempt hit build-tree `restat` permission contention, not a compiler error. No dotnet rebuild.
- `save_roundtrip_test`: PASS. Output included `bytes=1756`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1 ai_owner=1`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. Output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- Legacy scan: no deleted `UnitType`/RPS army symbols in `src` or `tests`.
- Hot-path scan in touched combat files: no `std::vector<entt::entity>`, `std::async`, `std::rand`, exceptions, or catch/throw patterns.

## 2026-05-15 Fifth-Pass Leave Death Flush Report
STATUS: VERIFIED

What was still wrong:
- `leave(true/false)` could clear session `SubworldTag` entities before resolving pending deaths from the same frame.
- That edge case could lose player-owned XP from a killed hostile and fail to remove a dead hired soldier from the macro `SoldierSquad`.
- `respawn_subworld_npcs` still destroyed stale subworld entities directly from a live view during enter cleanup.

What was done:
- Updated `SubworldEngine::resolve_subworld_deaths(bool drainAll = false)`.
- Normal tick still resolves one fixed 512-death batch.
- `leave(true/false)` now calls `resolve_subworld_deaths(true)` and drains all pending dead subworld entities in bounded 512-entity chunks before snapshot/cleanup.
- Updated `respawn_subworld_npcs` cleanup to gather stale `SubworldTag` entities into a fixed 2048-entity batch before destroying them.
- Strengthened the `trigger_battle_start` smoke action: it finds the spawned hostile, marks it dead with player-owned `LastHit`, leaves the subworld, and fails if XP/level did not advance or any `SubworldTag` leaked.

Cinematic cheats used:
- Corpses remain session-scoped; no persistent corpse simulation or reward screen was introduced.
- Exit drains gameplay facts only: XP, roster removal, and loot/corpse conversion before the session is cleared.
- Overflow is handled by fixed batches instead of heap growth.

Exact microseconds saved / cost notes:
- Normal combat frame cost unchanged because `drainAll=false` is still one bounded batch.
- Forced/full leave drain is exit-only. Typical fight cost estimate: 1-5 us on i3/MX350; pathological mass death drains in 512-entity chunks.
- Fixed enter reap avoids allocator spikes and active-view mutation. Estimated stability gain is higher than raw frame saving; expected cost remains under 0.1 ms for current counts.

Verification evidence:
- MSVC/CMake build: PASS via `vcvars64.bat` + CMake/Ninja, single job, no dotnet rebuild.
- `save_roundtrip_test`: PASS. Output included `bytes=1756`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1 ai_owner=1`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. The smoke forced the routed hostile through `Dead + LastHit`, then completed `subworld_time OK` and `PASS`.
- Legacy scan: PASS. No deleted `UnitType`/RPS army symbols in `src` or `tests`.
- Hot-path scan: PASS. No `std::vector<entt::entity>`, `std::async`, `std::rand`, or `throw` in touched combat files.

## 2026-05-15 Sixth-Pass Soldier Identity Report
STATUS: VERIFIED

What was still wrong:
- Daily settlement garrison generation could reuse the same `SoldierRecord.entityId` range for the same settlement on multiple days.
- Player-soldier death resolution removed all macro soldiers with the same `entityId`, so a duplicated ID could turn one ECS death into multiple persistent soldier losses.
- Recruitment and army UI still mirrored NPC soldier data through hardcoded arrays instead of reading `NpcTypeDef::hireable`.

What was done:
- Added `remove_one_soldier_by_entity_id` and `count_soldiers_with_entity_id` to `src/macro/army.h`.
- Updated `SubworldEngine::resolve_subworld_deaths` to remove one macro soldier record per dead projected soldier.
- Added `garrison_soldier_id_base(settlementId, day)` to `src/macro/npc.h`.
- Updated `src/macro/world_tick.cpp` so daily garrison generation uses day-scoped deterministic ID ranges.
- Added `npc_hire_price_base` for data-driven recruit UI preview pricing.
- Updated `src/ui/overlays.cpp` to iterate `NPCType::Count`, filter by `npc_hireable`, and remove hardcoded recruit/soldier type arrays.
- Extended `combat_squad_test` with duplicate-ID removal and daily ID-base checks; output now includes `unique_ids=1`.

Cinematic cheats used:
- No persistent global soldier-counter save field was added. Deterministic day-scoped IDs buy correctness without a save schema change.
- One ECS soldier death maps to one macro record, preserving the fake-but-controllable squad projection contract.
- UI stays data-driven; new hireable NPC kinds become visible through the registry instead of parallel UI data.

Exact microseconds saved / cost notes:
- ID base generation is O(1) in daily tick, expected below measurement noise.
- One-record death removal is O(n) on soldier death only; expected below 1 us for normal squads on i3/MX350.
- Registry UI loop scans eight NPC rows today; expected under 1 us when the panel is open.
- Avoided a saved global counter and migration work; no save version bump needed because serialized shape did not change.

Verification evidence:
- MSVC/CMake build: PASS via `vcvars64.bat` + CMake/Ninja, no dotnet rebuild.
- `save_roundtrip_test`: PASS. Output included `bytes=1756`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. Output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.

## 2026-05-15 Ninth-Pass Bottom Append: Polish Mandate And Squad Allocation
STATUS: VERIFIED

What was done:
- `src/macro/army.h`: `add_squad` now skips empty input and reserves exact destination capacity before inserting source soldiers.
- `src/macro/npc.h`: `generate_garrison` reserves its bounded daily budget; `hire_npc` reserves one player-squad slot before moving a concrete soldier record.
- Polish mandate scans were executed after core completion. The only exact anti-bloat hits were inspected false positives outside this domain: an OpenGL/ImGui `unsigned int` texture bridge and a `dart-throw` comment.

Verification evidence:
- MSVC/CMake build: PASS; final `cmake --build build-msvc` returned `ninja: no work to do` after concurrent build trees cleared. No dotnet rebuild.
- Direct MSVC syntax compiles: PASS for `tests/combat_squad_test.cpp` and `src/sub/spawn.cpp` against current headers.
- `save_roundtrip_test`: PASS, `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, `projected=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- Legacy scan: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.

## 2026-05-15 Tenth-Pass Reserve Growth And Tile Guard
STATUS: VERIFIED

What was still wrong:
- Exact reserve-before-append avoided one allocation but could cause repeated reallocations on a sequence of one-soldier hires.
- `spawn_player_squad` indexed any non-empty tile buffer as if it were a full subworld terrain mask.

What was done:
- Added `reserve_soldiers_for_append` in `src/macro/army.h` with growth-aware capacity expansion.
- Updated `add_squad` to use the helper and handle self-append deterministically.
- Updated `hire_npc` in `src/macro/npc.h` to use amortized growth instead of exact one-slot reserve.
- Updated `spawn_player_squad` in `src/sub/spawn.cpp` to sample water only when the tile buffer is full-sized.
- Extended `combat_squad_test` with self-append and malformed-tile-buffer projection checks.

Exact microseconds saved / cost notes:
- Repeated hire growth case: avoids repeated O(n) copies; estimated 1-30 us saved on low-end allocator growth cases.
- Normal frame cost: `0 us`; no per-frame path changed.
- Subworld entry projection cost: one boolean before soldier placement.

Verification evidence:
- MSVC/CMake build: PASS; touched targets rebuilt and `timaert.exe` plus `combat_squad_test.exe` relinked. No dotnet rebuild.
- `save_roundtrip_test`: PASS, `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- Legacy scan: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- Hot-path scan: PASS in combat-owned files.
- Hecton import revalidation: current quarantine tree has `1711` files and still contains `Docs/Tasks`, `Docs/AgentLogs`, `Root/COMPUTE_AUDIT_BRIEF.md`, and `Docs/Reports/COMPUTE_DOMINANCE_REPORT.md`.

## 2026-05-15 Ninth-Pass Polish Mandate And Squad Allocation Report
STATUS: VERIFIED

Prompt ID and domain:
- `TMA_COMBAT_NPC_SOLDIER_BKR`, `COMBAT_STATE_ARCHITECT`.

What was still wrong:
- The new authoritative squad state used `std::vector<SoldierRecord>` correctly, but append sites still relied on default amortized growth where exact incoming counts were already known.
- The Polish mandate had not been recorded after the user requested further hardening.

What was done:
- Read `<POLISH_MANDATE>` after core tasks were fully verified.
- Updated `src/macro/army.h`: `add_squad` now returns immediately for empty source squads and reserves exact target capacity before insertion.
- Updated `src/macro/npc.h`: `generate_garrison` reserves its bounded daily budget and `hire_npc` reserves one player-squad slot before moving a concrete soldier record.
- Re-ran anti-bloat, legacy, focused tests, direct header syntax compiles, and runtime smoke.

Cinematic cheats used:
- No new global soldier pool, freelist, or preallocated settlement cache was added. Exact local reserves remove allocator noise without creating standing memory bloat.
- The doc import remains quarantined under `Docs/Imported/Hecton8`; no active Timaert task/log folders were polluted.

Exact microseconds saved / cost notes:
- Day-roll squad merge: avoids one or more reallocations when a settlement garrison grows; estimated 1-20 us saved on low-end CPUs when capacity would have grown.
- Recruit action: avoids a one-record growth allocation when the player squad expands; estimated 1-10 us saved on low-end CPUs if capacity was full.
- Frame cost: `0 us`; no per-frame code path changed.

Verification evidence:
- MSVC/CMake build: PASS. Initial PowerShell quoting attempt was invalid; concurrent agent builds occupied shared CMake trees; final `cmake --build build-msvc` returned `ninja: no work to do`. No dotnet rebuild.
- Direct MSVC syntax compiles: PASS for `tests/combat_squad_test.cpp` and `src/sub/spawn.cpp` against current headers.
- `save_roundtrip_test`: PASS. Output included `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. Output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- Polish anti-bloat scan: inspected hits. Remaining hits are outside this domain and legitimate: `src/ui/macro_overlay.cpp` bridges an OpenGL texture id as `unsigned int`, and `src/macro/politik.cpp` contains `dart-throw` in a comment.
- Legacy scan: PASS. No deleted `UnitType`/RPS army symbols or dead battle-substate symbols in `src` or `tests`.
- Legacy scan: PASS. No deleted `UnitType`/RPS army symbols in `src` or `tests`.
- Hot-path scan: PASS. No `std::vector<entt::entity>`, `std::async`, `std::rand`, or `throw` in touched combat files.
- Soldier-ID scan: PASS. No `kSoldierNpcTypes`, `kRecruitableNpcTypes`, or macro-squad `remove_if` mass deletion pattern in `src` or `tests`.

## 2026-05-15 Seventh-Pass No-Battle-Substate Report
STATUS: VERIFIED

What was still wrong:
- `GameSubStateKind::Battle` still existed as a serialized macro substate even though battle mode/resolver was removed.
- `read_sub_state` validated against the dead battle enum.
- One ECS attribution comment still referenced a battle-mode resolver.

What was done:
- Removed `GameSubStateKind::Battle` from `src/macro/state.h`.
- Replaced `read_enum8(...GameSubStateKind::Battle)` with explicit byte handling in `src/macro/save.cpp`.
- Live substates now accept raw `0..4`; former raw `5` normalizes to `Exploring`; higher values fail the save read.
- Added a save test assertion that `GameSubStateKind::Event` remains raw value `4`.
- Updated the `LastHit` comment in `src/ecs/components.h` to describe normal subworld combat attribution only.

Cinematic cheats used:
- `BattleStart` stays as a compatibility event token only. It routes into normal subworld NPC combat, not a battle substate.
- Former battle substate payloads degrade to `Exploring`, avoiding resurrection of a deleted mode.
- No save version bump or migration path was added because serialized layout did not change.

Exact microseconds saved / cost notes:
- Runtime frame cost: 0 us.
- Save read cost: one byte branch, below measurable load-time noise.
- Architectural saving: removes a dead state-machine branch that could reintroduce battle-mode UI or resolver code.

Verification evidence:
- Initial rebuild attempt hit `main.cpp.obj` permission denial from a concurrent build-tree writer. Waited for the tree to go idle and retried.
- MSVC/CMake build: PASS via `vcvars64.bat` + CMake/Ninja, no dotnet rebuild. Final retry returned `ninja: no work to do`.
- `save_roundtrip_test`: PASS. Output included `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS. After isolating one transient `-1` run under concurrent load, boot-only, BattleStart-only, subworld-time-only, and final combined script all passed. Final script: `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- Legacy scan: PASS. No deleted `UnitType`/RPS army symbols in `src` or `tests`.
- Hot-path scan: PASS. No `std::vector<entt::entity>`, `std::async`, `std::rand`, or `throw` in touched combat files.
- No-battle-mode scan: PASS. No `GameSubStateKind::Battle`, enum `Battle,`, `battle mode`, `battle-mode resolver`, or `combat resolver` in `src` or `tests`.

## 2026-05-15 Eighth-Pass Hecton Documentation Transfer And Final Recheck
STATUS: VERIFIED

Prompt ID and domain:
- `TMA_COMBAT_NPC_SOLDIER_BKR`, `COMBAT_STATE_ARCHITECT`.

What was still wrong:
- The user reported that Timaert/Samosbor docs, tasks, and logs may still be sitting under the Hecton folder.
- Existing exact audits found no explicit Timaert/Samosbor/TMA labels in Hecton docs/tasks/logs, so a narrow label-filtered copy would transfer nothing.
- Active Timaert `Docs/Tasks` and `Docs/AgentLogs` could not be safely polluted with unrelated Hecton agent prompt state.

What was done:
- Verified the non-destructive import at `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs`.
- Confirmed imported coverage: Hecton root documentation/log files, Hecton `Docs`, `Docs/Tasks`, `Docs/AgentLogs`, `Docs/Reports`, root `COMPUTE_AUDIT_BRIEF.md`, `Docs/Reports/COMPUTE_DOMINANCE_REPORT.md`, and `Docs/AgentLogs/LOG_AI_FUNNEL_NAV_POLISH.md`.
- Confirmed provenance: `MANIFEST.tsv` has source path, destination path, bytes, and source UTC modification time.
- Updated `Docs/Imported/Hecton8_Timaert_Samosbor_Import_Audit.md` to record why full corpus preservation was used after exact Timaert/Samosbor labels were absent.
- Re-ran combat-domain scans and tests after the documentation import verification.

Cinematic cheats used:
- Documentation was quarantined under `Docs/Imported/Hecton8` instead of merged into live Timaert report folders. That keeps all candidate material available while preventing false active-agent state.
- No runtime abstraction or parser was added for the import. A manifest/index is enough evidence and has zero gameplay cost.

Exact microseconds saved / cost notes:
- Runtime frame cost: `0 us`.
- Save/load cost: `0 us`.
- Disk cost: `88,688,968` bytes for `1659` imported source files plus manifest/index.
- Avoided manual curation pass over unrelated Hecton logs; this prevents high-risk false ownership edits and preserves exact provenance.

Verification evidence:
- Import tree file count: `1661`, matching `1659` copied source files plus `MANIFEST.tsv` and `IMPORT_INDEX_2026-05-15.md`.
- Manifest lines: `1660`, including header.
- Legacy scan: PASS. No deleted `UnitType`/RPS army symbols or dead `GameSubStateKind::Battle` references in `src` or `tests`.
- Hot-path scan: PASS. No `std::vector<entt::entity>`, `std::async`, `std::rand`, word-boundary `try/catch/throw`, `dynamic_cast`, or `typeid` in combat-owned files.
- `save_roundtrip_test`: PASS. Output included `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS. Output included `projected=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42. Output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.

## 2026-05-15 Ninth-Pass Final Append: Polish Mandate And Squad Allocation
STATUS: VERIFIED

What was done:
- `src/macro/army.h`: `add_squad` skips empty source squads and reserves exact destination capacity before insertion.
- `src/macro/npc.h`: `generate_garrison` reserves the bounded daily soldier budget; `hire_npc` reserves one player-squad slot before moving a concrete soldier record.
- Polish mandate scans were executed after core completion. The only exact anti-bloat hits were inspected false positives outside this domain: an OpenGL/ImGui `unsigned int` texture bridge and a `dart-throw` comment.

Verification evidence:
- MSVC/CMake build: PASS; final `cmake --build build-msvc` returned `ninja: no work to do` after concurrent build trees cleared. No dotnet rebuild.
- Direct MSVC syntax compiles: PASS for `tests/combat_squad_test.cpp` and `src/sub/spawn.cpp` against current headers.
- `save_roundtrip_test`: PASS, `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, `projected=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- Legacy scan: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.

## 2026-05-15 Tenth-Pass Bottom Append: Reserve Growth And Tile Guard
STATUS: VERIFIED

What was done:
- Added growth-aware `reserve_soldiers_for_append` in `src/macro/army.h`.
- `add_squad` now uses amortized growth and has deterministic self-append behavior.
- `hire_npc` now uses amortized append reserve instead of exact one-slot reserve.
- `spawn_player_squad` now validates tile-buffer size before water-mask indexing.
- `combat_squad_test` covers self-append and malformed tile buffers.

Verification evidence:
- MSVC/CMake build: PASS; touched targets rebuilt and `timaert.exe` plus `combat_squad_test.exe` relinked. No dotnet rebuild.
- `save_roundtrip_test`: PASS, `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- Runtime smoke: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- Legacy scan: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- Hot-path scan: PASS in combat-owned files.
- Hecton import revalidation: current quarantine tree has `1711` files and still contains `Docs/Tasks`, `Docs/AgentLogs`, `Root/COMPUTE_AUDIT_BRIEF.md`, and `Docs/Reports/COMPUTE_DOMINANCE_REPORT.md`.

## 2026-05-15 Eleventh-Pass Bottom Append: Soldier Level Normalization
STATUS: PARTIAL

Prompt ID and domain:
- `TMA_COMBAT_NPC_SOLDIER_BKR`, `COMBAT_STATE_ARCHITECT`.

What was wrong:
- `SoldierRecord::level` stores an `int16_t`, but direct construction accepted raw `int` levels. Oversized or negative editor/debug/generated values could wrap before save validation and corrupt upkeep, XP reward, or projection math.
- The first prompt re-extraction regex was too strict for the live XML tag because the opening tag includes `role` and `chat_name` attributes.
- Current project-wide `build-msvc` relink is blocked outside this prompt by road-generation compile errors in `src/macro/spawners.cpp`.

What was done:
- Re-extracted the full prompt block with an attribute-aware CLI regex.
- Added `kMaxSoldierLevel=32767` and `normalize_soldier_level` in `src/macro/army.h`.
- Routed `make_soldier`, `soldier_level_factor`, and `npc_xp_reward` through the clamp.
- Added focused `combat_squad_test` coverage for negative and oversized soldier levels.
- Revalidated the imported Hecton documentation quarantine after concurrent deltas.

Cinematic cheats used:
- No new simulation path was introduced. Bad soldier levels are reduced to a predictable bounded scalar before they can affect the universal NPC combat path.
- Out-of-domain road code was not patched from this prompt; the blocker is recorded instead of contaminating road ownership.

Exact microseconds saved / cost notes:
- Normal frame cost: `0 us`; construction/reward/upkeep helpers pay one or two integer branches only when called.
- Avoided bad-state debugging cost from int16 wrap in soldier records.
- Hecton import remains docs-only: runtime cost `0 us`.

Verification evidence:
- Focused MSVC/CMake builds: PASS for `combat_squad_test`, `save_roundtrip_test`, and `quest_lifecycle_test`. No dotnet rebuild.
- `save_roundtrip_test`: PASS, `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- Runtime smoke using existing `build-msvc/timaert.exe`: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- Legacy scan: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- Hot-path scan: PASS in combat-owned files.
- Hecton import revalidation: current quarantine tree has `1764` files, `100,374,922` bytes, original `MANIFEST.tsv` has `1660` lines, and required docs/tasks/logs/report anchors exist.
- Full `cmake --build build-msvc`: BLOCKED outside this domain. Current errors are in `src/macro/spawners.cpp`: missing `torus_delta` plus `find_road_path` signature/call mismatch.

## 2026-05-15 Twelfth-Pass Bottom Append: Spawn Guards And Import Refresh
STATUS: VERIFIED

Prompt ID and domain:
- `TMA_COMBAT_NPC_SOLDIER_BKR`, `COMBAT_STATE_ARCHITECT`.

What was wrong:
- BattleStart hostile spawning trusted raw event levels and could store oversized values through `NpcLevel`.
- Hostile and fauna spawn paths still assumed a non-empty subworld tile vector was full-sized.
- The Hecton import tree was stale while Hecton docs/logs continued to change.

What was done:
- `src/sub/engine.cpp`: `spawn_hostile_npc` now clamps level with `normalize_soldier_level` and samples water only when `mgr_.tiles()` is large enough.
- `src/sub/engine.cpp`: subworld death resolution normalizes `NpcLevel` before XP/loot math.
- `src/sub/spawn.cpp`: fauna respawn and player-squad projection now use the same level clamp; fauna respawn also uses the same `tilesUsable` terrain-mask guard.
- Refreshed the Hecton import into `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs` and matching task/log/report convenience quarantines.

Cinematic cheats used:
- The terrain-buffer fallback is a controlled fake: if a caller gives a partial tile mask, spawns degrade to no water rejection instead of crashing or allocating repair data.
- Level overflow is clamped to the supported scalar range rather than adding a migration or dynamic stat path.

Exact microseconds saved / cost notes:
- Normal frame cost: `0 us`.
- Hostile spawn: one clamp and one cached tile-size comparison.
- Fauna wave: one cached tile-size comparison plus one clamp per spawned NPC.
- Documentation import: runtime cost `0 us`.

Verification evidence:
- MSVC/CMake full native build: PASS. A transient `RC1109` manifest-resource creation failure cleared on retry; final build command returned `ninja: no work to do`. No dotnet rebuild.
- `save_roundtrip_test`: PASS, `bytes=1800`.
- `quest_lifecycle_test`: PASS.
- `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- Runtime smoke on freshly linked `build-msvc/timaert.exe`: PASS. Script `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- Legacy scan: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- Hot-path scan: PASS in combat-owned files.
- Spawn guard scan: PASS, no stale direct `mgr.tiles()[...]`, raw fauna level cast, or direct `std::max<int>(1, level)` patterns.
- Hecton import refresh: `MANIFEST_REFRESH_2026-05-15_145814.tsv` records `2289` source snapshot files, `132,032,531` source bytes, and `Missing after copy: 0`; full quarantine tree has `2581` files and `259,459,099` bytes.
