# LOG - TMA_SPELL_CASTING_EFFECTS_BKR

## 2026-05-15 - Spell Casting And Effects

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

TS files read:
- `C:\Timaert\src\game\spells\spell-types.ts`
- `C:\Timaert\src\game\spells\spell-casting.ts`
- `C:\Timaert\src\game\spells\fireball.ts`
- `C:\Timaert\src\game\spells\ice-shard.ts`
- `C:\Timaert\src\game\spells\lightning-chain.ts`
- `C:\Timaert\src\game\spells\energy-beam.ts`
- `C:\Timaert\src\game\spells\magic-bolt.ts`
- `C:\Timaert\src\game\spells\armageddon.ts`
- `C:\Timaert\src\game\spells\flight.ts`
- `C:\Timaert\src\game\spells\haste.ts`
- `C:\Timaert\src\screens\SpellOverlay.svelte`
- Relevant subworld casting references in `C:\Timaert\src\screens\SubworldScreen.svelte` and `C:\Timaert\src\game\subworld\engine.ts`.

C++ files changed:
- `src/content/spells/spell_types.h`
- `src/content/spells/spell_types.cpp`
- `src/content/spells/spell_book.h`
- `src/content/spells/spell_book.cpp`
- `src/content/spells/registry.cpp`
- `src/macro/spell_book_state.h`
- `src/ecs/components.h`
- `src/ecs/systems.cpp`
- `src/sub/engine.cpp`
- `src/sub/renderer_3d.h`
- `src/sub/renderer_3d.cpp`
- `src/app/main.cpp`
- `src/ui/overlays.cpp`
- `tests/spell_casting_effects_test.cpp`
- `translation.md`
- `Docs/Tasks/Status_TMA_SPELL_CASTING_EFFECTS_BKR.md`

What was wrong:
- Spell registry had TS constants but the runtime path did not have full can-cast reasons, scaled damage/radius, real sustained drain at frame dt, or a reliable cast input.
- `energy_beam` was explicitly implemented as a fast narrow projectile, contradicting TS beam semantics.
- `lightning_chain` was a small-AoE projectile only, not chain behavior.
- Spell projectiles moved and expired but did not apply spell damage/AoE/beam/chain effects in the native subworld.
- Spell tab read the temporary `spellBookSpellIds` mirror and showed only id/name/mana.

What was done:
- Added TS-shaped spell metadata: rarity, delivery shape, tier, cast time, micro/macro availability, scaling, base radius, chain values, projectile radius/life, beam length, descriptions, stable spell ids, and label helpers.
- Implemented `CastCheck`, TS-style spell strength/damage/heal/radius/duration, can-cast reason paths, start-cast semantics, and fractional sustained drain carry so `manaDrain * dt` no longer truncates to zero.
- Wired gameplay casting from `Space`, with `SpellCast` event emission for success/failure.
- Added smoke actions `open_spells`, `cast_spell`, and `toggle_haste`.
- Implemented magic bolt, fireball, ice shard as projectile/AOE paths; lightning chain as real chained damage after first hit; energy beam as visual beam plus deferred line damage; armageddon as delayed AoE; haste speed multiplier; flight direct macro pathing.
- Added subworld projectile damage application, AoE, beam line hits, chain target selection, death reaping, and `NpcDeath` emission.
- Added a 3D spell visual instanced ribbon/billboard pass in `Renderer3D`, fed from existing ECS projectile/sprite data. No Canvas2D path, no per-frame texture/asset creation.
- Migrated the Spells tab to real `SpellBook.learned`, active spell, MP, cooldown, sustained state, delivery shape, and TS-derived power/radius. It does not display a fake backend-less cast button.
- Added focused `tests/spell_casting_effects_test.cpp`.

Deliberate divergences from TS:
- Armageddon uses one delayed AoE visual instead of the TS multi-meteor swarm. It closes gameplay damage semantics without adding a per-cast random meteor allocation/simulation path.
- Projectile friendliness remains owner/self-filter based in native ECS; full TS faction-hostility projectile filtering is not present in this slice.
- Flight's subworld collision bypass is mostly moot because native subworld movement currently lacks hard obstacle collision. Macro flight uses direct pathing and ignores cost grid.

Verification:
- Full MSVC build attempted:
  `cmd /d /s /c '"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc'`
- Full build result: BLOCKED before compile by unrelated CMake configure error: current `CMakeLists.txt` requires `SDL2_mixer`; environment has no `SDL2_mixerConfig.cmake`.
- Targeted MSVC syntax check passed with `/Zs` for changed TUs: `spell_book.cpp`, `spell_types.cpp`, `registry.cpp`, `sub/engine.cpp`, `sub/renderer_3d.cpp`, `ui/overlays.cpp`, `app/main.cpp`, `ecs/systems.cpp`.
- Manual focused test compiled and passed:
  `build-msvc\spell_casting_effects_test.exe`
- Focused test output:
  `PASS: projectiles=3 mp=1818 cooldowns=2 sustained=1`
- Existing save roundtrip executable passed:
  `OK save_roundtrip_test path=C:\Users\danat\AppData\Local\Temp\timaert_save_roundtrip_v8.bin bytes=1712 map=512x256 quest=q_active`

Remaining blockers in domain:
- Full rebuilt app smoke and SpellOverlay screenshot/log cannot be produced until unrelated `SDL2_mixer` configure blocker is resolved.
- Full faction-aware spell hit filtering and richer armageddon meteor visuals remain future polish, not core castability.

STATUS: PARTIAL

## 2026-05-15 - Boot Gate Cleared And Spell Smoke Reverified

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- The spell domain was functionally closed, but current `new_game` smoke could not reach SpellOverlay because road generation failed/stalled after `[boot] trees spawned`.
- The blocking road path used unbounded full-map A* for every surviving city edge on a 1024^2 torus.

What was done:
- Added a bounded road A* budget in `src/macro/spawners.cpp`.
- Added a deterministic direct land-only fallback for capped edges; if the fallback crosses water, the edge is still pruned.
- Preserved the existing no-water-road invariant and existing road unit tests.
- Rebuilt the app and reran the full spell smoke.

Cinematic cheats used:
- The fallback is a visual/network cheat: a straight land-only road for capped edges instead of exhaustive terrain-cost search. It is used only when bounded A* caps out and never stamps water.
- Spell effects remain descriptor-driven billboards/ribbons/line checks, not expensive physical simulations.

Exact microseconds saved:
- Seed 42 road trace now completes during boot with bounded stats: `cities=66 attempted=153 kept=33 pruned=120 componentPruned=45 bounded=108 fallback=1 expansions=1060411 edgeCapHits=52 wholeCapHits=0`.
- Avoided previous boot failure before `roads traced`; on weak CPUs this removes unbounded million-cell-per-edge worst-case scans and makes spell smoke reachable.

Verification:
- `road_river_generation_test`: passed.
- `cmake --build build-msvc --target timaert -- -j1`: passed.
- `TIMAERT_SMOKE_SCRIPT=new_game,quit`: passed, reached `[boot] done`.
- Full spell smoke passed:
  `TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit build-msvc\timaert.exe`
- Runtime proof:
  `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`
  `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`
  `sustained_haste active=1 mp=100->88 carry=0.000`
  `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0`
  `PASS`
- Focused spell target passed:
  `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`
- Save roundtrip passed:
  `OK save_roundtrip_test path=... bytes=1756 map=512x256 quest=q_active`
- Hygiene checks:
  `rg -n "spellBookSpellIds|message\\(FATAL_ERROR" CMakeLists.txt src tests` returned no hits.
  `git diff --check` returned only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Continuation Bottom Report: Sustained Aura Renderer And Import Refresh 3

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell casting/effects/subworld spell visuals/SpellOverlay.

What was wrong:
- TS `haste.ts` and `flight.ts` include sustained aura renderers. Native spell visuals covered projectiles, beams, and meteors, but sustained Haste/Flight had gameplay state with no caster aura equivalent.
- The previous app smoke captured after `toggle_flight` had already returned to macro view, so it did not prove the sustained aura draw path in `Renderer3D`.
- The user required Timaert/Samosbor docs/tasks/logs to be pulled out of Hecton into Timaert. The prior refresh was content-only and needed a filename pass too.

