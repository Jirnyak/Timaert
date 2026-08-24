# Biomes — Биомы

9 biomes from a 3×3 climate matrix (temperature × moisture), plus two
elevation-classified biomes — **Water** (below sea level) and **Mountain** (above
the massif line) — for 11 in total. Every macro pixel is **synthesised live** — no
baked textures.

- **Code:** [macro/biomes.h](src/macro/biomes.h),
  [macro/vk_macro_renderer.cpp](src/macro/vk_macro_renderer.cpp) +
  [shaders/macro.frag](shaders/macro.frag) (the per-biome synth),
  [sub/base_generator.h](src/sub/base_generator.h) (`BiomeConfig`),
  [sub/textures.cpp](src/sub/textures.cpp)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Procedural Biome Textures

## Model

- **Classification:** `Biome` = matrix lookup on temperature × moisture, with two
  elevation overrides outside the matrix — `Water` when `macroHeight < seaLevel`
  and `Mountain` when `macroHeight ≥ kMountainBiomeLevel` (the massif line);
  trees and roads remain orthogonal features composed on top of the Mountain
  base. **Honest state: the cascade is NOT single.** It exists in four CPU
  copies — `biome_at()` (`biomes.h`), `resolve_context` (`sub/engine.cpp`),
  `build_tree_layer` (`tree_layer.cpp`) and the zone bake (`zones.cpp`) — plus
  the shader mirror `bt_biome` (`shaders/macro.frag`). The copies diverge on
  the boundary byte: `biome_at` keys Water off `height < seaLevel` while the
  others key off the land/water MASK, so a cell at exactly the threshold can
  classify differently per caller. A CANON S26/S6 debt (second implementations
  of the one classifier), waiting on the one context door.
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

Biome rendering is a Vulkan graphics pipeline (`vk_macro_renderer.cpp`) running
the SPIR-V-compiled `shaders/macro.frag` over the data textures (`u_master`,
`u_featureMap`, `u_zoneMap`). Rivers need no input of their own: carved to
`Biome::Water` in generation, they fall out of `u_master`'s height/mask like
any sea cell. (The dead `u_riverMap` binding was deleted 2026-08-13; the river
mask lives on CPU-side as gameplay state — `TerrainData.riverData`.)
