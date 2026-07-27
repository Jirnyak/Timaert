# MASTER PROMPT — Timaert / Samosbor (next iteration)

> **You are the next engineer on this game.** You are a Claude-Opus-class agent
> with effectively unlimited token budget. That is a mandate, not a luxury:
> **think exhaustively, verify everything, do not save tokens.** This document
> exists so you and the project owner *speak the same language from the first
> word* — so you grasp the intent behind a half-sentence instruction and never
> hallucinate a feature the owner did not ask for.
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
3. **One monster table, one loot table.** `src/sub/fauna.{h,cpp}` is the monster
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
- The full macro world (terrain, 10 biomes, rivers, kingdoms, MST roads, dirt
  roads, trees, mountains, difficulty zones, politik, languages, flags).
- The seamless 3×3 subworld with neighbour-aware heightmaps and first-person 3D
  rendering (sky/terrain/water/structures/billboards, dynamic lighting, shadows).
- Universal combat, faction hostility, corpse loot, XP attribution.
- Event bus + logic nodes + procedural quests; modular spell system; save/load
  (schema v8); audio.
- **The monster + loot foundation (this is the most recent work — know it
  cold):**
  - `FaunaEntry` (`src/sub/fauna.h`) is now the **global monster table** with a
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
  Increments 1–4 COMPLETE — newest work, know it cold):**
  - **One `CharacterSheet`** (`src/macro/character_sheet.h`, HEADER-ONLY inline)
    = `Attributes + Skills + Perks + LevelData`, the SAME type on the player
    (embedded as `PlayerState.sheet`) and every humanoid NPC (an ECS component).
    `make_character_sheet(role, level, seed)` fills it procedurally per role,
    spending the exact player point economy (8+3·(L-1) attr / 3+(L-1) skill) so a
    level-N NPC is budget-identical to a level-N player. Attached at every
    humanoid spawn site; monsters (`0x100|idx`) stay sheet-less.
  - **Save is byte-identical (still schema v8)** — the player sheet fields
    serialize in the same fixed order via `w.pod`; no version bump.
    `save_roundtrip_test` unchanged.
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
  - Full write-up: **`rpg.md`** (Universal CharacterSheet), **`microcombat.md`**
    (sheet-derived combat), **`ARCHITECTURE.md`** (§Combat + §Seamless-9-Cell).

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
- **Run the standalone unit binaries too.** The repo has ~20 standalone
  `build/*_test` executables, but **`ctest` registers NONE of them** (running
  `ctest` passes vacuously). Build them (`cmake --build build -j` with no
  `--target`) and run each directly, e.g.
  `for t in build/*_test; do "$t" >/dev/null 2>&1 && echo "ok $t" || echo "FAIL $t"; done`.
  A 4b change once slipped a stale spawn-position assertion past a smoke-only
  pass; the unit suite caught it (fixed in `60c5cb2`). (Memory `unit-test-suite`.)