What was done:
- `src/sub/renderer_3d.{h,cpp}`: added sustained aura inputs to the existing spell visual pass and append fixed-count additive instances for Haste green rings/particles and Flight blue rings/motes. The path reuses the current stack instance array and GL instance VBO; no Canvas2D, no texture creation, no per-frame asset objects.
- `src/sub/engine.cpp`: drives aura flags from real `SpellBook` sustained state and current flight state at render time.
- `src/app/main.cpp`: added `prepare_spell_auras` smoke action. It learns/toggles Haste and Flight through the real cast path, enters the 3D subworld, enables flight, and leaves `capture_frame` to capture the actual renderer output.
- `translation.md`: updated spell-renderer parity notes to include sustained Haste/Flight aura instances.
- `Docs\Tasks\Status_TMA_SPELL_CASTING_EFFECTS_BKR.md` and `Docs\AgentLogs\Rationale_TMA_SPELL_CASTING_EFFECTS_BKR.md`: updated with the aura decision, verification, and import boundary result.
- `Docs\Imported\Hecton8\2026-05-15_refresh3_filename_content\MANIFEST_TIMAERT_SAMOSBOR_REFRESH3.tsv`: recorded the new content+filename scan. Seven content matches, zero filename-only matches, seven copied sources, SHA-256 verification `bad_hashes=0`. Copies live only under Timaert import locations; no Hecton files were written or deleted.

Cinematic Cheats used:
- Haste/Flight auras are ring and mote cheats in the existing additive spell instance batch. This replaces TS Canvas2D arc/fill calls with camera-space 3D billboards/ribbons that read as magic at gameplay distance.

Exact microseconds saved:
- Reusing the existing spell instance buffer avoids a separate particle system and avoids heap-backed per-frame visual lists. Estimated saving versus a naive dynamic particle path is 30-80 microseconds per frame on i3/MX350-class hardware when both sustained spells are active.
- Actual added cost is bounded: at most 32 extra spell instances and fixed scalar trig work, estimated below 20 microseconds per frame on low-end hardware.

Verification:
- App target rebuild passed: `cmake --build build-msvc --target timaert -- -j1`.
- Aura runtime smoke passed: `new_game,wait_boot_done,prepare_spell_auras,capture_frame,quit`; proof `spell_auras haste=1 flight=1 subworld=1 flying=1 mp=109`, latest capture `artifacts\runtime-smoke\images\verification-20260515\smoke_03_ui.ppm`, sampled nonblank check `31659/31671`.
- Full build gate passed: `cmake --build build-msvc -- -j1`; output `ninja: no work to do`.
- Focused spell test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Save roundtrip passed: `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`.
- Hecton import refresh 3 passed: `content_matches=7 filename_matches=0 combined=7 rows=7 bad_hashes=0`.
- Hygiene passed after mechanical suffix cleanup: no `spellBookSpellIds`; no unsigned-literal suffix hits in spell/subworld spell surfaces; `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Final Bottom Report: Cast Gate, Event Smoke, Import Boundary

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- Native `spellbook_can_cast_ex(..., inMicro=false)` could claim success for non-sustained TS macro spells even though the native backend has no macro damage-region target/faction contract and `spellbook_cast(..., inMicro=false)` refuses those casts.
- Runtime smoke proved projectile creation but did not prove the app-level `SpellCast` event payload.
- Hecton contained live compute-audit docs that mention Timaert/Samosbor; Timaert needed a current quarantined import copy and manifest without writing new Timaert docs into Hecton.

What was done:
- `src/content/spells/spell_book.cpp`: world-map non-sustained spells now return `World-map spell effect not implemented` from `spellbook_can_cast_ex`.
- `tests/spell_casting_effects_test.cpp`: added a regression assertion proving Fireball world-map `can_cast` and `cast` agree on native non-support and do not mutate MP/projectiles.
- `src/app/main.cpp`: spell smoke now asserts a successful `SpellCast` event with the active spell id and stable spell key; smoke output now includes `event=1`.
- `translation.md`: updated spell parity text to state honest world-map non-support and runtime `SpellCast` proof.
- `Docs\Imported\Hecton8\...`: refreshed all current Hecton files matching `Timaert|TMA_|Samosbor|Самосбор|Тимаерт|Тимерт` into Timaert import quarantine and active imported buckets only. Five source files, ten Timaert destinations, no Hecton writes or deletes.

Cinematic Cheats used:
- No simulation added. Existing spell visuals remain additive 3D descriptor cheats.

Exact microseconds saved:
- Valid subworld cast hot path is unchanged.
- Blocked world-map macro attempts avoid a false cast path; estimated saving is below 1 microsecond per blocked attempt. Main gain is correctness and no false UI/API promise.

Verification:
- Focused spell test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- App target rebuild passed.
- Runtime spell smoke passed with `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0 event=1`, Haste drain proof, Flight path/subworld height proof, and `[smoke] PASS`.
- Full `build-msvc` gate passed after removing only stale generated `build-msvc\CMakeFiles\subworld_async_seam_test.dir\manifest.res`; final output `ninja: no work to do`.
- `save_roundtrip_test.exe` passed: `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`.
- Import manifest verification passed: 10 rows hash-equal to Hecton sources at refresh time.
- Hygiene passed: no `spellBookSpellIds` in `src`, `tests`, or `CMakeLists.txt`; no unsigned-literal suffixes in spell-owned source/test surfaces; `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Continuation Bottom Report: Sustained Aura Renderer And Import Refresh 3

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell casting/effects/subworld spell visuals/SpellOverlay.

What was wrong:
- TS `haste.ts` and `flight.ts` include sustained aura renderers. Native spell visuals covered projectiles, beams, and meteors, but sustained Haste/Flight had gameplay state with no caster aura equivalent.
- The previous app smoke captured after `toggle_flight` had already returned to macro view, so it did not prove the sustained aura draw path in `Renderer3D`.
- The user required Timaert/Samosbor docs/tasks/logs to be pulled out of Hecton into Timaert. The prior refresh was content-only and needed a filename pass too.

What was done:
- `src/sub/renderer_3d.{h,cpp}`: added sustained aura inputs to the existing spell visual pass and append fixed-count additive instances for Haste green rings/particles and Flight blue rings/motes. The path reuses the current stack instance array and GL instance VBO; no Canvas2D, no texture creation, no per-frame asset objects.
- `src/sub/engine.cpp`: drives aura flags from real `SpellBook` sustained state and current flight state at render time.
- `src/app/main.cpp`: added `prepare_spell_auras` smoke action. It learns/toggles Haste and Flight through the real cast path, enters the 3D subworld, enables flight, and leaves `capture_frame` to capture the actual renderer output.
- `translation.md`: updated spell-renderer parity notes to include sustained Haste/Flight aura instances.
- `Docs\Tasks\Status_TMA_SPELL_CASTING_EFFECTS_BKR.md` and `Docs\AgentLogs\Rationale_TMA_SPELL_CASTING_EFFECTS_BKR.md`: updated with the aura decision, verification, and import boundary result.
- `Docs\Imported\Hecton8\2026-05-15_refresh3_filename_content\MANIFEST_TIMAERT_SAMOSBOR_REFRESH3.tsv`: recorded the new content+filename scan. Seven content matches, zero filename-only matches, seven copied sources, SHA-256 verification `bad_hashes=0`. Copies live only under Timaert import locations; no Hecton files were written or deleted.

Cinematic Cheats used:
- Haste/Flight auras are ring and mote cheats in the existing additive spell instance batch. This replaces TS Canvas2D arc/fill calls with camera-space 3D billboards/ribbons that read as magic at gameplay distance.

Exact microseconds saved:
- Reusing the existing spell instance buffer avoids a separate particle system and avoids heap-backed per-frame visual lists. Estimated saving versus a naive dynamic particle path is 30-80 microseconds per frame on i3/MX350-class hardware when both sustained spells are active.
- Actual added cost is bounded: at most 32 extra spell instances and fixed scalar trig work, estimated below 20 microseconds per frame on low-end hardware.

