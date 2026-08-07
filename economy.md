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
mountains; drained-and-discovered state persists as overrides). Iron is
FINITE — and as the world's iron runs out, the daily discovery roll rises
(depletion × 1/8 a day) until a stone quarry strikes a new vein: world news
in the player's journal. (Deposits are not yet DRAWN on the map — debt.)

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
band and the LOGISTIC population law: dP = r·P·(1−P/K)·drive, K = 16384 =
2^14 (the subworld's own NPC cap), growth damped toward K, starvation never
softened. Famine and shortfall land on the landmark as honest readouts.

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
stacks travel, nothing is minted in a deal.

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
