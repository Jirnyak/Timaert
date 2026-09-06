# RPG System — РПГ система

Character sheet: attributes, skills, XP/levels, the effective-sheet door,
items, inventory, equipment, loot.

> **Status (rewritten whole 2026-09-06, after Phases 0–6 settled).** CANON.md
> S13 (damage), S14 (sheet/progression/dice) and S15 (schools) are the design
> of record; [combat.md](combat.md) is THE damage doc. Phases 0–2 (save v78):
> the eight attributes, the skills, the learn law, creation. Phase 3 (v79):
> dice, crit, the 9×9 hybrid armour, weapon-in-hand. Phases 4–6 (v80–v81):
> the effective-sheet door, the integer derived house, school wiring, the
> three world readers. **The perk system was PURGED whole 2026-09-03** pending
> its redesign — the game carries no perk state at all (no enum, no bag, no
> points, no save bytes); only the aura door survives, returning empty.

- **Code:** [macro/attributes.h](src/macro/attributes.h) (attributes, skills,
  `CombatStats`, `DerivedBonuses`, the learn/spend doors),
  [macro/character_sheet.h](src/macro/character_sheet.h) (`CharacterSheet`,
  `make_character_sheet`, `effective_sheet`, `project_combat`),
  [macro/player_entity.h](src/macro/player_entity.h)
  (`player_standing_bonuses` / `player_effective_sheet` — THE door),
  [macro/items.h](src/macro/items.h), [macro/anatomy.h](src/macro/anatomy.h)
  (equipment slots, `weapon_in_hand`, `hand_strike_fields`),
  [core/dice.h](src/core/dice.h), [macro/damage_types.h](src/macro/damage_types.h)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1

## One sheet, every body

