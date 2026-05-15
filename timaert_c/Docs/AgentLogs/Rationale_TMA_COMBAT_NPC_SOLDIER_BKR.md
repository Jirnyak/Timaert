# Rationale: TMA_COMBAT_NPC_SOLDIER_BKR

## NPC-Kind Soldiers
Problem: The legacy army model used four `UnitType` histogram buckets, which could not enter subworld combat as normal NPCs.
Solution: Replaced unit histograms with `SoldierSquad` and `SoldierRecord{entityId, kind, level}`. NPC rows now define hireability, upkeep, and XP reward, so combat/economy data has one source.
Rejected Alternatives: Keeping `ArmyComposition` and adding an adapter would preserve RPS assumptions and duplicate NPC combat stats.
Scalability potential: Low uses short squads and O(n) scans; Middle supports settlement garrisons; High/Ultra can raise squad counts or visual followers without changing save shape.
Hardware Impact: On i3/MX350, the data shape avoids per-frame conversion from unit buckets to entities. Expected gain is correctness plus sub-0.01 ms upkeep scans for practical squads.

## Recruitment And Upkeep
Problem: Recruitment previously decremented synthetic unit counts and used fixed RPS hire/upkeep tables.
Solution: `hire_npc` moves a concrete garrison soldier record into the player squad and charges `soldier_upkeep * 30`; daily upkeep sums NPC kind upkeep with a level factor and CHA discount.
Rejected Alternatives: Per-settlement generated price tables were rejected because they hide the NPC-kind source and add save/UI churn.
Scalability potential: Low uses three hireable civilian/guard kinds; Middle can mark more NPC rows hireable; High/Ultra can add rare elite NPCs through the same row fields.
Hardware Impact: Linear scan is paid only on a button press. Expected runtime cost is under 1 us for normal settlement garrisons on low-end CPUs.

## Subworld Projection
Problem: Macro squads had no physical representation after entering the subworld, so soldier kills/loot/XP could not use normal combat.
Solution: `spawn_player_squad` projects each soldier record to an ECS entity with `PlayerSoldierTag`, `SoldierLink`, `NPCKind`, `Health`, `Combat`, and `NpcLevel`. The macro squad remains authoritative for persistence.
Rejected Alternatives: A fake party overlay was rejected because it would not participate in AI, damage, corpse, or XP systems.
Scalability potential: Low can spawn a small party; Middle can widen formation spacing; High/Ultra can attach richer visuals without changing combat contracts.
Hardware Impact: Spawn is entry-only and bounded by squad size. Expected cost is tens of microseconds for small squads on i3/MX350.

## Exit Gate
Problem: The design requires no battle mode, but dangerous zones must still prevent trivial escape while hostiles are engaged.
Solution: `leave(false)` reads `ZoneLayer`; zones 0-2 exit freely, zones above 2 block while a living hostile is within `kDetectionRadius`; a status line explains the refusal.
Rejected Alternatives: A global combat-state boolean was rejected because it can desync from actual ECS hostiles.
Scalability potential: Low scans current ECS entities; Middle can swap to spatial hash if counts grow; High/Ultra can add stronger danger feedback without changing the gate rule.
Hardware Impact: Scan occurs on exit attempt, not every frame. Expected cost is below 0.1 ms for current subworld populations on i3/MX350.

## Death, XP, Corpse Loot
Problem: Projectile and squad deaths were either deleted or unaffiliated, so hired-soldier kills could not reward player XP and corpses could not be looted.
Solution: Added `LastHit` attribution, dead-entity resolution, player-owned XP grants, loot/gold rolls, `Structure::Corpse`, `CorpseLoot`, and `SubworldEngine::interact`.
Rejected Alternatives: A reward modal after combat was rejected because it recreates a battle resolver path.
Scalability potential: Low rolls loot only on death; Middle can render corpse variants; High/Ultra can add richer corpse visuals and audio while keeping the same data path.
Hardware Impact: No normal-frame allocation beyond existing ECS views; corpse work is death-event bounded. Expected low-end impact is negligible unless many entities die in one frame.

## Save Schema
Problem: The serialized shape changed from POD histograms to vectors of soldier records.
Solution: Bumped save version to v8 and added bounded squad read/write functions with NPC kind and level validation.
Rejected Alternatives: Migration code was rejected because the prompt explicitly required no migration code.
Scalability potential: Low saves compact records; Middle/High can add more soldier records without schema churn; Ultra can add optional visuals later via separate versioned fields.
Hardware Impact: Save/load cost grows linearly with soldier count. For normal counts, estimated cost remains below a few microseconds per squad on low-end CPUs.

