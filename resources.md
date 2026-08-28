# Resources — the fields of the world

> THE system write-up for the resource-field registry: `macro/resource_field.h`
> (interface), `macro/macro_stock.{h,cpp}` (rows + ledger), carriers
> `macro/tree_layer.h` and `macro/deposit_layer.h`, capacity `macro/fauna.h`.
> Shipped 2026-08-13 (R2 track); tests: `resource_growth_test`,
> `macro_stock_test`, `fauna_stock_test`, `crop_stock_test`,
> `tree_layer_test`, `deposit_layer_test`.

## The law

The owner's causality, stated once and built exactly:

    клеточный мир → рельеф → климат → РЕСУРСЫ → и только потом заселение

Every raw thing the world holds — standing wheat, wild beasts, forest, clay,
iron, stone — is a **field over the macro map**: a per-cell quantity, one
number per WRAPPED cell, derived at worldgen from terrain/climate + seed and
alive from that moment on. Features and landmarks never enter a derivation:
a village sits by good land; good land does not appear because a village sat
down. Settlement (politics, the site score, the field stamp) only decides
WHERE a resource gets embodied.

This is deliberately ONE universal, extensible system, and everything
economic stands on it: peasants farm these fields (the woodcutter's chop and
the farmer's reap already settle against them), settlements are PLACED by
them (`macro/settlement_score.h`), the subworld embodies them (tree scatter,
crop parcels, fauna packs), and the player harvests the very same rows
through the very same doors — there is no second inventory of nature.

## The registry

One row per resource (`ResourceFieldId`): Wheat, Fauna, Trees, Clay, Iron,
Stone. A row is a `ResourceFieldDef` line in `macro_stock.cpp` — adding a
resource is adding a row, never a branch. Two storage dialects live behind
the ONE `resource_field_read` / `resource_field_apply` door:

* **Sparse scar rows** (Wheat, Fauna): capacity is the derived baseline, the
  live state is `baseline − scar`, scars self-erase when healed — the
  persisted set stays "cells play has scarred". A scar subtracts correctly
  from any embodied yield (subworld generation knows the exact stalks; the
  macro row only knows what was taken).
* **Carrier rows** (Trees; Clay/Iron/Stone): the live state is a dense
  structure the game already renders or walks — the `TreeLayer` grid
  (`u_treeMap` + revision), the per-KIND deposit maps. The derivation is the
  field's INITIAL CONDITION, not an attractor: the forest grows past its
  virgin state, so "sparse overrides" would stop being sparse. A deposit
  cell may hold SEVERAL kinds — a discovered iron vein lives IN a stone
  mountain and the quarry does not vanish.

Above the registry sits the **macro_stock ledger** (`macro/macro_stock.h`):
what the subworld borrows it pays back — a felled tree decrements its cell,
a hunted head decrements its pack, through signed deltas and stamped
receipts (`ecs::MacroDebt`). Ruin and creation are the same row.

Guard rails, each with a test: reads floor at zero; scars clamp to the
baseline; **a worked-out vein is ANNIHILATED** (owner, 2026-08-28: «истощённая
жила — это не существующая жила») — the cell leaves the map, the chronicle's
`Drained` fact is its only memorial, and scarcity is measured against the
DERIVED born-with baseline (`DepositLayer::virginUnits`, a pure function of
terrain + seed, recomputed free at every load); mining refuses a cell that
holds no vein (**mining invents no geology** — creation goes through the
genesis door, deliberately, and genesis of an EMPTY vein is refused too);
carrier writes move the carrier's revision so the renderer and the save can
never see a different forest than the ledger.

## The growth law

`resource_fields_daily_growth` — ONE walker, and every row's nuance is data
(`GrowthDomain` + `growthAt`), never a second mechanism. Growth is
long-term dynamics, so the walk is DISTRIBUTED: a cell is due once per
`kGrowthEpochDays = 32` (the year is 128 days = 4 seasons × 32 — the same
month the path grid re-bakes on) and one game day visits 1/32 of a domain.

| Row | Domain | Context (the row's nuance) |
| --- | --- | --- |
| Trees | CarrierGrid | The forest plants the forest: per-visit growth = local 3×3 density × 32 / (9 × 1024), gated by the biome's ambient table. A clear-cut inside a living massif is forest again in ~3.5 game years; brush below the seeding threshold never starts one; untended land thickens into чащобы; the desert almost never, water never. |
| Fauna | OwnScars | Beasts breed where beasts are: +1 head per visit while at least HALF the 3×3 valley lives. A hole heals from its living side inward; a region emptied whole is EXTINCT — nobody is left to breed. |
| Wheat | OwnScars | Fertility is the context: a reaped cell replants one stand per visit (the old 32-day law, preserved by construction). |
| Iron | Geology | Born where SCARCE: chance/day = depletion × 1/8, the lump lands on a stone host that lacks iron. New geology is world news. **The law exists TWICE** (CANON S26 debt): the LIVE implementation is inline in the `GrowthDomain::Geology` branch of `macro_stock.cpp`; a DEAD trio — `iron_depletion` / `iron_discovery_chance_per_day` / `discover_iron_vein` (`deposit_layer.h`) — has no caller outside `deposit_layer_test.cpp`, i.e. the test guards a corpse. The trio is for the axe. |
| Clay, Stone | None | Quasi-static (a row away from changing). |

Determinism is calendar-pure: every roll is a hash of (worldSeed, day) —
no RNG stream is consumed, candidate picks are sorted (an unordered map's
iteration order is not determinism), and a reload replays the same days.

## Persistence

The save carries the LIVING fields (since v36/v37): sparse rows as their scar
maps, carrier rows WHOLE (the tree grid, the deposit cells) — a field that
grows is not derivable from seed + scars, and the Persistence ruling says
write the state down. `save_game`/`load_game` take the carriers as REQUIRED
parameters: omit one and every felled forest or drained vein would silently
regrow on load, which is exactly why they cannot be omitted.

## What stands on it, and what is planned to

Standing today: the settlement score (cities/villages are placed by
arable + water + forest + deposits), the field stamp (parcels embody the
Wheat row), subworld embodiment (tree scatter, crop parcels, fauna packs —
each clamped by its row's `read`), the agents' work loops (woodcutter,
farmer — one law of labour, `kGatherPerWorkerDay`), iron discovery, the map
sprite (`u_treeMap`).

**The subworld sees the deposits now (C4 closed, 2026-08-24).** The partial
`MacroWorld` aggregates in `sub/engine.cpp` — three of which silently omitted
the `deposits` member while the deposit rows fail closed on a null carrier —
are gone: THE envelope ([macro/macro_world.h](src/macro/macro_world.h)) is
assembled once by its owner and handed whole, so a layer can no longer be
forgotten at a call site ([context.md](context.md)). Its first fruit is on
the street: `CellFacts.depositsNear` carries the live deposit kinds within
`kGathererReach`, and the town crowd reads it through the spawn law's
`depositGate` column (fauna.h `pick_town_row`) — a live vein in reach puts
the miner / quarryman / clay-digger into the street crowd and into the houses,
by the SAME radius and the same data that raise the macro profession.

Planned on the same rows, no new dialects (owner's design intent,
2026-08-13):

* **The economy entire** (work_vector №1): every gatherer role is "walk to
  the nearest cell where `read > 0`, take through `apply`, haul home". The
  first slice is BUILT (2026-08-13): ONE `ai_gatherer` loop drives every
  profession from a `kGathererDefs` row — {type, registry row, commodity,
  worksite} — woodcutter, farmer, miner, quarryman and clay-digger are five
  rows of it, and a village raises the professions its own ground holds (a
  live vein inside `kGathererReach` spawns its man; specialisation stays
  context, never a village type). Since 2026-08-24 the professions also reach
  the STREET: the subworld's town crowd rolls the same gate (see C4 above).
* **The player as harvester**: the player takes through the same doors and
  receipts as any agent — his chop already does (the console `chop`, the
  subworld prop harvest).
* **Prospecting-class skills**: the per-cell truth already exists —
  `resource_field_read(w, row, x, y)` for any cell; prospecting is a
  KNOWLEDGE/UI layer that reveals it to the player (which cells hold stone,
  iron, thick forest, rich soil), never a second store of the world. Rank
  can widen the radius, deepen the detail (presence → kind → remaining), or
  reveal what discovery has not yet struck.
