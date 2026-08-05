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
  `WorldTime` — one integer tick counter, one day every 8192 of them
  ([time.md](time.md)). The queue is budgeted per simulation STEP, not per
  drawn frame, so how fast the world catches up no longer depends on the frame
  rate of the machine.
- **AI cadence:** a macro NPC thinks every `kAiTicks = 32` WORLD ticks — half an
  hour of game time — not on a wall-clock timer. Underground, where the day
  stretches by `kSubworldTickDivisor`, so does their thinking
  ([time.md](time.md)).
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

## Resource squads — owner-approved DESIGN (not yet built)

Woodcutters are the first instance of ONE universal loop, part of the
economy (`ResourceId` Grain/Wood/Iron/Clay/Silver/Gems) — nothing here may
be hardcoded to "woodcutter":

1. **Spawn.** A village spawns worker squads; their number is a function of
   `population` + the village's needs (`EconomyState`). A squad ROLE is a
   data row — `{job, ResourceId, target field/terrain, yield per trip}` —
   so miners/clay-diggers later are one row each, no engine branches.
2. **Squad = macro party** (the M&B model, [possession.md](possession.md)):
   a leader NPC + roster. **Carry capacity is the sum of the members' own
   `get_carry_capacity(Attributes, Skills)`** — the same per-sheet weight
   system the player already uses; squad capacity is contextual from member
   tables, never a per-role constant.
3. **Farm.** Woodcutters O(n)-search the nearest forest-class cells of the
   **TreeLayer field** (the `ai_woodcutter` / `TreeGrid` path must migrate
   off the legacy `app.trees` point list) and harvest via `set_tree_count`
   — the count drops, the map sprite thins, the subworld agrees, the save
   carries it. Harvest amount is bounded by remaining capacity.
4. **Return.** A squad REMEMBERS its home landmark (village id), walks
   back, deposits `Wood` into the village `Inventory`/`eco.resources`, and
   dissolves back into `population` — the same universal
   squad ↔ settlement lifecycle garrisons use.
5. **Regrowth = an ECOSYSTEM** (owner decision — no baseline cap). One
   universal field-regrowth on the existing `WorldTime`, timescale YEARS of
   game time, rate fully CONTEXTUAL from the procedural map itself:
   - the 3×3 tree gradient (dense neighbours seed faster — the same
     smoothness rule as the derivation);
   - biome + climate (temperature/moisture): deserts and mountains grow
     poorly, wet warm biomes fast — the SAME classification inputs
     `derived_tree_count` already reads;
   - the seasons system (`macro/seasons.h`): no growth in winter.
   Rates are data rows per biome, to be balanced BY TESTS later.
   Engineering note for the implementer: keep growth lazily computable from
   `(count, lastChangedDay, context)` so ticks stay cheap and saves stay
   sparse; ecosystem growth may push cells past their derived value and
   eventually seed virgin neighbours — how far expansion reaches is a
   balance knob, solve its persistence cost when it lands (owner is fine
   iterating with tests).

## Backend / GPU (the primary target)

The second headline compute case: **thousands of macro NPCs/squads.** The mass
is **GPU-resident** simulation; only the **chunk of cells around the player** is
CPU-embodied for full-fidelity interaction. Transfers happen at load/transition
boundaries or amortised across frames — never a per-frame stall, never a freeze.
See [ARCHITECTURE.md](ARCHITECTURE.md) §GPU-Driven Simulation.