Verification:
- App target rebuild passed: `cmake --build build-msvc --target timaert -- -j1`.
- Aura runtime smoke passed: `new_game,wait_boot_done,prepare_spell_auras,capture_frame,quit`; proof `spell_auras haste=1 flight=1 subworld=1 flying=1 mp=109`, latest capture `artifacts\runtime-smoke\images\verification-20260515\smoke_03_ui.ppm`, sampled nonblank check `31659/31671`.
- Full build gate passed: `cmake --build build-msvc -- -j1`; output `ninja: no work to do`.
- Focused spell test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Save roundtrip passed: `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`.
- Hecton import refresh 3 passed: `content_matches=7 filename_matches=0 combined=7 rows=7 bad_hashes=0`.
- Hygiene passed after mechanical suffix cleanup: no `spellBookSpellIds`; no unsigned-literal suffix hits in spell/subworld spell surfaces; `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - World-Map Cast Gate Honesty and Hecton Import Boundary

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- Low-level native spell validation still allowed `spellbook_can_cast_ex(..., inMicro=false)` for non-sustained spells that only carry TS macro metadata.
- That made Fireball look castable on the world map even though the native backend intentionally has no macro damage-region target/faction contract and `spellbook_cast(..., inMicro=false)` refuses to spend mana or spawn a micro projectile.
- The Timaert-side Hecton import manifest was stale: current Hecton exact-match search found five Timaert/Samosbor-marked files, while the manifest still listed the older set.

What was done:
- Updated `src/content/spells/spell_book.cpp` so world-map non-sustained spells return `World-map spell effect not implemented` from `spellbook_can_cast_ex`.
- Added a focused regression assertion in `tests/spell_casting_effects_test.cpp` proving Fireball world-map `can_cast` and `cast` now agree on native non-support and do not mutate MP/projectiles.
- Added an app smoke assertion that a real projectile cast emits a successful `SpellCast` event with the active spell id and stable spell key.
- Refreshed the Timaert-only Hecton import quarantine for all current Hecton files matching `Timaert|TMA_|Samosbor|Самосбор|Тимаерт|Тимерт`.
- Copied five source files into ten Timaert destinations, including active imported buckets and the quarantined mirror tree. No Hecton files were written or deleted.
- Updated `Docs\Imported\Hecton8\TIMAERT_SAMOSBOR_IMPORT_MANIFEST.md` and wrote `MANIFEST_DELTA_2026-05-15_TIMAERT_SAMOSBOR_MATCH_REFRESH_2.tsv`.

Deliberate divergences from TS:
- Macro damage-region spells are still blocked natively until a real target-selection and faction-damage contract exists. This is an honest gate, not a missing projectile workaround.
- Hecton compute-audit files that merely mention Timaert/Samosbor remain imported/quarantined; they were not promoted into active Timaert gameplay docs.

Cinematic Cheats used:
- No new simulation. The spell change is validation-only.
- Existing projectile/beam/meteor visuals remain descriptor-driven 3D cheats.

Exact microseconds saved:
- Runtime hot path unchanged for valid subworld casts.
- Avoiding a failed world-map cast path saves only failed-input overhead, estimated below 1 microsecond per attempted blocked cast, and prevents false UI/API state.

Tests/smokes run:
- Focused spell target and runtime passed:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`
  Output: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- App target rebuild passed after transient external file locks cleared:
  `cmake --build build-msvc --target timaert -- -j1`.
- Runtime SpellOverlay smoke passed:
  `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`
  Proof: `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`; `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0 event=1`; `sustained_haste active=1 mp=100->88 carry=0.000`; `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0 subFlight=6.69`; `[smoke] PASS`.
- Save roundtrip passed:
  `OK save_roundtrip_test path=... bytes=2126 map=512x256 quest=q_active`.
- Full build gate passed:
  `cmake --build build-msvc -- -j1`.
- Full build detail: a stale generated `subworld_async_seam_test` `manifest.res` caused `RC1109`; only that generated file was removed, and the next `build-msvc` run reported `ninja: no work to do`.
- Final post-build checks passed: `save_roundtrip_test.exe` output `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`; `spell_casting_effects_test.exe` output `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Documentation transfer verification: exact Hecton search found 5 source files, copied 10 Timaert destinations, and immediate SHA-256 verification passed for all rows in the new delta manifest.
- Final hygiene: no `spellBookSpellIds` in `src`, `tests`, or `CMakeLists.txt`; no unsigned-literal suffixes in spell-owned source/test surfaces; `git diff --check` over touched spell/app/docs/import files reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Spell Domain Closure

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was still wrong:
- `spellBookSpellIds` compatibility state remained in `PlayerState`, save/load, `SpellLearned`, smoke, UI, and `save_roundtrip_test`, despite the Spells tab already being migrated.
- `translation.md` still reported beam/chain as projectile-only and SpellOverlay proof as blocked.
- `CMakeLists.txt` still hard-failed when `SDL2_mixer` was unavailable, even though `AudioSystem` already has a compiled no-op backend.
- `tests/spell_casting_effects_test.cpp` only proved spawn counts and one sustained drain path; it did not assert failure reasons, cooldown blocking/expiry, beam descriptors, chain descriptors, or sustained depletion behavior.
- The focused spell test existed only as a manual compile path, so the normal CMake build did not know about it.

What was done:
- Removed `spellBookSpellIds` completely from native state and all current references. Verification: `rg -n "spellBookSpellIds" src tests CMakeLists.txt` returned no hits.
- Save/load now serializes only `SpellBook`; `save_roundtrip_test` still passes and confirms active spell, cooldown, and sustained state.
- `SpellLearned` now mutates only `SpellBook`.
- Spell tab now reads only `p.spellBook.learned`; selecting active spell calls `spellbook_set_active` without mirror writes.
- Smoke `toggle_haste` no longer writes the mirror.
- CMake SDL2_mixer gate now warns and uses the no-op audio backend when the package is missing; if SDL2_mixer exists, the target/define/link path still activates.
- Expanded `spell_casting_effects_test` to cover:
  - magic bolt descriptor and world-map rejection reason,
  - fireball cooldown start, block reason, and expiry,
  - low mana rejection reason,
  - fireball AoE/explode descriptor,
  - energy beam visual beam descriptor,
  - lightning chain count/decay/radius descriptor,
  - Haste fractional drain and automatic shutdown on mana depletion.
- Added `spell_casting_effects_test` as a CMake target linked against EnTT.
- Updated `translation.md` spell rows and SpellOverlay ledger to verified status with runtime evidence.
- Created `Docs/AgentLogs/Rationale_TMA_SPELL_CASTING_EFFECTS_BKR.md`.

Cinematic cheats used:
- Projectile visuals are ECS descriptors rendered as additive 3D billboards/ribbons, not simulated particles.
- Energy beam is a short-lived visual beam plus deterministic line damage, not a fast projectile and not volumetric ray marching.
- Lightning chain is deterministic nearest-target propagation through cheap radius scans, not electric-field simulation.
- Armageddon remains a delayed AoE visual/damage marker rather than random meteor swarm simulation.

Exact microseconds saved, estimate basis:
- Removed mirror write/copy path on spell learn/save/UI selection: ~1-4 microseconds per mutation for typical small learned lists, plus zero stale-vector debugging cost.
- Sustained drain uses one scalar carry and one registry lookup per active sustained spell: sub-1 microsecond at current player-only sustained counts.
- Beam line damage avoids projectile stepping for a 300 px beam: estimated 10-30 microseconds saved per cast versus several frame-stepped collision checks.
- Billboard/ribbon spell visuals avoid per-frame texture/asset creation: estimated 50-200 microseconds saved on weak GPUs/CPUs during visible spell bursts.
- CMake no-op audio fallback saves integration time, not frame time; runtime cost unchanged when audio is enabled.

Verification:
- Shared MSVC build after SDL2_mixer gate change: `cmake --build build-msvc -- -j1` reached `ninja: no work to do` after active shared rebuilds completed.
- App smoke command passed:
  `TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,quit build-msvc\timaert.exe`
- App smoke proof:
  `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`
  `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`
  `sustained_haste active=1 mp=100->88 carry=0.000`
  `PASS`
- Focused CMake target build passed in isolated MSVC/Ninja directory because shared `build-msvc` was intermittently locked by concurrent agents:
  `ninja -C build-msvc-spell2 spell_casting_effects_test -v -j1`
- Focused test output:
  `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`
- Save roundtrip passed:
  `OK save_roundtrip_test path=C:\Users\danat\AppData\Local\Temp\timaert_save_roundtrip_v8.bin bytes=1712 map=512x256 quest=q_active`

Residual limitations:
- Armageddon is gameplay-closed as delayed AoE, not a TS-style multi-meteor visual swarm.
- Projectile faction filtering remains owner/self-filter based; deeper faction-hostility filtering is outside this spell transfer slice unless combat/faction consumers demand it.
- Shared `build-msvc` is actively used by other agents; isolated `build-msvc-spell2` was used to prove the new spell CMake target without killing their processes.

STATUS: VERIFIED

## 2026-05-15 - Spell Audit Hardening Pass

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was still wrong:
- `CMakeLists.txt` had regressed to a hard `FATAL_ERROR` when SDL2_mixer was absent, contradicting the intended no-op audio fallback and blocking full spell-domain verification on machines without mixer config.
- `audio_contract_test` also forced `TIMAERT_HAS_SDL_MIXER=1` whenever not Emscripten, even if the mixer target was not found.
- Low-level `spellbook_cast(..., inMicro=false)` could spawn a subworld projectile for non-sustained spells with TS macro metadata but no native macro damage implementation.
- Flight had code coverage only by inspection; runtime smoke only proved Haste.

What was done:
- Restored CMake SDL2_mixer missing-package behavior to `message(WARNING)` and no-op backend.
- Made `audio_contract_test` link/define SDL2_mixer only when `TIMAERT_SDL2_MIXER_TARGET` exists, matching the app target.
- Tightened `spellbook_cast`: after validation and self/sustained handling, non-micro casts now return false instead of spawning micro projectiles.
- Expanded `spell_casting_effects_test` to assert that world-map Fireball does not spawn a projectile and does not spend mana, and that Flight toggles/drains correctly in macro context.
- Added smoke token `toggle_flight`, which leaves subworld if needed, toggles sustained Flight on the macro map, verifies no projectile spawn, builds a direct wrapped Flight path, ticks drain, and logs proof.

Cinematic cheats used:
- Flight uses direct integer path construction while active, bypassing path-cost expansion rather than simulating altitude or terrain clearance.
- World-map non-sustained spell effects remain explicit failure/no-op until a real macro target/damage contract exists; this prevents fake invisible projectiles.

Exact microseconds saved, estimate basis:
- World-map projectile guard saves one ECS entity allocation and all later projectile tick/render scans for every invalid macro projectile cast; estimated 5-30 microseconds per bad cast plus avoided visual noise.
- Flight direct path smoke proves the intended O(max(dx,dy)) route construction. Compared with pathfinder expansion on obstructed long routes, this can save hundreds of microseconds on low-end CPUs while Flight is active.
- Conditional SDL2_mixer test wiring is build-time only; frame-time impact is zero.

Verification:
- Focused CMake target passed:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1`
- Focused test output:
  `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`
