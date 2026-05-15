# Timaert Agent Verification Matrix

Date: 2026-05-15
Workspace: `C:\Timaert\timaert_c`
Scope: Timaert/Samosbor only. No writes to Hecton are part of this report.

Root entrypoint: `TIMAERT_MASTER_CHANGESET_AND_COMMIT_MANIFEST_2026-05-15.md`
now lives at the Timaert repo root. Use it first; this matrix remains the
agent-evidence detail layer.

This matrix links the current TMA agent status files, logs, rationale files,
changed code domains, verification evidence, and remaining risks. It is meant
for commit preparation: do not trust a domain just because an agent wrote
`VERIFIED`; trust it only where there is build/test/smoke evidence.

Related reports:

- `Docs/Reports/2026-05-15_PRECOMMIT_CYCLE_DOCUMENTATION.md`
- `Docs/Reports/2026-05-15_CHANGESET_INVENTORY.md`

## Evidence Inventory

TMA status files observed:

```text
Docs/Tasks/Status_TMA_AUDIO_SDL_MIXER_PORTER.md
Docs/Tasks/Status_TMA_CHARACTER_PAPERDOLL_ATLAS_BKR.md
Docs/Tasks/Status_TMA_COMBAT_NPC_SOLDIER_BKR.md
Docs/Tasks/Status_TMA_EVENT_QUEST_SAVE_LEDGER_BKR.md
Docs/Tasks/Status_TMA_FEATURE_LAYER_SENTINEL.md
Docs/Tasks/Status_TMA_INTEGRATION_SENTINEL.md
Docs/Tasks/Status_TMA_ROAD_RIVER_TERRAIN_BKR.md
Docs/Tasks/Status_TMA_SETTLEMENT_NPC_ACTIONS_BKR.md
Docs/Tasks/Status_TMA_SPELL_CASTING_EFFECTS_BKR.md
Docs/Tasks/Status_TMA_STORY_DIALOG_UI_BKR.md
Docs/Tasks/Status_TMA_SUBWORLD_ASYNC_SEAM_BKR.md
Docs/Tasks/Status_TMA_SUBWORLD_GENERATOR_PARITY_BKR.md
```

TMA final logs observed:

```text
Docs/AgentLogs/LOG_TMA_AUDIO_SDL_MIXER_PORTER.md
Docs/AgentLogs/LOG_TMA_CHARACTER_PAPERDOLL_ATLAS_BKR.md
Docs/AgentLogs/LOG_TMA_COMBAT_NPC_SOLDIER_BKR.md
Docs/AgentLogs/LOG_TMA_EVENT_QUEST_SAVE_LEDGER_BKR.md
Docs/AgentLogs/LOG_TMA_FEATURE_LAYER_SENTINEL.md
Docs/AgentLogs/LOG_TMA_INTEGRATION_SENTINEL.md
Docs/AgentLogs/LOG_TMA_ROAD_RIVER_TERRAIN_BKR.md
Docs/AgentLogs/LOG_TMA_SETTLEMENT_NPC_ACTIONS_BKR.md
Docs/AgentLogs/LOG_TMA_SPELL_CASTING_EFFECTS_BKR.md
Docs/AgentLogs/LOG_TMA_STORY_DIALOG_UI_BKR.md
Docs/AgentLogs/LOG_TMA_SUBWORLD_ASYNC_SEAM_BKR.md
Docs/AgentLogs/LOG_TMA_SUBWORLD_GENERATOR_PARITY_BKR.md
```

TMA rationale files observed:

```text
Docs/AgentLogs/Rationale_TMA_AUDIO_SDL_MIXER_PORTER.md
Docs/AgentLogs/Rationale_TMA_CHARACTER_PAPERDOLL_ATLAS_BKR.md
Docs/AgentLogs/Rationale_TMA_COMBAT_NPC_SOLDIER_BKR.md
Docs/AgentLogs/Rationale_TMA_EVENT_QUEST_SAVE_LEDGER_BKR.md
Docs/AgentLogs/Rationale_TMA_FEATURE_LAYER_SENTINEL.md
Docs/AgentLogs/Rationale_TMA_INTEGRATION_SENTINEL.md
Docs/AgentLogs/Rationale_TMA_ROAD_RIVER_TERRAIN_BKR.md
Docs/AgentLogs/Rationale_TMA_SETTLEMENT_NPC_ACTIONS_BKR.md
Docs/AgentLogs/Rationale_TMA_SPELL_CASTING_EFFECTS_BKR.md
Docs/AgentLogs/Rationale_TMA_STORY_DIALOG_UI_BKR.md
Docs/AgentLogs/Rationale_TMA_SUBWORLD_ASYNC_SEAM_BKR.md
Docs/AgentLogs/Rationale_TMA_SUBWORLD_GENERATOR_PARITY_BKR.md
```

Latest integrator evidence group:

```text
Docs/AgentLogs/integrator_full_build_after_dirt_landmask_count.log
Docs/AgentLogs/integrator_smoke_boot_dirt_landmask_count_seed114.log
Docs/AgentLogs/integrator_alltests_after_dirt_landmask_*.log
```