- **Universal `CharacterSheet`:** one type — Attributes + Skills + LevelData —
  shared by the player (embedded in `PlayerState`, the only STORED sheet) and
  every humanoid NPC (whose sheet is DERIVED on demand from
  `(role, level, seed)` via `make_character_sheet` — same point economy, role
  weights decide where the points went). **Monsters have sheets too** (owner,
  2026-08-20; CANON S14): a creature row IS an NPC row, and every body — wolf
  or peasant — enters the world through the one door `emplace_body`
  (sub/spawn.cpp): `make_character_sheet` → `effective_sheet(sheet,
  squadBonuses)` (the leader's aura, folded in at birth) → `project_combat`.
- **Combat is derived, never stored:** `project_combat(sheet, base)` fills the
  ECS `Combat` (dice / flatAdd / multPct / luck / dmgType) and the hp ceiling
  from the sheet; the level is captured IMPLICITLY through the points the
  sheet spent, so no caller may apply a second per-level multiplier.
  `ecs::Health` is **integer** `{hp, maxHp}` (phase 4г, save v80) — the whole
  combat arithmetic is int end to end; fractional REGEN lives in its own
  accumulators, never in the bar.
- **NPC sheets are MONOTONIC in level** (2026-09-03): the generator draws
  from two level-free RNG streams, so a level-N sheet is the level-N−1 sheet
  plus exactly one more attribute pick and one more skill pick — a leader can
  never get weaker by levelling. `leader_sheet_seed(spawnOrdinal)` is the one
  home of the seed law; macro never stores a leader's sheet, only derives it.
- **Possession is body-native:** the player is a movable `PlayerTag` flag,
  and the body it lands on fights on its OWN `CharacterSheet` — the flag
  marks *who you control*, it never copies the hero's stats onto the target.
  The player KEYBIND died 2026-09-06 (a possession SPELL replaces it); the
  machinery stays — see [possession.md](possession.md).

## Attributes — the canon eight

`AttributeId`: **STR, END, INT, WILL, SPD, LCK, CHA, WIS** (`intl`/`wil` in
code — `int` is taken). Flat `uint8` array in a 16-slot envelope, every slot
born at 1, ceiling 255, enforced at the one door `spend_attribute_point`.
What each point buys (`calculate_combat_stats` / `calculate_derived`):

| attr | effect (the row's own words) | the law |
|---|---|---|
| STR | +1 physical damage, +10 kg carry | `carry = (100 + STR·10) × skill_mult(Weightlifting)`; phys damage via Armsmaster below |
| END | +10 max HP, +5 max SP | `maxHp = (100 + END·10) × skill_mult(Bodybuilding)` |
| INT | +1 spell damage | spell damage via Spellcraft below |
| WILL | +10 max MP, +5 max SP | `maxMp = (100 + WILL·10) × skill_mult(Meditation)` |
| SPD | asymptotic move speed | `moveSpeedPct = 100 + 100·spd/(spd+50)` — +50% at 50, ceiling +100% |
| LCK | shifts the game's dice | crit chance = 0.5% per point (`crit_procs`, core/dice.h); a crit ignores armour |
| CHA | 1% off prices and payroll per point | `tradeDiscountPct = CHA`, one spelling (`cha_trade_discount_pct`) |
| WIS | +1% EXP per point | `expMultPct = 100 + WIS` |

**The SP bar has two owners on purpose:** `maxSp = 100 + ((END+WILL)>>1)·10` —
an integer shift of the SUM (END 5 + WILL 4 → +40, not +45). No skill
multiplies the bar itself; Marathon multiplies its RECOVERY.

**The derived block is integer** (phase 4в): `DerivedBonuses` carries whole
percent (`100 = ×1`), the floor happens ONCE in `calculate_derived`, and
consumers (`award_exp`, `calculate_squad_upkeep`, the pace laws) take pct
ints. `rawPhysDamage = STR × skill_mult_pct(Armsmaster)/100`,
`rawSpellDamage = INT × skill_mult_pct(Spellcraft)/100`.

## Skills — 33 rows in the 64 envelope

`kSkillDefs` — **33 rows** (the canon 32 plus **Unarmed**, appended v79 as
the eighth weapon skill), in a fixed 64-slot envelope so a new skill never
moves the save. Groups: weapons ×8 (Sword, Axe, Spear, Mace, Dagger, Bow,
Staff, Unarmed — 10%/rank), armour ×4 (Heavy, Light, Unarmored, Shield —
10%/rank), the six schools (Fire/Water/Air/Earth/Arcane/Void Magic —
10%/rank), the generic pair (Armsmaster, Spellcraft — 5% ON TOP of the
final number), body ×5 (Bodybuilding, Meditation ×5%; Marathon, Athletics
×1%; Weightlifting ×10%), road & world ×4 (Travel −1% cost, Acrobatics,
Scouting, Prospecting ×1%), husbandry ×4 (Trade, Quartermaster −1% cost,
Foraging −1% cost, Learning ×1%). Leadership is absent deliberately —
it appends when its work is decided (CANON S14).

- **Attributes add, skills multiply.** THE progression law, and the one to
  extend by: an **attribute** is what the body IS — it contributes DIRECTLY;
  a **skill** is what it has been TRAINED to do — it MULTIPLIES that, at
  **its row's `pctPerRank`, capped at `kMaxSkillRank = 100`**. A rank
  therefore *reads as its percentage*: "Athletics 37" is +37% speed,
  "Travel 37" is −37% terrain stamina. ONE door states it —
  `skill_mult_of(SkillId, rank)` (sheet-reading wrapper `skill_mult`; integer
  twin `skill_mult_pct` for the combat house — growth direction only, no
  combat law buys a cost down). The row's `pctPerRank` column and
  `buysCostDown` flag decide the size and the direction; a cost skill at the
  capstone removes its cost outright (Travel 100 = free terrain, Foraging
  100 = the squad feeds itself off the land).

  Linear and capped on purpose: an asymptotic curve cannot be balanced by
  reading it, and the ceiling should be a decision, not an accident. 100
  rather than a power of two: nothing indexes an array by rank, and
  "rank == percent" pays back every time a human reads a sheet.

- **THE LEARN LAW: rank 0 is ignorance.** There is no separate "known" bit —
  rank 0 IS not knowing the skill, and `spend_skill_point` refuses it
  ("unknown: learn first"). `learn_skill` / `spend_learn_pick` are the one
  door out, fed by creation's 5 picks today and by teachers/events tomorrow.
  Mastery is reachable and means something: rank 100 is 1 learned + 99
  spends — a hundred levels poured into a single craft.

- **The grant is 1:1:** `try_level_up` gives +1 attribute point and +1 skill
  point, nothing else. (A perk point every 10th level is a COMMENT awaiting
  the perk redesign — no code.) `exp_to_next_level(level) =
  1000·level·(0.1·level+1)`; `award_exp` scales by `expMultPct` rounding
  half up.

- **One skill, one meaning.** `athletics` makes you FASTER, `travel` makes
  you get FURTHER on one bar of stamina — never both, or the sheet stops
  telling the player what his choice buys. Travel stamina is priced per
  macro CELL rather than per hour, which is what keeps the two orthogonal
  ([macroworld.md](macroworld.md), `macro/movement_cost.h`).

- **Adding a skill is ONE ROW plus one weight per role.** Ranks are a flat
  `std::array<uint8_t, 64>` addressed by `SkillId`, the meanings are
  `kSkillDefs` rows carrying their own enum as a column
  (`rows_in_enum_order` — a drifted table refuses to compile), and the
  character panel WALKS that registry. The per-role weight array is sized by
  `SkillId::Count`, so adding a skill is a compile error until every role
  says what it thinks of it. The panel's skill tooltip is ARITHMETIC off the
  row (`±pctPerRank% effect`), so a retuned percent cannot leave a stale
  sentence behind.

## The effective-sheet door (phase 4)

**One read of the player's numbers:** `player_effective_sheet(world, player)`
= base sheet + `player_standing_bonuses` — what he WEARS (worn `Bonus` rows
off `BodyEquipment` on his squad entity) plus what BURNS (sustained spells,
scaled by the BASE sheet's school ranks — the standing sum cannot read the
sheet it is itself a term of). `effective_sheet` returns a clamped COPY:
attributes floor at 1 (division laws), ranks clamp [0,100].

**Writes go to the BASE sheet only** — creation, level-up, learning, spends.
Every reader walks the door: the bars, march and burden (macro and subworld
— both legs pay through it), the strike and pace of his body, the cast, XP
of both layers, the squad aura at spawn, CHA prices / carry / upkeep, the
auto-battle side, the character panel. The panel shows EFFECTIVE numbers (a
worn ring is part of what he is) while its Learn/+ doors are gated by the
BASE rank — a charm's +3 to an unlearned craft must not eat a learn pick's
job; a working bonus rank on an unlearned skill prints honestly.

**The bars follow the standing sum, gated:** `recompute_combat_maxima(eff)`
runs ONLY when `player_standing_bonuses` changes (`App.lastStandingBonuses`)
— «+2 END on the plate fattens the SP bar», taking it off thins it back, and
there is no free heal (maxima recompute PRESERVES current pools; the full
restore belongs to creation and the level-up, which say they heal). Never
recompute per-step unconditionally: the whole derived block would be
rewritten, thawing runtime state a harness froze (the seed-999 conservation
failure, 2026-09-06).

**Worn armour protects underground by READING the macro squad entity**
(`defense_of`, sub/damage.cpp): the body with `PlayerTag` fetches
`BodyEquipment` off the `PlayerSquadTag` entity — no copy, «макро — это
контекст для микромира».

## Dice, strike, armour (the law lives in combat.md)

`Dice{n, m}` — `Nd1` is a mechanical constant costing zero RNG draws;
creatures and most spells roll Nd1, real dice live where variance is meant
(dagger 1d4, fist 1d2). One strike assembly for sword and spell
(`roll_strike`): dice + flatAdd, ×multPct, LCK crit at 0.5%/point that
IGNORES armour. Mitigation is the hybrid `max(A, dmg·A/(A+10))` over the
9×9 type table; auto-resolve credits the same numbers at expectation
(`strike_mean_x2`), crit deliberately not credited. `weapon_in_hand` picks
the first weapon on an unblocked grip; bare hands = Unarmed skill, Blunt.
Full law and rationale: [combat.md](combat.md), CANON S13.

## Schools (phase 5)

The school is a COLUMN of the spell-tag table (`kSpellTagDefs`: tag →
dmgType + school), not of the skill table. A spell reads **ITS school's
rank** into `spell_mult_pct` (school × `scalingPower`, the same shape as a
weapon skill × a swing), and its stat-effects scale through `spell_bonus`
(school is a REQUIRED argument). Spellcraft adds its 5% generic layer on
top via `rawSpellDamage`. **Body and Mind tags SLEEP** (`SkillId::Count` —
their spells scale by Spellcraft alone) «до апдейта паладинов и клериков»
(CANON S15). `spell_heal` and `spell_radius` read strength, not school.
See [spells.md](spells.md).

## Three world readers (phase 6)

- **Trade** — the price law (`trade_buy/sell_price`, macro/economy.cpp)
  takes the bargaining edge from the Trade ROW's percent
  (`skill_mult_of(Trade, rank) − 1`; 1%/rank today — owner ruling
  2026-09-06, the inline 2% died). Readers: both UI price doors (city
  trade, NPC barter) and the caravan vendor (`leader_trade_rank_` derives
  the leader's rank from his seed). CHA's 1%/point rides the same formula
  by its own door.
- **Foraging** — `feed_squads_daily` (macro/npc_ai.cpp): daily bread need =
  `roster × (100 − rank)/100`; rank 100 feeds the squad off the land. The
  player's rank comes through the effective sheet, a lord's from his
  derived sheet.
- **Scouting** — `squad_sight_cells` × `(100 + rank)/100`; the rank is
  cached on `MacroNpcRuntime.scoutRank` by the ONE refresh door
  (`refresh_leader_travel_stats` — same door as the march caches; the
  player refreshes it with his EFFECTIVE sheet every macro tick).

## Rest and recovery — one law, three bars

`kRestRegenPctPerHour = 1/8`: resting restores 12.5% of each bar's OWN
maximum per game hour — a full bar in 8 hours for any body. Rest is the
ONLY recovery; the march heals nothing (`kMarchRecoveryPct = 0` gates all
three bars, both layers, since 2026-09-03). Marathon multiplies SP recovery
alone (rank 100 → a 4-hour night). SP spends and mends through ONE signed
carry (`settle_sp_carry`); a march debt is not forgiven by the fraction
accumulator. Standing in open sea is not resting (`player_can_make_camp`).

## Items, inventory, equipment

`Item`, `Inventory` (count/add/remove); one unified loot registry in
`items.cpp` keyed by `lootId` (`roll_loot_profile`) — see
[monsters.md](monsters.md). Equipment is anatomy-slotted
([macro/anatomy.h](src/macro/anatomy.h)): worn `Bonus` rows feed the
standing sum, worn armour feeds `defense_of`, the gripped weapon feeds
`hand_strike_fields`. An `ItemDef` row carries dice, damage type, its
governing skill, armour profile, bonuses — an item that changes the fight
is one row.

## Renown — what the world thinks a band has done

A squad accumulates **renown** (`ecs::MacroNpcRuntime::renown`) by its
deeds: every fact it files pays it the fact kind's own `renown` column
(`macro/chronicle.h`). It is cumulative in the Mount & Blade sense — a
great deed or enough grind — and it is a **world quantity**, not a
bookkeeping counter for the chronicle.

Its first consumer is the chronicle itself: a band starts nameless and its
deeds are weather the ring forgets in a season; cross the bar and it is a
FIGURE, whose deeds enter the annals for good (CANON S20.1). The bar is
DERIVED — the most any single deed is worth in the table — so it reads as a
sentence: *become a figure by doing once what a figure does, or by adding
up enough lesser things.*

**The victim prices the deed.** Every macro entity already carries what it
is worth — its own renown — so a deed pays its row's base plus a tenth of
what the victim was worth. Fame is made of fame, by construction. See
[chronicle.md](chronicle.md).

**Every macro entity earns it**, not squads alone: a band, a city, a
people. The microworld has none — a mob, a projectile, a house have no
standing to win or lose.

Other mechanics are meant to hang off the same number rather than grow
counters of their own (who follows you, how you are spoken to, who is worth
a contract). Adding one is a reader, not a system.

## Ledger — sleeping rows and known debts (honest, 2026-09-06)

Deliberate sleepers (rows exist, no reader yet — content will wake them):

- **Learning, Acrobatics, Prospecting, Quartermaster** — zero readers
  (XP multiplies by WIS only; payroll discounts by CHA only).
- **The four armour skills** — `defense_of` reads creature row + worn
  armour; the skills await their mitigation hook.
- **7 of 8 weapon skills** are unreachable through the catalog: one weapon
  row exists (`wpn_dagger`). Bare hands exercise Unarmed. Items are content.
- **Earth and Void** schools have no spell in the 8-row catalog; Water only
  through Ice.
- **Player squad foraging sleeps**: Adventurer upkeep is `kNpcUpkeepNone` —
  the player's roster eats no bread at all (owner question pending).

Known debts (defects to burn down, not design):

- **Foraging and Scouting bypass the table column**: `(100 ± rank)/100`
  spelled inline (npc_ai.cpp) — numerically the row's 1% today, but a
  retuned column would not follow. Trade was the same and was fixed
  through `skill_mult_of`; these two await the same pass.
- **XP grants of quest_engine / effect_applicator land on the BASE sheet**
  (no world in the event bus) — a +WIS hat does not scale them; the door
  arrives with the PlayerState move (~550 sites, its own track).
- **The codex article PerksSkills and the console `levelup` help still
  describe perks** — a purged system (стартовые хвосты, NEXT_SESSION §4).

## Data-driven extension

Add an item → one `item_catalog()` row. Add a loot drop → one loot-profile
row keyed by a stable `lootId`. Add a skill → one `kSkillDefs` row (id,
key, label, effect text, `pctPerRank`, `buysCostDown`) plus one weight per
role. Express its effect through `skill_mult` — never a private curve, and
never a percent spelled inline (the ledger above lists the two surviving
violations). Add a spell school → a `kSpellTagDefs` column value; add an
attribute → append inside the 16 envelope with its law in
`calculate_combat_stats`/`calculate_derived`.

## Connections

XP is awarded to the killing blow's owner
([microcombat.md](microcombat.md)); gold/items flow through trade
([economy.md](economy.md)); rewards land here from quests; mana gates
spells ([spells.md](spells.md)); the chronicle prices deeds
([chronicle.md](chronicle.md)); possession rides the flag
([possession.md](possession.md)).
