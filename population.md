# Population — THE unified settlement system (CANON S28)

> **Status: UNDER CONSTRUCTION (§42).** Until 2026-09-10 there was NO unified
> settlement system for the subworld — that is a recorded historical fact
> (CANON S28), not somebody's oversight. Street crowds, house residents,
> dungeon dens, wild fauna and the macro projection lived as five private
> mechanisms, each with its own law of "how many", "who" and "where". The
> audit that exposed it is problems.md §42; the owner's verdict is to fold
> them into ONE system, increments below. This document is THE doc of that
> system: what is canon, what already stands, what is debt.

## The law (CANON S28)

**Bijection macroworld ↔ subworld ↔ macroworld.** Every macro number embodies
into bodies without loss and without castes — player/NPC, citizen/soldier/
beast alike; every body's death pays back into its number. Three layers,
only three:

1. **SOURCE** — whose number becomes a body: a place's population / a cell's
   fauna (ecology) / a squad's roster / garrison records / a design sheet /
   an event / fiat. The source is CONTEXT, never a branch: one fill
   selector, no generic-vs-design fork anywhere.
2. **PLACEMENT** — one data shape: **a world point + an honest spread around
   it**. No intent enums, no branches by body kind. The point is computed by
   the OWNER OF THE TRUTH:
   - *the scene's floor* — a catalog of standable points emitted by the
     generator (the only party that ever sees `trav`); "mannequins" are the
     inside of this computer, not a system of their own;
   - *a traveler from the map* — the entry context (macro truth is a whole
     cell, CANON S2: a fractional cell is unrepresentable, so the spread is
     the honest reachability band; the map's smooth motion is MacroVisual
     render glide, NOT truth);
   - *a mechanic/event* — its own anchor: a portal ± radius, a doorstep, a
     point behind the player.
3. **ONE BIRTH DOOR** — `emplace_body` (DERIVED/TRACKED, sub/spawn.h) plus
   the loan (`BodyLoan` → `stamp_macro_debt`; death settles at the single
   `settle_macro_debt` call site).

Laws on top:

- **No ceilings.** Silent truncation exists nowhere; counts are said out
  loud, a shortage of floor points is a loud WARN. The one physical bound is
  the scene crowd grid, and it already shouts (`truncated`).
- **A place's population is contextual** — born from the world's context
  (a city from its site score, a spire from its spell tier, a ruin from its
  danger zone), as registry columns, never literals.
- **One soul embodies once**: the street and the interiors split ONE
  population number (interiors reserve deterministic shares, the street is
  the remainder). Double embodiment is a defect.
- **Wild fauna is ecology** (hunting, food, the growth law), never a
  garrison. A dungeon place's garrison is its OWN population; recovery
  follows the fauna law (slow while alive; wiped clean stays dead until an
  S9 transition says otherwise).
- **Street guards are the place's garrison RECORDS**, embodied with the
  roster's strike-through loan — killing a guard is a hole in the defense.
  No free bodies.
- **Design addresses a point by tag/alias, never by coordinate**; event
  bodies carry their origin tag and leave with the event.
- The player's squad is a roster like any lord's (S14): same loan, same
  recenter idempotence.

## What stands today (the honest map)

One healthy core the system lands on:

- ONE body birth: `emplace_body` (sub/spawn.cpp), forms DERIVED (embodied
  number; face/sheet/loot from seed) and TRACKED (visible form of a macro
  entity). All spawn sites already pass through it.
- ONE loan ledger: `BodyLoan` stamped at birth, paid at death in exactly one
  place (`resolve_subworld_deaths` → `settle_macro_debt`). Spawn does NOT
  decrement stock — the loan is a receipt; that is why re-entry re-embodies
  and only deaths stick.
- `MacroStock::Population` is an alias of `Landmark::population`
  (macro_stock.cpp); FaunaCount is baseline−scar with the 1-head-per-32-days
  regrowth law.
- The entry context (macro/entry_context.h): two bytes — packed entry step +
  saturating ticks-in-cell — projected into a reachability band. Already the
  correct traveler point-computer; road/bridge fidelity is its future
  refinement.