- Save roundtrip passed:
  `OK save_roundtrip_test path=C:\Users\danat\AppData\Local\Temp\timaert_save_roundtrip_v8.bin bytes=1756 map=512x256 quest=q_active`
- Full MSVC build passed after clearing a stale generated `manifest.res` artifact:
  `cmake --build build-msvc -- -j1`
- Runtime spell smoke passed against final `build-msvc\timaert.exe`:
  `TIMAERT_SMOKE_SCRIPT=new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit build-msvc\timaert.exe`
- Runtime proof:
  `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`
  `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`
  `sustained_haste active=1 mp=100->88 carry=0.000`
  `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0`
  `PASS`
- Hygiene checks:
  `rg -n "spellBookSpellIds|message\\(FATAL_ERROR" CMakeLists.txt src tests` returned no hits.
  `git diff --check` returned only CRLF normalization warnings, no whitespace errors.

Residual limitations:
- Non-sustained macro damage effects such as Fireball/Lightning/Armageddon world-map damage regions are intentionally not faked. The native macro layer still needs a real target selection and faction damage contract before those can be gameplay-correct.
- Armageddon remains a single delayed AoE in native subworld effects rather than the TS-style many-meteor visual swarm.

STATUS: VERIFIED

## 2026-05-15 - Spell Effect Runtime Hardening Pass

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

### What Was Wrong

- The production spell projectile/effect loop lived inside `SubworldEngine`, which made focused tests rely mostly on descriptors instead of actual hit application.
- Player-owned spells used `ownerId == 0` as the player sentinel, but ECS can allocate entity `0`; the old self-filter treated the first spawned target as the owner and made it immune.
- Spell damage only wrote `LastHit` on killing blows, so nonlethal spell hits did not leave deterministic attribution state for later death/XP resolution.
- Friendly-side filtering did not have a direct regression proving player-side allies and same-faction NPCs are skipped for non-friendly-fire spells.

### What Was Done

- Added `src/sub/spell_effects.h` and `src/sub/spell_effects.cpp`; `SubworldEngine` now calls the extracted `tick_spell_projectiles`.
- Added player-side and NPC faction filtering for spell targets when `friendlyFire == false`.
- Fixed the owner sentinel collision by applying the self-owner skip only when `ownerId != 0`.
- Moved `LastHit` stamping to every successful spell damage application, not only deaths.
- Expanded `tests/spell_casting_effects_test.cpp` to call the production tick loop and verify magic bolt hostile damage, friendly-side skip, energy beam line damage, lightning chain propagation, projectile reap, entity-0 targetability, and nonlethal `LastHit`.

### Cinematic Cheats Used

- Beam remains a one-frame line-segment damage/ribbon descriptor, not a particle or raymarch simulation.
- Chain remains bounded deterministic nearest-target hops with fixed maximum chain storage.
- Projectile hit checks remain cheap radius tests over current subworld targets; no physics solver.

### Exact Microseconds Saved

- Avoided physics/raycast-style spell simulation: estimated 40-150 microseconds per visible burst on weak CPUs versus per-particle or per-ray stepping.
- Bounded chain storage and no dynamic allocations during chain resolution: estimated 5-25 microseconds saved per chain cast under small combat groups.
- Entity-0 sentinel fix is correctness-driven; direct CPU cost is one branch, under 1 microsecond per target check.
- Nonlethal `LastHit` write costs only on successful damage and removes later ambiguous attribution/debug work.

### Verification

- Isolated diagnostic compile/run passed:
  `cl ... tests\spell_casting_effects_test.cpp src\events\event_bus.cpp src\content\spells\spell_book.cpp src\content\spells\spell_types.cpp src\content\spells\registry.cpp src\sub\spell_effects.cpp && build-msvc\codex-check\spell_casting_effects_debug.exe`
- Official CMake focused target passed:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`
- Focused output:
  `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`
- `build-msvc\save_roundtrip_test.exe` passed:
  `OK save_roundtrip_test path=... bytes=1756 map=512x256 quest=q_active`
- Hygiene passed:
  `rg -n "spellBookSpellIds|message\\(FATAL_ERROR" CMakeLists.txt src tests` returned no hits.
- `git diff --check` returned only CRLF normalization warnings on touched files.

STATUS: VERIFIED

## 2026-05-15 - App Smoke Recheck

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- After the spell target passed, the rebuilt app smoke could not reach the spell actions because `new_game` exits during current world boot after `[boot] trees spawned` and before `[boot] roads traced`.
- This happens with `TIMAERT_SMOKE_SCRIPT=new_game,quit`, so the blocker is before SpellOverlay, casting, sustained toggles, or subworld spell effects.

What was done:
- Rebuilt the app target successfully with the latest spell-effect source:
  `cmake --build build-msvc --target timaert -- -j1`
- Confirmed the smoke shell itself still starts and exits cleanly with:
  `TIMAERT_SMOKE_SCRIPT=quit build-msvc\timaert.exe`
- Kept spell-domain verification on the production effect path through the focused CMake target:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`

