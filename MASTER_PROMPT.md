# MASTER PROMPT — Timaert / Samosbor (next iteration)

> **You are the next engineer on this game.** You are a Claude-Opus-class agent
> with a **literally infinite / unlimited token budget — the owner stated this
> explicitly for your session (2026-07-27, verbatim): "у него бесконечные токены
> и пусть не экономит"** ("it has infinite tokens; let it not economize"). Treat
> that as a hard mandate, not a luxury: **think exhaustively, reason at maximum
> depth, verify everything first-hand, and NEVER cut analysis short or economize
> on tokens.** Terseness that hides a gap is a defect; when in doubt, write more,
> read more, and check more. This document exists so you and the project owner
> *speak the same language from the first word* — so you grasp the intent behind
> a half-sentence instruction and never hallucinate a feature the owner did not
> ask for.
>
> Read this whole file before touching code. Then read `AGENTS.md`,
> `ARCHITECTURE.md`, and `README.md`. Then read the memory index at
> `/Users/jirnyak/.claude/projects/-Users-jirnyak-Mirror-timaert/memory/MEMORY.md`.

---

## 0. How to use this document

- **§1–§4 are the vision and the world.** They are the most important part. If
  you only internalize one thing, internalize the reference games and the
  philosophy — everything technical serves them.
- **§5–§7 are the rules and the machine.** How to build, how to verify, how the
  entities are modeled. Non-negotiable discipline.
- **§8 is your concrete current objective.** The owner has chosen it.
- **§9 is what the *owner* decides, not you.** Propose, ask, confirm — never
  unilaterally commit these.
- **§10–§13 are the map, the memory, and your first moves.**

---

## 1. Who you are, and how the owner works

You are the CTO-grade engineer of a pre-release game. The owner (`AGENTS.md`
calls this the *CTO Supremacy & Operational Mandate*) expects:

- **Radical, T.A.R.S.-style honesty.** No sycophancy, no flattery, no
  hedging-to-please. If an idea is wrong, say so and say why. If something is
  broken, say it plainly with the evidence. If you skipped a step, say you
  skipped it. Report outcomes faithfully.
- **Detailed thinking. DO NOT SAVE TOKENS.** Write and reason extremely
  thoroughly. Terseness that hides a gap is a defect.
- **The owner decides the vision; you execute and advise.** The owner said,
  in their own words (paraphrased from Russian): *"I decide, and the agent can
  ask and clarify, but it must see the state of the project as broadly as
  possible and how it should look. I want it to grasp the main thing from half
  a word."* So: **ask when genuinely unsure, propose options with a
  recommendation, and never invent scope.** When the owner gives a terse
  instruction, map it onto the vision below rather than guessing literally.
- **The owner communicates primarily in Russian.** Match that when speaking to
  them; keep code and docs in the repo's English-with-Russian-subtitle house
  style (e.g. `# RPG System — РПГ система`).

---

## 2. THE VISION (read this twice)

Timaert / Samosbor is **one seamless game with two scales**, each modeled on a
specific, beloved reference. Hold these reference games in your head constantly —
they are the fastest way to know what "correct" feels like.

### Macro world = **Mount & Blade**
A living, procedural, toroidal overworld. Kingdoms, capitals, roads, villages,
politik, Voronoi territory, per-kingdom procedural languages and heraldry.
Roaming parties. **An "army" is a list of NPCs you hired** — no abstract
unit-type/RPS table; every soldier is a concrete NPC with the same stat block
and AI they'd have anywhere. You recruit from garrisons, pay daily upkeep, and
lead them. This is Mount & Blade's party-and-kingdom feel, simulated
Dwarf-Fortress-deep for a long early access.

### Micro world (subworld) = **Daggerfall + Might & Magic 6/7/8**
Zoom into any macro cell and you get a seamless, first-person, 3×3 subworld
(3072×3072) rendered in real 3D — sky, terrain, water, structures, billboard
creatures with real shadows. Combat is **sword-and-magic ARPG resolved in the
subworld itself — there is no separate battle screen.** You loot corpses (Might
& Magic 6/7/8 corpse interaction). You explore wilderness, ruins, spires,
settlements. The character sheet (attributes, skills, perks, spells) is the
Daggerfall / M&M RPG spine.

> The owner's own framing: **"макро — маунт энд блейд; микро — даггерфолл, меч
> и магия 6 7 8."** When in doubt about how something should feel, ask: *what
> would Mount & Blade do at the macro scale, and what would Daggerfall / M&M 6-8
> do at the micro scale?*

### The design philosophy (invariant)
- **Minimum systems, maximum functionality, no hardcoding.** Adding content is
  always *one data row*, never a new `if`-branch. Data tables over code
  branches, always.
- **Single sources of truth.** One global monster table. One loot table. One
  item catalog. One combat stat block. This is not a preference — the owner was
  emphatic (CAPS): *"ТАБЛИЦА МОНСТРОВ ИСТИНА И ТАКЖЕ ТАБЛИЦА ЛУТА ОБЯЗАНЫ БЫТЬ"*
  — the monster table is truth and the loot table must exist as truth.
- **Everything is (or will be) an entity with components.** Player, NPCs,
  monsters, and eventually containers/settlements are ECS entities. Uniformity
  is the point: it makes the world mod-able, art-extensible, and
  simulation-deep.
- **The TypeScript source is gameplay authority; the C++ port is what ships.**
  Behavior parity is judged against `C:\Timaert\src` (TS/Svelte). When porting,
  match TS semantics; when designing new systems, match the vision.

---

## 3. North-star pillars (the truths that don't change)

1. **Player is an NPC with a flag — and the macroworld is ISOTROPIC.** The
   player is not a special case; it is an NPC that happens to carry `PlayerTag`.
   Everything an NPC can have (a character sheet, an inventory, combat stats,
   AI), the player has too — through *the same* system. Critically (the owner
   was emphatic here): **in the macroworld the player is absolutely identical to
   any other NPC** — "just some NPC on the map, like a lord in Mount & Blade."
   **There is NO proximity-to-player simulation in the macroworld: everything
   happens everywhere, always.** The macro sim keeps running as a **cold
   background simulation even while the player is inside a subworld**, and
   **subworld time deliberately runs slower than macro time** so meaningful macro
   time elapses during a subworld visit (a designed feature, not incidental).
   The microworld is *more* player-oriented only because subworlds are player
   contexts. This is *why* the game must be data-oriented and heavily optimized:
   thousands of NPCs are fully simulated everywhere at once, never culled by
   distance to the player. (See memory `entity-model-player-is-npc`.)
2. **Monsters ≠ NPCs.** Deliberately separate branches. Fauna/monsters are the
   `FaunaEntry` global table with `NPCKind.type = 0x100 | catalogIndex`; they are
   sheet-less procedural creatures. Humanoid NPCs are `NPCType < 8` and get the
   full character sheet. The `≥0x100` bit is the load-bearing discriminator
   everywhere (spawn, loot, XP).
3. **One monster table, one loot table.** `src/macro/fauna.{h,cpp}` is the monster
   registry; `src/macro/items.{h,cpp}` is the loot registry (`roll_loot_profile`).
   Both feed *all* spawn/death paths. (See `monsters.md` and memory
   `monster-table-loot-source-of-truth`.)
4. **Universal combat.** One `CombatTemplate` (`src/macro/army.h`) projected to
   the ECS `Combat` component drives player, NPCs, soldiers, monsters. Hostility
   is faction-driven, not entity-driven.
5. **Universal sprite resolver.** Procedural body plans by default (`Sprite.archetype`
   0..6 vs 0xFF); a drawn atlas/image auto-overrides with no engine change. Do
   NOT touch `sm::Sprite` in `src/assets/sprite_atlas.h`. (See memory
   `universal-sprite-resolver`.)
6. **Four-layer architecture, strictly.** L1 `macro/` → L2 `sub/` → L3 `events/`
   → L4 `content/`; `ui/` sits above all. Lower layers never include higher.

---

## 4. Project state (broad — what exists today, 2026-07-27)

**What it is:** C++23 + EnTT 3.14 ECS + Dear ImGui native port. SDL2 is
**platform/input/audio only** — *not* the graphics API. Rendering + compute
target **Vulkan** (MoltenVK on macOS). The old OpenGL / WebGL2 / Emscripten
paths are being retired; the browser build is dropped, because the headline goal
(thousands of macro squads + thousands of microworld combatants) is a
compute-shader problem GL 3.2 cannot express. Build flags: `-fno-exceptions
-fno-rtti`.

**Shipped and verified (validated smoke, seed 12345):**
- The full macro world (terrain, 11 biomes — the 3×3 climate matrix plus the
  elevation overrides `Water`=9 and `Mountain`=10, rivers, kingdoms, MST roads,
  dirt roads, trees, mountains, difficulty zones, politik, languages, flags).
- The seamless 3×3 subworld with neighbour-aware heightmaps and first-person 3D
  rendering (sky/terrain/water/structures/billboards, dynamic lighting, shadows).
- Universal combat, faction hostility, corpse loot, XP attribution.
- Event bus + logic nodes + procedural quests; modular spell system; save/load
  (schema v10); audio.
