# Features — Фичи

Static, persistent per-cell **man-made structures** between the biome and
landmarks: **road, dirt road, field, bridge** (`FT_Field` — ploughed
farmland, the grain deposit; `FT_Bridge` — the road's water crossing;
future: railways, canals). They never
alter the underlying biome. Natural cover is *not* a feature: mountains are
the elevation-classified Mountain biome ([biomes.md](biomes.md)) and forests
are the per-cell tree-count field (below) — `FT_Tree` was removed when the
count field took over (`FT_DirtRoad` byte 3 → 2, save v14).

- **Code:** [macro/features.h](src/macro/features.h),
  [macro/spawners.cpp](src/macro/spawners.cpp) (`trace_roads`,
  `trace_dirt_roads`, `spawn_trees`)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Feature Layer

## Model

- **`FeatureLayer`** — a byte grid stamped once at generation in pass order
  **DirtRoad → Road** (last-writer-wins), torus-wrapped, **fail-closed** on
  malformed data, and **water-filtered** by the active sea level.
- Uploaded to the GPU as `u_featureMap`; every renderer reads that single
  texture — no feature logic re-derived at render time.
- Roads: native terrain-cost A* baseline (documented divergence from TS
  corridor Bresenham; rejected-water pruning invariant is test-locked in
  `road_river_generation_test`).

## Bridges (`FT_Bridge`, owner 2026-08-29, save v64)

A road may cross water **exactly one cell thick, square-on** — and that
crossing is a **bridge**, its own feature byte on the water cell (the ONLY
feature that stands on water, and it stands only there). An explicit byte
rather than "road on water" is deliberate: the special status is the hook
future mechanics hang on (tolls, the troll under the bridge, destruction)
as data against this row.

- **Planner law** (`spawners.cpp` + `find_path` `waterCrossAxes`,
  [macro/pathfinding.h](src/macro/pathfinding.h)): a water cell is
  *bridgeable* on an axis where BOTH orthogonal neighbours are land
  (`build_water_cross_axes`). Bridgeable water is **paid, not refused**, at
  the step law's own water bed (10.0 — no new constant: the willingness to
  build a span IS what the march says the wet cell costs, so a bridge pays
  off where the detour is longer). Steps touching water are legal only as a
  single cardinal hop along a bridgeable axis: never water→water (no
  causeways), never diagonal. Wide water stays rejected (167 gate). Land
  components JOIN through bridgeable cells, so two shores of a one-cell
  river are one road component.
- **Every bridge is stone** (owner): a dirt lane crossing a river lays the
  same `FT_Bridge` the highway does — its row carries the paved road's own
  columns (bed 1.0, optics 0.65, civ 0.35), so the march, night glow, zones
  and optics all price the crossing as road, for free.
- **Subworld**: `FT_Bridge` → Road mode; `gen_road`'s `Biome::Water` branch
  (`add_bridge_segment` — deck chords + piers + arches) becomes reachable
  from the SHIPPING world, not just the test fixture. `is_road_feature`
  counts the bridge, so bank roads aim their edge anchors at it.

### The span, as built (owner in play, 2026-08-30)

Three laws, each answering something the first crossing got wrong:

1. **The deck stands at a WORLD height, not on the bed.** Its level is
   `kSeaLevelM + freeboard` — the same number for every span of every cell,
   because the water it clears is one height («у нас вода одной высоты,
   почему тогда высота моста скачет»). Stated in world metres
   (`Structure::zWorld`, sub/map_data.h): a bed-relative deck wobbled by
   the difference between two samplers — the generator reads exact tile
   heights, the renderer a 16-tile mesh — and that difference was the
   flight of steps a walker had to jump. Nothing under an absolute span is
   consulted, so nothing can move it.
   Off that level the deck carries an **arch**: it rises toward midspan and
   returns to the level at both shores, and its RISE is derived, not
   authored — half a metre per chord of half-span, capped — so a brook gets
   a near-flat plank and a river gets a real arch. Safe because macro only
   ever bridges water ONE cell thick: every arch begins and ends inside its
   own cell, so no two cells can disagree about a crown. Over land the same
   chain becomes a ramp continuing DOWN from the arch's shore height (not
   from the bare level — that opened a two-step gap at the water's edge)
   until it meets the ground; no fixed approach length and no lip, because
   the landing point is wherever the two heights cross. Every joint of the
   whole profile steps less than a kerb (`kStepUpM`), which is the
   invariant the flat deck was built to win and the arch may not spend.
   Low stone **kerbs** ride each chord — their own registry row
   (`Structure::Kerb`), knee-high on purpose so they trim the roadway
   without ever pinning a body against the water.
