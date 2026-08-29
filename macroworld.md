# Macroworld — Макромир

L1 core: the pure, deterministic simulation of the whole world — state, terrain,
time, kingdoms, NPCs, items, army. No GL/Vulkan, no events, no UI.

- **Code:** [`src/macro/`](src/macro) — [state.h](src/macro/state.h),
  [world_tick.h](src/macro/world_tick.h),
  [politik.h](src/macro/politik.h),
  [pathfinding.h](src/macro/pathfinding.h),
  [map_generator.h](src/macro/map_generator.h)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1 — Macroworld Core

## Model

- **State:** `GameState`, `PlayerState`, `WorldTime`, `Landmark` (ONE record
  with a `type` column — the structs `Settlement`/`Village`/`Spire` died with
  v62, [landmarks.md](landmarks.md)), `Politik`. Serialised at `kSaveVersion`
  (62 today, `state.h`). The world seed is an honest `uint32` since v61
  (`LayerParameters::seed` carried it as a float).
- **Generation order** (`boot_world`, `src/app/main.cpp`): terrain → trees +
  tree-count layer → deposit layer → politik (fed a `SettlementSiteContext` over
  trees/terrain/deposits) → snap-to-land → finalize (lake-snap + Voronoi) →
  landmarks → roads → dirt roads → feature layer → zones → spires. The order IS
  the law of causality (CANON S8): resources are derived **before** politics,
  because settlement placement READS them — the suitability score, not authorial
  dice, decides where people live.
- **Climate is COSINE latitude (CANON S19, owner 2026-08-29):** the map's
  Y-centre is the hot equator, top and bottom the cold poles — the one
  sanctioned anisotropy of the macro world (a latitude belt is a property of
  the WORLD, not of the coordinate grid). The profile is
  `0.5 − 0.5·cos(2π·uy)` blended with fBM noise by `temperatureVariation`
  (`map_generator.cpp`): the old triangle profile `1 − |uy−0.5|·2` had the
  right belts but a KINKED derivative at the y=0 seam — a seam defect like
  any other (S1); the cosine is smooth through the wrap by construction.
- **Time:** one integer tick is the world's quantum and the clock is one
  `uint64` — the ladder, the fixed simulation step and the subworld's slower day
  are all in [time.md](time.md). `world_tick` moves the clock by whole ticks and
  runs the daily settlement / village / economy simulation (see
  [macrosim.md](macrosim.md)).
- **ONE biome cascade (2026-08-24):** `biome_at_cell`
  ([map_generator.h](src/macro/map_generator.h)) is the cell's one classifier,
  and water is answered by the baked land **MASK** — the sea level vanished
  from every query, because the mask is its baked form (canon-audit C5/H10:
  a coastal cell used to be Meadow to a boot and Water to a wolf).
- **The baked landmark grid + the rebaker (2026-08-24):** "who stands on this
  cell" is answered by the baked u16 `landmark_grid`
  ([macro/landmark_grid.h](src/macro/landmark_grid.h),
  [landmarks.md](landmarks.md)), and every derived field — zones, that grid,
  the glow, the cost grid, the GPU zone texture — is rebaked whole by
  `rebake_world` on every load, seasonal settle and macro↔micro transition
  (CANON S7). The full contract is in [context.md](context.md).
- **Space:** toroidal — all distance/step math via
  [core/torus.h](src/core/torus.h), which owns the ONE wrap (six private copies
  of it lived in `macro/` until 2026-08-20) and the ONE signed shortest offset
  (`torus_delta`, a `std::remainder` fold — the UI's two copies corrected a
  single period and lost a marker past 1.5 world widths). A* over a
  traversability grid in `pathfinding`.
- **THE NOISE LAW (2026-08-20).** A procedural field's lattice must close on the
  world, and it closes only when the period is a WHOLE number of lattice cells
  that divides the map. Five auditors measured every field: everything that
  WALKS the cells was already toroidal (both A*s and their heuristics, the
  Voronoi and land-component BFSes, waterDist/edgeDist, the river A*, the
  night-glow Dijkstra, sight, the growth law), and everything that SAMPLED noise
  was cut. Fixed: the zone field (period 96 → 128), the forest massif FBM (no
  wrap at all — correlation across the seam was nil), the river meander lattice
  (integer division dropped the remainder, repeating every 1012 cells), and the
  mountain-ridge octave, whose period of 5.6 was not a seam problem at all: it
  cut THREE VERTICAL AND THREE HORIZONTAL CLIFFS THROUGH EVERY WORLD at columns
  the seed chose, 8.3× the map's median gradient and up to 140 bytes at maximum
  ridge intensity.