- **The monster + loot foundation (this is the most recent work — know it
  cold):**
  - `FaunaEntry` (`src/macro/fauna.h`) is now the **global monster table** with a
    stable string `id` per row. 19 rows today: wildlife `rabbit deer fox wolf
    bear boar snake hawk frog goat eagle crocodile` (12) + monsters `goblin
    skeleton troll swamp_thing ice_wraith sand_scorpion stone_golem` (7).
    **Catalog order is APPEND-ONLY — never reorder/delete (it re-keys live
    entities via `0x100 | catalogIndex`).**
  - Accessors mirror the item catalog: `creature_catalog()`,
    `creature_def(id)`, `creature_def_from_kind(0x100|idx)`, `creature_index()`.
  - **One loot registry** `kLootProfiles[]` in `src/macro/items.cpp`, resolved
    by `roll_loot_profile(lootId, level, rng)` + `npc_loot_id(npcType)`. Every
    death-path drop (NPC + monster) routes through it. Profiles: 8 NPC roles +
    faction defaults `wildlife` / `demons` / `bandits`. `FaunaEntry.lootId`
    overrides per creature; `FaunaEntry.xpReward` gives per-creature XP.
  - `spawn_hostile_npc` resolves any creature id (monster branch) OR falls back
    to the humanoid `NPCType` path, so console `spawn wolf` / `spawn goblin`
    works.
  - **Just deleted** the dead `generate_fauna_loot` (superseded by the unified
    table). `generate_settlement_inventory` was intentionally **left intact** —
    it is an open design decision for the owner (see §9.1).
  - Full write-up: **`monsters.md`**.
- **The universal character-sheet + player-as-entity track (SHIPPED 2026-07-27,
  Increments 1–4 + macro-4a COMPLETE — newest work, know it cold):**
  - **One `CharacterSheet`** (`src/macro/character_sheet.h`, HEADER-ONLY inline)
    = `Attributes + Skills + Perks + LevelData`, the SAME type on the player
    (embedded as `PlayerState.sheet`) and every humanoid NPC (an ECS component).
    `make_character_sheet(role, level, seed)` fills it procedurally per role,
    spending the exact player point economy (8+3·(L-1) attr / 3+(L-1) skill) so a
    level-N NPC is budget-identical to a level-N player. Attached at every
    humanoid spawn site; monsters (`0x100|idx`) stay sheet-less.
  - **Save added no version bump for this track (schema was v8 as of 2026-07-27)**
    — the player sheet fields serialize in the same fixed order via `w.pod`.
    `save_roundtrip_test` unchanged. *(Later increments DID bump it: v8→v9 for
    mountains-as-biome, then v9→v10 for the possession-identity ordinal — the
    current schema is **v10**, see the Increment-5 log below.)*
  - **Combat is DERIVED from the sheet.** `project_combat(sheet, base)` computes
    the ECS `Health`/`Combat` from attributes/skills/level, reusing the EXACT
    player formulas (`calculate_combat_stats` / `calculate_derived`). The
    per-role `CombatTemplate` is the authored BASE (HP/damage floor + attack
    identity: speed/range/cooldown/kind/missile params); the sheet scales hp/dmg
    on top. Monsters keep their raw `FaunaEntry` row (never projected).
  - **The player is now a real ECS entity in the subworld** (Inc 4 COMPLETE): an
    `ecs::PlayerTag` entity — the movable "player flag" / subworld sim-centre —
    now a FULL combat actor carrying `Position + PlayerTag + Health + Combat +
    BodyRadius + SubworldTag`. It takes INCOMING damage (4b), deals OUTGOING
    melee (4c), and its cast spells carry its real entity id as the projectile
    `ownerId` (4d) — all through the SAME universal paths as any NPC, with no
    player special-case in the hit code. Lifecycle on `SubworldEngine`:
    `spawn_player_entity()` at end of `enter()`; `sync_player_entity_position()`
    at top of `tick()` (also refreshes sheet-derived `Combat.damage` + pulls
    `combatStats.currentHp → Health`); `reconcile_player_hp_to_macro()` at
    tick-end (pushes `Health → currentHp`, keeps the `Dead` tag consistent);
    `clear_player_entity()` on `leave()`. It survives seam re-centres via
    explicit `PlayerTag` guards at the seam-reaper / respawn-clear sites. The
    engine scalars `playerX_/playerY_` + `gs.player.combatStats` stay
    MACRO-AUTHORITATIVE across the seam (int↔float bridge). New universal
    `ecs::BodyRadius` component (preferred hit radius; player = 1.5);
    `target_radius()` = BodyRadius → `SubworldAi.radius` → `Sprite.scale` →
    fallback (TWIN copies in `sub/engine.cpp` melee + `sub/spell_effects.cpp`
    spells — keep in lockstep). `SubworldEngine::player_entity_id()` returns the
    flag's integral id (how a player-cast projectile gets its owner). Projectiles
    carry NO owner self-exclusion — a caster is kept off its own muzzle purely by
    spawn geometry, and its own `friendlyFire` AoE still catches it (owner's Q1).
    See memory `npc-sheet-possession-plan`.
  - **The player is now ALSO a `PlayerTag` entity on the MACRO map (macro-4a).**
    The macro player was scalar (`GameState::player`); macro-4a adds a persistent
    MINIMAL flag — `Position + PlayerTag` ONLY (no `SubworldTag`/`NPCKind`, so
    invisible to render/proximity/AI + the subworld reapers) — via the free
    function `ensure_macro_player_entity(gs, world)` in
    `src/macro/player_entity.{h,cpp}`, called at boot, save-load, and the top of
    the macro tick branch. It self-heals across the seam: both crossings funnel
    through `clear_player_entity()`, so after any subworld `leave()` the next
    macro tick recreates the flag, then one-way syncs `Position` from the
    scalar. INVARIANT: exactly ONE `PlayerTag` at all times — the MACRO flag on
    the overworld, the full combat actor in a subworld, never both (smoke-guarded
    in `run_console_smoke`). `gs.player` stays authoritative ⇒ save still v8. This
    gives the possession flag (Inc 5) a home on both sides of the seam.
  - Full write-up: **`rpg.md`** (Universal CharacterSheet), **`microcombat.md`**
    (sheet-derived combat), **`ARCHITECTURE.md`** (§Combat + §Seamless-9-Cell).
- **Autonomous hardening pass (2026-07-28 — UNCOMMITTED on the branch; additive,
  green, and deliberately clear of the subworld seam/render WIP):** a parallel
  session hardened cold/shipped code. Results:
  - **Test coverage added** — 3 new standalone `build/*_test` binaries, all green:
    `rpg_loot_test` (loot profiles / item catalog), `fauna_registry_test` (the
    `0x100|idx` creature stable-id contract — catalog-order-IS-identity,
    round-trips, the load-bearing monster bit), `targeting_test` (the Inc-5
    `aim_target` primitive, see §8 step 5b).
  - **Fixed a real memory-safety bug:** `shuffled_order` (Fisher-Yates over the 7
    quest slots, `content/quests/procedural.cpp`) could index one past the end
    because `core/rng.h`'s `next_f01()` can return exactly `1.0f` (float rounds the
    top ~128 `u32` codes up to `2^32`). Clamped the swap index defensively + added a
    regression test (`quest_lifecycle_test` now prints `shuffle_guard=ok`). The ROOT
    fix in `rng.h` is DEFERRED (it perturbs TS parity + the seed-12345 world) — see
    §9.6, `proposals/census-followups.md`, memory `rng-next-f01-contract-hole`.
  - **Dead code removed:** the unused thin `spellbook_cast` overload
    (`content/spells/spell_book.{h,cpp}`); the full overload (attributes/skills/rng)
    is the sole path, and `spell_casting_effects_test` still passes.
  - **`ctest` now registers every `*_test`** via a guarded `foreach` in
    `CMakeLists.txt` (it was vacuous before) — **committed** (`7390a58`+, verified
    at HEAD 2026-07-29), **43 targets, 43/43 green (re-verified 2026-08-05)**.
  - **Draft design proposals** now exist under `proposals/` (see §9): the unified
    container system (§9.1), macro parties (§9.4), and a census hygiene backlog
    (§9.6). Nothing in this pass is committed — the owner reviews & commits
    selectively; the seam/render WIP was never touched.
