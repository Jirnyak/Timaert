# Economy — Экономика

The HONEST economy (W2, 2026-08-07): resource is conserved, work is done by
bodies, money is a commodity, and prices come from stock. Nothing here is a
formula over population — every unit is gathered by an agent, carried in a
bag, crafted from inputs, eaten by a need, or paid across a counter.

- **Code:** [macro/econ_day.h](src/macro/econ_day.h) (the day's laws),
  [macro/commodity.h](src/macro/commodity.h) (the 14 nouns),
  [macro/currency.h](src/macro/currency.h) (coins + wallet math),
  [macro/economy.h](src/macro/economy.h) (price laws),
  [macro/deposit_layer.h](src/macro/deposit_layer.h) (mineral cells),
  [macro/agent_memory.h](src/macro/agent_memory.h) (memory + debt facts),
  [macro/world_tick.h](src/macro/world_tick.h) (the daily tick)
- **Arbiters:** tests/econ_v1_test (conservation to the unit),
  tests/price_law_test (arbitrage死), tests/woodcutter_gather_test (the
  work-loops), tests/trade_law_test (agent torus law), tests/deposit_layer_test

## The one dictionary

The economy's nouns are rows of the ONE item catalog: the bread a city bakes
and the bread in the player's bag are the same row (`items.cpp`), with one
truth of MASS (weights match `commodity.h` verbatim — link law in
econ_v1_test). Commodities tier as Raw / Vital / Instrument / Luxury.

## The store is the inventory

A landmark's universal `Inventory` is its market, granary, warehouse and
TREASURY in one. Landmarks are born MID-LIFE (`seed_landmark_inventory`):
a larder of bread, a stretch of every need, raw buffers per head, and the
kingdom's own coin — so the market trades on day one while the first
caravans find their legs.

## Raw comes from the WORLD

Grain = FT_Field cells; wood = the tree layer; clay / stone / iron = the
deposit layer (cells derived from seed: clay by rivers, minerals in the
mountains; the living cells ride the save WHOLE since v37, one sparse map
per KIND — a discovered vein lives IN its host quarry and deletes nothing).
Iron is FINITE — and as the world's iron runs out, the discovery chance
rises (depletion × 1/8 a day, now a Geology row of the ONE growth law —
[resources.md](resources.md)) until a stone quarry strikes a new vein:
world news in the player's journal. (Deposits are not yet DRAWN on the map
— the future prospecting skill is the planned reveal.)
Wildlife too is a world-cell stock (Session 16): `fauna_count` derives from
the cell's own spawn table, the hunt scars it through the one receipt path,
and beasts breed back where beasts still live (one head per 32-day epoch at
a living edge; a valley emptied whole is extinct) — see
[monsters.md](monsters.md) and [resources.md](resources.md).

## Work is agents

