# Features — Фичи

Static, persistent per-cell decorations between the biome and landmarks: **road,
dirt road, tree, mountain.** They never alter the underlying biome.

- **Code:** [macro/features.h](src/macro/features.h),
  [macro/spawners.cpp](src/macro/spawners.cpp) (`trace_roads`,
  `trace_dirt_roads`, `spawn_trees`)
- **TS origin:** `game/features.ts`, `game/*-spawner.ts`, `game/road-network.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Feature Layer

## Model

- **`FeatureLayer`** — a byte grid stamped once at generation in TS pass order
  **Mountain → Tree → DirtRoad → Road**, torus-wrapped, **fail-closed** on
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
