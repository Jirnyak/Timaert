# Microworld — Микромир

L2 subworld: **the macroworld, detailed.** Each macro cell becomes a
1024×1024 tile map; the player stands in a seamless 3×3 grid (3072×3072) of them.
Rendering is **first-person 3D only** (Vulkan `vk_renderer_3d`); the flat 2D view
is the macro map / minimap, not a subworld mode.

- **Code:** [`src/sub/`](src/sub) —
  [engine.h](src/sub/engine.h),
  [seamless_manager.h](src/sub/seamless_manager.h),
  [base_generator.h](src/sub/base_generator.h),
  [vk_renderer_3d.h](src/sub/vk_renderer_3d.h)
- **Architecture:** [ARCHITECTURE.md](history/ARCHITECTURE.md) §L2 — Microworld (Subworld)

## Model

- **Seamless 9-cell grid:** player at centre; 8 neighbours generated around.
  Boundary crossing re-centres, installs deterministic placeholders, and
  generates exposed cells on `std::jthread` workers (no seam-path stall).
- **Neighbour-aware pipeline (per cell):** Layer 1 heightmap (macro blend +
  detail + coastal sculpting + mountain amplification) → Layer 2 features
  (roads connect toward road neighbours, forests blend) → Layer 3 landmarks
  (self-contained generators).
- **`CellContext`** carries macroHeight, biome, feature, landmark, seed — read,
  never re-derived — and it names the cell by its **wrapped macro index**: one
  cell of the world is one subworld, whether the player walked into it across
  the seam or entered it from the map. The window keeps its own running
  coordinate for composite geometry; the two used to be one number, and that is
  what made the same place generate two different worlds (see
  [seamless-crossing.md](seamless-crossing.md)).
- **`resolve_context` is a CONSUMER of the one assembler** (2026-08-24):
  the macro half of the context comes from `cell_facts`
  ([macro/cell_facts.h](src/macro/cell_facts.h), [context.md](context.md));
  this function adds only what generation alone needs — window geometry,
  seeds, the furrow phase. `CellContext` gained `zone` (the danger byte) and
  `depositsNear` (live deposit kinds within the profession reach), and the
  season rides its OWN facts column (`seasonTempOffset`), applied at this one
  sink into the tree-species temperature — biome classification never sees
  it, so a forest cannot reclassify to tundra in winter. `enter()` and
  `enter_dungeon_scene` take THE `MacroWorld` envelope whole; the per-call
  layer lists (with their silently missing `deposits`, canon-audit C4) are
  gone. It used to assemble the macro half by hand — one of the drifted
  copies the door replaced.
- **The nine window step-weights are CACHED**
  (`refresh_window_step_weights`, per scene change): the per-tick walking
  price is an array read — the door's performance contract. Canopy rides the
  same continuous step law as above ground; the climb term is deliberately
  absent, because down here the slope IS the honest 3D walk (S17).
- **Walk speed is DERIVED** — `kSubworldWalkTilesPerSecond = 96` carries its
  derivation from the macro march (8 cells/game hour over the stretched
  underground hour = 93.75, +2.4 % named allowance): the A8 "two walking
  speeds" debt is closed — see time.md (time.md снесён 2026-09-25 — см. CANON.md / SKELETON.md / history/recon-2026-09-25/).
- **Every global-coordinate noise closes on the world.** The detail waves, both
  ridge octaves, the domain warp, the crag, the massif edge, the dune and the
  bog all take the world's tile span (1024 × 1024) as their period, snapped to
  whole lattice cells. A thousand kilometres cannot repeat inside anything a
  player sees, so the ground is seamless by construction rather than by luck.
- **Honest 3D simulation.** World *generation* is 2D (terrain heightmap +
  decorations), and the seamless window shifts in 2D, but **all entity simulation
  is full 3D** — X, Y, Z are equal coordinates. Grounded bodies rest on the
  support surface and FALL with honest gravity when it drops away (height.h
  `vertical_step`); flying entities and projectiles own their Z. Projectiles are
  never CLAMPED, but the window is a closed box for them: they die on the terrain,
  on masonry, on the four XY walls, and on the same ceiling flying bodies are held
  under — so the sky is not an unbounded direction.
  All distance checks, hit detection, NPC AI, spell VFX, point lights, and sprite
  rendering use the entity's true `Position.z`. The sea-level water plane is the
  absolute Z = 0 reference.
- **Renderer:** 3D sky → terrain → water → spell effects → tree
  billboards → BODY billboards (drawn or procedural, one pass) → point lights
  at entity altitude.

## Settlements — a town grown, not drawn

A subworld **city** (`gens/city.cpp`) is built in the order the thing itself
was built, because each step reads what the last one left on the ground:

1. **the TRACT** arrives first — the macro map stamps a road on every city
   cell, and the module carves it to the seam anchors its neighbours aim back
   at;
2. **the WALL** is raised across it and opens exactly where it finds paving
   under itself;
3. **the STREETS** grow inside, unable to breach it;
4. **the HOUSES** line those streets, fronting them;
5. **the FIELDS** take the hinterland beyond the berm, their tracks returning
   to a gate.

### The shape is grown (`gens/kit/growth.h`)

A town used to be a disk: one radius from population, perturbed ±8 % so the
wall wandered. From inside, that reads as exactly what it was. The outline is
now GROWN — from a core the town holds regardless, outward over the cheapest
ground, until it covers the area its population needs. "Cheapest" is three
things the world already knew about itself:

