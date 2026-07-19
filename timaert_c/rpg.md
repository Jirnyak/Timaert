# RPG System — РПГ система

Character sheet: attributes, XP/levels, items, inventory, equipment, loot.

- **Code:** [macro/attributes.h](src/macro/attributes.h),
  [macro/items.h](src/macro/items.h),
  [macro/state.h](src/macro/state.h) (`PlayerState`)
- **TS origin:** `game/attributes.ts`, `game/items.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1 (attributes / items)

## Model

- **Attributes & levels:** stat block + XP curves in `attributes.h`.
- **Items & inventory:** `Item`, `Inventory` (count/add/remove); loot tables
  `kNpcLoot[]` in `items.cpp` (data-driven).
- **Equipment:** slot surface in the character panel (UI slots are still
  placeholder — see README ledger).

## Data-driven extension

Add an item → one table row. Add a loot drop → one `kNpcLoot[]` entry keyed by
NPC kind.

## Connections

XP is awarded to the killing blow's owner ([microcombat.md](microcombat.md));
gold/items flow through trade ([economy.md](economy.md)); rewards land here from
quests ([quests.md](quests.md)); mana gates spells ([spells.md](spells.md)).