## Domain Matrix

| Agent ID | Claimed status | Main code domain | Hard evidence | Remaining risk |
| --- | --- | --- | --- | --- |
| `TMA_AUDIO_SDL_MIXER_PORTER` | `VERIFIED_WITH_EXTERNAL_BUILD_BLOCKER` | `src/macro/audio.*`, CMake SDL2_mixer wiring, audio tests | Direct audio contract/runtime tests pass; smoke logs show music/SFX loaded; CMake native path fails fast if SDL2_mixer missing | Status file records external build/dependency blockers at the time of the audio pass. Treat audio as code-verified but environment-sensitive |
| `TMA_CHARACTER_PAPERDOLL_ATLAS_BKR` | `VERIFIED` | `src/assets/character_paperdoll*`, app paper-doll load/smoke hook | `character_paperdoll_test`, `character_paperdoll_gl_smoke_test`, boot smoke with atlas `964x964 sheets=286 entries=45760`, battle smoke after `NpcCharacter` assertion | Visual quality and animation polish still need visual review |
| `TMA_COMBAT_NPC_SOLDIER_BKR` | `VERIFIED` | `src/macro/army.h`, NPC records, subworld hostile spawn/level guards | `combat_squad_test`, `save_roundtrip_test`, `quest_lifecycle_test`, battle/subworld smoke with `battle_start routed hostiles=0->1` | Broad gameplay shape changed; commit separately from UI/roads |
| `TMA_EVENT_QUEST_SAVE_LEDGER_BKR` | `PARTIAL` | `src/events/*`, quest engine, procedural quests, save v8 | `quest_lifecycle_test` and `save_roundtrip_test` pass; EventBus contract proof exists; quest tag canonicalization guarded by static asserts | Extended TS tags exist in schema/save, but no normal live producers/consumers for several tags. Do not claim full event parity |
| `TMA_FEATURE_LAYER_SENTINEL` | `VERIFIED` | `features.ts` transfer: `FeatureLayer`, builder, path/zones/renderer/subworld consumers | `feature_layer_parity_test`, `pathfinding_parity_test`, `road_river_generation_test`, full build/test sweep, seed smokes | Outside-domain renderer sprite batching and event producers remain partial elsewhere |
| `TMA_INTEGRATION_SENTINEL` | `INTEGRATED` | Cross-agent build/smoke, conflict repair, integration build | `build-msvc-integrator` build, boot/trade/seam smokes, focused tests | Seam trace still recorded visible 3D upload cost; integrated does not mean performance-finished |
| `TMA_ROAD_RIVER_TERRAIN_BKR` | `VERIFIED` | macro terrain, rivers, road A*, feature masks, zone water boost | road/feature/path tests, baseline quest/save/subworld tests, seed smokes through seed 49 and integrator seed 114 | Road generation is native A* divergence, not exact TS corridor snapping. Hecton import is a snapshot, not a live mirror |
| `TMA_SETTLEMENT_NPC_ACTIONS_BKR` | `VERIFIED` | settlement build/trade UI, NPC panel/trade/attack, macro overlay/app routes | MSVC rebuild/link, Build/NPC panel/NPC Trade/Attack smokes, PPM captures with nonzero samples | UI screenshots prove nonblank states, but not full UX polish. High churn in `main.cpp`, `macro_overlay.cpp`, `overlays.cpp` |
| `TMA_SPELL_CASTING_EFFECTS_BKR` | `VERIFIED` | spell registry/book/types, `sub/spell_effects.*`, SpellOverlay, renderer spell visuals | `spell_casting_effects_test`, full build, SpellOverlay smoke with projectile/Haste/Flight, save roundtrip | Unsupported macro world effects must remain honest. Visual quality needs screenshot/playtest review |
| `TMA_STORY_DIALOG_UI_BKR` | UI verified; import stream `PARTIAL` | story/dialog modal routing, story completion, count-only dialog, UI docs | story/dialog/count-only/story-complete smokes pass; `feature_layer_parity_test` and `quest_lifecycle_test` pass in story verification | Hecton live import stream drifted during attempts, so import stream is partial. Story/dialog UI code evidence is stronger than import evidence |
| `TMA_SUBWORLD_ASYNC_SEAM_BKR` | `VERIFIED` | `src/sub/seamless_manager.*`, async seam worker jobs, snapshots, road-mask path | focused `subworld_async_seam_test`, subworld seam smoke, `subworld_time` smoke, baseline regressions | Smoke still reports meaningful upload cost. Async seam is improved, not proven final performance target |
| `TMA_SUBWORLD_GENERATOR_PARITY_BKR` | `VERIFIED` | `src/sub/gens/dispatch.cpp`, base generator/map factory, subworld generator parity | `subworld_generator_parity_test`, `subworld_async_seam_test`, focused feature/path/road tests, runtime subworld smoke | Bounded native generation intentionally differs from unbounded TS worker-style behaviour where docs justify the limit |

## Cross-Domain Test Map