## Data-driven extension

Add a kingdom → one `kingdom_defs()` row. Add an NPC kind → one `kNpcTypeDefs[]`
row. Reshape terrain/zones → edit the top-of-file `constexpr` tunables. No
engine branches.

## Rivers

Rivers are **honest water cells**, not a paint-on overlay. `generate_river_data`
(a post-pass inside `generate_terrain`) traces least-cost channels with a
binary-heap **A\***: the heuristic is BFS step-distance to the nearest sea
(`waterDist`), which is consistent, so the first pop of a cell is optimal and
lazy deletion is valid; ties break on cell index for cross-STL (MSVC vs libc++)
determinism. The step cost hugs **climate-biome edges** — rivers run along the
Voronoi boundaries — with a gentle downhill bias (`kRiverClimbShift`: an uphill
step pays `(nH-curH) >> 1`), so channels prefer to descend and stop crossing
ridges.

Each stamped river cell is then **carved below sea level** — `carveH` is
*derived*, not assigned: `carveH = seaLevel8 − 8` (clamped ≥ 1,
`map_generator.cpp`), i.e. always one fixed notch under whatever the sea is —
and re-masked, so the one `biome_at()` / `bt_biome()`
classifier reads it as `Biome::Water`. Two consequences fall out for free:

- **Render** — a river renders through the *exact* sea-water path (blue water +
  the wet-sand shore band); there is no separate `riverOverlay`. A 1-cell river
  reads as a thin sea inlet with crisp banks. Deleting the old translucent
  overlay is what removed the blue-halo bank bug. See [biomes.md](biomes.md).
- **Subworld** — because a river cell is water ringed by higher land, the
  subworld heightmap descends smoothly land→water *within* the cell (remap +
  bilinear blend + `kLandMargin`): a naturally sub-kilometre river with no
  coastline seam, since the water is genuinely there. See
  [microworld.md](microworld.md).

Rivers are **regenerated on load** (derived from height/climate, never
persisted). `river_generation_test` locks the invariants: determinism, every
river drains to sea, no cell left above sea, no anomalous long straight runs
(`maxrun < 120` — the pre-heap baseline hit 494), a sane density band, and the
honest submerged subworld descent.

## Roads

The road **topology** is built in `generate_politik`
([politik.cpp](src/macro/politik.cpp)); the road **cells** are then traced
between connected cities by `trace_roads`
([spawners.cpp](src/macro/spawners.cpp)) with a binary-heap A\* that reuses
existing road cells and rejects water.

**ONE A\* (2026-08-29).** `sm::find_path` ([pathfinding.cpp](src/macro/pathfinding.cpp))
is the macro world's one pathfinder — the road tracer's byte-for-byte private
twin is dead, and the algorithm's working set is an explicit **`PathScratch`**
(generation-tagged indexed heap + parent arrays) the caller owns and reuses
(`AppState::pathScratch` serves map-click travel and the smoke probe). New
knobs are parameters, not forks: `maxSteps`, `blockAtOrAbove`
(`kPathNoBlock` default), `stepsOut`.

**A road is a ROW of `kRoadClasses`** ([spawners.h](src/macro/spawners.h)):
`{surface, link}` — stone `FT_Road` for city connections, dirt `FT_DirtRoad`
for village→home-city and village→nearest-landmark. **Dirt roads walk the
same honest A\*** since 2026-08-29 (`trace_dirt_roads`): the old spiral-scan +
straight-line lerp, whose only worldly knowledge was "not water", is dead —
a footpath now pays the same bed/canopy/climb the march will pay. The
stone→dirt HIERARCHY is emergent, not coded: stone is traced first, and the
dirt pass prices laid stone at the paved bed
(`feature_def(FT_Road).bedWeight = 1.0`), so footpaths JOIN the highway
instead of duplicating it.

