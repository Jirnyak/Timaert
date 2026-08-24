# The Context Door — CANON S6, built

Track write-up, 2026-08-24. Increments 1–7 shipped (commits `c9f2bea..37f80a8`);
increment 8 (price / growth / auto-resolve contributions) in flight. This is
THE doc for the door: what the law is, where each piece lives, and the
contracts that keep it honest. The intent it implements is
[CANON.md](CANON.md) S6, with S5/S7/S12 riding along.

## The law

Every mechanic asks ONE assembler for a cell's facts and receives the
contribution of EVERY system — biome, feature, the landmark standing there
with its live fields, trees, fertility, elevation, danger, land owner, the
season's shift, the deposit gates. A system that does not apply contributes
zero **through data** (a null envelope layer, an empty mask bit), never
through a second code path. Adding a world system is a new fact column plus
coefficients in consumer rows — no mechanic is edited.

## The pieces

| Piece | Where | What it is |
|---|---|---|
| THE envelope | [macro/macro_world.h](src/macro/macro_world.h) | `MacroWorld` — every world layer, one struct, ecs-free. Grows by a FIELD, never by a call-site argument. Assembled ONCE per owner (`macro_world(app)`; a test builds its partial fixture with designated initializers). `enter()`, both AI drivers and the dungeon scene take it whole — the parallel per-function layer lists (and the silently missing `deposits`, canon-audit C4) are gone. |
| THE facts | [macro/cell_facts.h](src/macro/cell_facts.h) | `cell_facts(w, x, y)` → `CellFacts`: the one answer to "what is this cell". The subworld's `resolve_context` is its consumer and adds only generation-private extras (seeds, furrow phase, the dungeon ref). The season rides its OWN column (`seasonTempOffset`) instead of smuggled inside a shifted temperature. |
| ONE biome cascade | [macro/map_generator.h](src/macro/map_generator.h) `biome_at_cell` | The baked land MASK decides water — the sea level vanished from every query, because the mask is its baked form — elevation decides mountain, climate the rest. Four hand cascades and eight manual rgba decodes converged (C5/H10); `biome_at` stays as the pure-math core for the shader mirror and world-gen scratch. |
| ONE landmark answer | [macro/landmark_grid.h](src/macro/landmark_grid.h) | `LandmarkType` (the registry enum) is the one vocabulary — `CellLandmarkKind` and `LandmarkKind` died with their bridge (C1/C2). The baked u16 grid answers WHO stands on a cell in the iterator's one priority order; live fields (population, tier, depleted) resolve from GameState at the moment of asking. Rebaked where the landmark set changes — the living-landmarks track (S9) adds its transitions here and nowhere else. |
| Saved world fields | [macro/world_fields.h](src/macro/world_fields.h) | The per-cell world TRUTHS the save carries — trees, knowledge, deposits, scars — are registry rows that write and read their own bytes ([macro/save_stream.h](src/macro/save_stream.h)); save.cpp keeps only the order. A new truth-field (blood, the dark field, weather) is one row that saves itself. Derived fields are deliberately NOT rows: they belong to the rebaker, because a rebake is a dependency CHAIN. |
| THE rebaker | `rebake_world` (src/app/main.cpp) | Every derived field, rebaked whole, in dependency order, by the same stage functions the genesis uses: zones, the landmark grid, the glow, the cost grid, the GPU zone field. Runs on every LOAD (the derived world used to describe the seed's virgin state after one — G2), on the seasonal settle, and on EVERY macro↔micro transition (S7, the owner's literal word): CPU truth synchronously, the two texture uploads through the macro path's dirty flush — Session 19's no-mid-frame-drain law holds (the first wiring drained in-frame and froze the game). |
| THE spawn law | [macro/fauna.h](src/macro/fauna.h) | `weight(row, cell) = row.weight × habitat(row, cell) × danger_match(strength, danger)`. Danger is a byte CONTINUUM (0 safe … 255 the strongest demons; the ten old steps survive only as display bands). Strength is DERIVED — log₂ of the row's own combat power, normalized over the one table; strengthen a row and it migrates to dangerous ground by itself. The match is symmetric around strength == danger and halves per 256/10 bytes of mismatch, floor 1/1024 — «исчезающе малая вероятность», literally, never zero. Habitat is a bitmask column (the thirteen list-tables and their switch ladder are gone — F5); the town crowd rolls by the same law over its `kHabTown` stripe with the old 55/21/21/3 as row weights, professions gated by live deposits in reach (F4); a place forces its faction through the registry's `spawnFaction` column (a ruin's wolves ARE demons). The zone changes WHO — never a number on a body after the pick (S12; the autolevel negative controls stand). |
| THE step law | [macro/movement_cost.h](src/macro/movement_cost.h) | BED + CONTRIBUTIONS, the optics idiom: an engineered feature lays the bed (road 1.0 — the reference march, dirt 1.5, field 1.8; nothing built = the biome ground), the canopy adds continuously with tree density (a road through the wood is a cut — the bed gates the canopy), and the CLIMB is priced on the EDGE where it happens (`PathCostData::climb`, uphill only) by every walker: the player's A*, the greedy squad step, the per-cell charge, and the road planner — whose private second cost table died (H1 tail): roads are laid over the law they will be marched on, route around thickets and climb honestly. Below ground the canopy rides the same law and the climb is deliberately absent — down there the slope IS the honest 3D walk (S17). |
| The march calibration | same file | Owner's pure level-1 base data (2026-08-24): `kMacroWalkCellsPerHour = 8` («степени двойки»), `kStaminaPerCell = 1` — every modifier (travel skill, overload, terrain √) multiplies ON TOP. A fresh walker drops after ~10 hours of open country; a night's rest buys it back; roads stretch the bar by the law's own √2. The subworld's 96 tiles/s became DERIVED (8000 tiles per stretched game hour = 93.75, +2.4% named allowance) — the A8 "4× disagreement" was the map galloping, not the ground floor lying. |

## The performance contract

`cell_facts` is NEVER called from a hot loop. A* and the greedy step read the
baked flat grids (`PathCostData`: cell weights + height bytes); the subworld
caches its nine window step-weights per scene change (`refresh_window_step_
weights`) and the per-tick walking price is an array read — it used to be a
full facts assembly every simulation tick.

## Ready sockets (built, awaiting their systems)

- `CellFacts.zone` / `.ownerKingdom` / `.seasonTempOffset` / `.depositsNear` —
  live columns with consumers already reading some of them; taxes (S24) and
  weather (S19) plug in as columns, not code.
- `world_fields.h` — the blood field (S4), the dark field (S15) and a weather
  field (S19) land as ROWS: saved, loaded and rebake-scheduled the day they
  exist.
- `AutoBattleSide::terrain`, price contributions, the season's `yieldMul` —
  increment 8 of this track.

## Tests that guard the door

`subworld_spawn_parity_test` rolls through the shipping law and pins its real
invariants (danger shifts composition; perfect match pays the peak; full-span
mismatch is 1/1024; the roll is a fact of (seed, context); rabbit < wolf <
troll by derived strength) plus the autolevel negative controls.
`pathfinding_parity_test` pins MASK authority with a negative control and the
continuous canopy. The pace mirrors (`macro_travel_parity`, `squad_travel`,
`squad_war`, `macro_npc_ai_parity`) derive every expectation from the
constants — retuning the data touches no test.

## The debts this track closed

canon-audit III.12 (the door), G2 (loads walked a virgin world), C1/C2 (three
landmark vocabularies, four scans), C4 (the envelope's missing deposits), C5 +
H10 (biome cascade copies and the coastal double-answer), H1's cost half + §7.9
(the road planner's private table), H2 (the twin AI context assemblies), F4/F5
(the blind crowd and the fauna switch ladder), A5 follow-through (zones wrap),
A8 (the two walking speeds), D (the 4 MiB dead zone float field), H7 (the glued
registry header). Опасность стала континуумом, сезон — колонкой, сила —
выводом.
