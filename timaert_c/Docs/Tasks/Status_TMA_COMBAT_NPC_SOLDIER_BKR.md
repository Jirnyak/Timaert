# Status: TMA_COMBAT_NPC_SOLDIER_BKR

Domain: COMBAT_STATE_ARCHITECT
Task count: 8
Final status: VERIFIED

- [x] Prompt extracted from `TIMAERT BATCH.md`; cover and own XML block read with CLI.
- [x] Required docs read: `AGENTS.md`, `matwej.md` section 0/Tier A5, `translation.md`, `MERGE_PLAN.md`, `ARCHITECTURE.md` combat system.
- [x] TS authority files read: `npc.ts`, `items.ts`, `subworld/types.ts`, `subworld/spawn.ts`, `subworld/engine.ts`, and named Svelte read sites by search.
- [x] Task 1: NPC rows carry hireability, upkeep, XP reward. DOD: `NpcTypeDef` owns `upkeepGoldPerDay`, `hireable`, `xpReward`; rejected parallel unit metadata. Estimate: O(1), under 1 us per lookup.
- [x] Task 2: player squad and settlement garrison use NPC-kind records, not unit histograms. DOD: `SoldierSquad` stores `SoldierRecord{entityId, kind, level}` in player, settlements, deserter pool; rejected `{UnitType:n}` histograms. Estimate: upkeep O(n), about 0.3 us for 32 soldiers.
- [x] Task 3: recruitment uses NPC-kind hire path, no RPS costs or battle screen. DOD: `hire_npc` moves a concrete garrison record and charges NPC upkeep-derived price; UI reads NPC labels/counts. Rejected old `hire_unit`/RPS tables. Estimate: linear garrison scan, about 1 us for 64 records.
- [x] Task 4: subworld entry spawns real NPC soldiers from macro squads. DOD: `spawn_player_squad` creates ECS NPC entities with `PlayerSoldierTag`, `SoldierLink`, `NPCKind`, `Combat`, `Health`, `NpcLevel`. Rejected marker-only party display. Estimate: spawn only on entry, about 10-40 us for small squads.
- [x] Task 5: danger-zone exit gate blocks yellow/red while hostiles are nearby. DOD: `SubworldEngine::leave(false)` checks `ZoneLayer` and living hostiles inside `kDetectionRadius`; status line explains refusal. Rejected global "cannot exit in combat" flag. Estimate: one ECS scan on exit attempt, under 0.1 ms for current subworld counts.
- [x] Task 6: death attribution, XP, corpse loot, corpse interaction. DOD: `LastHit` tracks player-owned kills; hired-soldier kills grant player XP; dead NPCs roll corpse loot only when inventory/gold exists; `E` transfers loot then despawns corpse. Rejected battle reward screen. Estimate: death resolution scans dead-only list; normal frame cost unchanged unless deaths occur.
- [x] Task 7: legacy unit symbols deleted or exact remaining readers listed. DOD: `rg` found no `UnitType`, `ArmyComposition`, `kUnitStats`, `damage_multiplier`, `kHireCost`, `kUpkeepCost`, `hire_unit`, `calculate_army_upkeep`, or `total_units` in `src`/`tests`. Remaining `BattleStart` is an event compatibility token routed into normal subworld NPC combat.
- [x] Task 8: save schema bumped and save tests updated. DOD: save version is v8; squads serialize as bounded record lists with kind/level validation; save roundtrip fixture uses NPC soldiers. Rejected raw vector POD deserialization. Estimate: record serialization O(n), under 5 us for 100 soldiers.

Verification:
- [x] MSVC build: `vcvars64.bat` + CMake/Ninja built `timaert.exe`; stale locked exe was moved aside inside `build-msvc` before relink.
- [x] `save_roundtrip_test`: OK, v8 path, 1712 bytes.
- [x] `quest_lifecycle_test`: OK.
- [x] `combat_squad_test`: OK, hire/upkeep/garrison/XP checks.
- [x] Runtime subworld smoke: OK, `new_game,wait_boot_done,subworld_time,quit`, seed 42.

Second-pass hardening:
- [x] Player soldiers now carry `SubworldAi::Combat` as well as `CombatTemplate` projection; generic hostile AI skips player-owned soldiers so squad combat owns their targeting.
- [x] `BattleStart` events no longer end at a placeholder log; app runtime routes them into subworld entry plus a real hostile NPC spawn.
- [x] `combat_squad_test` now verifies subworld projection components: `PlayerSoldierTag`, `SoldierLink`, `NPCKind`, `Combat`, `Health`, `NpcLevel`, and `SubworldAi`.
- [x] Combined runtime smoke: OK, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.

