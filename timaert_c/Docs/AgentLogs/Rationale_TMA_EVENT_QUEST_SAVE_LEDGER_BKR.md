# Rationale: TMA_EVENT_QUEST_SAVE_LEDGER_BKR

## Decision 1: Failed Quest Ledger Split

Problem: `QuestFailed` was indistinguishable from completion because failed IDs were stored in `completedQuestIds`.
Solution: Added `PlayerState::failedQuestIds`, persisted it in save schema v8, and made `QuestEngine::is_known` check completed and failed ledgers.
Rejected Alternatives: Leaving TS behavior as-is would preserve a known bug; adding status suffixes into `completedQuestIds` would make save queries string-fragile.
Scalability potential: Low keeps a linear journal scan for small player ledgers; Middle/High/Ultra can replace both vectors with a hashed quest journal once save migration exists.
Hardware Impact: Expected cost is below 1 microsecond for normal quest counts on i3/MX350-class hardware; prevents repeated quest offer/evaluation work.

## Decision 2: Procedural Quest Template Port

Problem: Native procedural quests were a compact placeholder and did not represent TS economy/distance/village quest variety.
Solution: Ported seven TS generator templates into `content/quests/procedural.cpp`, using deterministic RNG, torus distance, compact objectives, and `SpawnEntity` onAccept payloads.
Rejected Alternatives: A generic random quest generator would be faster to write but would invalidate the parity ledger and make UI/story testing dishonest.
Scalability potential: Low runs the same cheap deterministic generators; Middle/High/Ultra can spend saved simulation cost on richer descriptions, markers, and optional spawn realization.
Hardware Impact: Per-settlement generation is dominated by a tiny candidate sort for visit quests; expected cost is visually irrelevant on i3/MX350 and amortized by daily quest caching.

## Decision 3: SpawnEntity As Flat Event Payload

Problem: TS quest `onAccept` can request spawn payloads, but native save/event schema had no matching event tag.
Solution: Added `EventTag::SpawnEntity` and stored type, map position, and level in existing flat `GameEvent` fields.
Rejected Alternatives: A polymorphic payload hierarchy would add save complexity and RTTI pressure for no current runtime gain.
Scalability potential: Low can ignore or cheaply realize one spawn; Middle/High/Ultra can use the same payload to fan out richer NPC spawn visuals and encounter setup.
Hardware Impact: Save/load delta is sub-microsecond per event payload because existing string/int fields are reused.

## Decision 4: LogicNode Pending Dispatch Hardening

Problem: `LogicNodeEngine` queued references into mutable node storage and then allowed node effects to mutate the node table/active set during the same dispatch pass. A self-removing node could also erase its own definition before dispatch read its `next` list.
Solution: Changed the pending-fire queue to stable node ID strings, copied each ID before running an effect, and snapshots the effect/`next` list before execution so `add_node` / `remove_node` cannot invalidate the current dispatch lookup.
Rejected Alternatives: Forbidding node mutation during effects would diverge from TS `NodeContext`; reserving larger containers only hides the failure and still leaves remove-node hazards.
Scalability potential: Low keeps dispatch predictable with small SSO IDs; Middle/High/Ultra can introduce numeric node handles if node graph density becomes measurable.
Hardware Impact: Expected cost is below 1 microsecond for current system nodes on i3/MX350; the fix buys deterministic behavior rather than visual cost.

## Decision 5: Separate Logic Node Registration From Activation

Problem: Native `LogicNodeEngine::add` registered and activated nodes in one call, while TS registers node definitions separately and activates only `INITIAL_ACTIVE_NODES` or explicit `ctx.activate()` targets.
Solution: Made `add` registration-only, kept `activate` explicit, and updated builtin/intro registration to activate the intended startup nodes.
Rejected Alternatives: Keeping auto-activation was convenient but kept newly added dynamic nodes hot by default, diverging from TS and increasing surprise work per tick.
Scalability potential: Low devices check only deliberately active nodes; Middle/High/Ultra can keep large registered node libraries dormant until content activates them.
Hardware Impact: Reduces accidental per-tick node scans. Current savings are sub-microsecond for system nodes, but the behavior matters once quest/story node counts grow.

## Decision 6: Village-Safe Procedural Quest IDs

