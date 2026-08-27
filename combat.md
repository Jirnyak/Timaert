# The Damage Door — work_vector §4, built

Track write-up, 2026-08-27. Increments 1–6 shipped (commits
`3a82a12..38abcd3`). This is THE doc for the blow: what the law is, where each
piece lives, and the contracts that keep the two scales of battle honest. The
intent it implements is [work_vector.md](work_vector.md) §4, with CANON S12,
S13, S14 and S16 riding along.

## The law

**One door subtracts hp.** Every weapon — a sword, a spell, a cliff, a dev
cheat, a harness script — strikes through `apply_damage`, and death from any
of them is indistinguishable by protocol. What differs between weapons is a
ROW, not a code path.

**One rule decides an enemy.** A relation below the threshold is hostility,
spelled once; everything else is a form of that answer — the battle masks are
its baked form, the stance colours its continuous projection, the per-entity
door adds only session state.

**One law of battle at both scales.** What a death pays — facts, reputation,
loot, experience — does not depend on whether the player fought it by hand or
resolved it on the map.

## The pieces

| Piece | Where | What it is |
|---|---|---|
| THE door | [sub/damage.h](src/sub/damage.h) | `apply_damage(reg, target, src, amount, kind, bus)` — the only code that subtracts subworld hp, in one fixed order: the already-dead guard, mitigation, subtract-then-judge, the protocol stamp, `Dead` once, `NpcDeath` under the ONE `PlayerTag` guard. `apply_lethal_damage` is the execution form (dev cheat, harness kills). |
| The kinds | same file | `kDamageKinds` — melee / spell / fall / script / dev, two columns: `attributesKiller` and `armourApplies`. "No XP for gravity" and "plate does not soften a fall" are both the Fall row's DATA, not a skipped component or an `if` at one call site. |
| Mitigation | `mitigate()` / `defense_of()` in [sub/damage.cpp](src/sub/damage.cpp) | The second step INSIDE the door, and the socket is filled (2026-08-27). A blow keeps `kArmorHalving / (kArmorHalving + armour)` — asymptotic, so armour SOFTENS and never makes a body immune, and a creature in its own skin (armour 0) is the limiting case of the law rather than a branch around it. `kArmorHalving` is not picked: it IS `kPlayerBaseMeleeDamage`, the game's own plain blow, which is what makes "armour 10" readable without a table — it halves a plain blow. Armour comes from the creature ROW (`NpcTypeDef::armor`, the owner's «броня массовки = число из строки»); a body that also WEARS things adds them at one line in `defense_of`, and **no damage site changes** to gain it. |
| THE hostility rule | [macro/state.h](src/macro/state.h) `factions_hostile` / `player_hostile_to` | One threshold over the one matrix. Six hand-spelled comparisons converged here, and the last private resolver (the macro Aggressive pursuit, which chased regardless of standing) now asks it. Both ends of the scale — `kHostileThreshold`, `kAllyRepThreshold` — and the price of a kill, `kKillRepPenalty`, live in [macro/faction.h](src/macro/faction.h) beside the matrix they cut. |
| Faction as an INSTANCE | [ecs/components.h](src/ecs/components.h) `NPCKind.factionIdx` | Owner's ruling, 2026-08-27: «в записи существа вообще не должно быть фракции — моб/нпц это чистый нпц». `NpcTypeDef.factionId` is gone; the SPAWNER dresses the body — a town in its kingdom's colours, a landmark in its `spawnFaction`, a squad in its leader's, the open land in the spawn law's own `wildFaction` column. The same wolf is wildlife in a meadow and a demon in a ruin. |
| THE reach | [sub/targeting.cpp](src/sub/targeting.cpp) `melee_pick_target` | In reach ⇔ `dist − body_radius(target) ≤ range`, ranked by the surface GAP — the same law the NPC strike always walked (`reach + radius[target]`). Attack reach is ONE number per row (`attackRange`); a spear will modify that number, never introduce a second system. Body WIDTH is one column ([macro/npc.h](src/macro/npc.h) `radius` + `npc_body_radius`); `CombatTemplate::bodyRadius` — a shadow copy authored by zero rows — is dead. |
| THE purse | [macro/npc.h](src/macro/npc.h) `kNpcPurse` | What a body of this row carries. It lived inside `make_npc`, so only persistent macro bodies had one and the subworld paid out through a faction-keyed multiplier — a second wealth vocabulary that the faction ruling made outright wrong (the same wolf was six times richer under a ruin's banner). One table now, read by both worlds. |
| Corpse loot | [macro/items.h](src/macro/items.h) `CorpseLootContext` | Owner's design: «контекст от таблицы мобов × зоны сложности (0..255) × богатство ландмарка/экономика, и расширяемо». A PRODUCT OF CONTRIBUTIONS, each 1.0 and silent when its system says nothing: the body (row purse × level), the cell's danger byte, the place's `wealthMul` (a landmark_registry column). It grows by a FIELD, exactly as `MacroWorld` does. |
| The way OUT | [macro/macro_world.h](src/macro/macro_world.h) `BattleFactSink` | The macro layer must not see the bus (L3), but a silent system is invisible to the story layer. The envelope carries the channel; the app plugs it in once and raises each fact as the ordinary `NpcDeath`. An auto-resolved kill counts toward a kill-N quest because it IS the same fact. |

## What the door killed

Protocol divergences, each now impossible by construction and pinned by
`damage_door_test`:

- a spell that killed the **player** emitted `NpcDeath` into quest kill-tallies
  (no PlayerTag guard) — while melee and gravity both guarded it;
- a spell that killed a **kindless** body emitted nothing, while every other
  weapon reported `kNoNpcType`;
- the spell path had **no already-dead guard**: a corpse could be struck again,
  and the re-attribution moved the kill to whoever kicked it;
- gravity and the dev cheat stamped `DamageFx` **without** `HitFlash`, breaking
  the invariant `components.h` states about those two travelling together;
- lethality was a **prediction** on two paths (`hp - damage <= 0`) and a fact on
  the others.

## The two scales agree

| What a death pays | Underfoot (the reaper) | On the map (the auto-resolve) |
|---|---|---|
| Facts | `NpcDeath` from the door | `BattleFact::Death` → the same `NpcDeath` |
| Kill reputation | `kill_is_no_crime` column, `kKillRepPenalty` | the same column, the same price |
| Loot | `roll_loot_profile` + the purse law | `roll_fallen_spoils` — the same doors |
| Player XP | `award_exp` with the wis dividend | `award_exp` with the wis dividend |
| NPC leader XP | `award_leader_xp` (increment 6) | `award_leader_xp` |

The last row is CANON S14 made literal: a lord grows from a fight underfoot
exactly as he grows from one on the map. The killer's body names its squad —
`MacroOrigin` for a projected leader, the roster receipt's spawn ordinal for a
member.

## Tests that guard the door

`damage_door_test` (98 checks) rolls every kind through the door and pins the
one protocol, the attribution column, the two guards and the identity of
mitigation. `melee_reach_test` pins the surface law with the frog/troll pair
(same range, same distance, different answer — because a troll is wider) plus
grid-arm/full-scan parity and the -1 fallback. `auto_resolve_speaks_test`
pins facts, the kill price and the rolled spoils with their negative controls
(a null channel still settles; outlaws cost nothing; a defeat pays nothing).
`rpg_loot_test` derives every expectation from the row, so retuning the purse
touches no test.

## Left standing, deliberately

- **Mitigation is the identity** until equipment exists. The socket is the
  point: armour is columns in one function, not a change at five sites.
- **`sight` stays unauthored** — every row answers 200 and only the alert chain
  reads it. Owner's ruling: it waits for the vision track (CANON S11), where
  it becomes a real column instead of a number invented here.
- **The auto-battle does not touch `Population`.** A squad wiped on the map
  pays its Roster row; whether a garrison's death should also thin the
  landmark that raised it is an economy question (the honest-loan track, CANON
  S5), not a defect of the blow.
