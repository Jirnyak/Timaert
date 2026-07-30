# Features — Фичи

Static, persistent per-cell **man-made structures** between the biome and
landmarks: **road, dirt road** (future: railways, fields, canals). They never
alter the underlying biome. Natural cover is *not* a feature: mountains are
the elevation-classified Mountain biome ([biomes.md](biomes.md)) and forests
are the per-cell tree-count field (below) — `FT_Tree` was removed when the
count field took over (`FT_DirtRoad` byte 3 → 2, save v14).

- **Code:** [macro/features.h](src/macro/features.h),
  [macro/spawners.cpp](src/macro/spawners.cpp) (`trace_roads`,
  `trace_dirt_roads`, `spawn_trees`)
- **TS origin:** `game/features.ts`, `game/*-spawner.ts`, `game/road-network.ts`
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
swing, console `chop`, future лесорубы) removes its `Structure::Tree` from the
owning cell + composite (`SeamlessSubworldManager::fell_tree_near`) and
decrements the owning macro cell's count via `set_tree_count`. Mutations
persist as **sparse overrides** (`GameState.treeOverrides`, cell → count,
save **v13**) re-applied over the derived layer on load — derive, don't
store, plus a mutation overlay. `TreeLayer.revision` drives a surgical
binding-5 re-upload (`upload_tree_field`), never per frame. The writeback is
delta-only by design: an untouched visit changes nothing, so the probabilistic
scatter can never drift the macro counts. Locked by `tree_layer_test`
(formula, torus build, clamps, override round-trip, scatter calibration ±20%)
and the console-smoke `chop` block (in-game count decrement + override).

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