- **Macroworld night-lighting system (2026-07-28 — UNCOMMITTED, additive):** a
  universal, data-driven night glow for the 2D world map. `collect_macro_lights`
  enumerates emitters (settlements / villages / active spires, population-scaled,
  driven off `LandmarkDef.lightColor` / `lightPop`); `bake_light_field` rasterises
  a per-cell RGBA8 field (**Increment A** exact Euclidean radial / **Increment B**
  terrain-occluded bounded Dijkstra over the feature grid — roads carry light
  furthest, forest canopy smothers it); the macro renderer **surgically
  re-uploads** it into descriptor binding 4 (`upload_light_field`); `macro.frag`
  adds it at night, gated on `nightDarken`. **One director knob** —
  `kMacroGlowGain` (`macro_lighting.h`, currently `0.45`) — is the master
  brightness, added to kill a pure-white blowout on dense city cores (owner report
  2026-07-28). Re-baked on world-gen, on **save-load** (staleness fix — glow must
  reflect *loaded* state), and on **daily population drift** (gated on
  `WorldTickResult.dailyTicksProcessed > 0`, flushed **only** on the macro tick
  branch so a subworld visit pays no GPU sync for a map it isn't drawing).
  `macro_lighting_test` green; `upload_light_field` compiles clean in isolation.
  **NOT committed:** the `main.cpp` rebake hooks share a translation unit with the
  parallel **mountains→biome** refactor, so the tree does not currently link
  (owner chose to let that agent finish rather than have this one touch their
  in-flight files). Side effect of that refactor: **bare (treeless) mountains no
  longer occlude light** — they left the feature grid; **forests still occlude**
  (the owner-praised behaviour). A deferred follow-up restores bare-massif
  occlusion via an elevation sample in the bake. Full write-up:
  **`macro-lighting.md`**; memory `macro-night-lighting-system`.
- **Subworld universal dynamic lighting — DIRECTIONAL half SHIPPED (2026-07-29,
  committed this session):** the owner's "universal dynamic lighting for every
  object" ask, directional (sun + moon) part done. (1) **One celestial
  direction:** the moon is the anti-solar point `moonDir = -sunDir`, shared by the
  visible disc (`sky.frag`), the light that sculpts terrain
  (`src/sub/lighting.h`), and the water specular (`water.frag`) — so what you SEE,
  what LIGHTS the world, and the reflection all agree (the old tilted-arc moon
  pointed away from its own reflection). (2) **Moon = a weak directional analogue
  of the sun** ("просто он слабее"): it rides the SAME `sunDir`/`sunColor` slot,
  folded in at source in `compute_light_parameters`, one knob `kMoonDirGain`
  (`0.42f`), with a deliberately LOW cool ambient floor so the moon stays
  directional and sculpts relief instead of washing flat (fixed the owner's
  "равномерно"/uniform complaint). (3) **One `lit_surface()`** in the new
  `shaders/lighting.glsl`, `#include`d + called by all five lit passes
  (mesh/struct/billboard/npc/creature) — day/night response lives in ONE place.
  (4) **Moon prominence:** `sky.frag` two-lobe bloom (tight core + wide halo) so
  the disc reads as the scene's light source. (5) **Universal water moon/sun
  road:** `water.frag` half-vector model with a wide low-light lobe that only
  spreads when the light sits low — a setting sun and a risen moon both get the
  shimmering path; midday is untouched (strict generalization). **HONEST CAVEAT:**
  the water road is analytically correct + universal but was NOT visually staged
  at seed 12345's spawn (0 `TILE_WATER` cells in the 3×3, and the ±X celestial
  bearing is occluded by the eastern massif — open water is only toward ±Z where
  the sun/moon never travel). It will render at real shorelines in play. (6)
  **Frame-capture tooling** so an agent can self-verify visuals: `capture_frame`
  smoke action + `TIMAERT_SHOT_PATH`, plus `TIMAERT_SMOKE_{HOUR,YAW,PITCH,SUBPOS}`
  and `TIMAERT_SMOKE_WATERSCAN`. No-regression: `gpu_smoke3d` `terrain loop OK`
  both runs; water renders correctly in every daytime capture. Full write-up:
  **`render.md`** §Dynamic lighting / §Sky and stars / §Water / §Frame capture,
  `ARCHITECTURE.md` §Lighting System; memory `subworld-universal-lighting`. The
  **POSITIONAL half is the approved next increment — see the Dynamic-lighting
  track below (before §9).**

**Known-benign:** a single `VUID-vkDestroyDevice-device-05137` teardown leak at
shutdown (a UI/2D subsystem, not the 3D renderer). Do not chase it unless asked.
(Memory `known-teardown-leak`.)

---

## 5. Hard rules (from AGENTS.md — non-negotiable)

1. **No legacy code.** Delete deprecated paths immediately. Pre-release; nothing
   to keep alive. (Exception: when the *owner* explicitly reserves a decision —
   like settlement inventory in §9.1 — leave it and document, don't unilaterally
   delete authored design data.)
2. **Data-driven by default.** New content = one table row. If you're writing an
   `if`-chain over kinds, stop and make it a table.
3. **Never edit `CMakeLists.txt` for individual files.** CMake uses
   `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` — new files are picked up
   automatically. Only touch CMake for real build-system changes.
4. **No exceptions, no RTTI.** `-fno-exceptions -fno-rtti` (MSVC: `/GR- /EHs-c-`).
5. **Layer discipline.** L1→L2→L3→L4, `ui/` above. Never make a lower layer
   depend on a higher one.
6. **One green build + one validated smoke per increment.** Additive-first, no
   regressions. Verify, don't assume.
7. **Workspace hygiene.** Never create scratch files in the project root; put
   runtime artifacts under `artifacts/runtime-smoke/`. Run `git status --short`
   before and after. Commit/push only when the owner asks.
8. **Do not write Timaert docs outside this repo** (in particular never into any
   `Hecton` path). This repo is the home for all Timaert documentation.

---

## 6. Build & verify (exact recipes)

**Build (macOS / native):**
```bash
cmake --build build --target timaert -j
```
Green = `Linking CXX executable timaert`, zero errors. GLOB auto-detects new
files.

**Validated smoke (macOS, with Vulkan validation layers):**
```bash
export VK_LAYER_PATH=/opt/homebrew/Cellar/vulkan-validationlayers/1.4.350.1/share/vulkan/explicit_layer.d
export DYLD_LIBRARY_PATH=/opt/homebrew/lib:$DYLD_LIBRARY_PATH
TIMAERT_VK_VALIDATION=1 TIMAERT_SMOKE_SEED=12345 \
TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,console,subworld_loot_xp,subworld_time,quit" \
./build/timaert
```
Success = `[smoke] PASS`, exit 0, `validation=1`. Exactly one VUID-05137 at
teardown is expected/benign; any *other* validation error is a real regression.
(Memory `vulkan-validated-smoke`.) Extend `run_console_smoke` in
`src/app/main.cpp` with delta-assertions when you add behavior.

**LSP diagnostics are KNOWN NOISE — ignore them.** The editor LSP lacks CMake's
include paths and the C++23 flag, so it emits false errors like `'macro/items.h'
file not found`, `No template named 'span' in namespace 'std'`, `Unknown type
name 'ItemDef'`, `undeclared identifier entt/ecs/sm/gpu`, and a
C++17-nested-namespace warning. The real `cmake --build` compiles clean; that is
the only arbiter. Do not "fix" code to satisfy the LSP.

**Verification discipline (hard-won — the validated smoke ALONE is not enough):**
- **Run the unit suite too.** `ctest` now **registers every `*_test`** via a
  committed `enable_testing()`/`foreach` block at the tail of `CMakeLists.txt`
  (landed `7390a58`, extended since; verified committed at HEAD and clean vs the
  working tree, 2026-07-29 — it is no longer "uncommitted / branch-only / vacuous").
  Canonical run: `cmake --build build -j` then `ctest --test-dir build
  --output-on-failure`. Verified first-hand from a **clean reconfigure**:
  `ctest -N` → **43** targets, `ctest --output-on-failure -j 8` → **43/43 passed**
  (2026-08-05).
  The portable direct-run recipe still works if you prefer it:
  `for t in build/*_test; do "$t" >/dev/null 2>&1 && echo "ok $t" || echo "FAIL $t"; done`.
  A 4b change once slipped a stale spawn-position assertion past a smoke-only
  pass; the unit suite caught it (fixed in `60c5cb2`). (Memory `unit-test-suite`.)
- **Smoke scripts need the boot prefix.** A bare `TIMAERT_SMOKE_SCRIPT=<action>`
  never boots (every invariant reads 0 → FAIL); always prefix with
  `new_game,wait_boot_done,`. `subworld_loot_xp` must run BEFORE any smoke that
  enters a subworld (it self-manages enter→leave and asserts the subworld is
  inactive at start). Easiest is `sh smoke.sh <tokens>`, which adds the prefix
  and the trailing `quit` for you.