## BattleStart Compatibility
Problem: Legacy encounter choices still emitted `BattleStart`; logging that event only was a hidden battle-mode dead end.
Solution: Runtime now converts `BattleStart` into subworld entry plus a real hostile NPC entity using the NPC type token, `CombatTemplate`, `SubworldAi::Combat`, level scaling, inventory, and normal death/loot/XP path.
Rejected Alternatives: Recreating a battle screen or resolving the event as instant damage was rejected because both violate the Tier A5 pivot.
Scalability potential: Low spawns one hostile for old encounter choices; Middle can spawn small packs by event payload; High/Ultra can layer cinematic arrival visuals without changing the combat contract.
Hardware Impact: The conversion is event-driven. Expected low-end cost is one NPC spawn plus existing subworld entry cost, paid only when a battle encounter is selected.

## Soldier AI Contract
Problem: Player soldiers projected into subworld combat but did not carry the `SubworldAi` component, leaving a stricter "existing AI" reading under-proven.
Solution: Player soldiers now receive `SubworldAi::Combat`; generic hostile AI skips `PlayerSoldierTag`, and squad combat controls their hostile targeting.
Rejected Alternatives: Letting the generic combat AI process player soldiers was rejected because it chases the player by design.
Scalability potential: Low uses one squad target scan; Middle can switch squad targeting to a spatial hash; High/Ultra can add formations and morale without changing component identity.
Hardware Impact: The extra component is POD and makes existing ECS queries explicit. Added normal-frame cost is a small view skip for player-owned soldiers.

## Hot-Path Allocation Removal
Problem: Subworld squad combat and death resolution gathered entities into heap `std::vector` buffers inside runtime ticks.
Solution: Replaced those gathers with fixed `std::array` batches: 2048 combat actors and 512 deaths per frame. Overflow is bounded and resumes next frame instead of paying allocator spikes.
Rejected Alternatives: Keeping recycled static vectors was rejected because it is hidden global mutable state and can still reallocate when entity counts spike.
Scalability potential: Low uses no heap churn; Middle can raise the constants after measurement; High/Ultra should buy denser visuals with saved allocator budget, not simulate a separate battle mode.
Hardware Impact: On i3/MX350 this removes allocator jitter from dense combat/death frames. Estimated gain is 2-15 us during spikes, with no normal-frame gameplay behavior change.

## Subworld Session Cleanup
Problem: `SubworldEngine::leave` nulled the ECS pointer but left `SubworldTag` entities alive until the next enter path cleared them. Macro overlay and other broad ECS readers could see temporary subworld NPCs after leaving.
Solution: Added bounded `clear_subworld_entities` after subworld snapshot and macro-player sync. The smoke-only BattleStart action now asserts zero `SubworldTag` entities after `leave(true)`.
Rejected Alternatives: UI-side filtering was rejected because it hides one symptom while leaving stale combat/corpse/session entities in the registry.
Scalability potential: Low clears current session actors/corpses; Middle can split persistent subworld state into save-backed records if needed; High/Ultra can add richer corpse visuals without leaking them into macro state.
Hardware Impact: Exit-only fixed-batch destroy is under 0.1 ms for current entity counts on low-end CPUs. It prevents later macro-frame work on stale subworld entities.

## Single Combat Movement Owner
Problem: `tick_npc_ai` moved `SubworldAi::Combat` hostiles, then `SubworldEngine::tick_subworld_combat` moved the same `ecs::Combat` actors again. That doubled hostile speed and made squad combat less predictable.
Solution: `tick_npc_ai` now zeroes velocity and returns for `PlayerSoldierTag` or any entity with `ecs::Combat`; the engine combat pass is the only owner for real combat actor movement and attacks. AI-only combat fallback still chases without dealing damage.
Rejected Alternatives: Reducing speeds to compensate was rejected because it hides double integration and breaks when either pass changes.
Scalability potential: Low keeps deterministic movement on cheap hardware; Middle can add formation/spatial targeting inside the single combat pass; High/Ultra can spend the saved integration budget on richer combat visuals without changing ownership.
Hardware Impact: Removes one duplicate movement integration per combat actor per frame. Estimated low-end gain is 1-4 us for 100 active combat actors, plus deterministic speed behavior.