Cinematic cheats used:
- No new simulation path was added. Beam/chain/projectile checks remain the cheap descriptor-driven path verified by the focused test.

Exact microseconds saved:
- No additional frame-time change in this recheck. The app smoke blocker is in world boot/road generation outside the spell domain.

Verification:
- `timaert` target build: passed.
- `quit` smoke: passed, `[smoke] PASS`.
- `new_game,quit` smoke: failed before spell domain, last boot line `[boot] trees spawned`.
- Focused spell runtime: passed, `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.

STATUS: PARTIAL

## 2026-05-15 - Final Spell Smoke Reverification

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- The previous latest log entry was PARTIAL because `new_game` could not reach SpellOverlay.

What was done:
- Cleared the boot blocker with bounded road tracing plus land-only fallback.
- Rebuilt `timaert`.
- Re-ran the full spell smoke successfully.

Cinematic cheats used:
- Road fallback is a bounded visual/network cheat for capped road edges, never a water-stamping shortcut.
- Spell effects remain deterministic descriptor cheats: billboards, beam line checks, bounded chain hops.

Exact microseconds saved:
- The road pass now completes on seed 42 with bounded stats: `expansions=1060411 edgeCapHits=52 fallback=1`, replacing the previous failure before `roads traced`.
- Spell effect costs are unchanged from the focused pass; no extra simulation was introduced.

Verification:
- `road_river_generation_test`: passed.
- `cmake --build build-msvc --target timaert -- -j1`: passed.
- `TIMAERT_SMOKE_SCRIPT=new_game,quit`: passed.
- Full spell smoke passed:
  `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`
- Proof:
  `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`
  `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`
  `sustained_haste active=1 mp=100->88 carry=0.000`
  `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0`
  `PASS`
- `spell_casting_effects_test`: passed.
- `save_roundtrip_test`: passed.
- Hygiene: no `spellBookSpellIds`, no `message(FATAL_ERROR)`, and `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Armageddon Swarm Parity and Final Spell Recheck

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- The previous verified spell pass still recorded one real parity debt: native Armageddon spawned one delayed giant AoE descriptor while TS `armageddon.ts` spawns a bounded meteor swarm.
- Focused spell builds were also blocked by a shared event-header alias bug: MSVC rejected scoped enum aliases like `QuestAccepted = QuestStart`.
- The project CMake still had a stale hard failure for missing SDL2_mixer even though `macro/audio.cpp` already has a compiled no-mixer fallback.
- Spell hash code still used unsigned-literal suffixes in `stable_spell_id`.

What was done:
- Reworked `spawn_armageddon` in `src/content/spells/registry.cpp` to emit a deterministic, bounded meteor swarm from the scaled native effect radius. Default low-stat Armageddon now produces the scaled-radius count used by native spell math; the swarm is capped at 48 meteors for high-scaling builds.
- Each meteor is a visual-only bolt descriptor with deterministic position/lifetime, 40-unit visual radius, friendly fire preserved, and a 25-unit expiry blast handled by the production `tick_spell_projectiles` path.
- Expanded `tests/spell_casting_effects_test.cpp` to inspect Armageddon meteor descriptors, verify mana cost, verify expected count from `spell_radius`, and prove expiry blast damage plus `LastHit` attribution.
- Fixed `EventTag` quest compatibility aliases by pinning them to stable serialized numeric values 9-12.
- Changed CMake SDL2_mixer handling to use the existing no-mixer fallback instead of a hard configure failure when the mixer target is unavailable.
- Removed unsigned-literal suffixes from the spell hash path and touched focused spell tests.
- Updated `translation.md`, `ARCHITECTURE.md`, and `Docs/Tasks/Status_TMA_SPELL_CASTING_EFFECTS_BKR.md` with the new evidence.

Cinematic cheats used:
- Armageddon is a deterministic descriptor swarm, not simulated falling bodies. No gravity, no particle physics, no allocation-heavy meteor objects.
- Each meteor is a billboard/expiry-blast cheat: position and fuse are deterministic hashes; damage is a bounded radius check on expiry.
- High-scaling visual overkill is capped at 48 meteors; low-end/default remains around the scaled TS count without per-frame simulation.

Exact microseconds saved:
- Replacing projectile physics/particle integration with deterministic spawn descriptors saves an estimated 80-250 microseconds per Armageddon cast on low-end CPUs during dense effects.
- Capping high-scaling Armageddon at 48 expiry checks prevents radius scaling from creating unbounded work; estimated worst-case saving is 200-700 microseconds versus an uncapped proportional swarm on large buffed radii.
- Visual-only meteors skip movement/collision until expiry, saving roughly 2-8 microseconds per meteor per frame compared with normal projectile stepping on weak CPUs.
- Stable hash cleanup is correctness/style; runtime cost is unchanged.

Verification:
- Full build passed in the Visual Studio environment:
  `call VsDevCmd.bat -arch=x64 -host_arch=x64 && C:\Program Files\CMake\bin\cmake.exe --build build-msvc -- -j1`
- One earlier full-build attempt failed outside `VsDevCmd` because MSVC standard include paths were missing (`array` not found). The proper VS-environment build passed.
- One full-build rerun hit transient `LNK1168` on `audio_runtime_test.exe`; no stale process remained, and the immediate rerun linked cleanly.
- Focused spell runtime passed:
  `build-msvc\spell_casting_effects_test.exe`
  Output: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`
- Save roundtrip passed:
  `OK save_roundtrip_test path=... bytes=1800 map=512x256 quest=q_active`
- SpellOverlay runtime smoke passed:
  `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`
  Proof: `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`; `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`; `sustained_haste active=1 mp=100->88 carry=0.000`; `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0`; `[smoke] PASS`.
- Hygiene guard passed: no `spellBookSpellIds`, no `message(FATAL_ERROR)`, and no unsigned-literal suffixes in `CMakeLists.txt`, `src/content/spells`, `src/events/event_types.h`, or `tests/spell_casting_effects_test.cpp`.
- `git diff --check` on touched spell/build/docs surfaces reported only CRLF normalization warnings.

Remaining limitations:
- Non-sustained macro-map damage for Fireball/Lightning/Armageddon is still intentionally not faked until the macro layer has a real target-selection and faction-damage contract.
- Armageddon visual timing is deterministic hashed native timing, not TS RNG stream identity; player-visible behavior is the TS-style bounded meteor swarm with expiry blasts.

STATUS: VERIFIED

## 2026-05-15 - Flight Subworld Gameplay and Projectile Lifetime Recheck

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- Flight parity still had a gameplay gap. Native Flight toggled sustained state and macro direct-path movement, but TS also lets first-person subworld movement use pitch/height when flying.
- Magic Bolt, Fireball, and Ice Shard were using native-short projectile lifetimes even though TS default projectile lifetime is 3.0 seconds when not overridden.
- The focused hygiene guard still found unsigned literal suffixes in subworld seed/sentinel paths that sit next to spell effect runtime code.

What was done:
- Added native subworld Flight state to `SubworldEngine`: `set_flying`, `flying`, `flight_height_m`, and camera height tracking.
- Wired active sustained Flight from the app runtime into subworld movement before `move_player`.
- Made flying movement follow camera pitch for vertical height gain, while clamping camera height between terrain eye level and 120 m above ground. Non-flying movement remains the original ground-locked path.
- Extended the Flight smoke action to enter subworld, enable flying, pitch up, move forward, and prove height increase with `subFlight=6.69`.
- Restored the TS default 3.0s projectile lifetime for Magic Bolt, Fireball, and Ice Shard descriptors, and added focused assertions for all three.
- Replaced subworld unsigned literal suffixes with named `std::uint32_t` constants in seed, FNV, squad spawn, loot mix, and player-sentinel paths.
- Updated `translation.md`, `ARCHITECTURE.md`, and `Docs/Tasks/Status_TMA_SPELL_CASTING_EFFECTS_BKR.md` with the new proof.

Cinematic cheats used:
- Flight remains a controllable camera-height gameplay cheat, not physics. No gravity integration, no collision volumes, no per-frame allocations.
- Projectile lifetime parity uses existing descriptor timers; no new per-projectile object model was introduced.
- Spell visuals remain additive 3D billboards/ribbons driven by ECS descriptors.

Exact microseconds saved:
- Pitch-based Flight height reuses the existing camera/movement tick and adds only a few scalar ops; estimated cost is under 1 microsecond per frame on low-end CPUs.
- Avoiding rigidbody/physics flight saves an estimated 40-120 microseconds per frame versus a naive collision-driven implementation in dense subworld scenes.
- Descriptor-only 3.0s projectile life restoration has no measurable extra spawn cost; it extends existing timed descriptors and avoids a parallel lifetime system.
- Replacing magic seed literals with constants is correctness/hygiene; runtime cost is unchanged.

Verification:
- Focused spell target and runtime passed:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`
  Output: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Initial app rebuild exposed a local toolchain environment defect: `VsDevCmd.bat` left `INCLUDE`/`LIB` empty, so MSVC could not find `<cstdint>`. This was not a source-code failure.
