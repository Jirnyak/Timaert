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
  subworld fight (macro/macro_stock.h — the roster is a stock like population,
  tree count and the wild headcount `fauna_count`, Session 16).
* **Kill the leader but not the troops → the survivors go to the deserter pool**
  (`GameState::deserterPool`, which is serialized today and has no gameplay
  writer yet). The macro sim later raises deserter and bandit squads out of that
  pool, which is where a good part of the world's danger should come from.

**A hostile squad on the map FORCES an encounter** (owner, 2026-08-06), the way
Mount & Blade does it: running into it opens a pre-battle interaction — talk,
pay, flee or fight — and fighting drops you into the subworld against the very
people the roster names. So meeting is geometric, but it is not silent: the map
stops you first. BUILT (Session 15): `detect_forced_encounter` +
`GameSubStateKind::PreBattle` open the action-table screen, and
`route_macro_npc_attack` became its "fight" row, exactly as planned.

**AUTO-RESOLVE is part of the same system, and it is not optional** (owner,
2026-08-06): two AI squads meeting each other must produce a winner without a
subworld, or the macro sim cannot run a war at all. One resolver, fed by what the
rosters already say — levels, numbers, the leader's sheet and its buffs, plus
context (terrain of the cell, fatigue, who ambushed whom) — and it must agree
with the fought version well enough that a player who fights by hand is not
playing a different game from the one the world plays around him. The player's
own battles may use it too (the M&B "auto-resolve" button) — same function, same
inputs, no second law of combat.

**SHIPPED (Session 15, 2026-08-06, seven commits 4da0c69 → 89af9ed).** Where
the code stands now:

* **Every macro entity IS a squad** — `ecs::SquadRoster` on every macro NPC
  (the entity is the leader, the roster holds everyone else; empty roster =
  a squad of one). The player is the same shape: his entity leads,
  `PlayerState::army` is his roster.
* **The roster is a macro stock** — `MacroStock::Roster` (subject = the
  squad's `MacroSpawnId` ordinal, key/receipt `detail` = the member's
  entityId), so members embody below as DERIVED bodies with a loan and die
  by name through `settle_macro_debt`. Dead leader + empty roster = no
  squad, emergently. `drain_dead_leader_squads` (macro/squad.h) is the
  deserter pool's first writer.
* **The leader's buff travels through the sheet** — macro/aura.h: a SET of
  modifiers collected from source functions (perk rows live — the Leader
  perk is +1 vit to every soldier; charisma/skills/items are future sources
  at the same door), applied into a member's own sheet at body birth and in
  the resolver alike.
* **ONE auto-resolve** — macro/auto_battle.h: fighter worth from the same
  sheet numbers subworld bodies fight with; `squad_power` is also the AI's
  flee-or-fight law; agreement with `steer_battle` pinned by test
  (tests/auto_battle_test.cpp). A leader's head is never diced: he dies only
  when his whole roster died with him (a lone loser therefore falls).
* **The macro world wars** — npc_ai.cpp: a transient `SquadIndex` + one
  threat step before every role behaviour (perceive → flee/pursue by
  `squad_power`, traits bend courage; same-cell hostiles = a battle settled
  through the ledger; victors rob and level by the player's own XP curve).
  The underground drive perceives but does not resolve
  (`allowAutoBattle=false`) — projected bodies own their own fight.
* **The map stops you** — the forced pre-battle screen (main.cpp,
  `GameSubStateKind::PreBattle`): a TABLE of actions — talk / pay off /
  flee / fight (the old `route_macro_npc_attack`, rebuilt as promised) /
  auto-resolve, the player's side priced by his own body's numbers
  (sub/engine.h player melee identity). Smoke: `force_encounter`.
* **Squads are created as data** — `SquadSpec` + `spawn_squad`
  (macro/npc_spawn) through the one `make_npc` door; `ecs::SquadOrders` is
  a waypoint route and only that — the route's presence IS the order
  (owner's ruling); new kinds of squad AI are type rows with their own `ai`
  column. Console: `spawn_squad`, `squad_orders`.

Known residue: member projection is enter-only (like the leader's always
was); ordinal reuse 19.24 unchanged (the S17 snapshot is the cure); balance
knobs (kAmbushEdge, payoff, flee odds) await playtests. ~~Squads still march
over water on a flat SP price~~ — Session 21's ONE movement-cost law SHIPPED
2026-08-06: try_move pays the player's own terrain rows off the baked cost
grid, steers greedily around water, and an unpayable ocean drowns the LORD
(he is the squad's avatar — owner ruling); the leader's sheet reaches the
macro march (maxSp from END, travel discount, marathon regen, spd pace).
Full record: proposals/session-prompts.md «Сессия 21».

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