## Leave Death Flush
Problem: A forced subworld leave could clear `SubworldTag` entities before unresolved deaths were converted into macro state. That can drop hired-soldier roster removal and player-owned hostile XP if death and exit happen in the same frame.
Solution: `SubworldEngine::leave` now calls `resolve_subworld_deaths(true)` before snapshot and cleanup. The drain path loops fixed 512-death batches until no dead subworld entities remain, while normal frame ticks keep the existing bounded one-batch behavior.
Rejected Alternatives: Resolving all deaths every normal frame was rejected because it can turn a rare mass-death spike into a frame-time spike. Persisting corpses across leave was rejected because corpse loot is a session interaction, while XP and squad consistency are the persisted combat facts.
Scalability potential: Low drains a few deaths on exit; Middle handles dense skirmish cleanup through fixed batches; High/Ultra can spend saved normal-frame budget on visuals while the leave path preserves deterministic state.
Hardware Impact: Normal tick cost is unchanged. Leave-only full drain costs roughly 1-5 us for typical fights and remains bounded in 512-entity chunks for pathological cases on i3/MX350.

## Enter Reap Stability
Problem: Subworld respawn cleanup destroyed `SubworldTag` entities while iterating the same view. Even if EnTT tolerates many cases, that pattern is fragile under component churn and parallel agent edits.
Solution: Added `clear_existing_subworld_entities` in `spawn.cpp`, using a fixed 2048-entity array and looped destroy pass before respawning fauna.
Rejected Alternatives: Reusing the engine cleanup helper was rejected because `spawn.cpp` should not depend on `SubworldEngine` instance state. Allocating a temporary vector was rejected due hot-path/no-GC rules.
Scalability potential: Low clears stale actors cheaply; Middle can raise the batch constant after measurement; High/Ultra keeps cleanup predictable while allowing denser fauna visuals.
Hardware Impact: Enter-only fixed batch cost is below 0.1 ms for current subworld counts on low-end hardware and removes iterator invalidation risk without heap allocation.

## Soldier Entity Identity
Problem: Settlement daily garrison generation used a stable settlement base, so the same settlement could mint duplicate soldier `entityId` values on different days. A projected soldier death also used `remove_if`, so one ECS death could delete every macro soldier with that duplicated ID.
Solution: Added `garrison_soldier_id_base(settlementId, day)` for deterministic day-scoped ID ranges and changed death removal to `remove_one_soldier_by_entity_id`. The focused test verifies duplicate IDs remove one record only.
Rejected Alternatives: Using `garrison.members.size()` as the next ID was rejected because hiring shrinks the vector and can reuse older IDs. Adding a saved global soldier counter was rejected because it changes save shape for an issue solvable inside deterministic daily generation plus one-record death semantics.
Scalability potential: Low gets correct one-death/one-record mapping; Middle can support longer garrison histories; High/Ultra can attach richer persistent soldier metadata without changing the identity contract.
Hardware Impact: ID base generation is O(1) inside daily tick. Death removal remains a single squad vector scan paid only when a player soldier dies; practical cost is below 1 us for normal party sizes on i3/MX350.

## Registry-Driven Recruit UI
Problem: `NpcTypeDef` owned hireability/upkeep, but the settlement/character UI still carried hardcoded recruitable/soldier type arrays. Adding a hireable NPC row would not surface in recruitment without another UI edit.
Solution: Replaced the hardcoded arrays with `NPCType::Count` iteration, `npc_hireable` filtering, and `npc_hire_price_base` for out-of-stock preview pricing.
Rejected Alternatives: Keeping mirrored UI arrays was rejected because it recreates a parallel unit registry. Showing all NPC rows without hireability filtering was rejected because merchants/bandits/witches would appear as false recruit choices.
Scalability potential: Low shows the current Peasant/Woodcutter/Guard rows; Middle can mark new NPC kinds hireable through data only; High/Ultra can add richer soldier classes without changing UI loops.
Hardware Impact: The current UI loop is eight rows and runs only when panels are open. Expected cost is under 1 us; the gain is fewer stale-code failure points.