- **slope**, priced by the one law the roads use (`kGradePenalty`) — a town may
  not sprawl up a hillside its own streets refuse to climb;
- **wet ground is not taken at all** — the mason refuses what the plough
  refuses (`kWetEdgeTop`), which is what puts a town ON the river, not in it;
- **the tract** — ground within a block of a road is worth twice ordinary
  ground, which is the whole reason towns are long rather than round.

A settlement's ground is **not a table**: a city used to damp its whole cell to
nothing and pull 280 tiles of it flat, so the town grew on a plate and the
growth had no relief left to read — every city came out a circle, and the cause
was in `terrain_mod_for`, not in the growth. What must be flat is the HEART:
the market and the streets that meet on it. The plateau is now the market and a
block of approach, and the outskirts keep their land.

The area is fixed and the shape is free: a population always gets the room it
needs and no more, but the ground decides its outline.

### The town is as big as its houses need

A town's AREA came from `70 + 3·√population` — a formula from nowhere — while
its HOUSES came from the hearth law, so their ratio (the density) was an
accident that happened to look right at one population and nowhere else. The
owner photographed the consequence as empty lots.

Now there is one number: **what one house occupies of the town**, spelled out
in its three parts because each is a fact about the plots the generator
actually lays — the house itself (its street face by its depth), its share of
the lane it fronts (half the width across its own frontage), and its yard (a
burgage plot runs back three times the house's own depth). The town's area is
its houses times that, plus its market; its wall radius is that area's circle.

**The market** is sized the same way: the room its sellers need on market day —
one household in ten keeps a stall, a stall with its aisle takes eight tiles,
and half the square again is room to move between them. ~40 tiles across for a
town of six thousand, where it used to be a 6×6 stamp.

### One curtain, and an UPPER QUARTER that is not a ring

A city used to raise up to five concentric rings, one per population step, each
at a fraction of the footprint radius. Concentric circles are not what a town
is — they are a pattern, and the owner caught it from the air. Real towns had
two DIFFERENT things: one city curtain (moved outward as the town grew, the old
one pulled down or built into the houses), and a castle's own small enceinte.

So there is **one curtain**, and the **upper quarter** is the other object —
a walled DISTRICT inside the city with its own gates, its own streets and its
own houses, holding the keep. (The first cut made it a castle-sized ring stuck
to the wall; the owner called that what it was.)

- **Who lives there decides how big it is**, and that number was already in the
  world: a place's garrison is `population >> garrisonShift` (§42, «гарнизон =
  армия ландмарка»). The garrison and its lord are the quarter's population;
  its hearths follow by the hearth law and its ground by the same
  ground-per-house as the city's. So the quarter is a fraction of the city only
  as a CONSEQUENCE, never as a chosen share — ~13 % at six thousand souls.
- Its streets grow from its gates toward its own heart: the same kit, a second
  call. That is what makes it a quarter rather than a walled yard.
- it stands on the HIGHEST ground the curtain reaches, backed into it — which
  is where upper towns were built and why: supply under siege, and a way out
  when it is the townsmen doing the besieging;
- and its wall is only the ARC THAT STANDS INSIDE THE TOWN: its outer side IS
  the city wall. `stamp_city_wall` takes an optional `clip` outline and builds
  nothing beyond it, because a full circle there raised a second wall running
  alongside the curtain with the quarter bulging out past the town — which is
  what "backed into the wall" must not mean;
- it has **its own gate outward**, past the town, and a road down to the
  market. One carve opens a gate in both walls, because a gate is wherever a
  ring finds paving under itself;
- the KEEP moved off the market square into it, and out-tops the curtain by two
  courses: the last defence is also the highest one. The market stays where the
  roads meet — that is what a market is.

### A street does not cross a wall

The way through masonry is the gate the road that was there first left in it.
Without that law a lane simply stopped being painted on the wall's tiles and
carried on beyond them, so from the air a street ran straight "under" an inner
wall with no gate — the lane was never crossing the wall, it was pretending the
wall was not there, which looks the same and is worse. The pomerium obeys it
too: the wall lane is interrupted where the castle backs into the curtain
rather than tunnelling through the enceinte.

The result is one `Outline` (`gens/kit/outline.h`, a radius per bearing about
the heart) and **every placer downstream reads it**: the wall is raised on it,
streets stop short of it, houses stand inside it, fields begin beyond it, the
tree line is cleared to it. That is why the shape could stop being a circle
without a single change to any of them.

### The streets are hyphae (`gens/kit/lanes.h`)

The radial-concentric plan that stood here — evenly spaced avenues, concentric
ring roads, tangential frontage stubs — drew geometry and hoped it would read
as a town. It did not, and every reason was visible from one aerial frame
(owner, 2026-09-13): the avenues were spaced from a random rotation and so
lined up with nothing, least of all the gates the traffic comes through; the
outer ring road sat at 0.92 of the usable radius, which is to say scraping the
curtain; the frontage stubs fired off at random angles to die in the grass; and
every one of them was the same width.

A hypha grows toward food, and a street's food is somewhere to go:

- **the trunks already exist** — the tract, carved before the wall; the growth
  WALKS them, seeding the side streets that branch off, rather than laying a
  twin down the same line;
- **a tip branches** at block intervals, each generation NARROWER than its
  parent, which is where the hierarchy comes from rather than being assigned;
- **a tip dies** on ground another lane already serves, at the wall, in the
  water — which is what makes coverage even without a global plan;