**Landed increments:**

- **Инк 1 (2026-09-10, `6723780`)** — the crowd's family is a registry
  column: `LandmarkDef::crowdHabitat` (City/Village → kHabTown, Spire →
  kHabSpire, Ruin/Lair → kHabRuin, others 0 = keeps no crowd;
  static_assert-guarded beside the bits in fauna.h). `pick_town_row` died,
  `pick_crowd_row` rolls the place's own stripe; the hardcoded City in
  `spawn_dungeon_residents` died (`DungeonSession::landmarkKind`). This is a
  SECOND dictionary beside `faunaHabitat`, deliberately: the crowd's family
  and the wild fauna's family answer different questions.

- **Инк 2 (2026-09-11)** — the floor catalog + the placement door.
  `dispatch_generate_dungeon` folds every `trav==1` tile into
  `SubworldMapData::standPoints` (map_data.h StandPoint) — the fold runs in
  the ONE place every interior passes, gated by the kind row's own columns
  (householdAbove/verminAbove, a cellar always); the witness sits on the
  FOLD (dungeon_cave_test: catalog == walkable floor, and it spans past the
  mouth chamber). Residents and vermin draw the catalog uniformly WITHOUT
  replacement (partial Fisher-Yates, wrap says itself aloud) — the mouth
  rectangle and the 24 silent attempts are dead. The street's disk and the
  wild cell's scatter go through the one `resolve_stand` door bit-for-bit,
  and every refusal is now COUNTED and said to stderr (`[spawn] WARN`) —
  no body is dropped silently anywhere.

- **Инк 3 (2026-09-11)** — the population door unlocked + the soul
  partition. `spawn_settlement_population` died;
  `spawn_landmark_population`'s gate is `pop > 0 && crowdHabitat != 0` —
  City, Village, Spire, Ruin, Lair alike (a spire embodies the day genesis
  gives it souls). THE household law lives once
  (`interior_household_share`): the engine's interior spawn clamps it by
  the live stock, the street SUBTRACTS the same shares as its reserve
  (`interior_reserve_for_cell` walks the cell's House doors with the same
  pure functions door-opening uses) — **street + hearths == population,
  soul for soul**, asserted by the partition witness in
  city_population_inside_walls_test. The size/tier overload is split:
  `LandmarkFacts.size` / `LandmarkContext.size` is POPULATION for every
  kind, the spire's spell tier rides its own `tier` field (the tower's
  gate/hatch tags read it). The guard/merchant/woodcutter prefix is DATA —
  registry crowd role rows (`LandmarkCrowdRole`, City guard min 2 /
  Village min 1); the City/Village branch and the per-kind street seeds
  are dead.

- **Инк 4 (2026-09-11)** — interiors draw from THEIR place. A scene that
  IS its landmark's interior (dungeon kind row column `placeGarrison`:
  SpireTower, Cave) draws its storey's share of the PLACE's population —
  its crowd family, as fighters, the place's own banner
  (`landmark_crowd_faction`: the registry's spawnFaction wins, else the
  kingdom) — through the same residents spawner and the same Population
  loan: clearing the climb thins the spire itself. The garrison partition
  (`interior_garrison_share`): picket outside = `pop >>
  crowdOutsideShift` (Spire = 2, a quarter), the rest split evenly over
  the storeys, remainder to the lower floors — **picket + Σ storeys ==
  population**, asserted in subworld_spawn_parity_test; the street's
  reserve walk subtracts the same shares (a tower's gate reserves its
  whole garrison). A wild cave (no landmark) honestly stays on FaunaCount,
  and a garrisoned scene never doubles as a wild den.