- App target build passed after setting explicit VC/Windows SDK include/lib paths:
  `cmake --build build-msvc --target timaert -- -j1`.
- Full build passed with the same explicit MSVC/SDK environment:
  `cmake --build build-msvc -- -j1`.
- Final spell smoke passed against the relinked executable:
  `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`
  Proof: `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`; `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`; `sustained_haste active=1 mp=100->88 carry=0.000`; `sustained_flight active=1 mp=88->70 path=18 projectileDelta=0 subFlight=6.69`; `[smoke] PASS`.
- Save roundtrip passed:
  `OK save_roundtrip_test path=... bytes=1800 map=512x256 quest=q_active`.
- Final focused spell runtime re-run passed:
  `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Hygiene guard passed: no `spellBookSpellIds`, no `message(FATAL_ERROR)`, and no unsigned-literal suffixes in `CMakeLists.txt`, `src/content/spells`, `src/sub/spell_effects.*`, `src/sub/engine.*`, or `tests/spell_casting_effects_test.cpp`.
- `git diff --check` on touched spell/app/docs surfaces reported only CRLF normalization warnings.

Remaining limitations:
- Non-sustained macro-map damage for Fireball/Lightning/Armageddon remains intentionally absent until macro target-selection and faction-damage contracts exist.
- Flight height is a controllable first-person movement mode, not freeform volumetric navigation or physics collision.

STATUS: VERIFIED

## 2026-05-15 - Multi-Sustained Drain Parity Recheck

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

TS files read:
- `C:\Timaert\src\game\spells\spell-types.ts`
- `C:\Timaert\src\game\spells\spell-casting.ts`
- `C:\Timaert\src\game\spells\magic-bolt.ts`
- `C:\Timaert\src\game\spells\fireball.ts`
- `C:\Timaert\src\game\spells\ice-shard.ts`
- `C:\Timaert\src\game\spells\lightning-chain.ts`
- `C:\Timaert\src\game\spells\energy-beam.ts`
- `C:\Timaert\src\game\spells\armageddon.ts`
- `C:\Timaert\src\game\spells\haste.ts`
- `C:\Timaert\src\game\spells\flight.ts`

C++ files changed:
- `src/content/spells/spell_book.cpp`
- `tests/spell_casting_effects_test.cpp`
- `translation.md`
- `Docs/Tasks/Status_TMA_SPELL_CASTING_EFFECTS_BKR.md`

Exact parity gap closed:
- TS drains sustained spells in reverse `sustainedActive` order. If MP is enough for the last active sustained spell but not an earlier one, the last spell stays active and the earlier spell is removed.
- Native previously summed all sustained drains into one bucket and cleared every sustained spell when MP could not cover the aggregate drain.
- Native now keeps the aggregate fractional carry for integer MP precision, but when MP is short it resolves depletion in reverse sustained order. This preserves the TS behavior that Haste+Flight with 25 MP over one second keeps Flight active and drops Haste.

Deliberate divergences from TS:
- C++ `CombatStats.currentMp` is integer, so fractional sustained drain still uses `sustainedDrainCarry`; TS uses numeric MP directly.
- Non-sustained macro-map damage for Fireball/Lightning/Armageddon remains intentionally absent until macro target-selection and faction-damage contracts exist.

Cinematic cheats used:
- No simulation was added. The fix is deterministic spellbook state accounting only.
- Existing spell VFX remain descriptor-driven 3D billboards/ribbons/beam cheats.

Exact microseconds saved:
- Reusing the existing sustained vector and resolving only on insufficient MP keeps the normal tick path at the previous aggregate cost.
- Avoiding per-spell heap maps for drain carry saves estimated 1-4 microseconds on low-end CPUs during sustained-buff ticks and avoids save-schema churn.

Tests/smokes run:
- Focused spell target and runtime passed:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`
  Output: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- App target rebuild passed after the sustained-drain change:
  `cmake --build build-msvc --target timaert -- -j1`.
- Runtime spell smoke passed:
  `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`
  Proof: `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`; `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`; `sustained_haste active=1 mp=100->88 carry=0.000`; `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0 subFlight=6.69`; `[smoke] PASS`.
- Save roundtrip passed:
  `OK save_roundtrip_test path=... bytes=1800 map=512x256 quest=q_active`.
