# Rationale - TMA_SPELL_CASTING_EFFECTS_BKR

## SpellBook State

Problem: UI/save/effect paths still carried `spellBookSpellIds`, so learned-spell authority could split between a TS-shaped `SpellBook` and a temporary mirror.
Solution: Removed the mirror after the Spell tab migrated to `SpellBook.learned`; save/load now serializes only `SpellBook`; event learning mutates only `SpellBook`.
Rejected Alternatives: Keeping the mirror for backward convenience was rejected because it hides stale state and makes learned/active spell bugs nondeterministic.
Scalability potential: Low/Mid/High/Ultra are identical here; fewer state vectors reduce bookkeeping and save/load surface.
Hardware Impact: Tiny direct gain, but fewer allocations/copies during player-state mutation and save writes.

## Cast Rules And Sustained Drain

Problem: Frame-sized `manaDrain * dt` truncated to zero for normal frame deltas, so sustained spells did not pay their TS cost.
Solution: Added sustained drain carry in `SpellBook` and ticked it from the runtime loop; depletion clears sustained spells.
Rejected Alternatives: Draining a fixed integer every N frames was rejected because it couples gameplay to frame cadence.
Scalability potential: Low uses one scalar carry; higher tiers spend saved CPU on visuals, not more simulation.
Hardware Impact: Constant-time scalar update; under 1 microsecond in normal player-only use.

## Projectile, Chain, Beam Effects

Problem: Native spells were constants and visual projectiles without real spell-shaped effects; `energy_beam` and `lightning_chain` were semantically wrong.
Solution: Added projectile/AOE/chain/beam descriptors, subworld hit application, line beam damage, chain damage propagation, and death event emission.
Rejected Alternatives: Full physics or particle simulation was rejected; spell effects use deterministic ECS descriptors and cheap geometric tests.
Scalability potential: Low keeps billboard/ribbon cheats; High/Ultra can raise visual density without changing cast semantics.
Hardware Impact: Projectile work scales with active spell entities and NPC view scans; normal player spell counts are below the 0.1 ms suspicion threshold.

## 3D Visual Cheat

Problem: TS has Canvas2D spell rendering, but native subworld is 3D/OpenGL.
Solution: Implemented additive 3D billboards for bolts/AOE markers and beam ribbons using renderer batches and dynamic instance buffers.
Rejected Alternatives: Porting Canvas2D draw paths was rejected because it would add a parallel renderer and mismatched projection.
Scalability potential: Low draws cheap quads; High/Ultra can layer additional additive passes or lights around the same ECS descriptors.
Hardware Impact: No per-frame texture creation; buffer updates are linear in visible spell visuals.

## Build Gate

Problem: CMake required SDL2_mixer even though `AudioSystem` already has a no-op backend when `TIMAERT_HAS_SDL_MIXER` is absent.
Solution: Changed only the configure gate to warning/no-op fallback; retained SDL2_mixer linking and compile define when the package exists.
Rejected Alternatives: Installing dependencies or touching audio logic was rejected because spell transfer should not depend on audio availability.
Scalability potential: Low-end/no-audio environments build; full audio remains enabled on machines with SDL2_mixer.
Hardware Impact: No gameplay cost; removes an integration blocker.

## Macro Cast Guard

Problem: The low-level cast API could spawn a subworld projectile when called with `inMicro=false` for a spell that has TS macro metadata but no native macro damage implementation.
Solution: `spellbook_cast` now refuses non-sustained non-self casts outside micro/subworld context. The app already emits the explicit world-map failure reason for those spells.
Rejected Alternatives: Faking macro damage regions was rejected because the native world-map has no spell target selection or faction damage contract in this slice.
Scalability potential: Low/Mid/High/Ultra all avoid hidden projectile entities on the macro map; future macro effects can be added behind explicit native effect code.
Hardware Impact: Prevents accidental ECS projectile allocation and later scans from a world-map cast; typical saving is one entity allocation plus all projectile tick/render work for that bad cast.