- **a tip that meets a lane JOINS it** (anastomosis) instead of running
  alongside: that is where the loops and the blocks come from, and it is what
  killed the parallel duplicates;
- **and it colonises** — every other tip is aimed at ground nothing reaches.
  Branching alone spends the whole budget along the trunks it sprang from, and
  the town comes out built along its roads and empty between them (measured:
  angular min/max 0.14 against the 0.20 floor, `city_distribution_test`).

**Width is derived, not chosen.** A man is 1.1 tiles wide
(`kNpcBodyRadiusDefault`), so an alley is two men abreast, a street a cart
passing a man, a high street two carts passing. The **pomerium** — the alley
running the whole way round just inside the wall — is how a garrison reaches
any stretch of its own curtain without threading a yard; it is the ring road
that used to graze the masonry, put where it belongs.

**The growth stops when the network can front the town's buildings**: length ×
two sides ÷ a plot's street face. Growing to an arbitrary cap instead gave a
town twenty times the street it could ever build on.

### Houses front the street (`gens/kit/plots.h lay_frontage`)

Houses used to be rejection-sampled anywhere within ten tiles of paving, so
they sat BESIDE streets at random angles — from the air, buildings dropped on a
meadow. Plots are now laid down both sides of every lane: a plot's face on the
street, its door opening onto it (the orientation is computed, not rolled), its
neighbour a yard away. The door law is untouched — one door per house, carrying
the ordinal the engine counts back.

### How many houses — every soul has a hearth

`city_house_target` was `pow(population, 0.80)` clamped to `[20, 380]`. The
exponent came from nowhere and the cap bound at 1 750 souls, so every city in
the world above that — market town and capital alike — had exactly 380 houses.

The number was already in the world: the hearth law (`interior_household_share`,
CANON S28). So **houses = population ÷ what a hearth holds**, and the only bound
left is the GROUND's. A 5 824-soul city went from 380 houses to 1 456.

**The hearth is a MEAN, not a ceiling** (2026-09-13). The count divided the
population by the LARGEST a hearth could be while the populator rolled each door
between one soul and that ceiling — so a town's doors held about five eighths of
its people and the rest had no door at all. Invisible while everyone stood
outside anyway; the day the sun began sending them home it showed as a city that
stayed half full at midnight. The roll is now uniform over `[1, 2·mean − 1]`,
symmetric about the mean, so the doors hold the town's people exactly in
expectation — and the mean itself is read off the doors that were actually
BUILT (`doors_in_cell`), not off the count the layout law wished for.

**The last quarter goes on the backlands** (`lay_backland_houses`). Between them
the frontage (wants a lane) and the road-gated scatter (wants paving within ten
tiles) seated about three quarters of the town. The remainder now takes the
baldest ground inside the walls, biggest patch first, and asks for no street at
all — owner, 2026-09-13: «пустыри всё равно без улиц есть, пусть там будут
дома». A 5 488-soul city went from 1 036 houses to 1 205 of the 1 372 it asks
for; the residue is the upper quarter's, which has no more ground.

### The yard is dressed (`gens/kit/plots.h lay_yards`)

The land-per-house law budgets **81 tiles of 117** as yard, and measurement
agrees with it to three per cent — so a town's bald ground was never missing
houses, it was yards with nothing on them. Each plot now carries a kitchen
garden, a hurdle across the back, and a well per block.

Nothing new was introduced to do it. The garden is `TILE_FIELD` — the same tile
the furlongs outside the wall use — so the town's one existing sower
(`scatter_field_crops`) sows it without being told a garden exists. The hurdle is
a `Wattle` row of the prop table, its own kind rather than a `Fence` in brown,
because a Fence is the dry-stone balk a ploughman piles at the edge of his strip
and a hurdle is what a townsman weaves round his own yard. The pass reads only
the HOUSES standing on the ground — not the lanes, not the wall, not the fields
— so it is a pass over a result, not a module reaching into a neighbour.

### The day pumps the town (CANON S28)

A settlement's population is a conserved quantity in two vessels and the light
moves it. `crowd_outdoor_share01` is the sun itself — `sun_dir(tod).y` mapped
from its own `[-1, +1]` swing onto `[0, 1]`, so noon is full, dawn and dusk are
half, midnight is empty, and no number was invented. Clamping it at zero for the
dark half was the first cut and is forbidden: it leaves no gradient to pump with.

The fraction of a hearth is resolved by the DOOR'S OWN PHASE (a constant from its
seed), so the expectation is exact while every house switches at its own moment —
a town's windows light one by one rather than by a thrown switch. There is no
loop over people anywhere: the law reads a count.

While the player stands in the town the pump keeps it honest (`tick_day_pump`):
the surplus walks to the nearest door and un-embodies there, the deficit is born
at doors. Walking home settles no macro debt — that is paid where a body DIES —
so the town never loses anybody, and opening that door finds him inside. A city
of 1 200 fields 1 200 on its streets at noon and 34 at midnight.

### A town's people live in the town

`city_layout.h` also owns the settlement **footprint** — where the town
physically is — because the citizen spawner needs it as badly as the generator
does. It used to be two magic-number expressions buried in `gen_city` /
`gen_village`, so `sub/spawn.cpp` drew each inhabitant as a uniform random tile
out of the whole 1024×1024 macro cell. A city walls 4–8 % of its cell and a
village ~1 %: **92–99 % of every settlement's population was born in the fields
and forests outside its own gates**, and the streets inside stood empty.

