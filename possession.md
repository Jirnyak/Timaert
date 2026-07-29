# Possession — Вселение (player = an NPC with a flag)

The player is not a special object. It is **one `ecs::PlayerTag` flag riding an
ordinary ECS body** — in the macroworld a minimal overworld marker, in the
subworld a full combat actor. *Possession* (вселение) moves that one flag onto a
different body: you become whatever you inhabit, and the body you left reverts to
an ordinary NPC. Every universal path (combat, targeting, render, AI, loot,
death) already respects the flag, so there is no player special-case to maintain.

- **Code:** [sub/spawn.h](src/sub/spawn.h) / `spawn.cpp`
  (`current_player_body`, `possess_entity`, `aim_target`,
  `project_macro_npcs_into_subworld`, `macro_exit_cell_for_body`,
  `adopt_possessed_macro_as_player`),
  [sub/engine.h](src/sub/engine.h) / `engine.cpp`
  (`spawn_player_entity` / `sync_player_entity_position` / `clear_player_entity`,
  `possess_aim` / `possess_by_id`, `remap_macro_player_to_origin`),
  [macro/player_entity.h](src/macro/player_entity.h) / `player_entity.cpp`
  (`ensure_macro_player_entity`, `reattach_player_to_macro_spawn`),
  [ecs/components.h](src/ecs/components.h)
  (`PlayerTag`, `MacroOrigin`, `MacroSpawnId`),
  [macro/state.h](src/macro/state.h) / `save.cpp`
  (`PlayerState::possessedMacroSpawnId`, `kSaveVersion` 10)
- **TS origin:** none — the TS prototype had a scalar player; this is a
  C++/ECS shipping-port design (memory `npc-sheet-possession-plan`).
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Combat System /
  §L2 — Microworld (the subworld player & possession block)

## Model

- **The player is a flag.** `PlayerTag` is an empty tag component. The tagged
  entity is the player; nothing else marks the player.
- **Exactly one `PlayerTag` at all times** — the single system-wide invariant.
  The minimal flag on the overworld, the full combat actor in a subworld, never
  both, never zero mid-frame. Smoke-guarded across a macro→sub→macro cycle.
- **Two homes across the seam.** On the macro map the flag is a *deliberately
  minimal* body — `Position + PlayerTag` only, no `NPCKind`/`SubworldTag`, so it
  is invisible to overworld render / proximity / AI and to the subworld reapers.
  `ensure_macro_player_entity(gs, world)` heals it at boot, at save-load, and at
  the top of every macro tick, one-way syncing its `Position` from the
  macro-authoritative `gs.player` scalar. In the subworld the flag rides a full
  combat body (`Position + Health + Combat + BodyRadius + SubworldTag`), whose
  `Position` is authoritative intra-subworld (the scalars are a derived mirror).
- **Body-native stats.** The flag marks *who you control*, nothing more. The
  possessed body fights on its **own** `CharacterSheet`/`Combat`/`Health` —
  possess a lord ⇒ strong as the lord; possess a rat ⇒ weak as the rat (M&B
  "take a leader"). The hero `gs.player` is preserved untouched as the revert
  target. The discriminator is `NPCKind`: the hero husk lacks it, every scene
  body has it, so teardown/reconcile branch on it (a husk is destroyed when
  vacated; a real body is only un-flagged).
- **The act.** `possess_entity(reg, target)` is literally
  `remove<PlayerTag>(old); emplace<PlayerTag>(target)`. Targeting is scale-split
  (owner D1): in the subworld you **look at a body and possess it** —
  `possess_aim` runs the `aim_target` forward-cone pick on the camera yaw
  (keybind **V** / console `possess`) — with `possess_by_id` as the debug
  by-id path; on the macro map a `control <id>` console command is the follow-on.
- **Met where they live (projection).** So the lords/bandits/peasants roaming
  the overworld can actually be *met* and possessed, `enter()` runs
  `project_macro_npcs_into_subworld`: every persistent macro NPC within ±1 cell
  of the window centre becomes a full combat body (copied identity/faction,
  carried HP, sheet-derived `Combat`, data-driven hostility), each carrying a
  runtime `MacroOrigin{macro}` backlink to its source. The macro entity is never
  touched; projections are session-scoped.
