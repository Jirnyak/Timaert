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
- **Time:** `world_tick` advances the clock and runs the daily settlement /
  village / economy simulation (see [macrosim.md](macrosim.md)).
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