The footprint is one set of pure curves — `city_wall_radius`,
`city_core_radius`, `city_target_area`, `village_core_radius`,
`village_wall_radius`, `city_wall_rings`, `city_ring_wall_radius` — read by the
generator that stamps the walls AND by the populator that fills them, so they
cannot describe different towns.

Since the town stopped being a disk, a scalar radius can only describe the part
of it that is GUARANTEED (`city_core_radius` — the disk holding half the target
area, which the growth never dips below). Filling that disk while the streets
and houses spread past it put a round crowd inside a shape that is not round —
visible at a glance on the minimap. So the populator **measures the shape off
the ground**: it walks out along each bearing and remembers the furthest
masonry (`measure_town`, `sub/spawn.cpp`). That is not a second definition of
the shape — the generator decides where the wall goes, the populator reads
where it went, exactly as the wall-integrity test audits the ring without
re-implementing it. Citizens are sampled over the town's reach with `r = R·√u`
(uniform in AREA) and kept only where that measured outline holds them;
water/house/wall tiles rejected as before.

`R` is not the mean wall radius: `wall_ring_noise` perturbs the outline by
two harmonics plus a per-bearing jitter, so the built wall wanders *inward* by
up to `roughness·(0.32 + 0.18 + 0.18)`. Those amplitudes live in the same header
(`SettlementWallRing`), the generator reads them, and `wall_inner_bound()`
derives the radius that is inside the ring's **worst inward excursion** — the
bound a citizen can be placed within and still, provably, be inside the walls.
For a 1200-soul city that is 158 tiles rather than the nominal 173.

`tests/city_population_inside_walls_test.cpp` locks it against the generator's
actual output: not one citizen beyond the outermost stamped wall tile on its own
azimuth (64 bins, so a gate opening never empties one), none in water or
masonry, and still spread — every angular sector populated and ≥ 55 % of the
population beyond half the radius, which a centre-clumping "fix" would fail.

### Oriented houses, curvature walls, real gates, round towers

Structures are **oriented volumes**, not axis-aligned squares
(`Structure{yaw, hx, hy, zBase, shape}` in `src/sub/map_data.h`):

- **Houses** draw independent continuous width/length and a free yaw
  (`add_house_obb`): the rotated footprint is stamped tile-by-tile, flattened
  to its mean (same pad rationale as below), and emitted as ONE oriented
  record. The keep gets a modest random lean.
- **City walls** are yawed chords following the smoothed ring's curvature
  (`gens/city_wall.cpp stamp_city_wall` pass 2): the whole ring is walked at ~1-tile steps,
  classified, and each wall run becomes short (≤8-tile) oriented pieces that
  drape the relief — no more string of overlapping axis-aligned blobs.
- **A GATE IS WHERE THE ROAD IS.** It used to be a fixed-width corridor cut
  along the pure compass bearing of a road-bearing neighbour — but the road it
  existed for runs to a jittered edge anchor up to 153 tiles off that bearing,
  so a town got an empty arch over grass AND a second opening where the real
  road crossed, often a few tiles apart. That is the "two gates side by side"
  the owner photographed. The corridor is gone: roads are carved FIRST and the
  ring opens exactly where it finds paving under itself. Each opening gets two
  round **jamb towers** plus a **lintel** — a bar whose solid span starts
  three body-eye-heights above the road (`zBase`), so a rider passes beneath
  while wall-walk defenders cross on top. A breach too wide to arch keeps its
  jambs and goes without.
- **Towers stand WHERE THE RING TURNS** — at local maxima of the outline's
  curvature, which is parameter-free, self-tuning with the shape, and puts
  towers on the salients of an organically grown town instead of on a lattice.
  A tower rises one full course of the wall module above the curtain: clearing
  a defender's eyes alone (+1.7 m on 12 m) is invisible from the ground, which
  is the same as not building it. Towers, jambs and the spire are
  `Shape::Cylinder` — round prisms in render AND collision.
- **Walls are raised BEFORE anything is built inside them.** They used to be
  stamped last, after houses and fields, and yielded to both — so a house grown
  into an inward dip of the ring produced no wall tiles and no wall solids
  there at all: a hole you could walk through. `tests/city_wall_integrity_test`
  now asks the question a player asks with his feet — is every opening in the
  curtain a gate? — and carries a negative control that knocks a breach in a
  sound town and demands the audit see it. It exposed three blind detectors
  before the surviving one; the lessons are written in its header.
- **Ruin walls** lean along their own segments (oriented rubble with honest
  gaps).

### Each kind raises its OWN wall (owner's ruling, 2026-09-13)

There used to be one `stamp_wall` with a `WallStyle{height, towers}` dial, and
the city's curtain, the upper quarter's enceinte and the village's wall all came
out of it. The bill was visible from the road: a hamlet of a hundred and thirty
souls stood behind **eight metres of ashlar**, flanked by round stone towers and
entered under a stone arch, because it was the city's wall with a number turned
down.

So the masonry is the city's (`gens/city_wall.cpp`) and the stockade is the
village's (`gens/village_palisade.cpp`), and the kit keeps only what is
literally one question for both — the SHAPE of a ring (`kit/outline`). Two
modules that look alike in places are not a debt; one module with dials for
three different things is (AGENTS.md, data-oriented law 5).