- **Инк 5 (2026-09-11)** — genesis. Places are BORN WITH SOULS: the born
  law is registry columns (`bornPopBase + bornPopPerScore ×` the kind's
  context score — a spire's spell tier, a ruin's danger byte), rolled as a
  discrete bell (`landmark_born_population`, two dice — the house gauss),
  each place its own stream off the world salt: no two cells repeat.
  Spire = 128 + 64×tier (tier 5 ≈ 448 ± bell); Ruin = 64 + zone (redder
  land haunts harder). `generate_ruins` (macro/ruins.cpp) places one ruin
  per city plus 4, Mitchell-spread over the row's own zone band — the §42
  stillborn kind lives, and the demo's scene 4 has targets. The garrison
  shift FLIPPED to the owner's eye («снаружи больше сотни, внутри десятки
  на ярус»): storeys keep `pop >> crowdInsideShift` (Spire = 2), the
  THRONG is outside — witnessed live by the spire_climb smoke (yard 350,
  storey guards 26, all shares of one number). Spire wild fauna returned
  to the GROUND (its demons are its population; Ruin keeps kHabRuin — the
  den dictionary). THE REGISTRY WATCHMAN: column `worldPlaces` — every row
  either places or says it does not (Lair/Shrine/Mine/Tower say no, for
  now), asserted with the genesis witnesses in spire_generation_test.

## Debt (approved increments, each under its own owner "да")
- **Инк 6 (2026-09-11)** — no ceilings, no special player, numbers out
  loud. `kMaxProjectedMacroNpcs` (128) is dead with its `truncated`
  plumbing: every macro body standing in the window walks in — an army of
  hundreds meets you as hundreds; the one physical bound left is the scene
  crowd grid, which already shouts. The player's soldiers carry the same
  Roster loan as any lord's men (subject = the player squad's reserved
  ordinal) — the hand-written roster removal in the death path died, one
  settle door for every army. The disengage gate holds the RED band
  («it is on you», ~40 m, honest 3D `dist3sq`) instead of the 200 m
  detection radius: being seen does not pin you, a throng 128 m below a
  rooftop hatch does not bar it (owner 2026-09-11) — witnessed both ways
  by subworld_exit_gate (blocked at arm's reach, freed at 80 tiles). THE
  MEASUREMENT: `spire_perf` smoke — tier-5 spire, 364 live hostile bodies,
  512 ticks: **0.654 ms/tick of the 15.625 ms budget = 4.2 % sim load**.
- **Инк 7 (2026-09-11)** — the garrison IS the place's army (owner:
  «гарнизон = армия ландмарка», «у городов должны быть сотни»). One roster
  form, a landmark for an owner: the registry column `garrisonShift` sets
  the target (`pop >> 3`: a 1200-city keeps ~150, a capital hundreds — the
  old √pop×0.3-cap-10 law sized a tavern recruit pool), composition
  60 % Guard / 25 % Woodcutter / 15 % Peasant (the hire pool lives inside
  the army). City AND village are born with their army (souls paid out of
  the born population — the bijection witness sums both sides); upkeep and
  recruiting run as ONE column-gated law for every kind (the City-only
  branches died); a day's recruit packet is at most target>>4 — a hole in
  the defense heals over days. New stock row `Garrison` {subject =
  landmark id, detail = record}: street guards are the LIVE records —
  killed on the wall = struck from the roll, on patrol / hired away = not
  on the street. Garrison identities come from THE one macro ordinal
  issuer — the high-bit id space died (шов 2). The pop/10 street-guard
  fiction and its crowd role rows are dead.

## Debt — the MILITARY LAYER (its own future track, substrate ready)

Lords-with-warbands, leader decisions, wars and land redivision, sieges
(the garrison as an AutoBattleSide of its place), the capital treasury
(?33), faction service, ONE upkeep law with the discontent counter
(?34/?32 — today field squads pay bread only, garrisons bread+wage, the
player wage only), a local deserter pool. The substrate already stands:
one soldier dictionary, one auto-battle law, threat/scent fields (CANON
S10: «войны и стратегия бесплатно — новые читатели тех же полей»), the
feudal graph with taxes as its first tenant.

## Related docs

[monsters.md](monsters.md) — the one body table and THE spawn law;
[microworld.md](microworld.md) — the subworld the bodies stand in;
[dungeons.md](dungeons.md) — interiors; [resources.md](resources.md) —
deposit gates in the crowd; CANON.md S28 — the law this file serves.
