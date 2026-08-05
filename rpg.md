# RPG System — РПГ система

Character sheet: attributes, XP/levels, items, inventory, equipment, loot.

- **Code:** [macro/attributes.h](src/macro/attributes.h),
  [macro/character_sheet.h](src/macro/character_sheet.h) (`CharacterSheet`),
  [macro/items.h](src/macro/items.h),
  [macro/state.h](src/macro/state.h) (`PlayerState`)
- **TS origin:** `game/attributes.ts`, `game/items.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1 (attributes / items)

## Model

- **Attributes & levels:** stat block + XP curves in `attributes.h`.
- **Universal `CharacterSheet`:** one type — Attributes + Skills + Perks +
  LevelData — shared by the player (embedded in `PlayerState`) and every
  humanoid NPC (an ECS component). Combat is **derived** from it, never stored
  inside: the player keeps an authoritative `CombatStats`, an NPC its ECS
  `Health`/`Combat`, both projected through the same formulas (`project_combat`,
  [microcombat.md](microcombat.md)). In the subworld the player additionally
  mirrors that `CombatStats` onto a real ECS `Health`/`Combat` entity so incoming
  damage — and the player's own outgoing melee, whose `Combat.damage` is refreshed
  from the sheet each tick — flow through the universal combat paths (int↔float
  bridge — see [microcombat.md](microcombat.md); `CombatStats` stays authoritative
  across the seam). Monsters are sheet-less by design.
- **Possession is body-native:** the player is a movable `PlayerTag` flag, and
  when it moves onto another body (вселение), that body fights on its
  OWN `CharacterSheet` — the flag marks *who you control*, it never copies the
  hero's stats onto the target. See [possession.md](possession.md).
- **Attributes add, skills multiply.** THE progression law, and the one to
  extend by (`attributes.h`):
  - an **attribute** is what the body IS — it contributes DIRECTLY
    (`maxSp = 100 + END×10`, `carry = 100 + STR×10`);
  - a **skill** is what it has been TRAINED to do — it MULTIPLIES that,
    at **one percent per rank, capped at `kMaxSkillRank = 100`**.

  A rank therefore *reads as its percentage*: "Athletics 37" is +37 % speed,
  "Travel 37" is −37 % terrain stamina. Two helpers state it once —
  `skill_bonus_mult(rank)` = 1 + rank/100 for a bonus, `skill_cost_mult(rank)`
  = 1 − rank/100 for a skill that buys a cost DOWN — and every skill uses one of
  them. Linear and capped on purpose: an asymptotic curve cannot be balanced by
  reading it, and the ceiling should be a decision, not an accident. The cap is
  enforced at the single door into a rank (`spend_skill_point`), so no formula
  has to defend itself against an impossible rank.

  100 rather than a power of two: nothing indexes an array by rank, so a po2
  bound buys nothing, while "rank == percent" pays back every time a human reads
  a sheet. (It still fits a byte if ranks are ever packed.)

  **Mastery is meant to be reachable and to mean something.** One skill point
  per level across eight skills makes rank 100 a hundred levels poured into a
  single craft; at that point a bonus skill doubles what it governs and a cost
  skill removes that cost outright — a capstone, not an exploit, because the
  other terms of the formula (an overloaded pack, the exhaustion curve) are
  separate and keep biting.

- **One skill, one meaning.** `athletics` makes you FASTER, `travel` makes you
  get FURTHER on one bar of stamina — never both, or the sheet stops telling the
  player what his choice buys. Travel stamina is priced per macro CELL rather
  than per hour, which is what keeps the two orthogonal: a sprinter and a plodder
  pay the same for the same road and simply arrive at different hours
  ([macroworld.md](macroworld.md), `macro/movement_cost.h`). Speed itself is
  quoted in cells per GAME hour, so the length of a real-time day can be tuned
  as a matter of feel without moving the travel economy ([time.md](time.md)).

- **Adding a skill today** touches five places — the `Skills` fields, the
  `SkillId` enum, the two `skill_value` switches, the UI row table
  (`ui/overlays.cpp`) and the per-role weight table
  (`macro/character_sheet.h`). That is four too many for a game that will grow
  many more skills: the intended next step is ONE skill registry (flat table,
  ranks as a flat array indexed by `SkillId`) so a new skill is one row plus one
  weight column, exactly as factions and biomes already work.

- **Items & inventory:** `Item`, `Inventory` (count/add/remove); one unified
  loot registry in `items.cpp` keyed by `lootId` (`roll_loot_profile`) — see
  [monsters.md](monsters.md).
- **Equipment:** slot surface in the character panel (UI slots are still
  placeholder — see README ledger).

## Data-driven extension

Add an item → one `item_catalog()` row. Add a loot drop → one loot-profile row
keyed by a stable `lootId` in `items.cpp`; point an NPC role or a monster's
`lootId` at it ([monsters.md](monsters.md)). Add a skill → give it a rank field
and a `SkillId`, then express its effect through `skill_bonus_mult` /
`skill_cost_mult` — never a private curve — and give every NPC role a weight for
it so procedural sheets can spend on it.

## Connections

XP is awarded to the killing blow's owner ([microcombat.md](microcombat.md));
gold/items flow through trade ([economy.md](economy.md)); rewards land here from
quests ([quests.md](quests.md)); mana gates spells ([spells.md](spells.md)).
