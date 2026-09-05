# Microcombat — Микробой (меч и магия ARPG)

**Unified, in-subworld combat — there is no separate battle mode.** Player,
NPCs, garrison soldiers, and bandits share **one stat block** and **one engine**.
Sword-and-magic ARPG resolution happens as normal subworld play. **All combat is
honest 3D** — melee range, projectile trajectories (`vx, vy, vz`), spell blasts,
NPC missile aim, and hit detection all operate in full XYZ space with the camera
look vector `(cos(yaw)*cos(pitch), sin(yaw)*cos(pitch), sin(pitch))` driving
player spell direction.

- **Code:** [macro/army.h](src/macro/army.h) (`CombatTemplate`),
  [macro/character_sheet.h](src/macro/character_sheet.h) (`project_combat`),
  [sub/ai.cpp](src/sub/ai.cpp), [sub/engine.h](src/sub/engine.h),
  [sub/spawn.h](src/sub/spawn.h)
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
  `ecs::BodyRadius` (`kPlayerBodyRadius = 1.5`, engine.cpp — a **recognised
  defect**, canon-audit A9: every human in the one table is 0.55, and the 1.5's
  only justification is the dead TS prototype; do not cite it as the norm);
  `target_radius()` reads that first, then
  `SubworldAi.radius`, then `Sprite.scale`, then a coarse fallback — the player
  needs the explicit one because it is the camera (no `Sprite`) and input-driven
  (no `SubworldAi`). `combatStats.currentHp` stays macro-authoritative: an
  int↔float bridge PULLs it onto the entity's `Health` at tick-top and PUSHes the
  reconciled value back at tick-end — and the same PULL refreshes the entity's
  `Combat` strike fields (dice/flatAdd/multPct/luck/dmgType since phase 3,
  2026-09-05) from the sheet AND the weapon actually in hand
  (`hand_strike_fields`, macro/anatomy.h — bare hands are 1d2 Blunt through
  Unarmed). Outgoing **melee** now flows from that `Combat`
  too: `tick_player_melee` rolls `roll_strike` from it (see combat.md for the
  one strike algebra and the crit), and the NPC actor loop still never swings it because
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
  removed the last one) and **no faction shield at all** (owner decision
  2026-07-30): a bolt strikes whoever stands in its path, ally or enemy.
  `Projectile.friendlyFire` survives only as the AoE-blast marker the spell
  tables use — it no longer gates who can be hit. A caster is kept off its OWN
  muzzle *purely by geometry*: the player (`caster_spawn_offset()`) and NPC
  missiles (`spawn_npc_missile`) alike spawn the bolt `casterRadius +
  projectileRadius + 2` ahead, fully clear of the caster's hit shell, and it then
  flies away. The caster's own AoE blast still catches them if they stand in it.
- **One body size, one place.** `body_radius()` ([sub/body.h](src/sub/body.h)) is
  THE half-width of a body, and every weapon asks it: melee reach, projectile
  contact, blast, crowd separation, the battle unit descriptor. Order, most
  specific first: explicit `ecs::BodyRadius` → the ONE table row (`NpcTypeDef` —
  there is one table for creatures and humanoids, so there is one lookup,
  `row_for`; the row's own `radius` first, then its `combat.bodyRadius`) →
  `SubworldAi.radius` → `Sprite.scale` → `kBodyRadiusFallback` 0.55.
  It used to exist twice — melee read the tables, projectiles did not, and their
  fallbacks were 0.55 and 6.0 — so a body was a different size depending on which
  weapon was pointed at it.
- **A projectile leaves from the EYE, not the feet** (`player_muzzle_z()` =
  feet + `kBodyEyeM`, sub/height.h). That is the same point the aim direction is
  taken from, so the crosshair and the bolt share one line at every range — no
  aim compensation anywhere. NPC missiles raise **both** ends by the same number
  (eye to eye); raising only one would tilt every shot.
- **Collision is SWEPT, not sampled.** `find_projectile_hit` tests the segment
  the bolt crossed this tick and returns the EARLIEST hit along it, so a bolt
  strikes the first body it reaches. Point sampling used to leave a dead zone out
  to ~8.5 units — the whole melee band — because a magic bolt strides 6.25 units
  per tick against a ~2.7 contact radius, and it made speed a liability. On the
  birth tick the sweep also covers the muzzle stretch, from the caster's centre
  to the spawn point; the caster is excluded from **that added stretch alone**
  (it passes through his own body by construction) and is an ordinary target
  everywhere else.
- **The 3×3 window is a closed box.** Four walls in XY, a floor of terrain and
  masonry, and a ceiling — the same one flying bodies are clamped to. A bolt that
  leaves through any face is simply gone; the sky is not a special direction.
- **Every body has a sheet — monsters included** (owner, 2026-08-20; CANON.md
  S14/S16). A creature row IS an NPC row (`FaunaEntry` is an alias of
  `NpcTypeDef`, [macro/fauna.h](src/macro/fauna.h)), and every body enters the
  world through the ONE door `emplace_body`
  ([sub/spawn.cpp](src/sub/spawn.cpp)): `make_character_sheet` →
  `apply_aura` (the leader's buff lands IN the sheet) → `project_combat`. A
  wolf and a peasant are the same record projected the same way.
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
([monsters.md](monsters.md)). Add a monster → one row of `kNpcTypeDefs` (the
ONE table; `FaunaEntry` is an alias of `NpcTypeDef`).

## Backend — the crowd is CPU (owner's ruling 2026-08-20)

Thousands of combatants are a **CPU** workload: one O(N) steering pass over bucket grids
sized from the bodies' own data (`sub/movement.{h,cpp}`), inside the universal 16384-body
cap. The GPU draws them — it does not simulate them. GPU-resident crowd simulation is
deferred to the far future; see [CANON.md](CANON.md) S5, S13 and ARCHITECTURE.md §GPU is graphics; the world is CPU.
