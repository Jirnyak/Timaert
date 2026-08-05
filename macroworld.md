# Macroworld — Макромир

L1 core: the pure, deterministic simulation of the whole world — state, terrain,
time, kingdoms, NPCs, items, army. No GL/Vulkan, no events, no UI.

- **Code:** [`src/macro/`](src/macro) — [state.h](src/macro/state.h),
  [world_tick.h](src/macro/world_tick.h),
  [politik.h](src/macro/politik.h),
  [pathfinding.h](src/macro/pathfinding.h),
  [map_generator.h](src/macro/map_generator.h)
- **TS origin:** `game/state.ts`, `game/world-tick.ts`, `game/politik.ts`, …
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L1 — Macroworld Core

## Model

- **State:** `GameState`, `PlayerState`, `WorldTime`, `Settlement`, `Village`,
  `Spire`, `Politik`. Serialised at `kSaveVersion` (see `state.h`).
- **Generation order** (`boot_world`): terrain → politik → snap-to-land →
  finalize (lake-snap + Voronoi) → landmarks → trees → roads → dirt roads →
  feature layer → zones → spires.
- **Time:** one integer tick is the world's quantum and the clock is one
  `uint64` — the ladder, the fixed simulation step and the subworld's slower day
  are all in [time.md](time.md). `world_tick` moves the clock by whole ticks and
  runs the daily settlement / village / economy simulation (see
  [macrosim.md](macrosim.md)).
- **Space:** toroidal — all distance/step math via
  [core/torus.h](src/core/torus.h). A* over a traversability grid in
  `pathfinding`.

## Data-driven extension

Add a kingdom → one `kingdom_defs()` row. Add an NPC kind → one `kNpcTypes[]`
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

Each stamped river cell is then **carved below sea level** (`carveH = 94`, under
`seaLevel8 = 102`) and re-masked, so the one `biome_at()` / `bt_biome()`
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
existing road cells cheaply (`kRoadShare = 0.30`) and rejects water.

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

## Connections

Feeds every subworld cell a `CellContext` (single source of truth — the
subworld never re-derives macro data). Consumed by all UI overlays. The macro
NPC/squad mass is the target of GPU simulation (see [macrosim.md](macrosim.md));
only the cell chunk around the player is CPU-embodied.
