# Possession — Вселение (player = an NPC with a flag)

The player is not a special object. It is **one flag PER SCALE riding an
ordinary ECS body** (scale split, owner verdict 2026-09-10): `ecs::PlayerTag`
in the MACROWORLD only — «кем я на карте», the player's own squad by default,
a possessed lord while he wears one, never leaving the macro side even while a
scene is live — and `ecs::AvatarTag` in the SUBWORLD only — «моё тело здесь»,
the hero husk or a possessed scene body, born with the scene and dead with it.
*Possession* moves the flag of its own scale onto a different body: you become
whatever you inhabit, and the body you left reverts to an ordinary NPC. Every
universal path (combat, targeting, render, AI, loot, death) respects the flag
of its scale, so there is no player special-case to maintain — and no scene
pass can ever find a cell-coordinate entity, because `view<AvatarTag>`
physically cannot see the macro side.

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
  (`PlayerState::possessedMacroSpawnId` — с v10, a historical number;
  `kSaveVersion` is 42 today, and identity via the `MacroSpawnId` ordinal
  still survives the save)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Combat System /
  §L2 — Microworld (the subworld player & possession block)

## Model

- **The player is a flag — one per scale.** `PlayerTag` (macro) and
  `AvatarTag` (scene) are empty tag components. The tagged entity is the
  player on that scale; nothing else marks the player.
- **Замысел флага — только ввод и камера** (владелец, 2026-09-03, дословно:
  «замысел со вселением простой — это что игрок = НПЦ, то есть PlayerTag
  просто отвечает за инпут от игрока и камеру-центровку; при этом сама
  камера — это агностик, независимая система, она может и свободной быть;
  и в принципе по игре может меняться, за кого игрок играет — это вообще
  не важно»). Следствие для каждого мирового закона: тело с флагом живёт
  ВСЕ судьбы обычного NPC — и закон, который уничтожает/растворяет тела
  (ротация артелей, будущие слияния), обязан решить, что происходит с
  флагом, а не предполагать, что игрок «не такой». Открытая дыра —
  problems.md §35 (растворение ротации у крыльца не проверяет PlayerTag).
- **Exactly one `PlayerTag` at all times, exactly one `AvatarTag` while a
  scene is live** — the system-wide invariants since the split. `PlayerTag`
  rides the player's ordinary squad entity through the whole
  macro→sub→macro cycle (it used to MOVE onto the scene body, which put a
  cell-coordinate entity into a dozen scene passes); `AvatarTag` appears
  with `spawn_player_entity` and dies with `clear_player_entity`.
  Smoke-guarded across the full cycle (PlayerTag=1 macro always; avatar
  0→1→0).
- **Two homes across the seam.** On the macro map the flag rides the
  player's ORDINARY squad (NPCKind + roster + runtime + bars — the merge of
  2026-08-27/09-10). `ensure_macro_player_entity(gs, world)` heals it at
  boot, at save-load, and at the top of every macro tick. In the subworld
  the avatar rides a full combat body (`Position + Pools + Combat +
  BodyRadius + SubworldTag`), whose `Position` is authoritative
  intra-subworld (the scalars are a derived mirror). Dropping macro
  possession on scene entry lives in `spawn_player_entity` (the one macro
  act of that function — moved out of `clear_player_entity` so leave()
  cannot strip the squad's own flag).
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
  (console `possess`) — with `possess_by_id` as the debug by-id path; on the
  macro map a `control <id>` console command is the follow-on.
  **The player keybind (V) died 2026-09-06** (owner: вселение — не игроцкая
  кнопка «просто как механика»; в игру оно придёт ЗАКЛИНАНИЕМ вселения, как и
  телепорт — своим спеллом). The machinery below is untouched and is exactly
  the door that spell will walk through; today it is reachable by the dev
  console and exercised by the `console` / `subworld_exit_remap` smokes.
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
| 5c | The possession act — body-native combat, console `possess` (the V keybind died 2026-09-06 — a future possession SPELL replaces it), non-mutating `player_display_hp`, AI/render skip the flagged body. |
| 5d | `project_macro_npcs_into_subworld` + `MacroOrigin` backlink (macro NPCs → combat bodies on enter). |
| 5e-1 | Exit **position** remap — land on the possessed body's macro origin cell. |
| 5e-2 | Exit **identity** remap — `adopt_possessed_macro_as_player` moves the macro `PlayerTag` onto the origin so you exit *as* the lord, and a deterministic `MacroSpawnId` ordinal (stored in `possessedMacroSpawnId`, **kSaveVersion 9→10**) re-finds the same lord after a save/load regenerates the NPCs (`reattach_player_to_macro_spawn`). Owner decision: identity **survives** save/load. |
| **5e-3** *(open debt — status not re-verified since July 2026)* | Re-**enter** a subworld while still possessing a lord preserves possession end-to-end. As last verified, re-entry drops the flag to the hero — `clear_player_entity` strips-not-destroys a `MacroNpcRuntime` flag holder, so the lord survives as an autonomous NPC and nothing leaks, but the hero husk is rebuilt on enter. Full carry-through needs the enter path to stamp the possessed origin onto the new subworld body. This is the track's standing open item; re-check against the code before building on it. |

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