2. **One line, not two, and the fork on the bank.** A crossing is
   ENGINEERED: the road carves straight (no terrain A*, no organic wiggle —
   masonry does not meander) and the deck is built from the same endpoints.
   The crossing cell has its own module (`gen_bridge_crossing`) and the
   dry-land road generator is untouched. The span is the AXIS whose two
   cardinal neighbours both carry road — which is exactly how macro lays a
   bridge, square-on and never diagonally — and every OTHER road-bearing
   neighbour meets that span at its BRIDGEHEAD. This
   is what a road passing along the far shore is: the subworld calls it
   connected because it carries a road byte, and the old hub-at-centre rule
   dragged it out over the river, forking the bridge mid-water. Now lanes
   merge before the bridge and the crossing stays single and straight.
   Guarded with a negative control (force the hub back and
   `subworld_generator_parity_test` reports the forked span).

   And a crossing is **three things, not one**: a road down to the water, the
   SPAN, and a road away from it. Only the middle one is masonry, and only
   masonry is straight — the owner caught the rest in play (2026-08-30: «у
   моста дороги становятся идеально прямыми, и это бросается в глаза»),
   because the whole line from cell edge to cell edge was ruled straight
   while every road around it wandered. The approaches are now ordinary
   roads (terrain A* + the organic wiggle) run between the edge anchor and
   the bridgehead; the straight line lives only between the two bridgeheads,
   and so does the deck.

   The bridgehead is found by walking the span **from the arm's own end
   inward, to the last point still standing on the bank** — the place the
   deck stops being a deck. Searching the other way (the span point nearest
   the arm) is what pinned the junction against the cell edge as a
   right-angled T on the seam: the nearest dry point to an edge anchor IS
   the edge. Working outward from the water instead puts the fork wherever
   the bank actually is — close to the river on a steep shore, further
   inland on a shallow one — and "bank" is measured against the DECK's own
   height, not the waterline. That is what makes the joint free: the span
   begins exactly where the ground has risen to it, so there is no ramp to
   fork onto and no step to climb.
3. **Under the arches the river is the river.** Road tiles below the water
   plane are turned back to water on a bridge cell: the DECK is the road
   over water, and the submerged half of the carved line was a second road
   lying visibly on the bed.

Arriving on a span needs no bridge case anywhere: a body materialises where
something would CARRY it above the water — `is_dry_footing` over the
solidity index (sub/height.h), which a deck, a quay or a wall walk all
answer identically. The index is now built at `enter()` instead of on the
first frame, so the player, his squad and the projected macro figures are
all placed against the real scene.
- **Map**: the bridge draws through the cobble branch of `roadOverlay` and
  joins `roadAt` connectivity; the chart inks it at the highway's strength.
- Test-locked in `road_river_generation_test`
  (`test_road_bridges_one_cell_river` — forced crossing on a two-barrier
  torus fixture, single span, square-on banks, `FT_Bridge` stamp;
  `test_two_separating_straits_stay_unbridged` — the negative control;
  `test_dirt_lane_lays_a_stone_bridge`), `feature_layer_parity_test`
  (byte 4 first-class, frontier at 5, water-mask stamp law) and
  `subworld_generator_parity_test` (`check_bridge_chain`: world-levelled
  decks, every joint under a kerb, a real rise crowning at midspan between
  two banks, piers footed in the bed, arches not dammed, no road tile on
  the bed; plus the fork fixture — a road passing along the far shore joins
  on land and no deck chord aims off the crossing axis).
### The ground under the feet is the SUPPORT's (owner's law, 2026-08-30)

A tile is one value per column of the map and cannot say *water below,
masonry three metres up* — so a body crossing a deck was priced as wading
the river it was walking over. The law: **what carries you lays the ground
you walk.**

- One column on the prop row (`StructureKindRow::walkTile`) — the tile a
  kind lays under whoever stands on it: a bridge and its kerbs lay road, a
  wall walk and a roof lay pavement, everything else is
  `kWalkTileTransparent` and the terrain answers. Deliberately a TILE and
  not a second speed table: the world already has exactly one answer to
  "how fast is this ground" (`kTileMovementSpeed`), and a per-material
  parallel would be a second law about one question (CANON S26).
- One door (`StructureIndex::walk_tile_at`, sub/collide.h): the highest
  floor within a step of the feet decides, which is the same rule that
  seats the body — so what holds you up and what you walk on can never
  disagree. Under the deck you are still in the river; on a lantern's head
  the road below stays road.