| Test | Domains it supports | What it actually proves |
| --- | --- | --- |
| `audio_contract_test` | Audio | Static contract and invalid-ID behavior for native audio |
| `audio_runtime_test` | Audio | Runtime SDL2_mixer path can initialize/use dummy driver path where configured |
| `character_paperdoll_test` | Paperdoll | Atlas data, deterministic descriptor/render-plan behavior |
| `character_paperdoll_gl_smoke_test` | Paperdoll/GL | GL-side paper-doll smoke path and atlas load |
| `combat_squad_test` | Combat/NPC/subworld spawn | Squad data, malformed tile guard, owner/ID behavior |
| `feature_layer_parity_test` | Feature layer/roads/renderer consumers | Feature priority, TS tree indexing, water guard, malformed storage, invalid bytes, torus wrap |
| `npc_spawn_contract_test` | Macro NPC spawn | Invalid/mismatched terrain fallback and invalid map fail-closed behavior |
| `pathfinding_parity_test` | Pathfinding/features/zones | Cost grid, malformed feature/terrain handling, zone fallback/water-mask behavior |
| `quest_lifecycle_test` | Events/quests/story/dialog/content | Quest lifecycle, EventBus contracts, node/encounter/dialog paths covered by test |
| `road_river_generation_test` | Roads/rivers/terrain/features | Water pruning, active sea level, A* proof, over-budget prune, malformed terrain, river/tree invariants |
| `save_roundtrip_test` | Save/events/spells/quests | v8 save shape and round-trip of expanded state covered by fixture |
| `spell_casting_effects_test` | Spells/subworld effects | Projectiles, cooldowns, sustained drains, metadata, macro flavor fields |
| `subworld_async_seam_test` | Seam manager/subworld persistence | Worker seam paths, saved restore, dirty cell counts, sparse road-mask proof |
| `subworld_generator_parity_test` | Subworld generation | Nonzero and guarded outputs for forest/ruin/spire/city/village/road/water/grassland/swamp families |

## Evidence Strength Ranking

Strong evidence domains:

- Feature layer and consumers.
- Road/river/terrain invariants.
- Subworld generator and seam focused tests.
- Spell casting/effects state.
- Paperdoll loader/GL smoke.
- Combat/NPC spawn guard paths.

Medium evidence domains:

- Story/dialog UI: good smoke coverage, but visual/UX flow still needs human pass.
- Settlement/NPC UI actions: smoke captures exist, but UI flow is broad.
- Audio: tests pass, but native dependency availability is environment-sensitive.

Partial domains:

- Event schema/live producer parity: tests pass for covered paths, but several TS event tags have schema/save presence without normal gameplay producers/consumers.
- TS renderer parity: native renderer replaces some TS Canvas2D/WebGL surfaces; this is documented, not exact parity.
- Hecton import mirror: useful snapshot under Timaert, not a guaranteed live mirror while Hecton keeps changing.

## Files Most Likely To Hide Cross-Agent Regressions

These are the files to inspect before staging because they are large, shared,
or touched by several agent domains:

```text
src/app/main.cpp
src/ui/overlays.cpp
src/ui/macro_overlay.cpp
src/sub/gens/dispatch.cpp
src/sub/seamless_manager.cpp
src/sub/engine.cpp
src/macro/spawners.cpp
src/macro/macro_renderer.cpp
src/events/event_types.h
src/events/event_bus.cpp
CMakeLists.txt
translation.md
```

Review focus:

- `src/app/main.cpp`: smoke actions, story/spell/audio/subworld routing, active sea-level plumbing.
- `src/ui/overlays.cpp`: dialog/story/spell/settlement modal correctness and clipping.
- `src/ui/macro_overlay.cpp`: settlement and NPC action surfaces.
- `src/sub/gens/dispatch.cpp`: city/village/road/ruin/spire generator invariants.
- `src/sub/seamless_manager.cpp`: worker lifecycle, queue shutdown, composite buffer shifts.
- `src/sub/engine.cpp`: combat/spell/flight/subworld integration.
- `src/macro/spawners.cpp`: road/river/tree/feature masks and malformed storage guards.
- `CMakeLists.txt`: SDL2_mixer discovery and test target graph.
- `translation.md`: make sure partial rows stay partial.

## No-Hecton-Write Boundary

Multiple agent reports explicitly state no files were written under:

```text
C:\hades\Hecton8
```

Broad Hecton import/reference buckets are excluded from the first push.

Boundary rule for commits:

- Commit imported Hecton-origin material only if it is intentionally part of
  the Timaert documentation snapshot.
- Do not reintroduce the broad `Docs\Imported\` mirror without a file-by-file
  Timaert/Samosbor relevance filter.
- Do not reintroduce `Imported_Hecton8` buckets without the same filter.
- Do not move Timaert reports into Hecton.
- Do not describe the import tree as live-synchronized if logs say the Hecton
  source was still changing.

## Commit Gate From This Matrix

Before commit slicing:

- Re-run `git diff --check`.
- Re-run the current 14-test sweep.
- Re-run one boot smoke and one UI smoke after any additional code edits.
- Review all `PARTIAL` rows before writing commit messages.
- Use the matrix status language in commit messages: `verified`, `partial`, or `environment-sensitive`; do not flatten everything to "done".
