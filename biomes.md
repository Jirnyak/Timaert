# Biomes — Биомы

9 biomes from a 3×3 climate matrix (temperature × moisture), plus two
elevation-classified biomes — **Water** (below sea level) and **Mountain** (above
the massif line) — for 11 in total. Every macro pixel is **synthesised live** — no
baked textures.

- **Code:** [macro/biomes.h](src/macro/biomes.h),
  [macro/macro_renderer.cpp](src/macro/macro_renderer.cpp) (GLSL `kFS`),
  [sub/base_generator.h](src/sub/base_generator.h) (`BiomeConfig`),
  [sub/textures.cpp](src/sub/textures.cpp)
- **TS origin:** `game/biomes.ts`, `game/biome-textures.ts`, `tundra.ts`…`tropics.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Procedural Biome Textures

## Model

- **Classification:** `Biome` = matrix lookup on temperature × moisture, with two
  elevation overrides outside the matrix — `Water` when `macroHeight < seaLevel`
  and `Mountain` when `macroHeight ≥ kMountainBiomeLevel` (the massif line). The
  single `biome_at()` classifier (CPU) resolves this cascade and is mirrored by
  the shader's `bt_biome`; trees and roads remain orthogonal features composed on
  top of the Mountain base.
- **Rivers reuse the `Water` override** rather than adding a biome of their own:
  `generate_river_data` carves each river cell below `seaLevel`, so `biome_at()`
  returns `Water` and the river renders through the identical sea-water path —
  crisp banks, no separate river overlay. See [macroworld.md](macroworld.md) §
  Rivers.
- **Macro synth:** per-biome `bt_<biome>(wp, sd)` GLSL functions on a 16×16
  sub-grid, neighbour-aware shore band, procedural climate overlay (snow/ice).
- **Subworld:** each biome has a `BiomeConfig` (tree density/step/size,
  heightScale, waterLevel, swampPools, duneNoise) driving terrain shaping.

## Data-driven extension

Add a biome → one `BiomeConfig` entry + one `bt_<biome>()` GLSL function + one
`bt_tex()`/matrix branch. No engine code.

## Backend note

Biome rendering is currently a GLSL fragment synth. Under Vulkan it becomes a
SPIR-V graphics pipeline reading the same data textures (`u_master`,
`u_featureMap`, `u_zoneMap`) — the per-biome math is unchanged. Rivers need no
input of their own: carved to `Biome::Water` in generation, they fall out of
`u_master`'s height/mask like any sea cell. (The dead `u_riverMap` binding was
deleted 2026-08-13; the river mask lives on CPU-side as gameplay state —
`TerrainData.riverData`.)