The **palisade** derives nothing from masonry: trunks set shoulder to shoulder,
each driven into its own patch of earth (so a stockade drapes over broken ground
without the gaps a straight stone chord leaves hanging at one end); a gate of two
posts and a head of timber that STANDS PROUD of the wall, because the clear a
gateway owes a rider is more than a stockade is tall — which is why a real
stockade gate is the tallest thing in a village; and one watch platform over that
gate and none around the circuit, since a village fields no garrison to man one.

### A lifted span sits on what it bridges (`sub/height.h seat_lifted_spans`)

A lifted solid states its clear as a height above ONE terrain sample — the one
under its own centre. That is the right seat only while the ground under the
whole span is that height, and on a hillside it never is: a gate on a ridge kept
**1.61 m** of air where it promises 5.10, so the arch sank into the roadway and
the gateway read from the ground as a hole with nothing over it.

The seat is therefore taken from the HIGHEST ground under the span, and it is
taken in a pass of its own AFTER the map is final — every generator's roads are
smoothed into the relief afterwards, so a seat computed at stamp time is measured
against a hill that no longer exists. It lives in `sub/height.h` because it is a
law of heights, not of walls: a rider is a rider whether the arch is stone or
timber. `tests/city_gate_lintel_test` holds it on sloping country, which every
other settlement fixture in the suite lacks.

### Solid structures — collision & support (sub/collide.h)

The SAME records the renderer draws are indexed as solid volumes by
**`src/sub/collide.h`** (`StructureIndex`, rebuilt on the composite's
`CompositeDirty.structs` signal). One solidity authority for every mover:

- **Blocking** — the player (walking AND flying), the mass-battle pass
  (`BattleTerrain.canStand`) — which moves EVERY non-player body, wander and
  flee included: brains in `tick_npc_ai` write intent only, one owner of
  legs — and spawn placement all refuse steps into a solid's side, with axis
  sliding. Spell bolts detonate on masonry like they detonate on terrain.