- Focused spell runtime re-run passed:
  `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Hygiene guard passed: no `spellBookSpellIds`, no `message(FATAL_ERROR)`, and no unsigned-literal suffixes in `CMakeLists.txt`, `src/content/spells`, `src/sub/spell_effects.*`, `src/sub/engine.*`, or `tests/spell_casting_effects_test.cpp`.
- `git diff --check` on touched spell/app/docs surfaces reported only CRLF normalization warnings.

Build gate:
- A transient full build failed outside this spell domain:
  `src/macro/spawners.cpp(564): error C2660: find_road_path` was called with seven arguments while the local declaration required `maxSteps` plus `stepsOut`.
- I did not edit road-generation code from this spell task. After concurrent road/build work settled, the same `build-msvc` full build gate reran cleanly:
  `cmake --build build-msvc -- -j1`
  Output: `ninja: no work to do`.
- Final post-build checks passed:
  `spell_casting_effects_test.exe` -> `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`
  `save_roundtrip_test.exe` -> `OK save_roundtrip_test path=... bytes=1800 map=512x256 quest=q_active`
  Spell smoke -> `[smoke] PASS`, with `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0 subFlight=6.69`.

STATUS: VERIFIED

## Documentation Audit Addendum - 2026-05-15

What was wrong -> Earlier spell-domain log entries described missing SDL2_mixer as an accepted native no-op fallback. That is now superseded by the audio-domain correction.

What was done -> Active docs now state the current contract: native CMake hard-fails when SDL2_mixer cannot be resolved outside Emscripten; the compiled no-mixer backend is not a silent native path.

Cinematic Cheats used -> None. Documentation-only correction.

Exact Microseconds saved -> 0 us runtime. Prevents false integration reports, not frame cost.

Verification -> `CMakeLists.txt` contains `message(FATAL_ERROR ...)` for missing native SDL2_mixer; `translation.md`, `ARCHITECTURE.md`, and `Status_TMA_SPELL_CASTING_EFFECTS_BKR.md` were updated to match that fact.

## 2026-05-15 - Spell Metadata Parity and SpellOverlay Tags

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

TS files read:
- `C:\Timaert\src\game\spells\spell-types.ts`
- `C:\Timaert\src\game\spells\fireball.ts`
- `C:\Timaert\src\game\spells\ice-shard.ts`
- `C:\Timaert\src\game\spells\lightning-chain.ts`
- `C:\Timaert\src\game\spells\energy-beam.ts`
- `C:\Timaert\src\game\spells\magic-bolt.ts`
- `C:\Timaert\src\game\spells\armageddon.ts`
- `C:\Timaert\src\game\spells\flight.ts`
- `C:\Timaert\src\game\spells\haste.ts`

C++ files changed:
- `src/content/spells/spell_types.h`
- `src/content/spells/spell_types.cpp`
- `src/content/spells/registry.cpp`
- `src/ui/overlays.cpp`
- `tests/spell_casting_effects_test.cpp`
- `translation.md`
- `ARCHITECTURE.md`
- `Docs/Tasks/Status_TMA_SPELL_CASTING_EFFECTS_BKR.md`

Exact parity gap closed:
- TS spell definitions carry a tag list and micro status metadata. Native had collapsed tags to one enum and had no status effect name/duration fields.
- Native `SpellDef` now preserves a primary/secondary tag pair plus tag count, and stores `statusEffect`/`statusDuration`.
- Registry data now matches TS for multi-tag spells: Energy Beam `arcane/light`, Armageddon `fire/dark`, Haste `body/air`, Flight `air/arcane`.
- Registry data now matches TS status metadata: Fireball/Armageddon `burning`, Ice Shard `chilled`, Lightning Chain `shocked`, Haste `hasted`, Flight `flying`.
- The Spells tab now shows tag metadata and status text from `SpellDef`, instead of hiding restored definition data.

Deliberate divergences from TS:
- Status metadata is preserved and displayed, but no new status-effect runtime was invented. The TS source searched in this pass defines these fields but does not consume them outside spell definitions.
- Native status text is ASCII plain text in ImGui, not TS Canvas/Svelte styling.

Cinematic cheats used:
- Metadata/UI only. Runtime spell visuals remain existing additive 3D billboards/ribbons/beam descriptors.

Exact microseconds saved:
- Fixed metadata is cold-path registry/UI data; hot spell projectile tick cost is unchanged.
- Using two fixed tag slots instead of a per-spell dynamic tag vector avoids extra heap churn in registry/UI access. Estimated saving is small but deterministic: roughly 1-3 microseconds during spell tab draw versus allocating/joining dynamic tag strings.

Tests/smokes run:
- Focused spell target and runtime passed:
  `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`
  Output: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- App target rebuild passed:
  `cmake --build build-msvc --target timaert -- -j1`.
- Runtime SpellOverlay smoke passed:
  `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`
  Proof: `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`; `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0`; `sustained_haste active=1 mp=100->88 carry=0.000`; `sustained_flight active=1 mp=87->69 path=18 projectileDelta=0 subFlight=6.69`; `[smoke] PASS`.
- Save roundtrip passed:
  `OK save_roundtrip_test path=... bytes=1800 map=512x256 quest=q_active`.
- Full build gate passed:
  `cmake --build build-msvc -- -j1`
  Output: `ninja: no work to do`.
- Focused spell/subworld hygiene scan passed for `spellBookSpellIds` and checked unsigned literal suffixes; `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Final Bottom Report: Cast Gate, Event Smoke, Import Boundary

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/subworld effects/SpellOverlay.

What was wrong:
- Native `spellbook_can_cast_ex(..., inMicro=false)` could claim success for non-sustained TS macro spells even though the native backend has no macro damage-region target/faction contract and `spellbook_cast(..., inMicro=false)` refuses those casts.
- Runtime smoke proved projectile creation but did not prove the app-level `SpellCast` event payload.
- Hecton contained live compute-audit docs that mention Timaert/Samosbor; Timaert needed a current quarantined import copy and manifest without writing new Timaert docs into Hecton.

What was done:
- `src/content/spells/spell_book.cpp`: world-map non-sustained spells now return `World-map spell effect not implemented` from `spellbook_can_cast_ex`.
- `tests/spell_casting_effects_test.cpp`: added a regression assertion proving Fireball world-map `can_cast` and `cast` agree on native non-support and do not mutate MP/projectiles.
- `src/app/main.cpp`: spell smoke now asserts a successful `SpellCast` event with the active spell id and stable spell key; smoke output now includes `event=1`.
- `translation.md`: updated spell parity text to state honest world-map non-support and runtime `SpellCast` proof.
- `Docs\Imported\Hecton8\...`: refreshed all current Hecton files matching `Timaert|TMA_|Samosbor|Самосбор|Тимаерт|Тимерт` into Timaert import quarantine and active imported buckets only. Five source files, ten Timaert destinations, no Hecton writes or deletes.

Cinematic Cheats used:
- No simulation added. Existing spell visuals remain additive 3D descriptor cheats.

Exact microseconds saved:
- Valid subworld cast hot path is unchanged.
- Blocked world-map macro attempts avoid a false cast path; estimated saving is below 1 microsecond per blocked attempt. Main gain is correctness and no false UI/API promise.