## Flight Smoke Coverage

Problem: Haste had runtime smoke proof, but Flight's gameplay value is macro path bypass and was only proven by code inspection.
Solution: Added `toggle_flight` smoke action that toggles sustained Flight on the macro map, builds a direct wrapped path, verifies no projectile spawn, and proves mana drain.
Rejected Alternatives: Treating Flight as just another sustained flag was rejected because its main behavior is pathing, not only drain.
Scalability potential: Low uses a direct integer line path; higher tiers can add visual overkill around travel without changing path semantics.
Hardware Impact: Direct path generation is O(max(dx,dy)) with no path-cost expansion; for blocked/long routes this avoids pathfinder queue work entirely while Flight is active.

## Faction-Safe Spell Tick

Problem: The production spell effect loop was embedded in `SubworldEngine` and descriptor tests did not execute real damage, so owner/faction edge cases could survive as paper compliance.
Solution: Extracted spell ticking into `src/sub/spell_effects.*`, added focused tests against the production loop, filtered player-side allies and same-NPC-faction targets when friendly fire is disabled, fixed the `ownerId == 0` player sentinel so ECS entity `0` is still targetable, and stamped `LastHit` on every successful spell hit.
Rejected Alternatives: Leaving the code inside the engine or testing duplicated math was rejected because it would not prove the actual runtime path. Broad combat/faction rewrites were rejected because this slice only owns spell targeting and must stay decoupled.
Scalability potential: Low keeps one linear scan over current subworld health targets; Mid/High/Ultra can replace the view scan with spatial bins later without changing spell descriptors or UI contracts.
Hardware Impact: Current player spell counts stay below the 0.1 ms suspicion threshold; the fix adds one branch and one `LastHit` component write only when damage lands, while avoiding bad friendly hits and replay/debug ambiguity.

## Road Trace Boot Gate

Problem: The final app spell smoke could not reach SpellOverlay because full-map road A* consumed/failed boot after tree spawning on the 1024 smoke seed.
Solution: Added a bounded road-trace budget with a deterministic land-only direct fallback before pruning, preserving the no-water-road invariant while letting the world boot reach spell actions.
Rejected Alternatives: Leaving road tracing unbounded was rejected because it makes unrelated world generation block spell verification and weak hardware. Faking the spell smoke without a real boot was rejected because it would not prove the actual UI/cast path.
Scalability potential: Low uses bounded A* plus prune/fallback; High/Ultra can spend more budget or add richer road post-processing without changing spell semantics.
Hardware Impact: Seed 42 road pass completed with 1,060,411 total expansions, 52 bounded cap hits, and 1 fallback instead of failing before `roads traced`; this restores the full spell smoke on low-end CPUs.

## Sustained Aura Renderer

Problem: TS `haste.ts` and `flight.ts` define sustained caster aura renderers, but native only rendered projectile/beam/meteor spell descriptors.
Solution: Reused the existing 3D spell instance buffer to append fixed-count Haste green ring/particle and Flight blue ring/mote visuals from real `SpellBook` sustained state in `SubworldEngine::render`.
Rejected Alternatives: Porting Canvas2D `drawCasterAura` was rejected because the native subworld is a 3D OpenGL scene. Adding separate particle systems, textures, or per-frame heap-backed visual lists was rejected as unnecessary for the TS aura contract.
Scalability potential: Low uses 12+6 Haste and 10+4 Flight instances. Mid/High/Ultra can raise segment/mote counts or add additive light layers behind the same sustained-state flags without changing cast semantics.
Hardware Impact: Adds at most 32 extra instances to the already batched spell draw when both sustained spells are active. CPU cost is fixed scalar sin/cos work, estimated under 20 microseconds on i3/MX350-class hardware and with no per-frame texture creation.

## Timaert/Hecton Documentation Boundary