Problem: TS village IDs are globally offset after city IDs, but native `populate_landmarks_from_politik` starts village IDs at zero. Procedural quest IDs using raw numeric IDs could collide between city and village quest sources.
Solution: Added a generator-only ID segment: city quests keep numeric IDs, village quests use `v<id>` in quest IDs. Gameplay fields such as `giverSettlementId` and objective target IDs remain the original native IDs.
Rejected Alternatives: Changing native village IDs in `state.cpp` would affect economy, NPC routing, save data, and other agents' domains. Adding random salt would hide collisions but make quest IDs less deterministic and less debuggable.
Scalability potential: Low devices get deterministic string IDs with no lookup table; Middle/High/Ultra can build richer quest registries on the same stable ID namespace.
Hardware Impact: One short string prefix per generated village quest, amortized by daily quest caching; expected cost is below 1 microsecond and prevents expensive duplicate/known-quest ambiguity later.

## Decision 7: Mark Intro Parity Only From Runtime Proof

Problem: The parity ledger still listed `plot/intro.ts` as not started even though native content and tests proved the intro story path.
Solution: Updated only the rows with direct evidence from `quest_lifecycle_test`: the L4 intro row and Phase F6.
Rejected Alternatives: Marking adjacent UI overlay work complete would be dishonest; the proof covers content data and `ShowStory` emission, not the full GUI intro playthrough.
Scalability potential: Low devices load a static constexpr story table; Middle/High/Ultra can spend rendering budget in the UI overlay without changing story data.
Hardware Impact: Static table lookup and one event emit are below 1 microsecond; no frame-time risk.

## Decision 8: Consume TS PlayerEnterSettlement Without Breaking Legacy Producer

Problem: TS `sys_settlement` listens to `PlayerEnterSettlement`, but native app code currently emits `SettlementVisit`. Replacing the producer is outside this prompt's ownership, but leaving the node unable to consume the TS tag blocked parity tests and future producers.
Solution: Added `EventTag::PlayerEnterSettlement` after existing tags to avoid changing prior serialized tag values. Updated `sys_settlement` to consume either `SettlementVisit` or `PlayerEnterSettlement`, and extended save roundtrip coverage for the new flat event tag.
Rejected Alternatives: Editing `app/main.cpp` to replace the producer would cross the owned-file boundary; adding `PlayerLeaveSettlement` would be speculative because no native consumer exists yet.
Scalability potential: Low devices still run one system node check; Middle/High/Ultra can migrate producers to the TS tag without changing the node or UI consumers.
Hardware Impact: One extra branch in the settlement-node event scan, below 1 microsecond; no new allocations in the hot path.

## Decision 9: Register Chapter 1 Placeholder Instead Of Ignoring It

Problem: TS `plot/chapter-1.ts` exports a dormant placeholder node that is registered with plot content and later activated after intro completion. Native app code already attempted to activate `plot_chapter_1`, but no node definition existed, so the activation was a no-op.
Solution: Added a header-only `plot_chapter_1` placeholder node and registered it through the existing plot registration path used by `register_intro_story_nodes`. It remains inactive at boot, activates after intro, and keeps the TS false condition so it emits no gameplay events until real chapter content exists.
Rejected Alternatives: Touching `app/main.cpp` to add a new plot registry function would cross the prompt boundary for no gameplay gain. Emitting a temporary dialog would invent content not present in TS.
Scalability potential: Low devices pay one dormant active-node predicate only after intro; Middle/High/Ultra can replace the false predicate with real chapter triggers without changing registration or activation semantics.
Hardware Impact: The dormant predicate is below 1 microsecond and allocates nothing per tick.

## Decision 10: Prove Encounter Data Instead Of Claiming It

Problem: The ledger claimed encounter parity, but runtime tests did not prove the encounter table and one C++ branch (`Abandoned Campfire` search reward) drifted to 15g, which is not a legal TS branch outcome.
Solution: Corrected the reward to a legal TS branch value and added focused table assertions for count, choice/effect structure, battle payloads, codex/reputation payloads, and TS-random legal branch outcomes.
Rejected Alternatives: Replacing the existing modal encounter pipeline with the TS `enc_random` node now would duplicate or destabilize native encounter triggering. Leaving the row as-is would be an unproven ledger claim.
Scalability potential: Low devices keep the cached table and existing modal path; Middle/High/Ultra can later migrate random trigger selection to a seeded node without changing encounter payload data.
Hardware Impact: Cached table validation is test-only. Runtime change is a constant value correction with no frame cost.