- **Exit AS the body (remap + identity).** On `leave()` the possessed body's
  `MacroOrigin` decides both *where* and *who* the macro player resurfaces as.
  `macro_exit_cell_for_body` returns the origin's torus-wrapped cell (5e-1), so
  possessing a lord and leaving lands you on *the lord's* overworld cell; then
  `adopt_possessed_macro_as_player` moves the single `PlayerTag` onto that macro
  NPC itself (5e-2), so you leave **as** the lord — the flag rides a real
  `MacroNpcRuntime` body, not the hero husk. Any un-possessed exit falls back to
  the window centre, and the next macro tick re-heals the ordinary hero husk.
- **Identity survives save/load.** The ECS is never serialized — macro NPCs
  regenerate from `worldSeed` in a fixed creation order every boot — so the
  durable identity is a deterministic **spawn ordinal** (`ecs::MacroSpawnId`,
  stamped by the sole creation path `make_npc`, i.e. the Nth NPC created gets
  ordinal N). `PlayerState::possessedMacroSpawnId` stores the possessed lord's
  ordinal (**kSaveVersion 9→10**); on load, `reattach_player_to_macro_spawn`
  re-finds the regenerated NPC by ordinal and hands the flag over from the
  freshly-built husk. A missing ordinal (the lord died before the save, or the
  seed changed) falls back to the hero, changing nothing. Owner decision
  (`npc-sheet-possession-plan`): the possessed identity **must** persist.

## Increments

The player-as-entity → possession track, built subworld-first, one stage per
commit (build + validated smoke + `build/*_test` green each stage):

| Stage | What shipped |
|-------|--------------|
| 4a–4d | Subworld player promoted scalar → real `PlayerTag` combat entity: inert anchor → incoming damage → outgoing melee → outgoing spells, all via the universal paths (owner self-exclusion gone, muzzle purely geometric). |
| macro-4a | Macro player promoted scalar → minimal `Position + PlayerTag` flag; self-healing across the seam; exactly-one invariant established. |
| 5a | Subworld position authority inverted (entity `Position` authoritative; scalars a mirror). |
| 5b | `aim_target` forward-cone pick primitive. |
| 5c | The possession act — body-native combat, keybind **V** / console, non-mutating `player_display_hp`, AI/render skip the flagged body. |
| 5d | `project_macro_npcs_into_subworld` + `MacroOrigin` backlink (macro NPCs → combat bodies on enter). |
| 5e-1 | Exit **position** remap — land on the possessed body's macro origin cell. |
| 5e-2 | Exit **identity** remap — `adopt_possessed_macro_as_player` moves the macro `PlayerTag` onto the origin so you exit *as* the lord, and a deterministic `MacroSpawnId` ordinal (stored in `possessedMacroSpawnId`, **kSaveVersion 9→10**) re-finds the same lord after a save/load regenerates the NPCs (`reattach_player_to_macro_spawn`). Owner decision: identity **survives** save/load. |
| **5e-3** *(next)* | Re-**enter** a subworld while still possessing a lord preserves possession end-to-end. Today re-entry drops the flag to the hero — `clear_player_entity` strips-not-destroys a `MacroNpcRuntime` flag holder, so the lord survives as an autonomous NPC and nothing leaks, but the hero husk is rebuilt on enter. Full carry-through needs the enter path to stamp the possessed origin onto the new subworld body. |

## Data-driven extension

There is nothing to add per body. Any entity with a `Position` (and, for a
fight, a `CharacterSheet`) is possessable the moment it exists — a new creature,
a new NPC role, a projected macro lord all work with zero possession-specific
code. The flag never branches on type; adding "content you can become" is the
same one data row that adds the content itself ([monsters.md](monsters.md),
[rpg.md](rpg.md)).

## Connections

- **[microcombat.md](microcombat.md)** — the flagged body takes and deals damage
  through the same universal paths as any NPC; possession is body-native.
- **[rpg.md](rpg.md)** — the possessed body fights on its own `CharacterSheet`
  (`project_combat`), so strength follows the body, not the hero.
- **[microworld.md](microworld.md)** — projection and exit remap happen at the
  3×3 seam; the macroworld stays authoritative across it.
- **[macrosim.md](macrosim.md)** — the macro flag rides a leader NPC, so
  possession is the seed of "take over a party by taking its leader"
  (MASTER_PROMPT §9.4 parties).
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — the full int↔float HP bridge,
  reaper bracketing, and seam reconciliation.
