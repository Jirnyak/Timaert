# Features — Фичи

Static, persistent per-cell decorations between the biome and landmarks: **road,
dirt road, tree.** They never alter the underlying biome. (Mountains are **not** a
feature — they are the elevation-classified Mountain biome; see
[biomes.md](biomes.md).)

- **Code:** [macro/features.h](src/macro/features.h),
  [macro/spawners.cpp](src/macro/spawners.cpp) (`trace_roads`,
  `trace_dirt_roads`, `spawn_trees`)
- **TS origin:** `game/features.ts`, `game/*-spawner.ts`, `game/road-network.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Feature Layer

## Model

- **`FeatureLayer`** — a byte grid stamped once at generation in pass order
  **Tree → DirtRoad → Road**, torus-wrapped, **fail-closed** on
  malformed data, and **water-filtered** by the active sea level.
- Uploaded to the GPU as `u_featureMap`; every renderer reads that single
  texture — no feature logic re-derived at render time.
- Roads: native terrain-cost A* baseline (documented divergence from TS
  corridor Bresenham; rejected-water pruning invariant is test-locked in
  `road_river_generation_test`).

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
ground (light runs along them), tree cover is *expensive* (canopy smothers
glow). The cost table lives beside the bake in
[macro/macro_lighting.cpp](src/macro/macro_lighting.cpp) — one row per
`FeatureType`, no engine branches. Terrain therefore shapes light for free from
the grid it already stamps; forests visibly darken, roads visibly carry. (With
mountains modelled as a biome rather than a feature, bare massifs no longer sit
in this grid and so do not yet occlude — see macro-lighting.md §7.)