## Decision 11: Objective Verb Proof Before Ledger Promotion

Problem: Phase E4 remained partial because previous tests did not exercise every quest objective verb as a runtime completion path.
Solution: Added focused runtime tests for `VisitCell`, `WaitAt`, `DestroyNpc`, and `InteractCell`, complementing existing delivery/find/lifecycle coverage.
Rejected Alternatives: Marking E4 complete from structure review alone would repeat the original ledger problem. Adding UI smoke dependency would cross ownership and make quest-engine proof depend on unrelated overlays.
Scalability potential: Low devices keep objective evaluation as simple switch dispatch over active quests; Middle/High/Ultra can add indexed objective wakeups later if active quest counts become measurable.
Hardware Impact: Tests prove existing O(active quest/objective) logic. No new runtime work was added outside test execution.

## Decision 12: Reuse Logic Node Next Snapshot Storage

Problem: The polish hot-path scan found that `LogicNodeEngine::tick` copied each fired node's `next` list into a fresh local vector so self-removal stayed safe. Correct behavior was present, but repeated node firing could allocate if route IDs outgrew small-string/local vector state.
Solution: Added a reusable member `nextSnapshot_` buffer. Node `next` routes are still snapshotted before effects run, but storage is retained across ticks and reserved when larger registered nodes appear.
Rejected Alternatives: Iterating the node-owned `next` vector directly would reintroduce the self-removal invalidation bug. Replacing node IDs with numeric handles is a larger migration not justified by current graph size.
Scalability potential: Low devices avoid avoidable heap churn on fired logic nodes; Middle/High/Ultra can still migrate to compact numeric node handles later.
Hardware Impact: Saves allocation risk on dispatch; measured by code-path inspection and preserved by `logic_rehash=ok` / `logic_self_remove=ok`.

## Decision 13: Promote Save Proof Only After Shell Smoke

Problem: The save row had binary roundtrip coverage but lacked canonical native shell save/load proof.
Solution: Ran the native smoke path through `save_game`, `open_load`, and `load_game`; it wrote and inspected a 51256-byte save slot and booted back into the loaded world.
Rejected Alternatives: Marking save complete from `save_roundtrip_test` alone would ignore the user-facing shell path. Editing LoadScreen/UI code is outside this prompt and was unnecessary because the smoke already covered the native shell.
Scalability potential: Low devices keep atomic binary save/load; Middle/High/Ultra can add richer save summaries without changing schema proof.
Hardware Impact: Save/load work is outside frame hot paths; no runtime frame cost was added.

## Decision 14: Move Random Encounters Into LogicNodeEngine

Problem: `node-registry.ts` still had one missing builtin: TS `enc_random`. Native app code handled random encounters as a direct auto-walk modal shortcut, which kept the node-registry row partial and risked divergent trigger semantics.
Solution: Added native `enc_random` as an active `LogicNodeEngine` builtin. It consumes `PlayerMove`, accumulates native millitile distance payloads as cell steps, applies the TS threshold/chance formula, and emits `ShowDialog` with copied encounter choices/effects from the cached encounter table. Removed the app-loop random modal trigger so the player does not get duplicate encounter rolls from two systems.
Rejected Alternatives: Leaving both systems active would double-roll encounters. Keeping only the modal shortcut would preserve a known TS parity gap. Rewriting overlay/modal UI was unnecessary because `ShowDialog` already consumes choice/effect payloads.
Scalability potential: Low devices keep one active predicate and cached encounter data; Middle/High/Ultra can later seed the node from world seed or add richer encounter presentation without changing the event payload path.
Hardware Impact: Per move event: one active node predicate, one float accumulation, one RNG sample only after threshold. Encounter dialog allocation happens only on successful encounter, not per frame. The millitile divide prevents short-frame movement from being overcounted as hundreds of TS steps.

## Decision 15: Keep TS-Only Event Tags Out Until Native Producers/Consumers Exist

