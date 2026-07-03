# Biomes — Биомы

10 biomes from a 3×3 climate matrix (temperature × moisture), plus Water. Every
macro pixel is **synthesised live** — no baked textures.

- **Code:** [macro/biomes.h](src/macro/biomes.h),
  [macro/macro_renderer.cpp](src/macro/macro_renderer.cpp) (GLSL `kFS`),
  [sub/base_generator.h](src/sub/base_generator.h) (`BiomeConfig`),
  [sub/textures.cpp](src/sub/textures.cpp)
- **TS origin:** `game/biomes.ts`, `game/biome-textures.ts`, `tundra.ts`…`tropics.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Procedural Biome Textures

## Model

- **Classification:** `Biome` = matrix lookup on temperature × moisture; `Water`
  when `macroHeight < seaLevel`.
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
`u_featureMap`, `u_zoneMap`, river) — the per-biome math is unchanged.
