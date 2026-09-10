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

## Debt (approved increments, each under its own owner "да")

- **Инк 2 — the floor-point channel + the placement door.** Generators emit
  their scene's standable-point catalog (computed at generation, while
  `trav` is alive — which fixes the cave: today's residents scatter into a
  `dungeon_room` rectangle that is only the cave's MOUTH, and 24 silent
  attempts drop bodies). One `Placement{point, spread}` shape at the core;
  the entry band stays as the traveler computer, untouched.
- **Инк 3 — the population door unlocks + the soul partition.** Gate becomes
  `pop > 0 && crowdHabitat != 0`; interiors reserve, street = remainder;
  `cell_facts` size/tier overload split (a spire's `landmark.size` is its
  spell TIER today — an armed trap).
- **Инк 4 — interiors draw from THEIR place.** A spire storey's bodies come
  from the spire's Population, not the mountain's FaunaCount; a wild cave
  (no landmark) honestly stays on FaunaCount.
- **Инк 5 — genesis.** Ruins placed on the map (surface generator already
  exists); contextual population columns; the registry watchman: every
  kLandmarks row is either placed by a pass or explicitly marked "the world
  does not place this kind".
- **Инк 6 — the projection cap (128) dies**; player-squad Roster loan +
  recenter idempotence; honest FPS measurement of mass combat, said out
  loud.
- **Инк 7 — street guards = garrison records** (the pop/10 street-guard
  share dies).

## Related docs

[monsters.md](monsters.md) — the one body table and THE spawn law;
[microworld.md](microworld.md) — the subworld the bodies stand in;
[dungeons.md](dungeons.md) — interiors; [resources.md](resources.md) —
deposit gates in the crowd; CANON.md S28 — the law this file serves.