- **Smoke scripts need the boot prefix.** A bare `TIMAERT_SMOKE_SCRIPT=<action>`
  never boots (every invariant reads 0 → FAIL); always prefix with
  `new_game,wait_boot_done,`. `subworld_loot_xp` must run BEFORE any smoke that
  enters a subworld (it self-manages enter→leave and asserts the subworld is
  inactive at start).
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
  across the seam via an int↔float bridge. Remaining work is the `control`
  possession command (Inc 5). See memory `npc-sheet-possession-plan`.
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
5. **← NEXT (Increment 5) — the `control` / вселение (possession) command +
   cross-seam flag reconciliation.** FINAL step of the track; it moves to the
   MACROworld. Resolve these WITH THE OWNER (present concrete options, per §1):
   - **Prerequisite to establish FIRST (UNVERIFIED — recon was interrupted):** is
     the MACRO player already a real ECS entity with `PlayerTag`, or still scalar
     state (as the subworld player was pre-4a)? All of 4a–4d was SUBWORLD-only;
     the macro player's representation was NOT audited this iteration. If it is
     still scalar, Inc 5 needs a 'macro 4a' first (make the macro player a
     `PlayerTag` entity) before the flag can move. Read `src/macro/state.h`, the
     macro update loop, and how the overworld camera/movement follow the player.
   - **How possession picks its target:** reticle/cursor pick vs. nearest vs. a
     console arg (`control <id>` / `control nearest`). A dev console already
     exists (command table + `run_console_smoke` in `src/app/main.cpp`) — the
     cheapest first cut is a console command.
   - **What happens to the vacated body and the possessed NPC's identity** (old
     body becomes a normal NPC; the possessed NPC keeps its sheet — §3.1 isotropy).
   - **Cross-seam reconciliation (owner's DECIDED invariant):** the MACROworld is
     authoritative; on possession INSIDE a subworld, at exit map the flag to the
     equivalent macro entity, else default back to the player entity. Ties into
     §9.4: the flag lives on a *leader NPC*, so possession = take over a party by
     taking its leader.
   Verify each step with build + validated smoke + the standalone unit suite;
   keep docs + memory `npc-sheet-possession-plan` in lockstep.

> **Branch state (handoff):** Increment 4 lives on branch
> `feat/subworld-player-4b-incoming-combat`, NOT yet merged to `main`: 4b
> `7c225a2`, 4c `5ca2919`, test-fix `60c5cb2`, 4d `433dc9f`. `main` is at
> `ede6e65`. Merge or continue on the branch as the owner prefers.

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

### 9.5 Vulkan / GPU-driven simulation (standing backend mandate)
The headline compute goal — thousands of GPU-resident combatants, embodied to
the CPU/ECS only when the player can act on them — remains the long arc. See
`ARCHITECTURE.md` §Rendering & Compute Backend and §GPU-Driven Simulation, and
`vulkan.md` / `render.md` / `vulkan_plan.md`. Advance it when the owner points
you there.

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
                 (the RPG sheet machinery), economy, politik, zones, biomes, features
  sub/           L2 — subworld. Key: fauna.{h,cpp} (GLOBAL MONSTER TABLE),
                 spawn.{h,cpp} (ambient spawn), engine.cpp (resolve_subworld_deaths,
                 spawn_hostile_npc), ai, vk_renderer_3d, sky, lighting
  events/        L3 — bus, logic nodes, effect applicator, quest engine
  content/       L4 — pluggable data: spells, procedural quests
  assets/        sprite atlas + paper-doll loaders (do NOT touch sm::Sprite)
  ui/            ImGui overlays (above all layers)
```

**Docs (all in repo root):** `README.md` (orchestrator), `ARCHITECTURE.md`
(layered source of truth), `AGENTS.md` (rules), `monsters.md`, `rpg.md`,
`microcombat.md`, `macrosim.md`, `economy.md`, `spells.md`, `biomes.md`,
`zones.md`, `landmarks.md`, `features.md`, `macroworld.md`, `microworld.md`,
`quests.md`, `progression.md`, `render.md`, `vulkan.md`, `vulkan_plan.md`,
`design.md` (high-level vision).

---

## 11. Memory system

Persistent memory lives at
`/Users/jirnyak/.claude/projects/-Users-jirnyak-Mirror-timaert/memory/`. The
index is `MEMORY.md` (one line per memory). Read it at session start. Current
memories worth knowing: `game-vision-refs`, `working-method-and-mandate`,
`monster-table-loot-source-of-truth`, `entity-model-player-is-npc`,
`universal-sprite-resolver`, `hybrid-monster-spawning`, `macro-parties-model`,
`npc-sheet-possession-plan` (**the §8 track — read before Inc 5**),
`master-prompt-and-next-track`, `vulkan-validated-smoke`, `unit-test-suite`
(**RUN the standalone `build/*_test` binaries — ctest registers none**),
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
--build build -j` (no `--target`, so the `build/*_test` unit binaries build too —
run them directly, ctest registers none); verify with the seed-12345 validated
smoke; a teardown SIGBUS after `[smoke] PASS` is the known flaky SDL crash, not a
regression; ignore LSP noise; the one VUID-05137 teardown leak is benign. The owner decides the vision, speaks Russian, wants T.A.R.S. honesty and
exhaustive thinking, and reserves the settlement/container, gold-unification,
balance, macro-party, and Vulkan tracks for their own call.
