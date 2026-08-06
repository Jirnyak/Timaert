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

## Two lore-driven extensions, not yet built

- **A village's prosperity should be readable at a glance** — freed villages
  livelier and better built, mage-ruled ones hunched and poor, the Empire
  sterile and rich. Not decoration: the visible output of this simulation
  ([lore.md](lore.md) §10).
- **A cult-held settlement keeps producing and trading** — the economy never
  switches off, so the late game is not a map of holes; what changes is the set
  of rules that apply to the place ([lore.md](lore.md) §3.5).

## Connections

Feeds quest context (scarcity → delivery quests, [quests.md](quests.md)); part
of the macro simulation ([macrosim.md](macrosim.md)); gold ties to the RPG
system ([rpg.md](rpg.md)) and recruitment ([microcombat.md](microcombat.md)).