- The mass-battle pass asks that door instead of the tile grid (one
  callback beside the solidity one, same index, same user pointer). Fixtures
  without solids behave exactly as before.
- Pinned by `structure_collide_test` part D (on/under/beside the deck, the
  transparent solid, and the speeds actually differing) with a negative
  control that reproduces the wading-on-stone bug.
### ONE MOVER, ONE GROUND (owner's ruling, 2026-08-30)

Chasing the support law uncovered a deeper split, and the owner named it:
«БОЙ В ИГРЕ НИЧЕМ НЕ ОСОБЕННЫЙ, ЭТО ПРОСТО РЯДОМ ОКАЗАЛИСЬ ВРАГИ». There is
no battle MODE (CANON S13) — what was called `sub/battle` is simply how a
body moves down here, and it already moved everything alive: townsfolk,
wildlife, fleeing prey, projected macro figures. Three consequences, all
shipped:

1. **`sub/battle` → `sub/movement`.** `steer_battle` → `steer_bodies`,
   `BattleTerrain` → `MoveGround`, `BattleUnits` → `BodyCrowd`, `BU_*` →
   `B_*`, `battle_ai_test` → `movement_steering_test`. War stays named
   where it is actually about war (hostility, the influence field).
2. **The player joined it.** He used to be `BU_Pinned` — moved by his own
   input path — which is exactly why he alone paid nothing for ground or
   slope while every peasant did (CANON S4: player-specific code does not
   exist). His keys now state an INTENT in tiles/s (`set_move_intent`),
   like every brain's `wantVx/Vy`, and the one mover turns it into a
   position: ground, slope, separation and solids all apply to him.
   He is `B_Passive` instead: his legs are his own and no war conscripts
   him (he strikes through `tick_player_melee`). Only the vertical half of
   flight stays his — the mover is 2D and a pitched dive is not a step.
3. **One ground law for both scales.** The map priced a cell by bed +
   canopy and the subworld priced a tile by a hand-written multiplier
   table: two laws about one world. The tile now carries a WEIGHT in the
   macro law's units (`kTileGroundWeight`: grass IS the meadow bed, road IS
   the paved bed, a wooded tile IS meadow + the canopy term) and its speed
   is that law's own `1/√weight`. The old numbers were this law all along
   with its normalisation lost — water 0.447 against its 0.45, scree 0.632
   against 0.65 — so the one visible change is that a road now beats grass
   by 41 % instead of 15 %, which is what the derivation says it always
   was. `kSubworldWalkTilesPerSecond` moved to `macro/movement_cost.h`,
   beside the march it is derived from.

4. **The scale is the march's, everywhere.** `CombatTemplate::speed` was an
   absolute tiles/second on a scale nobody had derived (peasant 20 against a
   march of 96), and the player was fitted to it by a private ×0.4 in the
   engine — a second speed law for one body. The column is now
   `speedMarchMult`, a FRACTION of the march: 1.0 is what the map says a man
   walks, a bandit's 2.25 means he runs, a golem's 0.75 that he lumbers. All
   27 rows are the old numbers divided by the peasant's, so every relative
   speed the fights were tuned around is preserved verbatim. The player wears
   `kHumanMarchMult` (1.0) times his own sheet, and the ×0.4 is gone.
5. **Inertia, and its DISSIPATION.** The acceleration limit is what makes a
   charge read as mass — and applied symmetrically it is ICE, which is what
   the owner met the moment the player joined the mover («как по льду
   скользит, невозможно играть»). Legs are a BRAKE as well as an engine, and
   the two are not the same strength: muscle builds speed over TIME, friction
   kills it over DISTANCE. Slowing is therefore priced by how far a body needs
   to pull up — a couple of its own radii, `a = v²/2d` — so a stop is crisp at
   any pace without being instantaneous, while TURNING (|want| ≈ |v|) still
   costs the full acceleration limit and a charge keeps its mass.
   Both halves scale by the ground's **grip** (`kTileGroundGrip`), which is
   all ice would ever need: one row with grip near zero and every body in the
   world slides on it, with no code anywhere knowing what ice is.

Locked by the new `subworld_walk` smoke — the player's legs had **no test
at all** until now: he moves from an intent, his peak pace on road against
grass matches the shared law's ratio (measured 1.417 against 1.414), and
released at speed he comes to a full stop within ~4 tiles instead of coasting
a quarter second. The brake law itself is pinned headless in
`movement_steering_test` (`test_stop_is_a_stop_not_a_skid`), with ice as the
negative control.

