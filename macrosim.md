# Macrosim — Макросим (Mount & Blade / Dwarf Fortress)

The living macro world: squads, settlements, economy, and politics ticking over
time at world scale — the strategic layer above microworld combat.

- **Code:** [macro/world_tick.h](src/macro/world_tick.h),
  [macro/politik.h](src/macro/politik.h),
  [macro/economy.h](src/macro/economy.h),
  [macro/army.h](src/macro/army.h) (`SoldierSquad`),
  [macro/npc_ai.h](src/macro/npc_ai.h)
- **TS origin:** `game/world-tick.ts`, `game/politik.ts`, `game/npc-ai.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1, §Combat System

## Model

- **Daily tick:** settlement, village, and economy simulation advance with
  `WorldTime`.
- **NPC-as-soldier:** a squad is a list of concrete NPC records; garrisons
  regenerate by kind; hire price + a single per-kind upkeep number, discounted
  by charisma. No histograms, no RPS.
- **Politics:** kingdoms, capitals, roads, Voronoi territory, faction relations
  drive the world's shape ([landmarks.md](landmarks.md)).
- **Player = a flag.** The macro player is a minimal `PlayerTag` entity, not a
  bespoke object; *possession* (вселение) can move that flag onto any NPC — the
  seed of taking over a party by taking its leader (MASTER_PROMPT §9.4). See
  [possession.md](possession.md).

## Data-driven extension

Add a kingdom → one `kingdom_defs()` row. Add a hireable kind → one stat +
upkeep row. Balance baseline: 1 gold/day for the weakest hireable.

## Backend / GPU (the primary target)

The second headline compute case: **thousands of macro NPCs/squads.** The mass
is **GPU-resident** simulation; only the **chunk of cells around the player** is
CPU-embodied for full-fidelity interaction. Transfers happen at load/transition
boundaries or amortised across frames — never a per-frame stall, never a freeze.
See [ARCHITECTURE.md](ARCHITECTURE.md) §GPU-Driven Simulation.
