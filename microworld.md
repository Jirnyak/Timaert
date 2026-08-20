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
- **TS origin:** `subworld/*`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L2 — Microworld (Subworld)

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

## Settlements — city street plan

A subworld **city** (`gens/dispatch.cpp` `gen_city`) is a walled disk of houses,
fields and a central plaza + keep. The interior street network is
**radial-concentric** rather than a centre-rooted starburst:

- **Avenues** — radial roads from the plaza to the rim, evenly spaced over the
  full circle from a seed-derived base rotation, so a city is symmetric in every
  direction *regardless of which neighbours carry roads*;
- **Ring roads** — concentric polygons (subdivided so they read as round) tying
  the avenues into blocks and spreading circumferential road density;
- **Frontage streets** — short tangential streets fanning from every
  avenue×ring node, so the road-gated house scatter finds frontage across the
  whole footprint instead of piling downtown.

The old design grew every street as a ray from the cell centre and, with no
road-bearing neighbour, defaulted its axis to angle 0 — so houses clumped into
the east/south-east corner with whole quadrants empty (the "one clump" report).
Measured on the parity-test city, the plan moves houses from **4 of 8 angular
sectors empty** (min/max 0.00) to **all 8 populated** (min/max ≈ 0.68) and lifts
the outer-half fraction from 0.44 to 0.63.

All layout tunables — avenue/ring/street counts vs population, ring radii, house
count curve — live in **[`src/sub/city_layout.h`](src/sub/city_layout.h)** as one
`CityLayout` config + pure `city_*()` response curves (the `seasons.h` /
`BiomeConfig` idiom), so retuning a city is data, not code. The street RNG is
seeded off `ctx.seed` so it never perturbs the r-stream that drives keep / house
/ field / wall placement (determinism + save-stability preserved). The spread is
locked by `tests/city_distribution_test.cpp` (no empty sector, angular balance,
radial spread across seeds/populations); counts stay locked by
`subworld_generator_parity_test`.

### A town's people live in the town

`city_layout.h` also owns the settlement **footprint** — where the town
physically is — because the citizen spawner needs it as badly as the generator
does. It used to be two magic-number expressions buried in `gen_city` /
`gen_village`, so `sub/spawn.cpp` drew each inhabitant as a uniform random tile
out of the whole 1024×1024 macro cell. A city walls 4–8 % of its cell and a
village ~1 %: **92–99 % of every settlement's population was born in the fields
and forests outside its own gates**, and the streets inside stood empty.

The footprint is now one set of pure curves — `city_wall_radius`,
`city_house_radius`, `village_core_radius`, `village_wall_radius`,
`city_wall_rings`, `city_ring_wall_radius` — read by the generator that stamps
the walls AND by the populator that fills them, so they cannot describe
different towns. Citizens are sampled in that disk with `r = R·√u` (uniform in
AREA — sampling the radius linearly would pile the whole population onto the
market square), water/house/wall tiles rejected as before.

`R` is not the mean wall radius: `stamp_settlement_wall` perturbs the ring by
two harmonics plus a per-segment jitter, so the built wall wanders *inward* by
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
  (`stamp_settlement_wall` pass 2): the whole ring is walked at ~1-tile steps,
  classified, and each wall run becomes short (≤8-tile) oriented pieces that
  drape the relief — no more string of overlapping axis-aligned blobs.
- **Gates** have LINEAR width (`kGateHalfWidth`, ~8-tile opening) instead of
  the old angular arc that grew to ~64 tiles at big radii. An opening is ANY
  road crossing the ring — outer main-road gates and interior avenues punching
  inner rings alike — and each bounded opening gets two round **jamb towers**
  plus a **lintel**: a bar whose solid span starts `kGateClearM` (5 m) above
  the road (`zBase`), so bodies walk through beneath while wall-walk defenders
  cross on top.
- **Towers** (every other ring vertex) and the spire are `Shape::Cylinder` —
  round prisms in render AND collision.