Problem: At the time of the audit, `event-types.ts` still defined tags that native code did not produce or consume: `NpcHpChange`, `SettlementMoodChange`, `PlayerStatChange`, `PlayerLeaveSettlement`, `BattleEnd`, `MagicSurge`, `FactionRelationChange`, `DialogStart`, and `CameraMove`.
Solution: Audited the repo for those exact semantics and kept them out of native `EventTag` until a real native producer or consumer exists. This follows the prompt's event-schema rule and keeps save/event payload bounds meaningful. `PlayerLeaveSettlement` was later promoted when `refresh_player_settlement` became the concrete producer point; see Decision 16.
Rejected Alternatives: Adding enum values just to make the ledger green would create dead serialized tags with no gameplay path. Inventing producers in app/subworld/UI domains would cross ownership and risk conflicting with active agents.
Scalability potential: Low devices avoid wider dead event scans and save payload branches; Middle/High/Ultra can add each tag when its native system lands.
Hardware Impact: No runtime cost added. The gain is avoiding dead branches in event dispatch and save validation.

## Decision 16: Promote Settlement Enter/Leave To TS Tags

Problem: After `PlayerEnterSettlement` had a native consumer, the app still produced legacy `SettlementVisit`, and `PlayerLeaveSettlement` remained absent even though settlement transitions have a real native producer point.
Solution: Changed `refresh_player_settlement` to emit TS-shaped `PlayerEnterSettlement` on entry and `PlayerLeaveSettlement` on exit, with settlement id mirrored in `a` and `ix`. Kept `SettlementVisit` readable by `sys_settlement` as a compatibility input, not a live producer.
Rejected Alternatives: Removing `SettlementVisit` entirely would risk old quest/save/test payloads and unrelated agents' work. Adding the remaining TS-only event tags without producers/consumers would still violate the batch prompt.
Scalability potential: Low devices pay one or two flat event emissions only when the player crosses a settlement boundary; Middle/High/Ultra can attach richer settlement enter/leave presentation or faction reactions to the same payloads later.
Hardware Impact: Zero per-frame cost when the settlement id is unchanged. Boundary-change cost is a pair of small flat `GameEvent` structs and no persistent heap growth beyond normal event-buffer storage.

## Decision 17: Non-Destructive Hecton Import Landing Zone

Problem: The user requested all Timaert/Samosbor docs, tasks, and logs be transferred out of the Hecton folder into the Timaert project, but broad Hecton docs/logs are unrelated Unity evidence and should not pollute the Timaert parity ledger.
Solution: Audited `C:\hades\Hecton8` by filename and content for `Timaert`, `Samosbor`, `TMA_`, and `timaert_c`. No candidates were found, so zero files were copied. Created `Docs/Imported/Hecton/` with a README and import manifest to define the correct destination for future misplaced Timaert/Samosbor files.
Rejected Alternatives: Blind-copying all Hecton docs/logs would mix unrelated Unity build evidence into the native C++ port. Deleting/moving files out of Hecton would be destructive and unsafe while other agents may depend on those records.
Scalability potential: Low devices are irrelevant; this is repository hygiene. For project scale, a stable import area keeps future cross-project evidence searchable without contaminating `Docs/Tasks` and `Docs/AgentLogs`.
Hardware Impact: No runtime impact. Disk impact is two small Markdown files and zero migrated payload files.

## Decision 18: Canonical TS Quest Event Names With Legacy Aliases

Problem: Native quest lifecycle events had real producers but were still named `QuestAccepted`, `QuestObjectiveProgress`, `QuestCompleted`, and `QuestFailed`, while TS `event-types.ts` names the equivalent schema tags `QuestStart`, `QuestUpdate`, `QuestComplete`, and `QuestFail`.
Solution: Promoted the TS names into `EventTag` at the existing serialized numeric slots and changed `QuestEngine` producers plus `EffectApplicator` consumers to use the TS names. Kept the previous native names as enum aliases and added static assertions so save tag values cannot drift silently.
Rejected Alternatives: Renumbering events would break v8 saves. Removing legacy aliases would force unrelated call sites/tests to churn. Leaving only the old names would keep a real event-schema parity gap despite native producers existing.
Scalability potential: Low devices get zero runtime cost because aliases are compile-time enum constants. Middle/High/Ultra gain cleaner event contracts for future UI/story consumers without an adapter layer.
Hardware Impact: No runtime cost. Compile-time assertions only; serialized quest event payloads remain the same size and numeric value.

## Decision 19: Reserve EventBus Runtime Buffers

