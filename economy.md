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
econ_v1_test) and ONE truth of PRICE — `ItemDef::value` is the single anchor
(`CommodityDef::baseValue`, a second price column with drift potential, was
axed 2026-08-29). Commodities tier as Raw / Vital / Instrument / Luxury.

## The conservation law — credit BEFORE debit (2026-08-29)

CANON S5 made mechanical: **no door destroys value on a full container**.
Every mover credits the receiving side first and debits only what was
accepted — `transfer_value` moves coin stack-by-stack and returns what
actually crossed; `barter_swap` settles the whole package on COPIES and
commits all-or-nothing; `econ_gather_day` books the take into the store
before scarring the deposit; `econ_produce_day` puts the INPUTS BACK if the
output finds no room; the gatherer's take (`ai_gatherer`) scars the field
only after his bag accepted the load; `haul_between` (the caravan) refuses
the leg the far side cannot take; loot and quest rewards report the DELTA
that landed, not the promise (an unpayable gold reward becomes a debt fact,
not minted coin). The one known stray: `deliver_bag_home` (npc_ai.cpp) still
debits the bag before asking the store — a full home store burns the haul; a
one-line fix awaiting its turn.

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

- **Peasant crew** (АУКЦИОН ЦЕЛЕЙ, CANON S10, 2026-09-02 — professions are
  DEAD as roles): the daily rotation raises N generic peasant crews per
  village; each takes an errand {verb, object} by a score-weighted roulette
  over the goal table (kGathererDefs rows: grain/wood/ore/clay/stone/silver)
  plus the sell run — score is MONEY by the price law (home stock_price ×
  a worker-day take / road). The gather loop itself is unchanged: take
  through the registry row into the OWN bag, land it in the home store,
  one law of labour (`kGatherPerWorkerDay`). The sell errand walks the
  vendor machine (surplus + tithe to the nearest city) — the Vendor TYPE
  died with the professions.
- **Caravan** (the CITY's agent): snapshots the home market at departure
  (AgentMemory MarketSnapshot — stock classes, a trader's memory, not a
  ledger), carries the city's plenty out to its villages and hauls back
  what the snapshot says the city LACKS. His hold is his OWN back since
  2026-08-29: capacity = `rt.carryCap` = the leader's sheet
  (`get_carry_capacity`) × the row's `haulMult` (Caravan ×32 — the mules) —
  the flat `kCaravanCapacityKg = 256` constant is dead, and the overload law
  prices the same number. Rob him and the cargo is yours.

## The day

Once a game day (`world_tick`): the city crafts — today's table first
(needs-ladder demand), then fair shares — everyone eats down the ladder,
and ONE continuous wellbeing (fed fraction × comfort) drives both the MOOD
band and the population law: dP = r·P·drive, r quoted per season. There is
**no K and no floor** (both crutches are dead): CANON S25 assigns no
population ceiling — supply is the only cap (the old kPopCarryingCap =
16384 died with canon-audit III.6) — and no minimum either: the old
minPop = 10/5 floor minted souls from air and made every settlement
immortal (canon-audit B6, removed 2026-08-29, bc9a1c4). Population falls
honestly to zero, the death files a Died fact, and the S9 transition turns
the empty record into a ruin when that track lands. Famine and shortfall
land on the landmark as honest readouts.

## Money and trade

Money is a COMMODITY: every realm mints its own light coin (imperial crown,
magika sigil, republic mark, northern ring — value 1 each until exchange
rates arrive). WHICH coin is a registry column since 2026-08-29:
`FactionDef::mint` names the row's currency and
`currency_for_faction_id` is its one reader — the old `strcmp` if-chain had a
"barbarians" branch that matched no faction row, so all four barbarian realms
silently minted imperial crowns; now they mint `coin_barbar`. There is no
gold field anywhere: the player's wallet is coin
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
and context (mood / temperament) stay the one `trade_price` law on top
(squad-agnostic since 2026-09-03 — was `player_trade_price`, but NPC vendors
priced through it too; CHA enters via THE one `cha_trade_discount` formula). Arbitrage dies two ways (price_law_test): full-shelf round trips lose
to slippage, and any residual drip from a favourable pair (a generous
merchant genuinely overpays — temperament, not a bug) drains his FINITE
purse and stops dead.

**Debt is a FACT**: a penalty the wallet cannot cover becomes a Debt memory
(«кто-то должен кому-то столько-то»), remembered entity-about-entity and
SUMMED by the fact arithmetic (per-kind fold: snapshots replace, debts sum).
It bites when macro relations arrive.

## Deferred (known debts)

Deposit rendering on the map; seasonal harvest pulse + field exhaustion; a
miner agent for the deposit layer; ~~econ facts raised to the journal/bus~~
(CLOSED 2026-08-28: caravan deals, player deals, a drained vein and a struck
vein are chronicle facts — [chronicle.md](chronicle.md)); exchange rates +
the currency bourse; coins minted FROM gold/silver; player
invest-into-owned-city (waits for ownership itself); taxes to the faction
treasury.

## 2026-08-30 — the balance track (五 commits, see CANON S10 for the laws)

The economy went from a dead ledger to a measured, living circuit in one
owner session, every step driven by the headless balance run
(`balance_run`, tests/balance_run.cpp — the «дубль-прогон» of CANON S10):

- **The one world baker** (`macro/world_gen.h`) + `balance_run` (TSV per
  day, 2 game years ≈ 20 s/seed) + `EconFactSink` wired with landmarkId.
- **Deals are real**: `trade_caravan_at_station` (city↔city, station by
  station, locality — sell into a shortage up to the market's daily demand,
  buy the surplus above it; the bounds ARE the price law) and
  `trade_vendor_at_market` (the village crew at its nearest city: sell all,
  spend the WHOLE purse down the home needs ladder to a season's stock —
  «деревня не копит капитал»). Fleet law: a city without a caravan outfits
  one from its population; vendors rotate with the labour crews.
- **Labour is SP** (kWorkCyclesPerBar; the person-day is derived), crews
  are transient (rotate_worker_squads), a squad's carry is the SUM of its
  backs, and one gatherer covers TENS of souls (kGatherPerWorkerDay = 32,
  the owner's productivity anchor).
- **Fields live**: a parcel regrows its whole potential in one season
  (kWheatSeasonsToRegrow, macro_stock.h) — world bread went 7/day → ~12k/day.
- **Demand is derived**: daily_demand_for flows recipe-output demand down
  to inputs, gated by the SITE that can run the recipe.

**The measured blocker (2026-08-30): the MONEY deadlock.** Cities spend
their genesis purses on famine-months grain and never earn coin back
(day 190: 64 cities hold ~100 coins, villages ~230k — a ×4 genesis purse
only delays it): villages have no coin SINK and cities no coin SOURCE, so
city production starves (no coin → no raw → no wares → nothing for the
village to buy). The cure is increment 4, now proven necessary, and its
shape is canon: the FEUDAL TAX GRAPH (village → its city/castle → lord →
capital; each node knows only its direct vassals and its suzerain — CANON
S24) as the villages' sink, MINTING (silver → the city's recipe) as the
cities' source, wages as the spread.

## 2026-08-31 — the guided balance session (owner-driven; CANON S10/S25)

Five increments in one guided session, each measured by the дубль-прогон
before and after; the world's population curve turned UPWARD for the first
time in the track's history (seed 1: 220.7k born → 192.8k at day 384 →
204.8k at day 512 and climbing; hunger 9%; trade 1229–1406 deals/day;
1398 villages).