## Removed Battle Substate
Problem: The project had removed the battle resolver path, but `GameSubStateKind::Battle` still existed as a serialized substate bound. That kept a dead battle-mode state in the macro state machine.
Solution: Removed the enum value and changed `read_sub_state` to accept only live raw values `0..4`. The former raw value `5` is normalized to `Exploring` so the runtime never re-enters a deleted mode.
Rejected Alternatives: Leaving the enum as inert compatibility was rejected because it keeps architectural ambiguity. Bumping save version was rejected because the binary layout did not change; only validation and normalization changed.
Scalability potential: Low keeps the state machine smaller and predictable; Middle can add future substates explicitly; High/Ultra avoids accidental resurrection of a battle resolver while still using `BattleStart` as a routed event token.
Hardware Impact: Save/load pays one byte branch. Runtime frame cost is zero. The value is correctness and lower integration risk, not measurable frame time.

## Hecton Documentation Import Placement
Problem: The user requested all Timaert/Samosbor docs, tasks, and logs that were left under the Hecton folder to be transferred into the Timaert project. Exact searches found no files explicitly labeled Timaert, Samosbor, or TMA inside the Hecton docs/tasks/logs, while broad substring searches produced false positives such as `material` and transliterated Russian words.
Solution: Preserve the full Hecton documentation-class corpus non-destructively under `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs` with the original `Root` and `Docs` relative structure, an import index, and a source-to-destination `MANIFEST.tsv`. This keeps all candidate docs/tasks/logs available to Timaert without merging Hecton agent state into active Timaert task/log folders.
Rejected Alternatives: Destructive moving from `C:\hades\Hecton8` was rejected because ownership is not proven and concurrent agents may still rely on those files. Flattening Hecton `Docs/Tasks` and `Docs/AgentLogs` into active Timaert `Docs/Tasks` and `Docs/AgentLogs` was rejected because it would pollute live Timaert reports with Hecton prompt IDs. Filtering by broad `tima` or `samos` was rejected because it misclassified unrelated text.
Scalability potential: Low keeps archived provenance readable through one import root. Middle can build curated Timaert indexes over imported Hecton reports. High/Ultra can mine the imported corpus for design references without touching runtime code or active task logs.
Hardware Impact: Runtime impact is 0 us because this is documentation-only. Disk impact is 88,688,968 bytes for 1659 source files plus manifest/index. No frame-time or save-load path changed.

## Squad Allocation Polish
Problem: After replacing army histograms with concrete `SoldierSquad` vectors, squad append paths could still rely on amortized vector growth during day-roll garrison merges and recruit actions. This is not per-frame, but it is avoidable allocator work in the new authoritative combat state.
Solution: Reserve exact capacity before appending in `add_squad`, reserve the bounded garrison budget inside `generate_garrison`, and reserve one slot before `hire_npc` moves a concrete soldier record into the player squad.
Rejected Alternatives: Leaving vector growth to amortized allocation was rejected because the size deltas are known in all three paths. Preallocating large standing capacities was rejected because it wastes memory across many settlements.
Scalability potential: Low avoids allocation spikes for tiny squads. Middle handles growing settlements without repeated reallocs during day-roll. High/Ultra can support richer persistent soldier records later while preserving the same append discipline.
Hardware Impact: Runtime frame impact is unchanged because these paths are day-roll or button-press actions. Estimated low-end gain is roughly 1-20 us on a growth append depending on allocator state and squad size; memory footprint remains exact rather than padded.

## Reserve Growth And Tile Guard
Problem: The ninth-pass exact reserve removed one allocation case but made repeated one-soldier hires capable of reallocating at every full-capacity append. `spawn_player_squad` also treated any non-empty tile vector as a full `kFullSize*kFullSize` terrain mask, which is unsafe for malformed or partial callers.
Solution: Added `reserve_soldiers_for_append`, a growth-aware reservation helper shared by `add_squad` and `hire_npc`. `add_squad` now also handles self-append through index-copy after reserve. `spawn_player_squad` computes one `tilesUsable` boolean and only samples water when the buffer is large enough for full indexing.
Rejected Alternatives: Exact one-slot reserve in `hire_npc` was rejected because it can defeat vector amortization across repeated hires. A hard failure on malformed tile buffers was rejected because squad projection should degrade to "no terrain mask" rather than losing the player's soldiers on entry.
Scalability potential: Low gets stable small squads and malformed-buffer tolerance. Middle avoids repeated allocator copies as settlements/garrisons grow over many days. High/Ultra can attach richer soldier metadata later while retaining amortized append behavior.
Hardware Impact: No per-frame code changed. Hire/day-roll growth cases avoid repeated reallocations; estimated savings are 1-30 us on low-end CPUs when capacity would otherwise grow repeatedly. Tile validation costs one boolean per subworld squad projection.