- **Woodcutter** (village's man since the home-link fix): chops through
  `set_tree_count`, hauls in his OWN bag, lands it in the home store.
- **Farmer** (the Peasant's behaviour): works the nearest home field for
  grain, same loop, same one law of labour (`kGatherPerWorkerDay`).
- **Caravan** (the CITY's agent): snapshots the home market at departure
  (AgentMemory MarketSnapshot — stock classes, a trader's memory, not a
  ledger), carries the city's plenty out to its villages and hauls back
  what the snapshot says the city LACKS, in a 256 kg hold of real cargo.
  Rob him and the cargo is yours.

## The day

Once a game day (`world_tick`): the city crafts — today's table first
(needs-ladder demand), then fair shares — everyone eats down the ladder,
and ONE continuous wellbeing (fed fraction × comfort) drives both the MOOD
band and the LOGISTIC population law: dP = r·P·(1−P/K)·drive, growth damped
toward K, starvation never softened. Famine and shortfall land on the
landmark as honest readouts. **The K in that formula is a recognized
defect** (canon-audit III.6): today it is the constant 16384 — a number
borrowed from the subworld's body cap, which is a render/sim bound, not a
fact about land. CANON S25 assigns **no population ceiling at all**: the
land's capacity decides how big a settlement is BORN
(`macro/settlement_score.h` already computes exactly that), and after birth
the only ceiling is supply — wellbeing falling as needs outrun provisioning.
K must be derived from the site capacity, not assigned.

## Money and trade

Money is a COMMODITY: every realm mints its own light coin (imperial crown,
magika sigil, republic mark, northern ring — value 1 each until exchange
rates arrive). There is no gold field anywhere: the player's wallet is coin
in his inventory like every other squad's, re-minted to his homeland at
chargen. Trade is universal BARTER by PACKAGE (owner ruling 2026-08-07):
both trade screens stage lines from BOTH shelves (+/− by the shared Amount
step, carry weight always shown), the footer faces the two totals, and ONE
Deal button settles the whole package through `barter_swap` — all-or-
nothing, counts checked against the pre-deal bags. The law: the player's
GIVEN value must cover the TAKEN; any excess is his own generosity. Coin is
a ware IN the package, always at FACE value on both sides (charisma pricing
a coin would mint money out of a round trip) — so "buying" is staging coin
against goods and "selling" is the reverse. `transfer_value` remains the
settlement half of scripted payments (recruit, rest, penalties): real coin
stacks travel, nothing is minted **in a barter deal**. (Outside barter the
world DOES mint — loot gold, quest rewards, spawn purses; see the honesty
debts below.)

**Prices come from STOCK** (the starcluster law): scarcity =
(demand + 1)/(supply + 1) po2-clamped to [1/4, 4], price = base × scarcity,
evaluated at POST-TRADE supply — every deal pays its own slippage. Charisma
and context (mood / temperament) stay the one `player_trade_price` law on
top. Arbitrage dies two ways (price_law_test): full-shelf round trips lose
to slippage, and any residual drip from a favourable pair (a generous
merchant genuinely overpays — temperament, not a bug) drains his FINITE
purse and stops dead.

**Debt is a FACT**: a penalty the wallet cannot cover becomes a Debt memory
(«кто-то должен кому-то столько-то»), remembered entity-about-entity and
SUMMED by the fact arithmetic (per-kind fold: snapshots replace, debts sum).
It bites when macro relations arrive.

## Deferred (known debts)

Deposit rendering on the map; seasonal harvest pulse + field exhaustion; a
miner agent for the deposit layer; econ facts raised to the journal/bus
(only vein discovery speaks today); exchange rates + the currency bourse;
coins minted FROM gold/silver; player invest-into-owned-city (waits for
ownership itself); taxes to the faction treasury.

**Honesty debts** (audit 2026-08-23, canon-audit.md §B — places where the
"honest economy" headline is not yet true):

- **The population floor mints people.** `settle_landmark_day` is called
  with `minPop = 10` (cities) / `5` (villages) in `world_tick.cpp`: a
  settlement cut down below the floor gets souls back from thin air and is
  effectively immortal. Defect against the conservation law (CANON S5 —
  a number appearing from nowhere is a defect).
- **Coin is only ever destroyed.** The one source is the world-birth seed
  (8/head in cities); the three sinks — hire, rest, quest penalty — pay no
  counterparty (`wallet_spend_up_to`, `ui/overlays.cpp`). Worse, loot gold
  is a **double mint**: `generate_loot_gold` (`items.cpp`) coins money into
  every corpse on top of the purse the same body was already minted at
  spawn (`npc_spawn.cpp`). Canon-audit B1–B3.
- **The village produces NOTHING.** All nine `kRecipes` rows carry
  `site = City` (`econ_day.h`), and `tick_villages_` only settles the day —
  yet the village eats the full needs ladder, so its norm is perpetual
  famine. Root cause: the Settlement/Village struct split
  ([landmarks.md](landmarks.md)).
- **The caravan confiscates, it does not trade.** `haul_between`
  (`npc_ai.cpp`) moves cargo both ways with no counter-value crossing the
  counter; the price law of `macro/economy.h` exists only for the player's
  two screens — the world itself does not know prices. Canon-audit B4.