Third-pass polish/hardening:
- [x] Subworld combat actor and death resolution gathers use fixed `std::array` batches, not per-frame heap vectors. DOD: touched hot path scanned for `std::vector<entt::entity>` and none remain in `src/sub/engine.*`; overflow degrades by capping one frame instead of allocating. Rejected heap-growing work buffers. Estimate: saves allocator spikes, about 2-15 us on low-end CPUs during dense combat/death frames.
- [x] `SubworldEngine::leave(true/false)` clears all session `SubworldTag` entities after snapshot and macro-player sync. DOD: bounded fixed-batch cleanup in engine-owned code; smoke asserts zero subworld entities after BattleStart force-leave. Rejected UI-side filtering because leaked ECS entities would still render/tick through other readers. Estimate: exit-only cleanup, under 0.1 ms for current entity counts.
- [x] MSVC/CMake build after polish: PASS, no dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1756 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1`.
- [x] Combined runtime smoke with cleanup assertion: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.

Fourth-pass combat-owner hardening:
- [x] `SubworldAi::Combat` no longer moves actors that also own `ecs::Combat`; `SubworldEngine::tick_subworld_combat` is the single movement/attack owner for real combat actors. DOD: `combat_squad_test` asserts a combat actor remains stationary under `tick_npc_ai`, and an AI-only fallback actor still moves. Rejected double integration in AI + engine. Estimate: removes one extra movement integration per hostile per frame, about 1-4 us for 100 combat actors and fixes speed determinism.
- [x] `combat_squad_test` now links `src/sub/ai.cpp` and prints `ai_owner=1`.
- [x] `src/sub/ai.h` header contract updated to match single-owner movement.
- [x] MSVC/CMake build after fourth pass: PASS after retrying a build-tree `restat` contention failure; no dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1756 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 ai_owner=1`.
- [x] Combined runtime smoke: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.

Fifth-pass leave-death drain hardening:
- [x] `SubworldEngine::leave(true/false)` drains pending subworld deaths before snapshot and cleanup. DOD: `resolve_subworld_deaths(true)` loops fixed 512-death batches until drained on leave; normal ticks still use one bounded batch. Rejected clearing dead entities without resolution because it loses hired-soldier roster removal and player-owned XP. Estimate: exit-only full drain; normal frame unchanged.
- [x] Subworld enter cleanup now reaps stale `SubworldTag` entities through a fixed 2048-entity batch loop instead of destroying from an active view. DOD: `respawn_subworld_npcs` calls `clear_existing_subworld_entities`; rejected mutating a live view because registry iteration invalidation risk is not worth the shortcut. Estimate: enter-only, under 0.1 ms for current entity counts.
- [x] BattleStart smoke now forces the spawned hostile to `Dead` with `LastHit{playerOwned=true}`, calls `leave(true)`, and fails if player XP/level does not increase or any `SubworldTag` remains. DOD: runtime smoke passed with `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- [x] MSVC/CMake build after fifth pass: PASS, no dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1756 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 ai_owner=1`.
- [x] Legacy scan after fifth pass: PASS, no deleted `UnitType`/RPS army symbols in `src` or `tests`.
- [x] Hot-path scan after fifth pass: PASS, no banned heap/async/random/exception patterns in touched combat files.

Sixth-pass soldier identity and registry UI hardening:
- [x] Daily settlement garrison generation no longer reuses the same soldier `entityId` base every day. DOD: `garrison_soldier_id_base(settlementId, day)` reserves a deterministic high-bit namespace with 32 IDs per settlement/day; `world_tick.cpp` uses it when adding daily NPC-kind garrisons. Rejected using current garrison size because hiring can shrink the vector and reintroduce duplicates. Estimate: O(1), zero frame cost beyond existing daily tick.
- [x] Hired-soldier death resolution removes exactly one macro soldier record by `entityId`. DOD: `remove_one_soldier_by_entity_id` replaces `remove_if`; `combat_squad_test` verifies duplicate IDs lose one record, not all records. Rejected mass removal because one ECS death must map to one persistent soldier. Estimate: one vector scan on soldier death only.
- [x] Recruitment/army UI is now NPC-registry driven. DOD: `overlays.cpp` loops `NPCType::Count`, filters with `npc_hireable`, and uses `npc_hire_price_base` when a hireable row is out of stock. Rejected hardcoded `kRecruitableNpcTypes`/`kSoldierNpcTypes` lists because new NPC soldier rows should not require UI edits. Estimate: eight-row UI loop today; under 1 us.
- [x] MSVC/CMake build after sixth pass: PASS, no dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1756 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke after sixth pass: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42.
- [x] Legacy scan after sixth pass: PASS, no deleted `UnitType`/RPS army symbols in `src` or `tests`.
- [x] Hot-path scan after sixth pass: PASS, no banned heap/async/random/exception patterns in touched combat files.
- [x] Soldier-ID scan after sixth pass: PASS, no hardcoded recruit/soldier type arrays and no macro-squad `remove_if` mass deletion pattern.