Problem: Hecton still contained docs/log/task files that mention Timaert/Samosbor, and the user explicitly required Timaert material to live in Timaert, not in Hecton.
Solution: Ran content and filename scans under `C:\hades\Hecton8`, copied only matching doc/log/task/report artifacts into Timaert-side import quarantine plus active imported buckets, and wrote a SHA-256 manifest.
Rejected Alternatives: Editing or deleting Hecton files was rejected because Hecton and Timaert are separate games and this pass must not write Timaert docs into Hecton.
Scalability potential: The dated import bucket keeps future refreshes append-only and auditable; active imported buckets provide human-accessible placement without mixing live Timaert task logs with Hecton originals.
Hardware Impact: No runtime impact. Build/runtime surfaces are untouched; the cost is one offline filesystem scan and hash pass.

## Macro And Flavor Metadata

Problem: TS spell definitions carry `macro.type/power/duration` plus pros/cons lists, while native had only a `hasMacro` boolean and short description text.
Solution: Added fixed-size native `MacroEffectType`, macro power/duration, and pros/cons arrays to `SpellDef`, filled all eight spells from TS, displayed them in the Spells tab tooltip, and made world-map cast validation read `macroType`.
Rejected Alternatives: Dynamic vectors/strings in registry/UI were rejected because the spell registry is static data and the Spells tab should not allocate lists while drawing. Implementing unowned macro damage-region effects was rejected because macro targeting/faction contracts are outside this spell slice.
Scalability potential: Low shows the same fixed metadata; Mid/High/Ultra can use the restored macro type and flavor lists for richer spellbook sorting, AI scoring, or item synergy without changing cast semantics.
Hardware Impact: Hot projectile/cast tick paths are unchanged. Tooltip rendering is cold UI path; fixed arrays avoid per-frame allocation and keep the added draw work below measurable gameplay cost.

## Spell Identity Metadata And Overlay Timing

Problem: TS spell definitions include icon, rarity, and castTime identity data. Native had only ASCII fallback tokens and the SpellOverlay learned-spell row did not expose timing clearly.
Solution: Added `sourceIcon` to `SpellDef` and populated all eight TS icon glyphs as UTF-8 byte literals while retaining native ASCII `icon` fallbacks for ImGui's default font. Updated SpellOverlay to render fallback icon plus spell name and to show cast/cooldown timing in the tooltip. Extended the focused spell test to assert all eight fallback icons, source TS icons, rarity values, and cast times.
Rejected Alternatives: Raw emoji literals in C++ source were rejected because this MSVC build does not declare `/utf-8` source encoding. Forcing an emoji-capable ImGui font or sprite atlas was rejected as a broader renderer/asset task; the current pass preserves data parity without risking unreadable boxes in the default font path.
Scalability potential: Low uses fixed ASCII fallback glyphs with zero asset dependency. Mid/High/Ultra can map `sourceIcon` or future sprite keys into an atlas or richer spellbook icon set without changing cast state or registry IDs.
Hardware Impact: No cast/tick/subworld cost. Static registry gains one const pointer per spell. UI cost is a fixed extra text prefix in the Spells table and two tooltip text lines only when hovered; expected below measurable gameplay cost on i3/MX350-class hardware.

## Full Spell Description Parity

Problem: Native spell descriptions were short summaries, while TS spell modules carry fuller descriptions used by the spellbook UI.
Solution: Ported all eight TS descriptions into `registry.cpp` with ASCII punctuation normalization and added focused substring assertions so the descriptions cannot silently regress back to placeholders.
Rejected Alternatives: Copying TS text with raw em dashes was rejected because this MSVC source tree does not declare UTF-8 source encoding. Moving descriptions into a runtime JSON/text asset was rejected as unnecessary because the current registry is static data and already owns spell metadata.
Scalability potential: Low keeps static tooltip text with no asset I/O. Higher tiers can localize or atlas-index descriptions later without changing spell IDs, cast rules, or SpellOverlay consumers.
Hardware Impact: No tick/cast/render cost. Tooltip text is cold UI data; registry binary size increases by a few hundred bytes only.