## Soldier Level Normalization
Problem: `SoldierRecord::level` is stored as `std::int16_t`, but direct construction paths could pass negative or huge `int` values into `make_soldier`. That can wrap before save validation and poison upkeep, XP, and combat projection math.
Solution: Added `kMaxSoldierLevel` and `normalize_soldier_level`, then routed `make_soldier`, `soldier_level_factor`, and `npc_xp_reward` through the clamp. The focused combat test now constructs negative and oversized soldiers and asserts the stored levels are bounded.
Rejected Alternatives: Clamping only during save load was rejected because gameplay/debug/editor/generated callers can build transient squads without going through a save read. Using unsigned storage was rejected because it changes serialized shape and still needs validation.
Scalability potential: Low keeps malformed data from destabilizing small squads. Middle supports authored high-level NPC soldiers without overflow. High/Ultra can add elite or promoted soldier tiers later while keeping derived math predictable.
Hardware Impact: Construction and reward/upkeep helpers pay one or two integer branches. Normal frame cost is 0 us unless a caller recalculates soldier economy/reward; even then the cost is below measurement noise and prevents expensive downstream bad-state debugging.

## Eleventh-Pass Integration Blocker
Problem: Current full `build-msvc` relink stops in out-of-domain road-generation code before `timaert.exe` can be relinked. The observed errors are missing `torus_delta` and `find_road_path` declaration/call drift in `src/macro/spawners.cpp`.
Solution: Do not edit roads from the combat/NPC-soldier prompt. Rebuild and run focused combat/save/quest targets instead, run legacy/hot-path scans, and record the full-build blocker explicitly.
Rejected Alternatives: Touching `src/macro/spawners.cpp` was rejected because the batch prompt explicitly says not to touch roads and a separate road-domain agent is active. Claiming the full build passed from a stale executable was rejected because it would be a false report.
Scalability potential: Low/Middle/High/Ultra combat behavior remains covered by focused targets and smoke. Project-wide integration remains dependent on the road-domain compile fix before a clean app relink can be claimed.
Hardware Impact: Combat runtime impact is unchanged. The blocker is compile-time only; focused combat targets still build and pass.

## Hostile And Fauna Spawn Guards
Problem: BattleStart hostile spawning still trusted raw event level input and assumed `mgr_.tiles()` was full-sized when non-empty. Fauna respawn had the same tile-buffer assumption and stored `f.baseLevel + levelBonus` directly as `int16_t`.
Solution: Route hostile, fauna, player-soldier projection, and death-resolution level reads through `normalize_soldier_level`. Cache subworld tile vectors and sample water only when they have at least `kFullSize*kFullSize` entries.
Rejected Alternatives: Leaving the checks only in `spawn_player_squad` was rejected because BattleStart and fauna use adjacent subworld spawn paths and should obey the same malformed-buffer contract. Hard-failing partial buffers was rejected because subworld entry should degrade to no terrain mask instead of dropping NPCs.
Scalability potential: Low handles partial/generated maps without crashes. Middle keeps BattleStart encounter data bounded. High/Ultra can push denser spawn visuals while keeping data validation centralized.
Hardware Impact: Spawn-time only. Hostile spawn pays one level clamp and one cached size comparison; fauna respawn pays one cached size comparison per wave and one level clamp per spawned NPC. Normal frame cost is 0 us.

## Hecton Import Refresh
Problem: The user explicitly required all Timaert/Samosbor-relevant docs/tasks/logs from the Hecton folder to live under Timaert. The Hecton folder was still receiving new docs/logs while this work ran, so the previous import snapshot was stale.
Solution: Refreshed the non-destructive import under `Docs/Imported/Hecton8/2026-05-15_docs_tasks_logs`, then updated the task/log/report convenience quarantines. Final snapshot `MANIFEST_REFRESH_2026-05-15_145814.tsv` records 2289 source files, 132,032,531 source bytes, and missing-after-copy 0.
Rejected Alternatives: Destructive moving was rejected because active Hecton agents may still rely on their files. Flattening into active Timaert report folders was rejected because it would mix foreign prompt state with live Timaert status/log files. Pretending the folder is stable was rejected because the source was visibly mutating during the sync.
Scalability potential: Low keeps a readable import tree. Middle keeps tasks/logs/reports reachable through matching Timaert quarantines. High/Ultra can mine the imported corpus later without runtime coupling.
Hardware Impact: Runtime and save/load cost is 0 us; this is documentation storage only. Disk footprint after refresh is 259,459,099 bytes in the full quarantine tree.
