# The Damage Door — work_vector §4, built; dice + 9×9 landed (phase 3)

Track write-up, 2026-08-27; updated 2026-09-05 (RPG phase 3: the dice door,
the 9×9 type symmetry, the hybrid armour law, the crit — commits
`4efe468..`). This is THE doc for the blow: what the law is, where each
piece lives, and the contracts that keep the two scales of battle honest. The
intent it implements is [work_vector.md](work_vector.md) §4, with CANON S12,
S13, S14 and S16 riding along.

## The law

**One door subtracts hp.** Every weapon — a sword, a spell, a cliff, a dev
cheat, a harness script — strikes through `apply_damage`, and death from any
of them is indistinguishable by protocol. What differs between weapons is a
ROW, not a code path.

**One roll makes a wound (phase 3, CANON S13).** Damage is NdM dice of the
weapon/spell/creature row through the ONE assembly `roll_strike`
([macro/damage_types.h](src/macro/damage_types.h)):
`(roll NdM + attribute add) · skill percent`, integer end to end. Attributes
ADD, skills MULTIPLY, and there is no to-hit — the physics of the swing or
the projectile decides contact. Fixed damage is the degenerate die Nd1 and
consumes zero rng. LCK's ONE reader is the crit (`crit_procs`, 1 LCK = 0.5%):
a crit is the blade finding the ARMOUR GAP — it ignores mitigation and
multiplies nothing. The verdict is rolled at the strike site (melee) or rides
the projectile from the cast/loose (spells, missiles) as
`DamageSource.critical` / `Projectile.critical`.

