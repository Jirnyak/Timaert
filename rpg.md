# RPG System — РПГ система

Character sheet: attributes, XP/levels, items, inventory, equipment, loot.

- **Code:** [macro/attributes.h](src/macro/attributes.h),
  [macro/character_sheet.h](src/macro/character_sheet.h) (`CharacterSheet`),
  [macro/items.h](src/macro/items.h),
  [macro/state.h](src/macro/state.h) (`PlayerState`)
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
  across the seam). **Monsters have sheets too** (owner, 2026-08-20; CANON.md
  S14): a creature row IS an NPC row (`FaunaEntry` = `NpcTypeDef`), and every
  body — wolf or peasant — enters the world through the one door `emplace_body`
  (`make_character_sheet` → `apply_aura` → `project_combat`, sub/spawn.cpp).
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

  **The percent is a COLUMN (settled 2026-08-27, canon-audit A7 closed).** The
  law used to say "one rank is one percent, ceiling ×2" while four of the most
  expensive numbers in the game — `maxHp`, `maxMp`, `rawPhysDamage`,
  `rawSpellDamage` — were computed inline at `·0.05` per rank with no clamp,
  bypassing the helper and the ceiling. The owner's ruling was to legitimise
  the per-skill multiplier rather than flatten every skill to 1 %: each row of
  `kSkillDefs` states its own `pctPerRank`, so the ceiling is DERIVED and
  differs by skill (bodybuilding tops out at ×6 because its row says 5). One
  function — `skill_mult(skills, id)` — turns a rank into a multiplier, and
  every formula a skill governs asks it. The RANK cap (100) is still the law's,
  enforced at the one door into a rank.

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

- **Adding a skill is ONE ROW plus one weight per role** (2026-08-27). It used
  to touch five places — a named field in `Skills`, a `SkillId` value, a case in
  each of two `skill_value` switches, a row in the UI table and a weight column
  — and that was four too many. Ranks are now a flat `std::array<uint8_t, 32>`
  addressed by `SkillId`, the meanings are `kSkillDefs` rows carrying their own
  enum as a column (`rows_in_enum_order`, so a drifted table refuses to
  compile), and the character panel WALKS that registry instead of restating
  it. The envelope is fixed at 32 slots for 8 skills on purpose: a new skill
  does not move the save format. The per-role weight array is sized by
  `SkillId::Count`, so adding a skill is a compile error until every role says
  what it thinks of it — a silent zero would be a role that never trains it.

- **Items & inventory:** `Item`, `Inventory` (count/add/remove); one unified
  loot registry in `items.cpp` keyed by `lootId` (`roll_loot_profile`) — see
  [monsters.md](monsters.md).
- **Equipment:** slot surface in the character panel (UI slots are still
  placeholder — see README ledger).

## Data-driven extension

Add an item → one `item_catalog()` row. Add a loot drop → one loot-profile row
keyed by a stable `lootId` in `items.cpp`; point an NPC role or a monster's
`lootId` at it ([monsters.md](monsters.md)). Add a skill → one `kSkillDefs`
row (id, key, label, effect text, `pctPerRank`, and whether it buys a cost
DOWN) plus one weight per role. Express its effect through `skill_mult` — never
a private curve, and never a percent spelled inline.

## Renown — what the world thinks a band has done

A squad accumulates **renown** (`ecs::MacroNpcRuntime::renown`) by its deeds:
every fact it files pays it the fact kind's own `renown` column
(`macro/chronicle.h`). It is cumulative in the Mount & Blade sense — a great
deed or enough grind — and it is a **world quantity**, not a bookkeeping
counter for the chronicle.

Its first consumer is the chronicle itself: a band starts nameless and its
deeds are weather the ring forgets in a season; cross the bar and it is a
FIGURE, whose deeds enter the annals for good (CANON S20.1). The bar is
DERIVED — the most any single deed is worth in the table — so it reads as a
sentence: *become a figure by doing once what a figure does, or by adding up
enough lesser things.*

Other mechanics are meant to hang off the same number rather than grow
counters of their own (who follows you, how you are spoken to, who is worth a
contract). Adding one is a reader, not a system.

## Connections

XP is awarded to the killing blow's owner ([microcombat.md](microcombat.md));
gold/items flow through trade ([economy.md](economy.md)); rewards land here from
quests ([quests.md](quests.md)); mana gates spells ([spells.md](spells.md)).