- **The chain-wide productivity anchor**: bread = kGatherPerWorkerDay (32)
  per baker-day — «рабочий любого звена кормит ~32 душ»; the old 8/day ×
  the 1/8 quota was a knife-edge that starved villages atop grain hoards.
- **The roster law** (landmark_registry): a landmark's own registry row
  carries its crews (`crews` + `labourShift`; village ½, city = couriers
  only — the M&B doctrine «деревня добывает, город производит»). Five
  spawn hardcodes became rows; fold-targets left: caravan fleet law,
  garrison recruiting.
- **Provisioning + the M&B maintenance law**: bread and pay are for the
  ROSTER only (a lone leader needs nothing); every squad a landmark raises
  is loaded with bread = soldiers × (roundtrip days + the work day)
  (provision_squad). Unprovisioned crews were bleeding an eighth a day
  into the deserter pool → bandits.
- **The living plough + the ripest field**: a crew whose parcel gave out
  spends its remaining cycle stamping a NEW field (plough_cell_ok — the
  worldgen stamp's own gates); find_home_field picks the RIPEST parcel,
  not the nearest (the nearest-pick pinned village grain to one cell's
  regrowth). Grain flow ×4–5.
- **The universal value-tribute** (дань стоимостью): on the pay-day an
  eighth of the WHOLE store's value is assessed into Landmark.titheOwed;
  carriers pay it down in kind — coin exact, then the fattest stacks
  (transfer_worth). The in-kind tribute is the non-monetary food channel
  that finally feeds the cities.
  **SUPERSEDED 2026-09-02 — the tribute is BY POSITION now** (owner: «дают
  по 1/8 всего со склада, с округлением до меньшего»; save v73): an eighth
  of EACH commodity stack (floor) plus an eighth of the coin are assessed
  into per-position debts (`titheOwedGoods[15]` + `titheOwedCoin`) and the
  carriers deliver them IN KIND. The value-debt's «fattest stack» draw paid
  the suzerain in grain while the silver stayed home (measured: 4308 silver
  parked in a village for 100 days, mint starved) — a slice of every stack
  carries the mint metal too. `transfer_worth` is dead with no caller. The
  eighth is universal on EVERY edge of the graph (village→city,
  city→capital) — «десятина» is a word, not a tenth (CANON S10).
- **Mines + field birth**: a mine is a FEATURE per deposit kind
  (kDepositDefs.mineFeature; the crew's first day builds it and
  consolidates the connected vein cluster into the cell — stock stays in
  the deposit layer, every worksite law reads on unchanged); villages are
  born by the settlement FIELD (terms at the crews' working radii, placed
  villages press the field — village_pressure; separation/quota/cap died;
  the self-feeding gate; souls = the owner's scale 64..191).

**The ore hunt closed the session (2026-09-01, save v72).** The dead-ore
root was found by probe, not guess: a silver crew stood frozen two cells
from a live vein — a mountain RIVER between them (greedy stepping only
walks strictly-closer, so it cannot detour), and the crew neither walked
nor rested (regen only fired for Resting/at-target bodies). Four laws
landed, each the owner's verdict:

- **Regen is unconditional** («встал — отдыхаешь»): refused legs — a full
  step's budget standing unspent after the think — count as standing.
- **The reach wave** (npc_ai.cpp ReachWave): an ore worksite must have a
  MARCHABLE route; a vein one water cell away is flagged bridgeable.
- **Crews build bridges**: a span costs one worker's day of material
  (32 = kGatherPerWorkerDay) of whatever the home store holds more of —
  stone lays FT_Bridge (road bed), timber lays FT_WoodBridge (dirt bed).
- **Geology is a FIELD** (core/field_noise.h — the climate's own fbm
  stack, one noise for every field of the world): ore concentration =
  profile (blob / RIDGE along orogeny) × terrain weight (height⁴ for
  metals — «горы дают веса, не гейт»; river moisture for clay), veins
  where the field crests, richness ∝ the excess — nests fat at the core.
  Thresholds calibrated to the hash law's world totals ([deposits]
  fingerprint).

Measured (4 seeds × 4 years): populations are the healthiest the track
has seen — born 220-228k, dip −8..−19%, growing at the tail (191-211k
final, all seeds climbing); silver is mined by hands for the first time
and THE MINT CAME ALIVE — 89,568 coins on seed 1. Two measured tails ride
problems.md §34: the SILVER LOTTERY (mint fired on one seed of four while
the ground held silver on all — mining villages vs the arable-dominant
score, or nests behind wide water) and the INFLATION TAP (+57% money mass
over 4 years — the daily crews' spawn purses, ×1400 villages: the
honest-spawn-wealth track is now material).

### Increment 4 complete (2026-08-31, commits 7a2a4f1..d9ba96d)

The FEUDAL MONEY CIRCUIT is built and measured end to end: village tithe
(an eighth, riding WITH the vendor) → city; city eighth (a TaxCollector
courier, robbable) → its capital; silver veins → the mint recipe (the price
table IS the mint, seigniorage emergent); garrison wages by the ONE upkeep
law into a value-purse, soldiers eat off the town store; squads eat bread
out of their own bags on the march; a shorted day (pay or bread — TWO parallel
needs) bleeds an EIGHTH of the roster into the deserter pool AT ONCE
(owner: no patience counters). Wages and tithes are SEASONAL — each
landmark pays on its own day of the 32-day season (ordinal % season), the
world's one slow cycle; paid wages LEAVE the economy into the loot pool
(spent soldiers' coin is lost money — ruins and dungeons will return it). Deaths with no victor fold their worth into the world LOOT POOL
(one value; ruins/dungeons will roll loot with a budget from it). Walking
NPCs never enter ground they cannot camp on (water sans bridge — the
Session-21 ford is dead); everyone is born ON their landmark's cell.
Measured across seeds: money supply stable ~1.0–1.1M for two game years,
trade alive the whole horizon (150–400 deals/day), the victorless-death
pool down from 26M to ~1.5k.

**Honesty debts** (audit 2026-08-23, canon-audit.md §B — places where the
"honest economy" headline is not yet true):

- ~~The population floor mints people~~ (CLOSED 2026-08-29, bc9a1c4: the
  minPop = 10/5 floor is gone from `settle_landmark_day` — population dies
  honestly to zero and the transition files a Died fact).
- **Coin sinks, partly honest now.** HIRE pays its counterparty since
  2026-08-29: the recruit's price — the `NpcTypeDef::hireGold` column
  (derived: 30 × the row's daily upkeep) × the one level law
  `soldier_level_factor` (`1 + (L−1)/3`, the same factor upkeep pays) — is
  moved by `transfer_value` into the settlement's own inventory-treasury
  (`ui/overlays.cpp`; a partial transfer refunds and puts the recruit back).
  Rest and quest penalties still burn through `wallet_spend_up_to` with no
  counterparty, and an unpayable penalty becomes a Debt fact rather than
  silence. Loot-gold **double mint** (`generate_loot_gold` on top of the
  spawn purse) remains open. Canon-audit B1–B3.
- ~~The village production DOOR is open, its recipes are not written~~
  (HALF-CLOSED 2026-08-30: bread is `EconSite::Any` — a village bakes, worse
  only by the population-efficiency law log2(pop)/4; the other eight rows
  stay City until the balance pass says otherwise).
- ~~The caravan confiscates, it does not trade~~ (CLOSED 2026-08-30: the
  station and vendor deals above pay through `transfer_value` with
  conservation by construction; `caravan_deal_test` holds the corridor and
  the no-coin-no-confiscation control).
