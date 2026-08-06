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

## Squad as THE macro entity (owner's design, 2026-08-06)

**A macro entity is a SQUAD, not a person.** The map carries squads; the
subworld embodies their members. Everything below follows from that one line.

* **Meeting is geometric, not scripted.** Run through the subworld onto a cell
  where a squad stands on the map, and you meet exactly those people there.
* **Members come from the ONE table.** Each roster entry embodies as its
  NPC/monster row (`kNpcTypeDefs` / the fauna rows) through the single body
  birth in `sub/spawn.cpp`. A squad is CONTEXT — it decides who stands there and
  under whose banner, never what a body is made of.
* **Slot 0 is the leader, always.** In the macro world a squad ALWAYS has one,
  because the RPG system hangs off it: the leader's character sheet buffs its
  troops. The player is the same shape — the player's army is a squad and the
  player is its leader — so possession and lordship need no special case.
  A squad with no leader is the degenerate form: a faceless band of whatever its
  units are ("a squad of peasants"), which is what deserters and bandit mobs
  look like.
* **In the subworld the leader is a special NPC**, embodied by the same rule as
  the player's own body.
* **A squad cannot hold zero members.** Kill them all and the squad is gone from
  the map: no ghost entity, no empty banner. This is the macro write-back of a
  subworld fight (macro/macro_stock.h — the roster is a stock like population
  and tree count).
* **Kill the leader but not the troops → the survivors go to the deserter pool**
  (`GameState::deserterPool`, which is serialized today and has no gameplay
  writer yet). The macro sim later raises deserter and bandit squads out of that
  pool, which is where a good part of the world's danger should come from.

Where the code stands against this (2026-08-06): macro NPCs are still INDIVIDUAL
entities (`ecs::MacroNpcRuntime`), squads exist only as the player's
`SoldierSquad` roster, and the deserter pool has no writers. The body birth
(`emplace_humanoid_body`) and the macro-stock ledger are the two pieces already
built in this direction.

### Three rulings that pin the shape (owner, 2026-08-06)

1. **A lone wanderer is a squad of one, and it is its own leader.** There is no
   second kind of macro entity: a peasant on the road, a witch, a caravan and a
   lord's warband are the same structure at different sizes. One meeting path,
   one death path, one save path — the thing we keep paying for elsewhere.
2. **The leader's buff reaches a trooper THROUGH THE SHEET, and it is not one
   number.** The leader's perks and artifacts may modify the troops in arbitrary
   ways — +10 HP to every soldier, or anything else a perk cares to say — so the
   mechanism must be a SET of modifiers sourced from the leader's own sheet and
   gear, applied when a member's body is born. Not a global `moraleMult` beside
   the sheet: a second source of strength next to the sheet is exactly the split
   this project keeps closing.
3. **Kill the leader and the squad lives on, leaderless, until the fight ends.**
   That is the "faceless squad of peasants" form. Only when the fight is over
   (or the player leaves) do the survivors fall into the deserter pool, out of
   which the macro sim later raises deserter and bandit squads.

Implementation order these imply — the roster is a macro STOCK
(macro/macro_stock.h), so members embodied below are borrowed and their deaths
are paid back up; a squad that reaches zero members is removed from the map by
the same rule, not by a special case.