## Tree-count layer (contextual cells)

Every cell carries a scalar **tree count** — [macro/tree_layer.h](src/macro/tree_layer.h)
`TreeLayer` (uint16), golden constant **`kMaxTreesPerCell = 16384` (2^14)** =
the densest forest, an `FT_Tree` cell with all 8 neighbours forested. The count
is the ONE authority three consumers read:

- **Derivation** (worldgen, regenerated from the seed each boot):
  `count = clamp(biomeBase(biome) + 16384 · massifCells₃ₓ₃/9, 0, 16384)`;
  water = 0. The massif mask is `spawn_trees`' domain-warped FBM — the same
  organic лесные массивы the old feature-based map drew. The 3×3 fraction is
  a box filter over that binary mask, so the field is *smooth by
  construction* — that is the whole boundary story for the SUBWORLD scatter.
  `kForestClassTreeCount = 8192` (2^13) is the ONE binary "is forest"
  threshold: subworld Forest mode, forest fauna, tooltip, **and the map
  sprite gate**. The sprite itself is *procedurally dense*: every
  forest-class cell sources 1–4 organic crown blobs (hashed positions/radii,
  per-cell species colour), MORE and LARGER the higher its count — massif
  cores close into solid canopy, rims thin to scattered single crowns, and
  the canopy visibly thickens toward denser neighbours (each fragment
  composites its whole 3×3). The boundary is crisp — below 8192 a cell grows
  nothing, felling snaps it out — but its *shape* is the noisy union of
  crowns, локально случайная как берег: no alpha fades, no straight cell
  edges. Man-made cells (roads, and the settlement cells that always sit on
  one) source no canopy — neighbours' crowns overhang their rim, so the
  forest recedes around features/landmarks with an organic clearing edge.
  Biome ambience (`kBiomeBaseTreeCount`, max 1450) sits far below the class
  threshold and only feeds the subworld ground scatter.
- **Macro sprite** (`u_treeMap`, binding 5, R8 = count/16384): `macro.frag`'s
  tree decor is now *density-driven*, not feature-gated — taiga's ambient
  trees show, a felled cell visibly thins, опушка fades with the field.
- **Subworld scatter**: `scatter_universal_trees` derives its per-cell rate
  from the neighbours' counts (`rate = count / (area · kTreeScatterYield)`,
  yield = measured FBM-gate survival, so *placed ≈ count*), bilinearly
  blended across the ring exactly as before — seams stay smooth.

**Micro → macro writeback**: felling a tree in the subworld (a no-target melee
swing, console `chop`, the woodcutter agents) removes its `Structure::Tree`
from the owning cell + composite (`SeamlessSubworldManager::fell_tree_near`)
and decrements the owning macro cell's count through the registry's Trees
CARRIER row (`resource_field_apply` — [resources.md](resources.md)). The
grid is the row's LIVING state: the save carries it whole (**since v36**; the old
sparse `treeOverrides` died with the derive-plus-overlay model, because a
growing forest outruns its derivation). `TreeLayer.revision` drives a
surgical binding-4 re-upload (`upload_tree_field`), never per frame. The
writeback is delta-only by design: an untouched visit changes nothing, so
the probabilistic scatter can never drift the macro counts. Locked by
`tree_layer_test` (formula, torus build, clamps, v36 restore round-trip,
scatter calibration ±20%) and the console-smoke `chop` block (in-game count
decrement + revision).

## Data-driven extension

Add a feature → one `FeatureType` value + one placement handler + one GLSL
overlay branch. No if-chains in the engine.

## Backend note

`u_featureMap` is an R8 texture sampled by GLSL today; under Vulkan it becomes a
sampled image read by the terrain pipeline — same byte semantics.

## Light occlusion (macro night lighting)

The same feature grid is a second time an **optical-cost field** for the macro
night-glow bake ([macro-lighting.md](macro-lighting.md)). `bake_light_field`
spreads each emitter's light by bounded Dijkstra whose per-cell step cost is
`kFeatureOpticalCost[feature]`: roads and dirt roads are *cheaper* than open
ground (light runs along them). Canopy occlusion comes from the tree-count
field instead — a continuous `kCanopyOpticalCost · (count/16384)` term added
per step, so a deep massif smothers glow exactly like the old binary
`FT_Tree` row (full density spends 2.5×) while thin cover only dims it; bare
massifs occlude via the elevation climb term. One table + one knob, no
engine branches.