**Nine damage types = nine armour columns (CANON S13).** `DamageType` —
Slash/Pierce/Blunt + the six school elements — is ONE enum for blows and
armour both; a body's defence is an `ArmorProfile` (nine columns on the
creature row AND the item row), and the blow argues with the column of its
own type. A spell's type is its tag's row (`kSpellTagDefs.dmgType`, the canon
remap as data); a weapon's is its item column; a fist is Blunt.

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
| THE door | [sub/damage.h](src/sub/damage.h) | `apply_damage(reg, target, src, amount, kind, type, bus)` — the only code that subtracts subworld hp, in one fixed order: the already-dead guard, mitigation of the blow's own type column (skipped when `src.critical` — the gap was found), subtract-then-judge, the protocol stamp, `Dead` once, `NpcDeath` under the ONE `PlayerTag` guard. `amount` is integer like every combat quantity. `apply_lethal_damage` is the execution form (dev cheat, harness kills; a fractional bar dies to the ceiling blow). |
| THE dice | [core/dice.h](src/core/dice.h) | `Dice{n,m}` as table data; `roll_dice` (exactly n draws, Nd1 = zero), `crit_procs` (LCK·5 per mille), `dice_mean_x2` (exact expectation ×2 in ints — the auto-resolve's read). |
| THE strike assembly | [macro/damage_types.h](src/macro/damage_types.h) | `roll_strike(rng, dice, flatAdd, multPct, luck)` — every blow's algebra, and `strike_mean_x2` its expectation. Carriers: `ecs::Combat` (dice/flatAdd/multPct/luck/dmgType, filled by `project_combat` for NPCs and `hand_strike_fields` for the player), `SpellDef.dice` + `spell_strike` for casts. The player's weapon IN HAND (`weapon_in_hand`, macro/anatomy.h) picks the dice, the type and WHICH weapon skill multiplies; bare hands are `kFistDice` 1d2 Blunt through Unarmed. A spell's percent is its scaling column × its SCHOOL's rank (phase 5, `spell_mult_pct` — the school multiplies a cast exactly where a weapon skill multiplies a swing; sleeping Body/Mind tags multiply by nothing). The player's sheet everywhere in this table is the EFFECTIVE one (phase 4, `player_effective_sheet`). |
| The kinds | same file | `kDamageKinds` — melee / spell / fall / script / dev, two columns: `attributesKiller` and `armourApplies`. "No XP for gravity" and "plate does not soften a fall" are both the Fall row's DATA, not a skipped component or an `if` at one call site. |
| The world remembers | [chronicle.md](chronicle.md) | A death that the auto-resolve settles is filed as a FACT at the cell it happened on, and pays the killer renown priced by what the victim was worth. The trail a monster leaves is made of these. |
| Mitigation | `mitigate_amount()` in [macro/damage_types.h](src/macro/damage_types.h), read by `mitigate()`/`defense_of()` in [sub/damage.cpp](src/sub/damage.cpp) | The second step INSIDE the door — THE HYBRID (owner verdict 2026-09-05): armour A of the blow's own type cuts the LARGER of A itself (the threshold — a blow the plate outweighs never lands; 100% reduction is real and countered by crits, big dice and the right type) and `dmg·A/(A+10)` (the percent — a big blow is softened, never zeroed). The branches cross at `dmg = A + kArmorHalving`. Integer law. `kArmorHalving = 10` is the historical plain blow the rows were tuned against. Armour comes from the creature ROW's nine columns (`NpcTypeDef::armor`, scalar era converted with `uniform_armor`); what a body WEARS adds per column (`worn_armor`). The law's HOME is damage_types.h because both scales read it — the macro layer may not include the ECS-facing `damage.h`. |
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
| **Armour** (2026-09-05) | `mitigate_amount` — the hybrid, per type column | `fighter_power` (auto_battle.h) credits the PERCENT branch as effective HP (`(kArmorHalving + armour)/kArmorHalving` over the physical-mean column) and the swing as `strike_mean_x2` — the same algebra taken at its expectation. Two deliberate BIASES, not second laws: the threshold branch and the crit both depend on the opponent, which a per-fighter scalar cannot know, so heavy armour soaks slightly more in a fought battle than the resolver credits and crits pierce slightly more. The agreement test (`auto_battle_test`) softens its hand-fought hits through `mitigate_amount` itself. |
| **XP price of a body** (2026-08-29) | `npc_xp_reward(type, level)` | the same `npc_xp_reward` (`xp_for_fallen`, macro/squad.h) |

CANON S14 made literal: a lord grows from a fight underfoot
exactly as he grows from one on the map. The killer's body names its squad —
`MacroOrigin` for a projected leader, the roster receipt's spawn ordinal for a
member.

**One XP law (2026-08-29).** What a body is WORTH is a column of its own row:
`NpcTypeDef::xpReward`, authored as `5 × (baseLevel + 1)` per row, and the one
reader `npc_xp_reward(t, level)` adds `(level − 1) × 5` for a levelled
instance — the identity reproduces the old `10 × level` payout at the row's
own level, but the number now lives with the creature instead of inside a
formula. `exp_from_fight` is DELETED (a tombstone comment in `attributes.h` is
all that remains); the three award sites — the player's kill, the NPC
leader's kill underfoot, and the auto-resolve's `xp_for_fallen` — all ask the
same function.

## Tests that guard the door

`dice_door_test` (11 checks) locks the roll contract: bounds, Nd1 = zero
draws, exactly-n draws, the crit law's frequencies, determinism, and that
`dice_mean_x2` IS the expectation of `roll_dice` — the two scales share one
algebra by test. `damage_door_test` (105+ checks) rolls every kind through
the door and pins the one protocol, the attribution column, the two guards,
the hybrid law's shape (full block under the threshold, softening past the
crossover, monotone in armour, identity at zero) and that the door routes
through `mitigate_amount` of the blow's own column. `melee_reach_test` pins the surface law with the frog/troll pair
(same range, same distance, different answer — because a troll is wider) plus
grid-arm/full-scan parity and the -1 fallback. `auto_resolve_speaks_test`
pins facts, the kill price and the rolled spoils with their negative controls
(a null channel still settles; outlaws cost nothing; a defeat pays nothing).
`rpg_loot_test` derives every expectation from the row, so retuning the purse
touches no test.

## Left standing, deliberately

- ~~Worn ARMOUR still protects nobody underfoot~~ — CLOSED, phase 4б
  (2026-09-06): `defense_of` reads the player's `BodyEquipment` off his MACRO
  squad entity when the flagged body carries none of its own («макро — это
  контекст для микромира», no projected copy to go stale on a dungeon
  re-dress) — the same read the strike already does for the weapon in hand.
  Keyed to `PlayerTag`, else-branch so a body that one day carries its own
  wardrobe cannot be counted twice; pinned with its negative control in
  `damage_door_test`.
- ~~`Health` stays float STORAGE~~ — CLOSED, phase 4г (2026-09-06): the bar
  is `int{hp, maxHp}` (save v80). Fractional regen lives in its carry
  accumulators (macro/player_recovery.h), never in the bar; the execution
  blow (`apply_lethal_damage`) is exactly the remaining number, no ceil.
- **NPC weapon skills** — `multPct` is 100 for every NPC (their rows hold no
  weapons); their strength rides `flatAdd` through Armsmaster as before.
- **`sight` stays unauthored** — every row answers 200 and only the alert chain
  reads it. Owner's ruling: it waits for the vision track (CANON S11), where
  it becomes a real column instead of a number invented here.
- **The auto-battle does not touch `Population`.** A squad wiped on the map
  pays its Roster row; whether a garrison's death should also thin the
  landmark that raised it is an economy question (the honest-loan track, CANON
  S5), not a defect of the blow.