- **Support** — the surface under any body's feet is `max(terrain, highest
  structure top within a step)`: anyone can STAND on a wall walk, roof or
  lintel, walk off edges honestly, and a flyer descending onto a wall lands
  and stays (the flight floor is the same support).
- **Z-layering** — solids are vertical spans, so a lintel blocks nobody at
  street level, carries a defender on top, and head-bumps a flyer rising into
  it: over/under crossings work honestly.
- **Escape rule** — a body already inside a solid may always move (out);
  blocking only refuses entry, so nothing can ever be trapped.
- **Gravity** — nothing is pinned to the ground any more. The ONE vertical
  integrator (`sub/height.h` `vertical_step`: an honest constant-g fall —
  the whole family is powers of two (g = 8 m/s²: rounding-exact multiplies,
  mental math v² = 16·h, house style) — terminal speed, slope-stick for
  resting bodies only) runs every non-flying body,
  player included (`sync_player_vertical`), with a lazy `ecs::Airborne{vz}`
  that exists only while off the ground. Walk off a battlement → ballistic
  fall onto whatever support is beneath; lose flight mid-air → fall from that
  altitude; flight itself is plain gravity-free 3D movement of the same z
  (the old `flightCamY_` camera scalar is gone — the camera is a pure reader
  at `playerZ_ + eye`). The only 2D left in the subworld is generation and
  the 3×3 composite assembly; the simulation is full 3D.
- **Jump** — `[Space]`: an upward `kJumpSpeedMps` impulse (exactly a 1 m apex) through
  the same integrator, only from solid footing; emergent from the physics,
  not a scripted arc.
- **Fall damage** — honest kinetics, not percentages (`height.h
  fall_damage`): landing faster than `kFallSafeSpeedMps` (exactly a 4 m drop)
  hurts by the EXCESS kinetic energy `m(v² − v_safe²)/2`, with the body
  radius every creature/NPC row already carries as the linear mass proxy —
  with the po2 family the whole curve is `4·radius·(dropMetres − 4)`.
  Flat physical damage — in a systemic RPG a pumped-health character
  survives the fall that kills a peasant *because of* those points. Applies
  universally (player via the entity-Health path + combat log; NPCs get the
  dust burst and a neutral, no-XP death when lethal). A jump can never hurt:
  its landing speed equals its take-off speed, under the safe threshold.

Geometry (`structure_half_x/y`, `structure_visible_height`,
`structure_solid_span`, per-kind minimum floors) is shared verbatim with the
renderer's instance builder — what you see is exactly what collides. Trees stay
non-solid by design (undergrowth is a battle speed cost, not a wall); the
HOUSE/WALL rows of `kTileMovementSpeed` now only price the verge tiles hugging
a footprint. Locked by `tests/structure_collide_test.cpp`: hand-built index
semantics (blocking / support / lintel layering / oriented boxes / cylinders /
slide / escape) plus a generated-city functional pass — the road network is
BFS-walkable from the plaza out through the gates, every grounded wall body
blocks at street level, and wall tops are standable.

### House pads (buildings sit level, not on cliffs)

The 3D renderer seats each house / keep box at **one** elevation — a bilinear
heightmap sample at the footprint centre (`vk_renderer_3d.cpp` `sample_height_m`)
— while the terrain mesh under it follows the per-tile heightmap. On a slope those
disagree: terrain pokes *through* the floor uphill and the box *floats* downhill
(the "towns on cliffs" report). So `gen_city` / `gen_village` **flatten each
footprint** to its mean elevation at stamp time (`dispatch.cpp` `flatten_footprint`,
called from `add_house_rect` + `stamp_landmark_house`), and the box then sits on
level ground. Measured on hillside settlements (≈300 world-u of relief), this drops
the mean within-footprint height range from **~1.1 world-u to ~0.003** (a 100–500×
flatten); a footprint's road-fronting edge still ramps into the street because the
road smoother's shoulder pass runs afterward (a natural foundation curb, not a
cliff). Locked by `tests/house_pad_flatten_test.cpp` with a negative control.

This is deliberately **not** the road smoother: that pass is an 80-iteration
Laplacian that converges to a *harmonic* (curvature-free but still slope-following)
surface over a large connected road corridor — correct for a road, wrong for a
building floor, which must be dead level. Feeding tiny scattered footprints (mostly
boundary tiles) through the road Laplacian was measured to inject the surrounding
grass noise and make pads ~7× *rougher*; that approach was rejected in favour of
the direct per-footprint flatten. Fields are intentionally left to drape the relief
(sloped farmland reads fine, and flattening a large multi-tile field to one level
would cut mesa/pit steps at its edges).

### Mountain massifs (smooth crests, not aliased spikes)

Mountains are raised by a domain-warped 2-octave **ridged multifractal**
(`base_generator.cpp` `apply_mountain_ridges`), blended over the base terrain in
elevation-classified `Biome::Mountain` cells. The ridge octaves are deliberately
low-frequency (~250- and ~110-tile wavelengths) so a massif reads as one coherent
shape — but that alone did **not** stop the "chaotic spiky peaks" the top-down
minimap never showed. The culprit was the *crest shaping function*: the classic
ridged fold `sig = (1 − |2s − 1|)²` has a **slope discontinuity (a C0 corner)** at
every 0.5-crossing of the noise. A corner is broadband — it synthesises
high-frequency **harmonics** of the low-frequency base field, and the 3rd/4th
harmonic folds straight back onto the 16-tile-spaced terrain mesh and **aliases**.
That is what the eye saw as spiky in 3D and the low-passing minimap didn't.

The fix replaces the fold with the **C1 smooth crest** `sig = 4·s·(1 − s)` — the
same 0→1→0 hump peaking on the ridge line, but a smooth maximum (zero derivative
at the crest, no corner). Measured by reproducing the 3D renderer's exact mesh
sampling (192 quads, one box-averaged vertex every 16 tiles) and taking the
discrete Laplacian at the mesh vertices, this drops **median mesh curvature ~70 %**
(16.5 → 4.7 m of kink per 16 m quad, mean −58 %) while **preserving the massif's
range to <0.5 %** and its parity dominance over plains (~27–36× the range, ~6× the
curvature). The right metric is curvature *at the mesh vertices*, not per-tile:
the mesh box-averages over ±8 tiles and samples every 16, so per-tile roughness is
invisible and mesh-scale curvature is exactly what renders. Locked by
`tests/mountain_mesh_smoothness_test.cpp`, which brackets the crest from both
sides (fails if it aliases *and* fails if mountains get pancaked into plains) with
a negative control that confirms the guard fails on the old ridged fold.

**Slope rebalance — third edition (owner round 3, the one in the code).** The
history, so no edition is re-built: round 1 diagnosed walls — even smooth
crests read as sheer faces (median slope 43°, p90 70°, ~600 m of ridge
amplitude on 110–250-tile waves) — and round 2 answered with long waves
(`kFreqs` → 0.0018/0.004, ~550/250-tile) and the pure C1 parabola crest. That
over-corrected: homogeneous hills — no crests, no gullies, no character. Round
3 (`base_generator.cpp` `apply_mountain_ridges`) settles both axes as a
COMPROMISE:

- **Wavelengths between the two extremes:** `kFreqs = {0.0026, 0.006}`
  (~385/167-tile waves) — between the old aliasing 250/110 and the
  over-smoothed 555/250.
- **The crest is a BLEND, not a pick:** `fold²·0.45 + para·0.55`, where the
  classic ridged fold `(1−|2s−1|)²` contributes the sharp V-ridge / ravine
  STRUCTURE and the C1 parabola `4s(1−s)` rounds the very apex enough to keep
  mesh-scale curvature under the smoothness-test aliasing ceiling. Either
  ingredient alone was a failed edition.

Kept from round 2: the **micro-crag octave** (~6 m at ~120 tiles, riding the
ridges — curvature scales with A/λ² but slope only with A/λ, so the grain
costs ~1° of slope) and the smoothstep massif edge (C1 at both ends, warped so
the massif fingers into the plain). The valley floor deepened 0.90 → 0.88 of
macro height so ravines cut as well as ridges rise.
`mountain_mesh_smoothness_test` still brackets the crest from both sides (its
aliasing ceiling is unchanged).

### Universal terrain flattening under macro content

The heightmap generator receives one `TerrainMod {damp, plateauR}` per 3×3
neighbour, resolved by `terrain_mod_for(landmark, feature)`
(`base_generator.cpp`): City `{1.0, 280}`, Village `{0.9, 200}`, Ruin/Spire
`{0.6, 120}`, road-feature cells damp ≥ 0.55, and ploughed farmland
(`FT_Field`) damp ≥ 0.35 — worked ground, calmer than wilderness, but the
plough follows the land more than a road bed does. **Recognised debt
(canon-audit F3):** despite its comment calling itself "ONE data table", this
is a `switch` over landmark kind inside an engine primitive, with numbers that
carry no derivation (a 280-tile plateau for a city) — a row-table it is not,
yet. `damp` scales down that cell's
ridge/noise/gradient columns *before* the bilinear blend (so a calmed city cell
fades seamlessly into a wild massif next door); `plateauR` pulls the manifold
toward the settlement cell's centre height (full inside R, smoothstep skirt to
2R < cell size), keyed to global cell-grid centres so every neighbouring window
computes the identical pull. This is why roads no longer staircase down cliffs
and a mountain city sits on a level walled plateau in a bowl instead of hanging
off a face. Neighbour landmarks ride `effective_landmark(ctx)` through
`dispatch_generate` and the async `GenerationJob`.

### Cross-seam ground materials (no texture walls)

The ground material used to be `terrain_material_for(tile, CELL biome)` with
the biome constant per 1024-tile cell — so while the *height* manifold blends
across the 3×3 ring, the ground *colour* flipped along a perfectly straight
line at every cell border (the "texture wall" on mountain river banks).
`sub/material.{h,cpp}` now picks the biome **per tile**: bilinear weights over
the owning cell's captured 3×3 **ground ring** (same 0.5-centre convention as
the heightmap, sharpened to a ~256-tile mixing band) and a **dither** keyed to
absolute tile coordinates — taiga gives way to meadow the way foothills fade
into plains. Authored tiles (roads, fields, rock, shore, water) never dither.

**THE GROUND-BOUNDARY LAW (2026-09-12).** That dither used to be an
independent COIN flipped per square metre, and so did the treeline's grass↔
stone — one mechanism, copied by hand into two places. A coin makes PEPPER
where nature puts patches: a metre of stone, a metre of grass, a metre of
stone, which the owner photographed on a treeline and correctly asked whether
the defect was the mountain's or everyone's. It was everyone's. Both boundaries
now consult ONE correlated field, `ground_dither01` (24 m lattice), so ground
changes its mind in patches; the test measures a 34-tile run where the coin
gave 2. `structure_shade` keeps its coin — a per-object wobble is not a
boundary. The full account, the measured before/after and the four
optimisations that paid for it are in **[ground.md](ground.md)**, which is THE
document for everything the ground LOOKS like; this file remains THE document
for how a tile's material is ROUTED.
The ground ring is the biome ring through `ground_biome()` (map_data.h): a
**flooded cell enters it as its unflooded climate ground** —
`biome_from_climate` of its RAW temperature/moisture channels, resolved once
in `resolve_context` — so the ring carries no Water entry at all, and a river
cell's DRY margin (its tiles are reclassified to grass by
`sync_water_tiles_from_heightmap`) blends like any land↔land pair. Before the
alias (2026-08-29) the dither zeroed+renormalised water corners and, where the
band saturated, fell back to the ring's **first land in scan order** — which
painted a water cell's banks by its NW-most neighbour and drew razor-straight
ground walls mid-cell and between adjacent water cells (owner screenshots);
before *that*, the margin painted as brown "water bed". Each `LoadedCell`
carries both rings (`cell_biome_ring` for generation, `cell_ground_ring` for
material), so a cell's material bytes are a window-independent property of the
cell: the GPU toroidal seam shift relocates them unchanged and the
from-scratch selfcheck still matches byte-for-byte (`material shift
mismatch=0`). One `fillCellMaterial` helper replaced the renderer's five
duplicated LUT loops; separable axis tables keep a full-cell fill ~2-3 ms, and a
PLACEHOLDER cell — one tile id repeated across its whole 1024² — short-circuits
to a memset (or, inside the treeline dither band, to a two-value select), which
is what took a mountainous world's crossing from 19.7 ms to 2.6. The fill was
then hoisted again when the boundary law arrived (2026-09-12): the four grounds
a tile blends are asked once per axis SPAN rather than per tile, the span
bounds once per cell rather than per row, and a non-authored tile reads its
material from an eleven-byte biome table instead of two switches — 19.4 → 16.9
ms for a full 9-cell fill, which is **faster than before the law existed**.
`sh ~/timaert_shotkit/seam_log.sh` prints those numbers plus the byte-level
self-check for a crossing.
Locked by `material_seam_test` (determinism, pure core, seam-continuity of the
mix fraction with the old per-cell rule as negative control, the ground-alias
law with the removed water fallback reimplemented as negative control,
axis≡reference, and **structure shade surviving a window re-centre**
— also with a negative control, because keying a structure's shade to its
composite coordinate made every building jump brightness at a crossing).

### Alpine treeline + tree slope rule

`scatter_universal_trees` thins trees from normalised height 0.72 (1080 m) to
zero at 0.92 (1380 m) — sized to the rebalanced massifs (floor ≈ 0.60, peaks
≈ 0.98) so a mountain's base can be fully forested while its upper slopes go
bare, like real mountains (owner decision) — and refuses any face steeper than
~35° (crowns pasted on a scarp read as wallpaper, not forest).

### Tree size — the place rolls it, the species scales it

A tree's height is authored in **metres**, in two halves that each live in one
place. The **place** gives the band: `BiomeConfig::treeMinHeightM/treeMaxHeightM`
(a mature stand ≈ 10-20 m; tundra/desert stunted to 6-10, tropics up to 20),
rolled once per tree by the scatterer and stored verbatim in
`Structure::height`. The **species** scales it — the table in
[sub/tree_atlas.h](src/sub/tree_atlas.h), which is where the species itself is
resolved (the atlas row comes from the 3×3-blended macro temperature at draw
time, not from the cell). The scatterer also turns that height into the
record's crown footprint once (`Structure::radius = height · kTreeCrownRatio`),
and `tree_billboard()` composes the record with the species into the drawn quad
— scaling height and width by the SAME factor, so the authored aspect survives
and neither field is write-only. The renderer, the shadow caster and the smokes
all size trees through it. The seat sink is a fraction of the tree's **own** height and
smaller than one sprite row, so trunks stand clear of the ground; it used to be
a fraction of the metric height applied to a quad sized from the *radius*,
which buried up to half of every slim tree and left crowns squatting in the
dirt.

### 3×3-contextual tree density (опушка gradients)

Tree density is no longer a per-cell constant with a binary `forestBoost`:
each ring cell contributes a **tree rate** (trees/tile², derived from its
macro tree COUNT — `macro/tree_layer.h`, the ONE scalar authority; the old
biome-config-plus-`FT_Tree`-boost source died with the feature byte), and the
per-node rate is
the *unsharpened* bilinear blend of the ring over ONE global 2-tile lattice
(probability `rate·step²` preserves each biome's trees-per-area in cell
interiors; the old per-biome scan step also made tree spacing jump at cell
borders). Emergent, from one rule: a forest deepens among forests (~10.7k vs
~6.7k trees for a lone forest cell), a plain grows a smooth опушка on its
forest side (measured 348→1008 trees across the meadow quarters toward the
forest), and water contributes nothing so banks clear naturally.
`fill_base_tiles` no longer stamps phantom `TILE_TREE_DECOR` (decor with no
`Structure::Tree` behind it) — the scatterer is the ONE tree authority, so
every mark on the 2D maps is a real 3D tree. The subworld map and minimap
sample each pixel's full tile footprint (the minimap used to read one centre
tile — a per-pixel lottery) and tint toward the tree colour by the pixel's
real tree fraction, so forest edges shade in gradually.

## Seamless crossing (no hitch)

A boundary crossing must re-centre the 3×3 window by one cell **without a visible
frame** — no hitch, no texture/lighting pop, no vanishing structures. The manager
does not regenerate the world; it **toroidally shifts** its CPU composite buffers
(`composite_tiles_`, `composite_height_`) by `(shiftX,shiftY) ∈ {−1,0,1}²`,
fills only the newly exposed cells with deterministic placeholders, and hands the
renderer a `CompositeDirty` describing exactly that shift + the fresh cells
(`mark_composite_shift`, `shift_composite_buffers`). Workers generate the real
exposed cells and stitch them in later as ordinary per-cell drains.

The kept overlap — 6/9 (axis) or 4/9 (diagonal) of the grid — is byte-identical,
so the renderer **relocates it on the GPU** and rebuilds only the 3–5 fresh
cells: a crossing is **O(new content)**, not O(3×3). The full design, the
GPU ping-pong, the shift math, the self-checks, and the hard-won gotchas
(`-ffast-math`, validation-layer timing) live in
**[seamless-crossing.md](seamless-crossing.md)**.

## Data-driven extension

Add a biome → one `BiomeConfig` + one ground texture. Add a landmark → a
generator function + one entry in `dispatch_generate`
([gens/dispatch.{h,cpp}](src/sub/gens/dispatch.h) — honestly: gens/ holds only
the dispatcher today; the "self-contained module per generator" promise is
kept in [dgn/](src/sub/dgn) — cave, house, spire_tower — while the surface
generators live in the shared TUs the dispatcher calls). Add an **interior** →
one self-contained module in [dgn/](src/sub/dgn) plus its row. Add a **prop**
(door, lantern, chest, well…) → one row in `kStructureKindRows`, whose columns
decide which pass draws it, how it looks, what light it casts and what pressing
E on it does.

## The layer below: interiors

A door in the subworld opens on a **dungeon** — a pocket scene raised on this
same engine, ECS and renderer, with the 3×3 window pinned and its ring sealed.
Houses, cellars and caves are its two shipped generators; a dungeon is a
projection OF the subworld exactly as the subworld is a projection of the map,
so nothing below the door is saved and every lasting act pays up through a
macro stock. `enter_dungeon_scene` takes the same `MacroWorld` envelope, and
interiors spawn by the same laws as the street: residents through the door
cell's `pick_crowd_row` (deposit-gated professions live in houses too), vermin
through `roll_spawns` with the door cell's danger byte
(monsters.md (monsters.md снесён 2026-09-25 — см. CANON.md / SKELETON.md / history/recon-2026-09-25/)). The whole layer, the prop table it stands on
and the one E-verb dispatch are written up in **[dungeons.md](dungeons.md)**.
How the subworld gets its PEOPLE is being unified into THE settlement system
(source × placement × one birth door) — **[population.md](population.md)**,
CANON S28.

## Connections

Reads the macroworld as source of truth (macroworld.md (macroworld.md снесён 2026-09-25 — см. CANON.md / SKELETON.md / history/recon-2026-09-25/)).
Hosts all combat ([microcombat.md](microcombat.md)) and spell visuals
(spells.md (spells.md снесён 2026-09-25 — см. CANON.md / SKELETON.md / history/recon-2026-09-25/)). The combatant crowd is CPU-simulated under the O(N) bound (CANON.md S5). Overworld NPCs within ±1 cell are projected into
the 3×3 as real combat bodies. A projection OWNS NOTHING — its bars, bag, gear and
sheet are its macro record's, read through one door (`sub/record.h`), so what you
do to it happens to HIM in the tick it happens, and nothing is folded back on the
way out. The player's flag can move onto any of them, and on leaving he is whoever
he was standing in — вселение is that flag move and nothing more.
