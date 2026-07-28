# Microcombat — Микробой (меч и магия ARPG)

**Unified, in-subworld combat — there is no separate battle mode.** Player,
NPCs, garrison soldiers, and bandits share **one stat block** and **one engine**.
Sword-and-magic ARPG resolution happens as normal subworld play.

- **Code:** [macro/army.h](src/macro/army.h) (`CombatTemplate`),
  [macro/character_sheet.h](src/macro/character_sheet.h) (`project_combat`),
  [sub/ai.cpp](src/sub/ai.cpp), [sub/engine.h](src/sub/engine.h),
  [sub/spawn.h](src/sub/spawn.h)
- **TS origin:** `subworld/engine.ts`, `subworld/ai.ts`, `game/army.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Combat System

## Model

- **One combat curve for humanoids and the player.** Each humanoid NPC (and
  the player) carries a `CharacterSheet`; its ECS `Health`/`Combat` are
  **derived** from that sheet via `project_combat(sheet, base)`, which reuses
  the *exact* player formulas (`calculate_combat_stats` / `calculate_derived`).
  The per-role `CombatTemplate` is the authored **base**: the HP/damage floor
  plus the attack identity (speed, range, cooldown, melee/missile, missile
  params) — attributes/skills/level then scale hp/damage on top. No RPS table;
  per-unit variance comes from the sheet, not a per-type stat row.
- **The player is a combat target like any other.** In the subworld the player
  is a real ECS entity (`PlayerTag + Health + Combat + BodyRadius + SubworldTag`)
  struck by melee, projectiles, and blasts through the *same* paths as any NPC —
  there is no player special-case in the hit code. Its hit size is an explicit
  `ecs::BodyRadius` (1.5); `target_radius()` reads that first, then
  `SubworldAi.radius`, then `Sprite.scale`, then a coarse fallback — the player
  needs the explicit one because it is the camera (no `Sprite`) and input-driven
  (no `SubworldAi`). `combatStats.currentHp` stays macro-authoritative: an
  int↔float bridge PULLs it onto the entity's `Health` at tick-top and PUSHes the
  reconciled value back at tick-end — and the same PULL refreshes the entity's
  `Combat.damage` from the sheet. Outgoing **melee** now flows from that `Combat`
  too: `tick_player_melee` reads its damage/range/cooldown instead of
  recomputing, and the NPC actor loop still never swings it because
  `is_player_side()` makes the player non-hostile-to-itself. Outgoing **spells**
  now carry the player's real entity id too (4d): `player_entity_id()` stamps
  each player-cast projectile's `ownerId` exactly as an NPC missile carries its
  firer's, so the old `ownerId == 0` sentinel is retired — ownership is decided
  purely by the owner entity's tags (`PlayerTag`/`PlayerSoldierTag`).
- **Possession is body-native.** The player is one movable `PlayerTag` flag;
  `possess_entity` hops it onto a body you aim at (`aim_target` cone, keybind
  **V** / console) and the inhabited body fights on its OWN sheet-derived
  `Combat`/`Health` — possess a lord ⇒ strong, a rat ⇒ weak. `gs.player` (the
  hero) is the preserved revert target; the flagged body is what enemies target,
  what dies, and what the HUD reads (`player_display_hp`). Full model in
  [possession.md](possession.md).
- **Projectiles are universal — everyone can hit everyone, the caster included.**
  A spell projectile just flies; it carries no exclusion of its own caster (4d
  removed the last one). Faction rules protect allies for ordinary bolts, but a
  `friendlyFire` bolt (the fireball) bypasses them — so a caster is kept off its
  OWN muzzle *purely by geometry*: the player (`caster_spawn_offset()`) and NPC
  missiles (`spawn_npc_missile`) alike spawn the bolt `casterRadius +
  projectileRadius + 2` ahead, fully clear of the caster's hit shell, and it then
  flies away. The caster's own AoE blast still catches them if they stand in it.
- **Monsters are sheet-less** (`NPCKind.type & 0x100`): their `Combat`/`Health`
  stay the raw `FaunaEntry` row — a plain `CombatTemplate`, never projected.
- **Hostility is faction-driven:** derived from `factions[...].relation`;
  `kHitRepPenalty` on attacking neutrals, `kHostileThreshold` flips a faction.
- **Engine:** `tick_combat_move` for melee + missile; `kCrowdPenalty` spreads
  gangs into natural formations; `kDetectionRadius` awareness.
- **Death:** killing blow → XP to the killer's owner; corpse holds data-driven
  loot resolved through the one `roll_loot_profile` registry
  ([monsters.md](monsters.md), [rpg.md](rpg.md)); zone danger gates whether the
  player may leave.

## Data-driven extension

Make a kind hireable/soldier-capable → tag it + set one stat row + one upkeep
number in the registry. Add loot → one loot-profile row keyed by `lootId`
([monsters.md](monsters.md)). Add a monster → one `FaunaEntry` row.

## Backend / GPU (the primary target)

This is the headline compute-simulation case: **thousands of combatants**. The
crowd is **GPU-resident** (compute shaders over packed SSBOs); an NPC is
**embodied** to the CPU/ECS the instant the player can act on it (reticle /
engagement radius), then de-embodied afterward — same entity, no discontinuity.
Kernels obey the four crowd rules (data packing, lookup buffers, branchless
damage math, cohort sorting) and the **no-stall** transfer rule (embody the few,
never read back the mass). See [ARCHITECTURE.md](ARCHITECTURE.md)
§GPU-Driven Simulation.
