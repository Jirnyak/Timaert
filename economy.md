# Economy — Экономика

Per-settlement inventories, prices, and a daily trade tick.

- **Code:** [macro/economy.h](src/macro/economy.h),
  [macro/world_tick.h](src/macro/world_tick.h)
- **TS origin:** `game/economy.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1 (economy)

## Model

- Each settlement holds an inventory and prices; `world_tick` runs the daily
  trade simulation as part of the macro sim.
- Buy/sell executes through the settlement Trade tab and the NPC-trade popup
  (native intentionally has no separate full-screen trade shell).

## Data-driven extension

Goods and price rules live in tables — add a good → one row.

## Connections

Feeds quest context (scarcity → delivery quests, [quests.md](quests.md)); part
of the macro simulation ([macrosim.md](macrosim.md)); gold ties to the RPG
system ([rpg.md](rpg.md)) and recruitment ([microcombat.md](microcombat.md)).