- **Ruin walls** lean along their own segments (oriented rubble with honest
  gaps).

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

**Slope rebalance (2026-07-30, owner-approved).** Even smooth crests read as
sheer *walls* in first person: a mountain cell measured a median slope of 43°
(p90 70°, p99 78°) — ~600 m of ridge amplitude on 110–250-tile waves. The
rebalance carries the same relief on longer waves (`kFreqs` → 0.0018/0.004,
~550/250-tile), eases the peak target, halves the mountain detail-noise scale
and makes the ridge blend a smoothstep (C1 at both ends, so ridge growth no
longer stacks its full gradient right at the massif edge). A **micro-crag
octave** (~6 m at ~120 tiles, riding the ridges) restores mesh-scale mountain
grain — curvature scales with A/λ² but slope only with A/λ, so the grain costs
~1° of slope. Result: p50 ≈ 31°, p90 ≈ 49°, peaks ≈ 1480 m — massifs, not
walls. `mountain_mesh_smoothness_test` was re-pinned for the new look (its
aliasing ceiling is unchanged).

### Universal terrain flattening under macro content

The heightmap generator receives one `TerrainMod {damp, plateauR}` per 3×3
neighbour, resolved by the ONE data table `terrain_mod_for(landmark, feature)`
(`base_generator.h`): City `{1.0, 280}`, Village `{0.9, 200}`, Ruin/Spire
`{0.6, 120}`, road-feature cells damp ≥ 0.55. `damp` scales down that cell's
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
the owning cell's captured 3×3 biome ring (same 0.5-centre convention as the
heightmap, sharpened to a ~256-tile mixing band) and a **dither** keyed to
absolute tile coordinates — taiga speckles into meadow the way foothills fade
into plains. Authored tiles (roads, fields, rock, shore, water) never dither;
Water neighbours never bleed onto land. Each `LoadedCell` carries its ring
(`cell_biome_ring`), so a cell's material bytes are a window-independent
property of the cell: the GPU toroidal seam shift relocates them unchanged and
the from-scratch selfcheck still matches byte-for-byte (`material shift
mismatch=0`). One `fillCellMaterial` helper replaced the renderer's five
duplicated LUT loops; separable axis tables keep a full-cell fill ~2-3 ms, and a
PLACEHOLDER cell — one tile id repeated across its whole 1024² — short-circuits
to a memset (or, inside the treeline dither band, to a two-value select), which
is what took a mountainous world's crossing from 19.7 ms to 2.6.
Locked by `material_seam_test` (determinism, pure core, seam-continuity of the
mix fraction with the old per-cell rule as negative control, water
containment, axis≡reference, and **structure shade surviving a window re-centre**
— also with a negative control, because keying a structure's shade to its
composite coordinate made every building jump brightness at a crossing). A water cell's DRY margin (its tiles are
reclassified to grass by `sync_water_tiles_from_heightmap`) inherits the
adjacent land biome — it used to paint as brown "water bed", a straight
green|brown wall on coastal cell borders.

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
each ring cell contributes a **tree rate** (trees/tile² from its biome config,
boosted when it carries the FT_Tree forest feature), and the per-node rate is
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

Add a biome → one `BiomeConfig` + one ground texture. Add a landmark → one
self-contained generator TU in [gens/](src/sub/gens). Add an **interior** →
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
macro stock. The whole layer, the prop table it stands on and the one E-verb
dispatch are written up in **[dungeons.md](dungeons.md)**.

## Connections

Reads the macroworld as source of truth ([macroworld.md](macroworld.md)).
Hosts all combat ([microcombat.md](microcombat.md)) and spell visuals
([spells.md](spells.md)). The combatant crowd is CPU-simulated under the O(N) bound (CANON.md S5). Overworld NPCs within ±1 cell are projected into
the 3×3 as real combat bodies and can be *possessed*; leaving as a possessed body
remaps the macro player onto its overworld cell ([possession.md](possession.md)).