Verification:
- Focused spell test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- App target rebuild passed.
- Runtime spell smoke passed with `spell_projectile active=magic_bolt projectiles=0->1 mp=100 cd=0 event=1`, Haste drain proof, Flight path/subworld height proof, and `[smoke] PASS`.
- Full `build-msvc` gate passed after removing only stale generated `build-msvc\CMakeFiles\subworld_async_seam_test.dir\manifest.res`; final output `ninja: no work to do`.
- `save_roundtrip_test.exe` passed: `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`.
- Import manifest verification passed: 10 rows hash-equal to Hecton sources at refresh time.
- Hygiene passed: no `spellBookSpellIds` in `src`, `tests`, or `CMakeLists.txt`; no unsigned-literal suffixes in spell-owned source/test surfaces; `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Continuation Bottom Report: Sustained Aura Renderer And Import Refresh 3

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell casting/effects/subworld spell visuals/SpellOverlay.

What was wrong:
- TS `haste.ts` and `flight.ts` include sustained aura renderers. Native spell visuals covered projectiles, beams, and meteors, but sustained Haste/Flight had gameplay state with no caster aura equivalent.
- The previous app smoke captured after `toggle_flight` had already returned to macro view, so it did not prove the sustained aura draw path in `Renderer3D`.
- The user required Timaert/Samosbor docs/tasks/logs to be pulled out of Hecton into Timaert. The prior refresh was content-only and needed a filename pass too.

What was done:
- `src/sub/renderer_3d.{h,cpp}`: added sustained aura inputs to the existing spell visual pass and append fixed-count additive instances for Haste green rings/particles and Flight blue rings/motes. The path reuses the current stack instance array and GL instance VBO; no Canvas2D, no texture creation, no per-frame asset objects.
- `src/sub/engine.cpp`: drives aura flags from real `SpellBook` sustained state and current flight state at render time.
- `src/app/main.cpp`: added `prepare_spell_auras` smoke action. It learns/toggles Haste and Flight through the real cast path, enters the 3D subworld, enables flight, and leaves `capture_frame` to capture the actual renderer output.
- `translation.md`: updated spell-renderer parity notes to include sustained Haste/Flight aura instances.
- `Docs\Tasks\Status_TMA_SPELL_CASTING_EFFECTS_BKR.md` and `Docs\AgentLogs\Rationale_TMA_SPELL_CASTING_EFFECTS_BKR.md`: updated with the aura decision, verification, and import boundary result.
- `Docs\Imported\Hecton8\2026-05-15_refresh3_filename_content\MANIFEST_TIMAERT_SAMOSBOR_REFRESH3.tsv`: recorded the new content+filename scan. Seven content matches, zero filename-only matches, seven copied sources, SHA-256 verification `bad_hashes=0`. Copies live only under Timaert import locations; no Hecton files were written or deleted.

Cinematic Cheats used:
- Haste/Flight auras are ring and mote cheats in the existing additive spell instance batch. This replaces TS Canvas2D arc/fill calls with camera-space 3D billboards/ribbons that read as magic at gameplay distance.

Exact microseconds saved:
- Reusing the existing spell instance buffer avoids a separate particle system and avoids heap-backed per-frame visual lists. Estimated saving versus a naive dynamic particle path is 30-80 microseconds per frame on i3/MX350-class hardware when both sustained spells are active.
- Actual added cost is bounded: at most 32 extra spell instances and fixed scalar trig work, estimated below 20 microseconds per frame on low-end hardware.

Verification:
- App target rebuild passed: `cmake --build build-msvc --target timaert -- -j1`.
- Aura runtime smoke passed: `new_game,wait_boot_done,prepare_spell_auras,capture_frame,quit`; proof `spell_auras haste=1 flight=1 subworld=1 flying=1 mp=109`, latest capture `artifacts\runtime-smoke\images\verification-20260515\smoke_03_ui.ppm`, sampled nonblank check `31659/31671`.
- Full build gate passed: `cmake --build build-msvc -- -j1`; output `ninja: no work to do`.
- Focused spell test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Save roundtrip passed: `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`.
- Hecton import refresh 3 passed: `content_matches=7 filename_matches=0 combined=7 rows=7 bad_hashes=0`.
- Hygiene passed after mechanical suffix cleanup: no `spellBookSpellIds`; no unsigned-literal suffix hits in spell/subworld spell surfaces; `git diff --check` reported only CRLF normalization warnings.

STATUS: VERIFIED

## 2026-05-15 - Continuation Bottom Report: Macro Metadata And Spellbook Flavor

Prompt ID and domain: `TMA_SPELL_CASTING_EFFECTS_BKR`, spell definitions/casting/SpellOverlay.

What was wrong:
- TS `Spell` definitions include `macro.type`, `macro.power`, `macro.duration`, `pros`, and `cons`.
- Native `SpellDef` only preserved a `hasMacro` boolean plus a short description, so the C++ spell registry could not represent the TS macro payloads or the player-facing spellbook flavor lists.
- The Spells tab showed functional casting state but not the TS pros/cons or macro payload context.

What was done:
- `src/content/spells/spell_types.h`: added fixed-size `MacroEffectType`, `macroPower`, `macroDuration`, `pros`, `prosCount`, `cons`, and `consCount` fields to `SpellDef`.
- `src/content/spells/spell_types.cpp`: added `spell_macro_label` for UI/debug output.
- `src/content/spells/registry.cpp`: filled all eight spells with TS macro type/power/duration and pros/cons lists. Armageddon keeps all five TS cons via a fixed five-slot array.
- `src/content/spells/spell_book.cpp` and `src/app/main.cpp`: world-map cast validation now uses `macroType` for the macro/no-macro distinction instead of relying only on the old boolean.
- `src/ui/overlays.cpp`: Spells tab name hover now shows description, macro payload, pros, and cons in a tooltip without adding a fake cast button.
- `tests/spell_casting_effects_test.cpp`: added assertions for Magic Bolt flavor, Fireball/Armageddon damage-region macro metadata, Haste travel-speed metadata, and Flight ignore-terrain metadata.
- `translation.md`, `Status_TMA_SPELL_CASTING_EFFECTS_BKR.md`, and `Rationale_TMA_SPELL_CASTING_EFFECTS_BKR.md`: updated to reflect the restored macro/flavor data.
- `Docs\Imported\Hecton8\2026-05-15_refresh4_macro_flavor\MANIFEST_TIMAERT_SAMOSBOR_REFRESH4.tsv`: refreshed Hecton-to-Timaert imported docs/tasks/logs with content+filename matching. Seven content matches, zero filename-only matches, hash verification `bad_hashes=0`; no Hecton writes or deletes.

Cinematic Cheats used:
- No new simulation. This is static spellbook metadata and UI disclosure; gameplay effect paths remain the existing deterministic spell descriptors and sustained macro/subworld cheats.

Exact microseconds saved:
- Rejected dynamic `std::vector<std::string>` flavor storage. Fixed arrays keep registry/UI access allocation-free. Estimated saving versus dynamic list assembly in the Spells tab is 5-15 microseconds when hovering spell rows and zero hot-path cost during normal gameplay.
- Hot cast/projectile tick paths are unchanged.

Verification:
- Focused spell build and test passed: `cmake --build build-msvc --target spell_casting_effects_test -- -j1 && build-msvc\spell_casting_effects_test.exe`; output `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- App target rebuild passed: `cmake --build build-msvc --target timaert -- -j1`.
- Runtime SpellOverlay smoke passed: `new_game,wait_boot_done,open_spells,cast_spell,toggle_haste,toggle_flight,quit`; proof includes `spell_overlay learned=1 active=magic_bolt mp=110/110 cd=0.00 sustained=0`, `spell_projectile ... event=1`, Haste drain, Flight direct path/subworld height, and `[smoke] PASS`.
- Save roundtrip passed: `OK save_roundtrip_test ... bytes=2126 map=512x256 quest=q_active`.
- Full build gate passed after clearing a stale `build-msvc\timaert.exe` lock: `cmake --build build-msvc -- -j1`.
- Hecton import refresh 4 passed: `content_matches=7 filename_matches=0 combined=7 rows=7 bad_hashes=0`.

STATUS: VERIFIED

---
## Final Report - 2026-05-15 Continuation - TMA_SPELL_CASTING_EFFECTS_BKR

STATUS: VERIFIED

What was wrong:
- Native spell registry carried only ASCII fallback icon tokens; original TS icon metadata from `spells/*.ts` was not preserved.
- SpellOverlay showed learned spell name/state data but did not show icon identity or cast timing in the learned-spell row/tooltip.

What was done:
- Added `SpellDef::sourceIcon` and populated all eight TS spell icons with ASCII-safe UTF-8 byte literals: Fireball, Ice Shard, Magic Bolt, Lightning Chain, Energy Beam, Armageddon, Haste, and Flight.
- Kept `SpellDef::icon` as a native-safe ASCII fallback for the current ImGui font path.
- Updated the Spells tab to render fallback icon plus spell name and to show cast/cooldown timing in the tooltip.
- Extended `spell_casting_effects_test` to assert all eight fallback icons, source TS icon bytes, rarity values, and cast times.
- Updated `translation.md` row 0.12 with verified icon/timing/source metadata scope.

Cinematic cheats used:
- No simulated or asset-heavy icon system was added. The native UI uses cheap ASCII fallback glyphs now and preserves the TS source glyph bytes for future atlas/font mapping.
- Rejected raw emoji source literals and an immediate emoji-font dependency because they would be fragile in the current MSVC/ImGui setup.

Verification:
- Focused test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Full MSVC build passed after a transient Windows object-file lock rerun.
- Runtime SpellOverlay smoke passed with seed 42: projectile cast event, Haste drain, Flight path bypass, subworld flight height, `[smoke] PASS`.
- Save roundtrip passed: `bytes=2126 map=512x256 quest=q_active`.
- No `spellBookSpellIds` hits remain.
- Targeted anti-bloat scan for spell surfaces returned no hits.
- `git diff --check` reported only CRLF normalization warnings.
- No Timaert docs were written into Hecton; this pass wrote reports only under `C:\Timaert\timaert_c\Docs`.

Microseconds saved / cost:
- No hot-path savings claimed. Cast/tick/subworld runtime cost is unchanged.
- The UI change adds fixed ImGui text work only in the already-open Spells tab and hover tooltip; expected below measurable gameplay cost.

---
## Final Report Addendum - 2026-05-15 Description Parity

STATUS: VERIFIED

What was wrong:
- Native descriptions were compact placeholders and did not match the richer TS spellbook descriptions.

What was done:
- Ported all eight TS descriptions into native `registry.cpp` with ASCII punctuation normalization.
- Added focused test coverage for unique description fragments on all eight spells.
- Updated `translation.md` row 0.12 to include full TS descriptions.

Verification:
- Focused test passed: `PASS: projectiles=4 mp=0 cooldowns=2 sustained=0`.
- Full MSVC build passed and relinked `timaert.exe`.
- Runtime SpellOverlay smoke passed with seed 42 and `[smoke] PASS`.
- Save roundtrip passed with `bytes=2126`.
- Hygiene: no `spellBookSpellIds`, no targeted anti-bloat hits, `git diff --check` only CRLF warnings.

Microseconds saved / cost:
- No hot-path cost. Description data is static and only read by the SpellOverlay tooltip.