**The planner walks THE step law (2026-08-24).** Its private second cost
table — its own `kLand = 3.33` / `kMountain = 16.7` and a third, raw-byte
classification of what a mountain is (canon-audit H1/§7.9) — is dead.
`trace_roads` takes the tree layer and builds its grid through the one
`build_cost_grid` (`movement_cost.h`): biome bed + the continuous canopy — a
pine thicket finally costs more than a meadow, so **roads route AROUND deep
woods** — and the uphill climb is priced on the edge inside `find_road_path`
exactly as every walker prices it, so roads pay ascents honestly. Roads are
laid over the very weights the march will pay ([context.md](context.md)).
Water is not priced, it is **REJECTED** — the sentinel is a block flag, never
a weight a path can pay. Reuse stays the honest way round: existing road
cells and city anchors are priced at the paved bed —
`kRoadShare = feature_bed_weight(FT_Road) = 1.0`, derived, the price
*floor* — because the old sub-floor discount (0.30) broke the heuristic's
admissibility and had never actually caused a single cell of reuse; the
comment block in `spawners.cpp` keeps the measurement.

Topology per kingdom is a **Prim's MST** rooted at the capital (guarantees every
city is reachable), plus **one redundancy edge per city** — its nearest
not-yet-connected neighbour — so the network has loops, not just a tree, and
inter-kingdom **bridges** join the closest city pair of adjacent kingdoms.

The redundancy pass has a **fan guard** (`kRoadFanCosThreshold = 0.90`,
≈ cos 26°). Three roughly-collinear cities A–B–C get MST links A–B and B–C; the
raw redundancy rule would then add a bypass A→C that fans off almost parallel to
A→B — a *doubled diagonal* that adds no real alternate route. The guard skips a
candidate edge whose bearing (shortest wrapped delta, `torus_bearings_parallel`
in [torus.h](src/core/torus.h)) runs within the threshold of a road either
endpoint already has, checked at **both** ends. It is strictly subtractive — it
only suppresses near-parallel fans, never removes an MST edge nor adds a
long cross-map road — so it **cannot disconnect a kingdom** (verified: 0
disconnected kingdoms across 12 seeds; near-parallel edge pairs fall 494→281,
−43%). Genuine wide-angle loops (≥ ~26° apart), perpendicular spurs, and
opposite-direction alternates all survive.

`torus_geometry_test` locks the bearing math (normalisation, short-way wrap) and
the fan decision on the exact A–B–C road scenarios; `road_river_generation_test`
locks the downstream `trace_roads` water-pruning and fail-closed behaviour.

## Night lighting

The macro map has a data-driven **night-glow** field baked from world state:
`collect_macro_lights` ([macro/macro_lighting.h](src/macro/macro_lighting.h))
turns settlements, villages and active spires into population-scaled warm
emitters (off each type's `LandmarkDef.lightColor`), and `bake_light_field`
rasterises their terrain-occluded spread into a per-cell field the macro shader
adds at night. It is **pure L1** — no Vulkan, depends only on `GameState` + the
landmark table — and is re-baked only on world-change: generation, save-load, and
daily population drift (`WorldTickResult.dailyTicksProcessed > 0`). One knob,
`kMacroGlowGain`, sets master brightness. Full write-up:
**[macro-lighting.md](macro-lighting.md)**.

## Knowledge (fog of war)

The player does not know the world from turn one: per-cell knowledge
(Unknown / Explored / Visible, saved since v40) with
sight propagated by the same
terrain-optical sweep as the night glow. The whole system — mechanic, render
law, the map page, future vision skills and trackers — is written up in
[map.md](map.md), THE doc.

## Connections

Feeds every subworld cell a `CellContext` (single source of truth — the
subworld never re-derives macro data). Consumed by all UI overlays. The macro
NPC/squad mass is simulated on the CPU (see [macrosim.md](macrosim.md));
only the cell chunk around the player is embodied.

**A cell is a PLACE** (fix 2026-08-20): `CellContext.cx/cy` is the **wrapped
macro index** `[0, worldCells)` — which cell of the world this is — while the
3×3 window's running coordinate lives separately in the seamless manager. One
macro cell therefore builds ONE subworld, however the player arrives: before the
split, walking east off the last column made the cell call itself 1024 while
reading macro cell 0's biome, and every derived seed (detail noise, road anchor,
tile hash) built a different world for the same place (measured: 100 % of tiles
differed, heights by up to 63 m). See `src/sub/map_data.h`.