Seventh-pass no-battle-substate hardening:
- [x] Removed the dead `GameSubStateKind::Battle` enum value. DOD: no `GameSubStateKind::Battle` or enum `Battle,` remains in `src`/`tests`; `BattleStart` remains only as an event compatibility token routed to subworld combat. Rejected leaving the enum as inert save headroom because it contradicts the no-battle-mode contract. Estimate: zero runtime cost.
- [x] Save substate reading no longer references the removed battle enum. DOD: live substates accept raw `0..4`; the former raw `5` normalizes to `Exploring`; higher invalid values fail. Rejected using `read_enum8(...Battle)` because that preserves a deleted mode in validation. Estimate: one byte branch on load only.
- [x] `LastHit` comment no longer references a battle-mode resolver. DOD: no-battle-mode scan passes for `GameSubStateKind::Battle`, enum `Battle,`, `battle mode`, `battle-mode resolver`, and `combat resolver`.
- [x] MSVC/CMake build after seventh pass: PASS. One attempt failed on `main.cpp.obj` permission denial due concurrent build-tree writer; waited, retried, and got `ninja: no work to do`. No dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1800 bytes, substate enum layout assertion active.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke after seventh pass: PASS after isolating a transient `-1` run under concurrent load. Boot-only, BattleStart-only, subworld-time-only, and final combined `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit` all passed with seed 42.
- [x] Legacy scan after seventh pass: PASS, no deleted `UnitType`/RPS army symbols in `src` or `tests`.
- [x] Hot-path scan after seventh pass: PASS, no banned heap/async/random/exception patterns in touched combat files.

Eighth-pass Hecton documentation transfer and final recheck:
- [x] Hecton documentation import verified at `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs`. DOD: import index and manifest present; source files copied count is 1659, manifest has 1660 lines including header, import tree has 1661 files including manifest and index. Alternative rejected: destructive move/delete from Hecton. Estimate: zero runtime cost; docs-only storage cost 88,688,968 bytes.
- [x] Destination placement verified for requested docs/tasks/logs. DOD: imported tree has `Docs`, `Docs/Tasks`, `Docs/AgentLogs`, `Docs/Reports`, root `COMPUTE_AUDIT_BRIEF.md`, `Docs/Reports/COMPUTE_DOMINANCE_REPORT.md`, and `Docs/AgentLogs/LOG_AI_FUNNEL_NAV_POLISH.md`. Alternative rejected: flattening files into active Timaert `Docs/Tasks` or `Docs/AgentLogs`, which would contaminate live Timaert reports with Hecton agent state. Estimate: zero gameplay cost.
- [x] Import audit reconciled. DOD: exact-name/content search found no Timaert/Samosbor/TMA labels in Hecton docs, so the full Hecton documentation/task/log corpus was preserved under `Docs/Imported/Hecton8` with provenance instead of pretending a label-filtered transfer existed. Alternative rejected: broad `tima`/`samos` filtering because it produced false positives like `material` and transliterated Russian words. Estimate: documentation-only.
- [x] Final legacy scan after import: PASS, no deleted `UnitType`/RPS army symbols or dead `GameSubStateKind::Battle` references in `src` or `tests`.
- [x] Final hot-path scan after import: PASS, no banned heap/async/random/exception patterns in combat-owned files.
- [x] `save_roundtrip_test`: PASS, v8 path, 1800 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke after import: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.

Final status:
- [x] STATUS: VERIFIED.