Problem: `EventBus` was TS-faithful but its tick, last-tick, listener, and query-result vectors started at zero capacity, so the first normal gameplay events and queries could allocate on the event hot path.
Solution: Added an `EventBus` constructor that reserves common tick/last/listener capacity and added conservative reserves to `find_all` and `query_history` result vectors. History allocation policy remains lazy and capped at 4096 entries.
Rejected Alternatives: Hard-capping tick events would change semantics and risk dropping gameplay events. Reserving full history on construction would waste memory for tests and dormant buses. Replacing vectors with a fixed ring would be larger than the current measured need.
Scalability potential: Low devices avoid first-event allocation spikes in normal gameplay. Middle/High/Ultra can raise the reserve constants if event density grows without changing the public API.
Hardware Impact: Small upfront memory reserve per bus; expected runtime gain is avoiding allocator calls on the first common event/listener/query paths.

## Decision 20: Prove EventBus Surface Directly

Problem: Quest and node tests exercised `EventBus` indirectly, but the TS parity row claimed the full event-bus query/lifecycle surface: `emit_all`, listeners, unsubscribe, flush, history queries, trim, and reset.
Solution: Added a focused `quest_lifecycle_test` slice that drives the public `EventBus` API directly and reports `event_bus=ok` only after tick order, listener delivery, listener removal, last/history promotion, newest-first query order, trim semantics, and reset state are verified.
Rejected Alternatives: Adding another standalone test target would require CMake churn for no additional gameplay coverage. Rewriting the bus as a ring buffer would be larger than the current parity proof and not required by the TS contract.
Scalability potential: Low devices keep the existing flat vectors and bounded history; Middle/High/Ultra can later swap internals behind the same proved public contract if event density grows.
Hardware Impact: The initial proof added no runtime code except ASCII comment cleanup in the header; the follow-up dispatch-mutation hardening is recorded in Decision 21. The gain is regression prevention: future event-bus edits that break query/order/reset behavior now fail the focused native test.

## Decision 21: Defer EventBus Subscription Mutation During Dispatch

Problem: `EventBus::emit` iterated the subscription vector directly while invoking callbacks. A UI or gameplay listener that called `on`, `unsubscribe`, or `reset` during its callback could reallocate or erase the vector while a `std::function` stored inside it was still executing.
Solution: Added dispatch-depth tracking, inactive subscription marking, deferred pending additions, and post-dispatch compaction. Callbacks added during an emit become active on the next emit; callbacks removed during an emit cannot invalidate the current dispatch.
Rejected Alternatives: Copying every matching `std::function` before invocation would avoid invalidation but risks heap churn per event. Forbidding callback mutation would be brittle for UI code and diverge from JS-style imperative listeners.
Scalability potential: Low devices keep stable flat-vector dispatch with no normal per-event allocation; Middle/High/Ultra can later introduce tag-indexed listener slabs behind the same API if listener counts become measurable.
Hardware Impact: Normal emit cost adds one depth counter and active-flag branch per subscribed listener. Mutation handling is cold-path only and prevents undefined behavior crashes instead of adding frame work.

## Decision 22: Quarantine Hecton Transfer Artifacts In Timaert Only

Problem: The user repeated that Timaert/Samosbor docs must not be written to Hecton. Fresh Hecton search found Hecton meta logs/status files that discuss the Timaert/Samosbor transfer boundary, while concurrent Hecton agents were still mutating docs/logs.
Solution: Treat Hecton as read-only source, refresh the existing quarantined import tree under `C:\Timaert\timaert_c\Docs\Imported\Hecton8\2026-05-15_docs_tasks_logs`, preserve source-relative paths, and record manifests in Timaert. The live-settle loop copied/refreshed current deltas until selected missing/stale counts reached zero.
Rejected Alternatives: Writing correction notes into Hecton would violate the user's instruction. Deleting Hecton sources would be destructive and unsafe because the matched files are Hecton provenance, not active Timaert task ownership. Merging imported files into active Timaert `Docs\Tasks` / `Docs\AgentLogs` would pollute live Timaert agent state.
Scalability potential: Low/Middle/High/Ultra project hygiene stays predictable: Timaert can inspect Hecton-origin evidence without using Hecton as a Timaert documentation sink and without contaminating active Timaert reports.
Hardware Impact: No runtime impact. Documentation copy only; no C++ runtime source changed in this refresh.