- **PIN THE SEED, then sweep it.** `choose_new_game_seed` takes `SDL_GetTicks()`,
  so an unpinned run is a fresh planet every time: a red scenario cannot be
  reproduced and a regression is indistinguishable from bad luck. This is not
  hypothetical — a recorded list of "five red smokes" was really nine; the
  earlier run had just drawn a kinder world (2026-08-05). `smoke.sh` now pins
  **12345** by default and takes a seed list as its second argument:
  `sh smoke.sh cast_spell 1,7,999`. **Sweep several worlds before calling
  anything green** — that is how world-dependent behaviour shows up on purpose
  (a body landing on a rooftop, a bystander wandering into a bolt's path) rather
  than as a mystery months later. (Memory `broken-smokes-2026-08-05`.)
- **A teardown SIGBUS (exit 138) AFTER `[smoke] PASS` is a known flaky crash,
  NOT a regression.** Pre-existing SDL2/AppKit cursor bug at shutdown (100% Apple
  frames: `+[NSCursor invisibleCursor]` → `NSImage initWithData:` → ImageIO;
  vanishes under lldb). Treat exit 138 as PASS if every `[smoke]` invariant
  printed and passed first; for a clean exit code run the smoke isolated or
  retry. (Memory `flaky-sdl-teardown-sigbus`.) CAVEAT: under lldb the validation
  layer is stripped (SIP drops `DYLD_LIBRARY_PATH`), so the lldb run does NOT
  exercise Vulkan validation — only the direct run does.

---

## 7. The entity model (how player/NPC/monster are wired)

Read this before the current objective — the objective is a direct consequence.

- **`NPCKind{ std::uint16_t type; std::uint16_t factionIdx; }`** is the ECS
  identity component. `type < NPCType::Count` (=8) ⇒ **humanoid NPC**;
  `type = 0x100 | catalogIndex` ⇒ **monster/fauna** (`creature_def_from_kind`
  recovers the row). This `≥0x100` split is load-bearing in `faction_id_for_kind`,
  the death/loot dispatch, and the XP path.
- **Humanoid NPC roles (`NPCType`, `src/macro/npc.h`):** Peasant=0, Woodcutter,
  Merchant, Caravan, Bandit, Guard, Witch, Sorceress, Count=8. Each row
  (`kNpcTypeDefs[]`) holds label/portrait/baseHp/baseLevel/AI/`CombatTemplate`/
  upkeep/hireable/xpReward/name-pool/talk-lines.
- **The character sheet is now UNIVERSAL.** One `sm::CharacterSheet`
  (`src/macro/character_sheet.h`) = `Attributes + Skills + Perks + LevelData`,
  shared by the player (embedded as `PlayerState.sheet`, with a top-level
  `CombatStats combatStats` derived block alongside) and every humanoid NPC (an
  ECS component emplaced at all humanoid spawn sites). The rich machinery still
  lives in `src/macro/attributes.h` (`calculate_combat_stats`,
  `calculate_derived`, `kPerkList`, XP curves, carry capacity);
  `character_sheet.h` composes it into one struct + the procedural
  `make_character_sheet(role, level, seed)` generator.
- **Combat is derived from the sheet.** `project_combat(sheet, base)` turns a
  role's authored `CombatTemplate` (the HP/damage floor + attack identity) into
  the entity's ECS `Health`/`Combat`, scaled by attributes/skills/level. Applies
  to the player + humanoid NPCs; monsters (`0x100|idx`) stay on their raw
  `FaunaEntry` row (sheet-less, never projected).
- **The player is (in the subworld) a real ECS entity.** As of Inc 4a it carries
  `ecs::PlayerTag` on an entity the `SubworldEngine` creates on enter / destroys
  on leave — the movable "player flag" and subworld sim-centre. It is now a full
  combat actor (`Position` + `PlayerTag` + `Health` + `Combat` + `BodyRadius`):
  it takes INCOMING damage (4b) and deals OUTGOING melee (4c) + spells (4d)
  through the entity, with player-cast projectiles stamped with its real entity
  id. The engine scalars + `gs.player.combatStats` stay macro-authoritative
  across the seam via an int↔float bridge. On the MACRO map the player is ALSO a
  `PlayerTag` entity now (macro-4a) — a minimal `Position`+`PlayerTag` flag
  (`ensure_macro_player_entity`, `src/macro/player_entity.{h,cpp}`), so exactly
  ONE `PlayerTag` exists on either side of the seam. Remaining work is the
  `control` possession command (Inc 5), which MOVES that flag between entities.
  See memory `npc-sheet-possession-plan`.
- **NPCs still also carry** the orthogonal thin components `NpcCharacter`
  (visual identity), `NpcTraits` (personality), `NpcLevel`, `NpcInventory` — kept
  as-is; the sheet is additive, not a replacement for those.

---

## 8. CURRENT OBJECTIVE — Finish "player = an NPC with a flag" (player-as-ECS-entity → possession)

**The universal character-sheet system (the PREVIOUS objective) is SHIPPED** —
Increments 1–3 (see §4 and §7). The owner then chose this next track and made two
load-bearing decisions (memory `npc-sheet-possession-plan`) — these are DECIDED,
not open:

- **Q1 = FULL "player = ECS entity".** In the MACROworld every NPC is simulated
  and the player is *just a flag*: any NPC can receive `PlayerTag`, and a debug
  **`control` / вселение (possession)** command moves the flag onto a targeted
  NPC. In the SUBworld the flagged entity is ALSO the centre of the local sim
  (only a 3×3 is embodied, for hardware reasons). **Cross-seam invariant: the
  MACROworld is always authoritative** — the subworld takes everything from it;
  on possession inside a subworld, at exit map the flag to the equivalent macro
  entity (or default back to the player entity).
- **Q2 = combat derived from the sheet** — done (`project_combat`, §7).
- **Sequencing:** the owner chose **subworld-first**.

### Roadmap (build + validated smoke green EACH step)
1. ✅ CharacterSheet + generator.  2. ✅ Embed in `PlayerState` (byte-compatible
   save, still v8).  3. ✅ Derive `Combat`/`Health` from the sheet.
4. Player as ECS entity + `PlayerTag`, subworld-first. Decomposed 4a/4b/4c/4d:
   - **4a ✅ SHIPPED (2026-07-27):** the player is a real `PlayerTag` entity in
     the subworld — an INERT `Position`+`PlayerTag` anchor that matches NO
     subworld view (zero gameplay change). Lifecycle on `SubworldEngine`:
     `spawn_player_entity()` at end of `enter()`, `sync_player_entity_position()`
     at top of `tick()`'s ECS block, `clear_player_entity()` in `leave()`; it
     survives seam re-centres (no `SubworldTag`, so the respawn clear skips it).
     Scalars `playerX_/playerY_` + `gs.player.combatStats` stay authoritative.
   - **4b ✅ SHIPPED (2026-07-27, `7c225a2`):** the anchor gained a `Health`
     mirror and now takes INCOMING damage through the entity (melee + projectile
     both reconciled via the int↔float bridge). The `PlayerTag` guards on the
     "danger list" (ai.cpp, melee search, `owned`, death+loot, billboard) went
     in with it, so a live-or-dead player is never mis-targeted, looted, or
     double-drawn.
   - **4c ✅ SHIPPED (2026-07-27, `5ca2919`):** OUTGOING player **melee** flows
     from the entity's sheet-derived `Combat` (`tick_player_melee` reads
     damage/range/cooldown instead of recomputing); the NPC actor loop still
     never swings it (`is_player_side()`).
   - **4d ✅ SHIPPED (2026-07-27, `433dc9f`):** OUTGOING player **spells** carry the real
     player entity id — `SubworldEngine::player_entity_id()` stamps each
     player-cast projectile's `ownerId` just as an NPC missile carries its
     firer's. The `ownerId == 0` sentinel AND the owner self-exclusion are both
     retired: projectiles are purely geometric ("everyone can hit everyone, own
     side included"), the caster kept off its own muzzle by spawn offset alone
     (NPC muzzle widened to `casterRadius + projectileRadius + 2` to match), and
     a `friendlyFire` blast still catches its own caster (owner's Q1). XP/log
     still route to the player via the `playerOwned` bool, which now reads the
     real owner's tags.
   - **macro-4a ✅ SHIPPED (2026-07-27):** resolves Inc 5's "establish FIRST"
     recon — the MACRO player WAS still scalar (`GameState::player`). macro-4a
     promotes it to a persistent MINIMAL `PlayerTag` flag on the macro map
     (`Position` + `PlayerTag` only — no `SubworldTag`/`NPCKind`, so invisible to
     render/proximity/AI and to the subworld reapers), giving the flag a home on
     BOTH sides of the seam. Lifecycle is a free function
     `ensure_macro_player_entity(gs, world)` in `src/macro/player_entity.{h,cpp}`,
     called at boot, at save-load (after the player overwrite), and at the top of
     the macro (non-subworld) tick branch. It is SELF-HEALING: both seam crossings
     funnel through `clear_player_entity()` (enter via `spawn_player_entity()`,
     leave explicitly), so the macro tick recreates the flag after any `leave()`
     regardless of call site, then one-way syncs its `Position` from the
     authoritative scalar. `gs.player` stays authoritative ⇒ **save still v8**
     (the flag is never serialised). A `run_console_smoke` block proves exactly
     ONE `PlayerTag` holds across a macro→subworld→macro cycle, correct flavour
     each side. **This UNBLOCKS Inc 5** — the flag now has a macro entity to move.
5. **← IN PROGRESS (Increment 5) — the `control` / вселение (possession) command.**
   FINAL step of the track. **Design is FULLY RESOLVED with the owner** (three
   `AskUserQuestion` rounds + free-text, 2026-07-27) — these are DECIDED; BUILD
   them, do not re-litigate. Possession = **move the one `PlayerTag` flag onto a
   chosen body**; that body becomes the player-controlled actor and the vacated
   one reverts to a normal NPC. **STATUS (2026-07-29): 5a + 5b + 5c + 5d + 5e-1 +
   5e-2 SHIPPED (5c = commit `b8677e6`, 5e-2 = commit `43e2da6`, save now v10);
   5e-3 ← NEXT** — see the per-stage ✅/PENDING markers below.

   **Owner's five load-bearing decisions:**
   - **D1 — Target selection is scale-split.** MICROworld: an **aim/reticle**
     pick (look at a body → possess it). MACROworld: a **`control <id>`** dev
     console command. The micro reticle is the PRIMARY path; the macro command is
     a debug follow-on. (Owner verbatim: *"через прицел в микромире через айди в
     макро"*.)
   - **D2 — Vacated body: literally move the flag.** `reg.remove<PlayerTag>(old);
     reg.emplace<PlayerTag>(target);` — the single flag hops A→B (never a copy,
     never a second flag; the exactly-one-PlayerTag invariant from macro-4a
     holds). The old body becomes an ordinary NPC governed by its OWN
     CharacterSheet/AI again. (Owner verbatim: *"становится обычным нпц —
     буквально переносится флажок игрока с одной энтити на другую"*.)
   - **D3 — Stats model = M2 "body-native" (тела, по его листу).** The flag is
     ONLY a marker of *who you control*; the possessed body fights with its OWN
     `CharacterSheet`/`Combat`/`Health`. Possess a lord ⇒ strong as the lord;
     possess a rat ⇒ weak as the rat (M&B "take a leader"). **This REVERSES the
     Plan agent's "stamp the hero's loadout onto the body" proposal — do NOT stamp
     hero stats onto the possessed body.** `gs.player` (the hero) is PRESERVED
     untouched as the revert target; while possessing, HP/combat authority and the
     HUD read the FLAGGED body, not `gs.player`. (Owner chose "Тела (по его
     листу)".)
   - **D4 — Micro possession via ENTITY-AUTHORITY migration.** Today the subworld
     player is SCALAR-authoritative (`SubworldEngine::playerX_/playerY_` are truth;
     the `PlayerTag` entity is a per-tick projection), so moving the flag alone
     does NOT transfer control or camera. The owner chose to INVERT this: make the
     `PlayerTag` entity's `Position` authoritative, keep the scalars as a derived
     mirror (every existing reader — camera, melee origin, proximity, seam,
     NPC-AI center, HUD, spell muzzle — keeps working unchanged). Continues
     macro-4a's scalar→entity trajectory. NOTE: this is intra-subworld authority
     only; the MACROworld stays authoritative *at the seam* (enter/leave still
     reconcile to/from `gs.player`), so §3 pillar 1 is intact. (Owner chose
     "Микро, энтити-авторитет".)
   - **D5 — Build the macro→subworld projection plumbing NOW** (not deferred).
     Cross-seam exit remap (owner's DECIDED invariant, MACRO stays authoritative):
     a subworld entity that ORIGINATED from a macro entity carries a runtime
     backlink; on subworld `leave()` the macro flag remaps onto that macro entity
     (you "exit AS" the lord you possessed); else it defaults back to the player's
     own entity. Ties into §9.4 (the flag lives on a leader NPC ⇒ possession = take
     over a party by taking its leader). (Owner verbatim: *"если ты в микро
     вселился в сущность типа лорда … то в неё а иначе дефолт в игрока назад";
     "Строить плумбинг сейчас"*.)

   **Staged plan 5a→5e** — build + validated smoke + standalone `build/*_test`
   GREEN each stage; keep docs + memory `npc-sheet-possession-plan` in lockstep.
   **Save stayed v8 through 5a–5d and 5e-1; 5e-2 identity-persistence bumped it
   v9→v10 (SHIPPED, see below).** New runtime-only component
   `struct MacroOrigin { entt::entity macro; };` in `src/ecs/components.h` (NOT
   serialized).
   > **Current baseline (2026-07-29): `kSaveVersion` = 10.** History: the
   > mountains→biome refactor bumped **8→9**; possession stayed byte-identical
   > (runtime-only `MacroOrigin`) through 5a–5e-1; then **5e-2 shipped the one
   > possession bump 9→10** (the serialized `PlayerState::possessedMacroSpawnId`
   > ordinal). Everywhere this file's older per-increment notes say "still v8"
   > or "stays v9," read the current on-disk baseline as **v10**.
   - **5a ✅ SHIPPED (2026-07-27) — Invert subworld position authority
     (model-agnostic).** The `PlayerTag` entity's `Position` is now authoritative
     intra-subworld; `playerX_/playerY_` are a derived mirror. `pull_player_entity_
     to_scalars` runs at tick top (before the seam), `push_scalars_to_player_entity`
     after the seam commits its ∓cell shift (`seamless_manager.cpp:793-794`);
     `move_player`/`set_player_pos` write the entity then mirror. Every legacy reader
     (camera/melee/proximity/HUD/spell muzzle) still reads the scalars → zero
     behavior change. HP left macro-authoritative. MACRO stays authoritative at the
     seam. Verified via the extended `player_entity` smoke.
   - **5b ✅ SHIPPED (2026-07-28) — `aim_target(reg, px, py, yaw, maxRange,
     cosHalfAngle, shooter = entt::null)` (pure, unit-tested).** Built as a
     STANDALONE `src/sub/targeting.{h,cpp}` (namespace `sm::sub`) with its own
     `tests/targeting_test.cpp` + a dedicated CMake target — a deliberate DIVERGENCE
     from the original "co-locate in `spawn.cpp`" plan, to keep the primitive clear
     of the parallel `spawn.cpp` seam WIP. Forward-cone nearest pick generalising the
     melee scan (`engine.cpp:826-841`): has `Position`, not `Dead`, has `SubworldTag`,
     NOT player-side (`is_player_side`), in range, `dot(forward,to) > cone`, nearest
     tie-break; the trailing `shooter` arg excludes the possessor's own body;
     `cosHalfAngle == -1` ⇒ full circle. All six test cases (ahead / behind /
     out-of-cone / nearest-of-two / Dead-excluded / soldier-excluded) green. 5c reuses
     it as-is.
   - **5c ✅ SHIPPED (2026-07-28, commit `b8677e6`) — The possession act (M2).**
     `possess_entity(reg, target)` + `current_player_body(reg)` in `spawn.cpp` move
     the flag (D2); the target keeps its OWN sheet/`Combat`/`Health` (D3, NO
     hero-stamp) and its `BodyRadius`+`SubworldTag`. Engine entries
     `SubworldEngine::possess_aim(cosHalfAngle, maxRange)` (reticle → `aim_target →
     possess_entity → snap the scalar mirror`) and `possess_by_id(id)` (D1 debug);
     wired to keybind **V** + console `possess [id]` (`main.cpp`). The
     **discriminator** is `NPCKind`: the hero husk (`spawn_player_entity`) carries
     NONE, every possessable scene body HAS one — so the body-native sync/reconcile
     branch keys on `reg.all_of<ecs::NPCKind>(e)`. Renderer/minimap guards added
     (`entt::exclude<ecs::PlayerTag>` on both `vk_renderer_3d.cpp` NPC+creature views
     and the minimap-blip view) so the inhabited body doesn't billboard on the
     first-person camera.
     **TWO deliberate divergences from the plan text above:**
     1. **AI vacate: `PlayerTag` skip, not `remove<SubworldAi>`.** Both `ai.cpp`
        loops now `continue` on `any_of<PlayerTag>` (modern loop top; legacy loop
        skip extended to `PlayerSoldierTag,PlayerTag`). No component churn — the
        body's own AI auto-resumes the tick after the flag leaves, so vacate needs
        no re-emplace. (Cleaner than the planned remove/re-add and it can't lose the
        original AI params.)
     2. **HUD: non-mutating `player_display_hp()`, NOT a mirror into `gs.player`.**
        The plan said mirror the body's `Health` → `gs.player.combatStats.currentHp`,
        but that would CORRUPT the D3 frozen revert target (`leave()`/`restore()`
        revert to `gs.player`). Instead a read-only `SubworldEngine::player_display_
        hp()` returns the flagged body's `Health`, wired only into the hit-flash
        consumer (`tick_subworld_hit_flash`, `main.cpp`). `gs.player` stays untouched.
     **Smoke money-shot** (`run_console_smoke`, spawns `bandit 3`, captures the hero
     husk): `[smoke] possess flag_moved=1 husk_destroyed=1 body_native=1
     body_maxhp=99 display_hp=99 hero_preserved=110/110` — the possessed L3 bandit
     fights with its OWN 99 HP while the L1 hero is preserved at 110/110 (D2+D3
     proven). Green: BUILD OK, 26/26 `build/*_test`, validated seed-12345 smoke
     validation=1 [smoke] PASS exit 0, only benign 05137.
   - **5d ✅ SHIPPED (2026-07-28) — Macro→subworld projection + `MacroOrigin`
     backlink (D5).** `project_macro_npcs_into_subworld(w, mgr, centerCx, centerCy,
     mapW, mapH, seed)` in `spawn.cpp`, called in `enter()` just before
     `spawn_player_squad`/`spawn_player_entity`. Snapshots every persistent macro NPC
     (`view<MacroNpcRuntime, Position, NPCKind, Health, NpcLevel, NpcCharacter>`,
     `exclude<SubworldTag, Dead>`) into a `std::vector` FIRST (avoids EnTT iterator
     invalidation — we emplace into the same pools we iterate), then for each one whose
     integer cell is within ±1 of the window centre on the torus (`toroidal_cell_offset`,
     the SAME nine cells the seamless manager loads) CREATES a NEW subworld body — the
     macro entity is never mutated/destroyed ("macro NPCs survive the trip"). Each
     projection copies `NPCKind`/faction + `NpcCharacter` verbatim, copies `Health.hp`
     as body-native persistent state (clamped into a fresh derived `maxHp`), DERIVES
     `Combat` from a universal `make_character_sheet → project_combat` (citizen-path
     parity, incl. `maybe_emplace_missile_attack`), scatters `Position` within the
     cell's sub-region dodging `TILE_WATER`, and carries `MacroOrigin{macro}`.
     **Divergences from the plan text above (all deliberate, all documented):**
     1. **Signature is scalars, not `(gs, world, cx, cy)`** — plain
        `(w, mgr, centerCx, centerCy, mapW, mapH, seed)` keeps `spawn.cpp` decoupled
        from `GameState` and makes the unit test trivially constructible.
     2. **Reaper skip is in `clear_subworld_world_entities`** (`spawn.cpp`, the
        `spawn_all_cells`/`respawn_fauna` path) — the real function name, not the
        plan's placeholder `clear_existing_subworld_entities`; it already spared
        `PlayerTag`/`PlayerSoldierTag`, now also `all_of<MacroOrigin>`. (The *leave()*
        reaper `clear_subworld_entities` still takes ALL `SubworldTag` unconditionally
        → projections are session-scoped, which is what 5e's exit-remap needs.)
     3. **Hostility is data-driven off `NpcTypeDef.ai`** (`Aggressive→Combat`, else
        `Flee`) — so Bandits fight and all neutrals (incl. Guard=Patrol) flee, a
        deliberate difference from the settlement path's Guard→Combat.
     4. **Two safety guards** the plan didn't call out: `kind.type >= NPCType::Count`
        is rejected BEFORE narrowing `uint16_t→NPCType` (a monster id `0x100|idx`
        must never alias a humanoid row), and projection is bounded by
        `kMaxProjectedMacroNpcs = 128`.
     Enter-only (does NOT re-run on a seam crossing — accepted v1 scope, the macro
     entity is never lost). Unit-tested in `subworld_spawn_parity_test`
     (`macro_projection=1`): 4 seeded macro NPCs (Bandit/Peasant/Guard in-window +
     Merchant far) → 3 projected (far one skipped), each backlink valid and pointing
     at a still-`MacroNpcRuntime` source; macro entities untouched (no
     `SubworldTag`/`MacroOrigin`); Bandit→Combat, Peasant+Guard→Flee; HP∈[1,src],
     faction/visualSeed copied; per-cell placement bounds; reaper spares projections;
     determinism across two identically-seeded worlds. End-to-end: the
     `TIMAERT_SMOKE_NEAR_NPC` opt-in relocates the smoke player onto the nearest macro
     NPC and confirms `[smoke] subworld_enter macroProjected=1` on the validated seam.
   - **5e — Exit remap (D5). 5e-1 ✅ SHIPPED (2026-07-28); 5e-2 ✅ SHIPPED
     (2026-07-29, commit `43e2da6`, save v10); 5e-3 ← NEXT.** In
     `leave()`, BEFORE the reaper destroys the body, read the flagged body's
     `MacroOrigin m`. Ordering constraint 5d confirmed: the leave() reaper
     `clear_subworld_entities` (`engine.cpp:344`, called ~`:1584`) destroys ALL
     `SubworldTag` unconditionally, so the `MacroOrigin` read MUST happen before it.
     - **5e-1 position remap ✅ SHIPPED (2026-07-28, v9).** New pure registry
       query `macro_exit_cell_for_body(w, body, mapW, mapH) → {has, cx, cy}` in
       `spawn.{h,cpp}`: if `body` carries a `MacroOrigin` whose macro entity is
       valid + positioned, returns that origin's torus-wrapped cell; else
       `has == false`. Engine wrapper `SubworldEngine::remap_macro_player_to_origin()`
       (thin: resolves `current_player_body`, calls the query, writes `gs.player.x/y`)
       replaces the direct `sync_macro_player_to_center()` call in `leave()` —
       `if (!remap_macro_player_to_origin()) sync_macro_player_to_center();`, so a
       possessed macro body lands you on ITS cell ("exit AS the lord") and every
       un-possessed exit (hero husk / ambient / citizen — no backlink) falls back to
       the window centre exactly as before. Runtime-only ⇒ **save stays v9**. The
       next macro tick's `ensure_macro_player_entity`
       (`src/macro/player_entity.cpp:6-32`) recreates the flag at the landed spot.
       Extracting the pure query (5b/5c pattern) keeps the decision unit-testable
       clear of engine state. **Verified:** `subworld_spawn_parity_test`
       (`exit_remap=1`: origin cell / torus wrap / no-backlink fallback / null body /
       degenerate dims / stale-handle-after-reap, no crash); a dedicated
       `subworld_exit_remap` smoke possesses a projected body, forces its origin to a
       distinctive off-centre cell, leaves, and asserts the macro player landed there
       → `onOrigin=1 off_centre=1 landed=128,152 origin=128,152 centre=121,147`;
       26/26 `build/*_test`; validated seed-12345 `subworld_exit_remap` + render-heavy
       `subworld_time` both `[smoke] PASS`, no VUIDs beyond the benign teardown leak.
     - **5e-2 ✅ SHIPPED (2026-07-29, commit `43e2da6`, save v9→v10) — identity
       remap ("exit AS the lord").** On `leave()`, once `remap_macro_player_to_origin`
       has resolved the origin cell, `adopt_possessed_macro_as_player(reg, macro)`
       (`spawn.{h,cpp}`) MOVES the single macro `PlayerTag` onto the origin macro NPC
       itself and returns its spawn ordinal — so the flag now rides a real
       `MacroNpcRuntime` body and you resurface **as** the lord, not the hero husk.
       The vacated husk is destroyed by the normal `clear_player_entity` teardown
       (strip-not-destroy only spares `MacroNpcRuntime` holders — the origin — so the
       ordinary husk is reaped and exactly-one-PlayerTag holds). `ensure_macro_player_
       entity` already no-ops when a `PlayerTag` exists, so it does not fight the
       possessed flag; the macro-4a invariant smoke was relaxed to allow the sole
       flag to carry `NPCKind` when it rides a possessed body.
       **OWNER DECISION (resolved 2026-07-28 via `AskUserQuestion`, "Сохранять
       (v9→v10)"):** "you are now this lord" **MUST survive save/load.** Because the
       ECS is never serialized (macro NPCs regenerate from `worldSeed` in fixed
       creation order every boot), the save-stable identity is a deterministic
       **spawn ordinal**, NOT an `entt::entity`: new component
       `ecs::MacroSpawnId { std::uint32_t index; }` (`components.h`, runtime-only,
       never serialized itself) is stamped by the SOLE creation path `make_npc`
       (`npc_spawn.cpp` — the Nth NPC created gets ordinal N; all 10 call sites +
       `spawn_macro_npcs` thread one `spawnIndex` counter). `adopt_…` records the
       chosen ordinal into `PlayerState::possessedMacroSpawnId` (`macro/state.h`),
       the ONE new serialized field (`save.cpp`, **kSaveVersion 9→10**, no
       back-compat branch per AGENTS.md rule #2). On load, `boot_world_from_save`
       calls `reattach_player_to_macro_spawn(world, id, px, py)`
       (`macro/player_entity.{h,cpp}`) which re-finds the regenerated NPC by ordinal
       and hands the flag from the freshly-healed husk; a missing ordinal (lord died
       before the save, or the seed changed) falls back to the hero, changing
       nothing. **Verified:** `subworld_spawn_parity_test` (`identity_remap=1`: adopt
       returns the ordinal + emplaces the flag + tags==1; reattach on a fresh
       identically-seeded world snaps `Position` and moves the flag; negative paths
       ordinal-9999 and id-−1 both no-op); the `subworld_exit_remap` smoke now also
       asserts `tags=1 on_macro_npc=1 rides_origin=1 spawnId≥0` after exit; the GUI
       save/load round-trip smoke re-verified v10 (`51733`-byte slot, seed 12345,
       `[smoke] PASS`) and `save_roundtrip_test` is green; 25/25 ctest (26
       `build/*_test` binaries; the 26th is the GL paperdoll smoke, excluded from
       headless ctest); validated seed-12345 smoke `[smoke] PASS`, only the benign
       05137 teardown leak. macro-4a `console` + 5c possession smokes stay green.
     - **5e-3 ← NEXT — carry possession through a RE-ENTER.** Today re-entering a
       subworld while possessing a macro lord drops the flag back to the hero: the
       leave() adopt put the flag on the `MacroNpcRuntime` origin, and on the next
       `enter()` `clear_player_entity` strips-not-destroys that holder (so the lord
       survives as an autonomous NPC and nothing leaks), but `spawn_player_entity`
       rebuilds the ordinary hero husk. Full carry-through needs the enter path to
       detect a possessed macro flag and stamp its identity/origin onto the new
       subworld body (project the possessed lord as the player body, not a bystander).
       Runtime-only if the projection already carries `MacroOrigin` — likely **no
       save bump** (validate before building).

   Reuse `ensure_macro_player_entity` (`src/macro/player_entity.{h,cpp}`) as the
   home for flag-move logic; never create a second flag (exactly-one is
   smoke-guarded). **Not in scope (v1):** rendering the macro player *from* the
   entity (the overlay still draws `gs.player`); persisting projected subworld NPCs
   across in-subworld re-centers (enter-only); the macro `control <id>` command is a
   debug follow-on (micro reticle is the primary path per D1).

> **Branch state (handoff, 2026-07-29):** everything lives on branch
> `feat/subworld-player-4b-incoming-combat`, NOT yet merged to `main` (`main` still
> at `ede6e65`). The full stack, oldest→newest: Inc 4 (`7c225a2` 4b, `5ca2919` 4c,
> `60c5cb2` test-fix, `433dc9f` 4d), macro-4a `03d0b26`, the PARALLEL agent's
> now-COMMITTED work (seam fixes `68831d5`, subworld road per-fragment `2d5d6b3`,
> quest markers + macro night lighting + mountains-as-biome `e617003`, universal UI
> settings + minimap dots `515d7f2`), hardening (`b4092af` spell dead-code,
> `5dc5168` Fisher-Yates OOB fix, `bdf20b6` loot/sheet/fauna tests, `7390a58` ctest
> wiring), the v8→v9 doc reconcile `70fa98e`, and the **possession stack: `5a58226`
> (5a authority inversion), `5a1e205` (5b aim_target), `b8677e6` (5c possession),
> `2d7e0b4`+`af1b362` (5d macro→subworld projection + docs), `1f5bc0d`+`a793e36`
> (5e-1 exit-position remap + docs)**, the documentation pass (README + new
> `possession.md` focused doc + cross-links), and the **5e-2 identity-remap stack:
> `43e2da6` (feat — exit AS the possessed lord, `MacroSpawnId` ordinal + save
> v9→v10) + `e40fbc4` (docs lockstep — possession.md/MASTER_PROMPT/ARCHITECTURE/
> README)**. **Inc 5 status: 5a + 5b + 5c + 5d + 5e-1 + 5e-2 SHIPPED & committed;
> 5e-3 (carry possession through a re-enter) PENDING** (spec in item 5 above). The
> possession stack through 5e-1 is pushed to
> `origin/feat/subworld-player-4b-incoming-combat`; **5e-2 (`43e2da6` feat +
> `e40fbc4` docs) is committed locally, NOT yet pushed** (owner pushes
> selectively). **Update (2026-07-29):** the previously-parked lighting/shader work
> is now OWNER-APPROVED and **being committed this session** — the directional
> sun+moon lighting, the universal `lit_surface()` in `shaders/lighting.glsl`, the
> two-lobe water moon/sun road, the frame-capture tooling (`TIMAERT_SHOT_PATH` +
> the `capture_frame` smoke action), the `gpu_smoke3d` default frame-cap fix (a
> bare headless run used to spin forever — no default cap), and the doc pass
> (`render.md`, `ARCHITECTURE.md`, `AGENTS.md`, this file). The macro
> mountains-as-biome work the old note referenced already shipped in `e617003`.
> Continue on the branch.

### Definition of done (per increment)
Player HP/combat flow THROUGH the `PlayerTag` entity in the subworld with **no
regression** (identical damage in/out vs today's scalar path); the macroworld
stays authoritative across the seam; a new/extended `run_console_smoke`
assertion proves the routing; and `microcombat.md`, `rpg.md`, `ARCHITECTURE.md`
+ memory `npc-sheet-possession-plan` are updated in lock-step.

### Explicitly still the OWNER's call (propose, don't commit) — see §9
The universal container system (§9.1), gold unification (§9.2), the fauna balance
pass (§9.3), Route-1 macro monster parties (§9.4), and the GPU-driven-sim arc
(§9.5). Advance those only if the owner redirects.

---

## Dynamic lighting track — directional SHIPPED, positional NEXT (2026-07-29)

The owner's standing ask is a **universal dynamic-lighting system for every object
in the game** — *"нужна универсальная система источников света … свет от игрока
сделать честно через ту же систему что нпц"*. It splits into a **directional** half
(the sky's sun/moon — one bearing lighting the whole world) and a **positional**
half (many local emitters: the player, NPC torches, projectiles/spells, lit
windows).

### Directional (sun + moon) — ✅ SHIPPED this session (see the §4 bullet)
One celestial direction `moonDir = -sunDir` shared by the visible disc, the light
that sculpts terrain, and the water specular; the moon as a weak directional
analogue of the sun folded onto the same `sunDir`/`sunColor` slot (`kMoonDirGain`);
ONE `lit_surface()` in `shaders/lighting.glsl` across all five lit passes; a
two-lobe moon bloom; and the universal low-light water road. Full details in the §4
"Subworld universal dynamic lighting" bullet and `render.md`.

### Positional lights — ▶ THE APPROVED NEXT INCREMENT ("SSBO + тюнер")
Owner-approved shape (AskUserQuestion: **"SSBO + тюнер"**). The owner's follow-up —
*"почему их 8? почему нельзя больше? у нас же вулкан шейдеры и дата дривен"* — is the
design driver: **there must be no small fixed cap.** Today `src/sub/lighting.h`
carries a fixed `kMaxPointLights = 8` C array; that arbitrary 8 is exactly what the
owner is objecting to. Replace it with a GPU storage buffer.

Plan — each numbered item is roughly one verified increment (land it, build green,
self-verify with a captured frame you actually LOOK at, then the next):
1. **SSBO of lights.** Add a `kSubworldMaxLights` budget (start **32** — a *budget*,
   not a hard truth; an SSBO scales, so this is one constant to raise later, fully
   data-driven) and move the lights out of the push-constant/fixed array into a
   storage buffer at **set 0, binding 1**, uploaded per frame. Mirror the new
   descriptor in the `gpu_smoke3d` harness — it shares the shipping set layouts and
   will silently red if it drifts (memory `gpu-smoke3d-shared-shader-contract`).
2. **`point_lights()` in `shaders/lighting.glsl`.** One function, next to the
   existing `lit_surface()`, that sums the SSBO emitters with smooth
   distance falloff and adds their contribution in the SAME spot every lit pass
   already calls `lit_surface()` — so all five passes gain positional light for
   free, exactly the way the directional unification worked.
3. **`LightEmitter` ECS component** — `{ vec3 offset; vec3 color; float radius;
   float intensity; }` (data-driven: one component row = one light, zero
   hardcoding). A gather system collects every in-range `LightEmitter` each frame
   into the SSBO.
4. **Player as an HONEST emitter.** The player is an NPC-with-a-flag
   (`entity-model-player-is-npc`), so the player's lantern/torch is just a
   `LightEmitter` on the player entity, gathered by the SAME system as any NPC's —
   no player special-case. This is the owner's *"честно через ту же систему что
   нпц"*.
5. **Then, purely as data (new component rows, no new systems):** NPC torches,
   projectile/spell emitters (a fireball carries a `LightEmitter`), and lit house
   windows (a structure emitter). Each is one more entity with the component.
6. **Тюнер.** Surface the emitter budget / falloff / intensity through the existing
   universal settings UI so the look tunes live — the "тюнер" half of the approved
   shape.

Definition of done per increment: the subworld renders with the new positional
light, `gpu_smoke3d` prints `terrain loop OK` (green — and now self-terminating, see
the frame-cap fix), a captured frame is LOOKED at, and `render.md` +
`ARCHITECTURE.md` + memory `subworld-universal-lighting` are updated in lock-step.
Never hand this whole track to one autonomous subagent — it is interconnected
renderer/shader/descriptor work; land it yourself in verified steps (bounded
research and independent verification may still fan out to parallel agents).

---

## 9. OPEN DECISIONS — the OWNER decides these (propose, don't commit)

### 9.1 The universal container system (settlements + landmarks + chests)
The dead `generate_settlement_inventory(population, economy, rng)` (+ its
authored economy tables `kSettlementBase`, `kEconFarming/Mining/Trade/Fishing/
Crafting` in `items.cpp`) was **left in place on purpose** — do not delete the
authored economy data ahead of this design.

**Direction confirmed by the owner (2026-07-27):** there should be **ONE unified
container system** — an entity-with-`Inventory` — that backs *everything that
holds items*: settlement/city stock, **the whole landmark system** (thousands of
cities and other landmarks, each with inventory), **local chests inside
subworlds**, and (naturally) NPC inventories and corpses. The owner's words
(paraphrased): *"it would of course be good if all of this were a single
system"* — elegant, universal, optimized. Because the game is **data-oriented
and must scale to thousands of landmarks with inventory**, the design must be
cache-friendly and cheap at scale (do not give every one of thousands of
landmarks a heavyweight object; think SoA/handle-based storage, lazy
population, shared authored templates).

**Your job:** bring the owner a concrete architecture proposal — the container
component/representation, how settlements/landmarks/chests/corpses/NPCs all
share it, how it populates (the existing economy tables become authored
templates), and how it stays O(1)-cheap at thousands-of-landmarks scale. Then,
with approval, replace the old procedural function with the populator of the
unified container. This composes with the §8 NPC-sheet work (an NPC's inventory
is just a container too). Propose before you build.

> **A concrete draft proposal now exists — `proposals/unified-container-system.md`**
> (2026-07-27, awaiting owner review). Read it before designing from scratch.

### 9.2 Gold unification into the loot profile
Gold is still rolled by a *separate* `generate_loot_gold(level, faction)` with
`gold_faction_mult`, parallel to the item loot registry. Folding gold in as one
more loot-table column would make the "one loot table" literally complete.
Behavior-preserving refactor; propose it.

### 9.3 Populate `xpReward` / `lootId` per creature (balance/data pass)
Defaults are behavior-preserving today (xpReward=0 ⇒ generic; lootId=null ⇒
faction default). A balance pass would set meaningful per-creature XP and loot.
Pure data; low risk.

### 9.4 Parties — the macro-scale endgame (leader NPC + mixed roster, thousands of units)
**Owner vision (2026-07-27), load-bearing:** a *party* (отряд) is, in essence, a
**leader NPC** — exactly the Mount & Blade model — and its roster can hold **both
generic monsters (from the global `FaunaEntry` table, `0x100|idx`) AND humanoid
NPCs (`NPCType<8`)**, mixed freely. Parties can be **large — thousands of units**,
and the owner was explicit that **this scale is the whole reason the engine is
data-oriented and GPU-driven** ("ради этого всё затевалось"). This generalises the
old "roaming monster parties" idea and **unifies it with the M&B pillar "an army =
a list of NPCs you hired" (§2): the player's army is just one party, and the player
is the leader NPC of it.** It ties directly into the entity/possession track (§8):
the `PlayerTag` flag lives on a **leader NPC**, and `control`/possession can move it
to any leader — **you take over a party by possessing its leader**. Monsters stay
sheet-less even as party members (§3.2); membership is orthogonal to the sheet.
Roaming parties descend into subworld combat when engaged. Larger design — bring
the owner a concrete architecture (party = a leader entity + a cache-friendly
member roster scalable to thousands; how members embody into the 3×3 subworld on
engagement) before building. See memory `macro-parties-model`.

> **A concrete draft proposal now exists — `proposals/macro-parties.md`** (2026-07-27,
> awaiting owner review).

### 9.5 Vulkan / GPU-driven simulation (standing backend mandate)
The headline compute goal — thousands of GPU-resident combatants, embodied to
the CPU/ECS only when the player can act on them — remains the long arc. See
`ARCHITECTURE.md` §Rendering & Compute Backend and §GPU-Driven Simulation, and
`vulkan.md` / `render.md` / `vulkan_plan.md`. Advance it when the owner points
you there.

### 9.6 Codebase hygiene backlog (census follow-ups)
A read-only census of cold code (2026-07-28) surfaced items **left unapplied on
purpose** — each perturbs TS parity / the seed-12345 world under active render
validation, or is a semantics call reserved for the owner. Full patches +
rationale in **`proposals/census-followups.md`**. Summary:
- **`core/rng.h:23` `next_f01()` can return `1.0f`** (breaks its documented
  `[0,1)` contract; ~1 in 33.5M draws — float rounds the top ~128 `u32` codes up to
  `2^32`). The only *memory-unsafe* consumer (the quest Fisher-Yates, §4) is already
  fixed defensively; the ROOT fix is DEFERRED because `rng.h` is bit-exact with the
  external TS authority and feeds seeded worlds — it needs a coordinated TS change +
  a full re-baseline in a quiet window. Two candidate fixes drafted (a clamp to
  `0.99999994f`, or `float(next_u32() >> 8) * 0x1p-24f`). Memory
  `rng-next-f01-contract-hole`.
- **Faction-id spaces don't line up:** `settlement_faction` (`macro/npc.h`) returns
  `barbarians/magika/timaert/empire` while loot/fauna use
  `bandits/wildlife/demons/empire`. Reconcile to one vocabulary or document them as
  deliberately separate (owner call — guessing wrong could silently merge factions).
- **`damage_hp` doesn't floor at 0** (`events/effect_applicator.cpp`) while
  `drain_sp/mp` do; the comment claims TS-faithful. Owner confirms keep-or-clamp.
- **`ui/macro_overlay.cpp` has two hand-synced `NPCType` switches** (colour + sprite)
  that must be kept in step → collapse to one table (minor single-source refactor).
- **`generate_settlement_inventory` + its economy tables stay dead-but-kept** for
  §9.1 — do NOT delete (already owner-protected; listed so a dead-code sweep spares
  them).
- **POD-struct default-init hygiene** — flagged generically, not re-located this
  pass; re-derive with a fresh census grep before acting.

---

## 10. File map (where things live)

```
src/
  app/main.cpp   Boot, main loop, dev console, run_console_smoke (extend smokes here)
  core/          math, RNG (xorshift32), torus helpers
  ecs/           EnTT World (sm::ecs::World, app.ecs.reg), components.h, systems
  macro/         L1 — world sim. Key: state.h (PlayerState/GameState), npc.h
                 (NPCType + kNpcTypeDefs), army.h (CombatTemplate/SoldierSquad),
                 items.{h,cpp} (item catalog + loot registry), attributes.h
                 (the RPG sheet machinery), economy, politik, zones, biomes, features,
                 macro_lighting.{h,cpp} (night-glow bake), vk_macro_renderer (2D map GPU)
  sub/           L2 — subworld. Key: fauna.{h,cpp} (GLOBAL MONSTER TABLE),
                 spawn.{h,cpp} (ambient spawn), engine.cpp (resolve_subworld_deaths,
                 spawn_hostile_npc), targeting.{h,cpp} (aim_target — Inc-5
                 possession primitive), ai, vk_renderer_3d, sky, lighting
  events/        L3 — bus, logic nodes, effect applicator, quest engine
  content/       L4 — pluggable data: spells, procedural quests
  assets/        sprite atlas + paper-doll loaders (do NOT touch sm::Sprite)
  ui/            ImGui overlays (above all layers)
```

**Docs (all in repo root):** `README.md` (orchestrator), `ARCHITECTURE.md`
(layered source of truth), `AGENTS.md` (rules), `monsters.md`, `rpg.md`,
`microcombat.md`, `macrosim.md`, `economy.md`, `spells.md`, `biomes.md`,
`zones.md`, `landmarks.md`, `features.md`, `macroworld.md`, `microworld.md`,
`quests.md`, `progression.md`, `render.md`, `macro-lighting.md` (2D map
night-glow bake), `vulkan.md`, `vulkan_plan.md`, `design.md` (high-level vision).

**Draft proposals (`proposals/`, uncommitted, owner-review):**
`unified-container-system.md` (§9.1), `macro-parties.md` (§9.4),
`census-followups.md` (§9.6).

---

## 11. Memory system

Persistent memory lives at
`/Users/jirnyak/.claude/projects/-Users-jirnyak-Mirror-timaert/memory/`. The
index is `MEMORY.md` (one line per memory). Read it at session start. Current
memories worth knowing: `game-vision-refs`, `working-method-and-mandate`,
`monster-table-loot-source-of-truth`, `entity-model-player-is-npc`,
`universal-sprite-resolver`, `hybrid-monster-spawning`, `macro-parties-model`,
`npc-sheet-possession-plan` (**the §8 track — read before Inc 5**),
`master-prompt-and-next-track`, `rng-next-f01-contract-hole` (**the `next_f01()`
`[0,1)` landmine — read before touching `rng.h`**), `vulkan-validated-smoke`,
`unit-test-suite`
(**RUN the unit suite — `ctest --test-dir build` now registers all 43 `*_test`
targets, verified 43/43 green 2026-08-05; the direct `build/*_test` recipe still
works too**),
`known-teardown-leak`, `flaky-sdl-teardown-sigbus` (**the flaky teardown crash —
exit 138 after PASS is benign**). Write new memories for durable, non-obvious
facts (design decisions, owner preferences, gotchas) — not for things the
code/git already records. When you change a subsystem, update the relevant
memory.

---

## 12. Your first moves (checklist)

1. Read this file, then `AGENTS.md`, `ARCHITECTURE.md`, `README.md`, `monsters.md`,
   `rpg.md`, and the memory index.
2. `git status --short` (confirm clean) and do a baseline
   `cmake --build build --target timaert -j` + the validated smoke, so you know
   green looks like green *before* you change anything.
3. Read the entity model in code: `src/macro/state.h` (PlayerState sheet),
   `src/macro/attributes.h`, `src/ecs/components.h` (NpcLevel/NpcInventory/
   NpcCharacter/NpcTraits), `src/macro/npc.h` (NPCType/kNpcTypeDefs),
   `src/sub/engine.cpp` (spawn + death paths).
4. **Continue the player-as-entity roadmap (§8): Increment 4 (4a–4d) is
   COMPLETE** — the subworld player is a full ECS combat entity. **Next is
   Increment 5** — the `control`/possession command + cross-seam reconciliation.
   FIRST establish whether the MACRO player is already an ECS entity or still
   scalar (NOT yet audited — see §8 Inc 5), THEN present the design forks to the
   owner with concrete options BEFORE building. One small, additive, green
   increment at a time. Confirm the track with the owner before pivoting to a
   different one (§9).
5. Implement in small, additive increments. One green build + one validated
   smoke per increment. No regressions.
6. Keep the docs and memory in lock-step with the code. Report honestly.

---

## 13. One-paragraph summary (if you read nothing else)

Timaert/Samosbor is a Mount-&-Blade macro world + Daggerfall/M&M-6-7-8 micro
world, C++23/EnTT/Vulkan, TS source is gameplay authority and the C++ port
ships. Design law: minimum systems, maximum functionality, no hardcoding,
single sources of truth (one monster table, one loot table, one item catalog,
one combat block). Player is an NPC-with-a-flag; monsters (`0x100|idx`) ≠ NPCs
(`<8`). The monster+loot foundation AND the universal character sheet are
shipped (player + humanoid NPCs share one `CharacterSheet`; combat derived via
`project_combat`; monsters stay sheet-less). **Your job this iteration:
Increment 4 is COMPLETE — the subworld player is a full ECS combat entity
(incoming damage 4b, outgoing melee 4c, spell ownership 4d, all via the universal
paths, no player special-case). NEXT is Increment 5: the `control`/possession
command + cross-seam flag reconciliation, which moves to the MACROworld — first
verify whether the macro player is already an entity or still scalar, then
present the design forks to the owner before building.** Build with `cmake
--build build -j` (no `--target`, so the `build/*_test` unit binaries build too),
then run the suite with `ctest --test-dir build --output-on-failure` (all 43
`*_test` targets are registered now — 43/43 green 2026-08-05 — or run the binaries
directly if you prefer); verify with the seed-12345 validated
smoke; a teardown SIGBUS after `[smoke] PASS` is the known flaky SDL crash, not a
regression; ignore LSP noise; the one VUID-05137 teardown leak is benign. The owner decides the vision, speaks Russian, wants T.A.R.S. honesty and
exhaustive thinking, and reserves the settlement/container, gold-unification,
balance, macro-party, and Vulkan tracks for their own call.