Ninth-pass Polish mandate / squad allocation polish:
- [x] Polish mandate read after core task completion. DOD: `TIMAERT BATCH.md` `<POLISH_MANDATE>` lines 715-739 were extracted; anti-bloat, hot-path, layer, build, tests, and runtime smoke were executed. Alternative rejected: running Polish before verified core tasks. Estimate: documentation/runtime verification only.
- [x] Squad append paths reserve exact capacity before append. DOD: `add_squad` now no-ops on empty source and reserves `target + src`; `generate_garrison` reserves the bounded daily budget; `hire_npc` reserves one player-squad slot before moving the concrete soldier. Alternative rejected: leaving vector growth to amortized allocation on day-roll/recruit actions. Estimate: saves one avoidable allocation burst per growing squad merge/hire, about 1-20 us depending on allocator state and squad size.
- [x] MSVC/CMake build after ninth pass: PASS. First command was misquoted and ignored by PowerShell; second build was delayed by concurrent CMake/Ninja activity from other agents; final `cmake --build build-msvc` returned `ninja: no work to do`. No dotnet rebuild.
- [x] Header syntax validation after ninth pass: PASS. Direct MSVC `/c` compile succeeded for `tests/combat_squad_test.cpp` and `src/sub/spawn.cpp` against current headers.
- [x] `save_roundtrip_test`: PASS, v8 path, 1800 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke after ninth pass: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- [x] Polish anti-bloat scan inspected. DOD: exact scan only reported `src/ui/macro_overlay.cpp` OpenGL/ImGui `unsigned int` texture bridge and `src/macro/politik.cpp` comment text `dart-throw`; both are outside this domain and not runtime combat violations.
- [x] Final legacy scan after ninth pass: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.

Final status after ninth pass:
- [x] STATUS: VERIFIED.

Tenth-pass reserve growth and malformed-tile hardening:
- [x] Squad append reservation changed from exact one-step reserve to growth-aware reservation. DOD: `reserve_soldiers_for_append` doubles capacity up to the required size; `add_squad` uses it and handles self-append deterministically. Alternative rejected: exact reserve on every single hire because it can force repeated tiny reallocations. Estimate: avoids O(n) reallocation spikes during repeated recruit/day-roll appends; 1-30 us saved on low-end allocator growth cases.
- [x] `spawn_player_squad` now validates tile buffer size before water checks. DOD: malformed non-empty tile buffers are treated as unavailable terrain masks instead of indexed as `kFullSize*kFullSize`. Alternative rejected: assuming every non-empty external buffer is full-sized. Estimate: zero normal-frame cost; one boolean computed per squad projection.
- [x] `combat_squad_test` now covers self-append and malformed tile-buffer projection. DOD: output includes `malformed_tiles=1`.
- [x] MSVC/CMake build after tenth pass: PASS; touched targets rebuilt and `timaert.exe` plus `combat_squad_test.exe` relinked. No dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1800 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke after tenth pass: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- [x] Final legacy scan after tenth pass: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- [x] Final hot-path scan after tenth pass: PASS in combat-owned files.
- [x] Hecton import revalidated after concurrent deltas: import tree currently has `1711` files, `94,280,063` bytes, `Docs/Tasks`, `Docs/AgentLogs`, `Root/COMPUTE_AUDIT_BRIEF.md`, and `Docs/Reports/COMPUTE_DOMINANCE_REPORT.md`; original `MANIFEST.tsv` still has `1660` lines and delta manifests are present.

Final status after tenth pass:
- [x] STATUS: VERIFIED.

