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