Eleventh-pass soldier level normalization and integration recheck:
- [x] Prompt re-extracted with attribute-aware CLI regex from `TIMAERT BATCH.md`. DOD: full `<AGENT_PROMPT id="TMA_COMBAT_NPC_SOLDIER_BKR" role="COMBAT_STATE_ARCHITECT"...>` block read; rejected the stale exact-opening-tag regex that returned `PROMPT_NOT_FOUND`. Estimate: cached file scan only.
- [x] Soldier level inputs are normalized before storage and before derived combat/economy math. DOD: `kMaxSoldierLevel=32767`, `normalize_soldier_level`, `make_soldier`, `soldier_level_factor`, and `npc_xp_reward` clamp negative/oversized levels instead of allowing int16 wrap. Alternative rejected: relying on save validation only, because editor/debug/generated callers can still construct records directly. Estimate: 1-2 branches on construction or reward/upkeep calculation; zero per-frame cost.
- [x] `combat_squad_test` covers negative and oversized level construction. DOD: `make_soldier(...,-400,...)` stores level `1`; `make_soldier(...,1000000,...)` stores `kMaxSoldierLevel`; high-level factor stays positive. Alternative rejected: untested helper change. Estimate: test-only.
- [x] Focused MSVC/CMake targets rebuilt after the pass: `combat_squad_test`, `save_roundtrip_test`, and `quest_lifecycle_test` all built successfully. No dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1800 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke with existing `build-msvc/timaert.exe`: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- [x] Final legacy scan after eleventh pass: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- [x] Final hot-path scan after eleventh pass: PASS in combat-owned files.
- [x] Hecton import revalidated after concurrent deltas: quarantine tree currently has `1764` files, `100,374,922` bytes, `Docs/Tasks`, `Docs/AgentLogs`, `Root/COMPUTE_AUDIT_BRIEF.md`, and `Docs/Reports/COMPUTE_DOMINANCE_REPORT.md`; original `MANIFEST.tsv` still has `1660` lines.
- [!] Full `cmake --build build-msvc` is blocked outside this prompt by current road-generation compile errors in `src/macro/spawners.cpp`: missing `torus_delta` and a `find_road_path` signature/call mismatch. DOD: blocker recorded instead of touching roads, which are explicitly out of scope for this prompt. No dotnet rebuild.

Final status after eleventh pass:
- [x] STATUS: PARTIAL. Combat/NPC-soldier domain verified by focused builds, tests, scans, and runtime smoke; current full app relink is blocked by out-of-domain road-generation source state.

Twelfth-pass hostile/fauna spawn guard and Hecton import refresh:
- [x] `spawn_hostile_npc` now normalizes event-provided levels and checks the subworld tile buffer size before water lookup. DOD: `normalize_soldier_level` clamps BattleStart hostile levels before `NpcLevel` storage and stat scaling; `tilesUsable` prevents partial-buffer indexing. Alternative rejected: relying on event payloads and manager tile vectors always being clean. Estimate: two integer branches and one cached tile-size boolean per hostile spawn; zero normal-frame cost.
- [x] Fauna respawn uses the same tile-buffer and level guard. DOD: `respawn_subworld_npcs` caches `mgr.tiles()`, computes `tilesUsable`, and stores normalized `NpcLevel`; stale direct `mgr.tiles()[...]`/raw `int16_t(f.baseLevel + levelBonus)` scan passes. Alternative rejected: hard failing partial subworld maps because the safer behavior is to degrade to "no terrain mask". Estimate: one boolean per respawn wave.
- [x] Full Hecton docs/tasks/logs import refreshed non-destructively. DOD: final refresh `MANIFEST_REFRESH_2026-05-15_145814.tsv` records `2289` source snapshot files, `132,032,531` source bytes, and `Missing after copy: 0`; full quarantine has `2581` files / `259,459,099` bytes, and required convenience quarantines remain under `Docs/Tasks/Imported_Hecton8`, `Docs/AgentLogs/Imported_Hecton8`, and `Docs/Reports/Imported_Hecton8`. Alternative rejected: destructive move from Hecton or flattening Hecton logs into active Timaert report folders. Estimate: runtime cost 0 us.
- [x] MSVC/CMake full native build after twelfth pass: PASS. A transient `RC1109` manifest-resource creation failure cleared on retry; final `cmake --build build-msvc --target timaert combat_squad_test save_roundtrip_test quest_lifecycle_test` returned `ninja: no work to do`. No dotnet rebuild.
- [x] `save_roundtrip_test`: PASS, v8 path, 1800 bytes.
- [x] `quest_lifecycle_test`: PASS.
- [x] `combat_squad_test`: PASS, `projected=1 malformed_tiles=1 ai_owner=1 unique_ids=1`.
- [x] Runtime smoke on freshly linked `build-msvc/timaert.exe`: PASS, `new_game,wait_boot_done,trigger_battle_start,subworld_time,quit`, seed 42; output included `battle_start routed hostiles=0->1`, `subworld_time OK`, and `PASS`.
- [x] Final legacy scan after twelfth pass: PASS, no deleted army/RPS/battle-substate symbols in `src` or `tests`.
- [x] Final hot-path scan after twelfth pass: PASS in combat-owned files.
- [x] Final spawn guard scan after twelfth pass: PASS, no stale direct tile indexing or raw level casts in `src/sub/spawn.cpp` or `src/sub/engine.cpp`.

Final status after twelfth pass:
- [x] STATUS: VERIFIED.
