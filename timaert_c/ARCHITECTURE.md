# Architecture — Timaert (C++ / OpenGL / EnTT port)

Native rewrite of the Timaert TS/Vite/WebGL2 prototype in **C++23 + SDL2 +
OpenGL 3.2 Core + EnTT + ImGui**. This document is the **single source of
truth** for `timaert_c/` and is a faithful translation of `../ARCHITECTURE.md`
(the TS reference build) into native idioms — same four-layer model, same
patterns, same rules. The only differences are language (TS → C++23) and
runtime (WebGL2 → OpenGL Core, Svelte → ImGui, Web Worker → `std::thread`,
`Uint8Array`/`Float32Array` → `std::vector<std::uint8_t>`/`std::vector<float>`).

Four strict layers. Each depends only on layers below it — never sideways,
never up. Removing any Layer 4 file must leave the game fully functional.

```
┌──────────────────────────────────────────────────┐
│  L4 — Plot Content                               │
│  Pure data modules: quests, encounters, story     │
│  slides. No engine includes, no bus subscriptions.│
├──────────────────────────────────────────────────┤
│  L3 — Event System                               │
│  EventBus, LogicNodeEngine, effect-applicator.    │
│  Condition → effect graph that drives the game.   │
├──────────────────────────────────────────────────┤
│  L2 — Microworld (Subworld)                      │
│  Per-cell detail: city interiors, forests, ruins. │
│  Tile maps, local NPC AI, battle engine.          │
├──────────────────────────────────────────────────┤
│  L1 — Macroworld Core                            │
│  World state, settlements, time, terrain gen,     │
│  pathfinding, NPCs, attributes, items, army.      │
└──────────────────────────────────────────────────┘
```

## Dependency Rule

> A file may include from its own layer or any layer below.
> Never include upward. Never include from `ui/`.

The UI layer (ImGui overlays in `src/ui/`) sits above everything and
orchestrates layers via thin wrappers — it never owns game logic.

## Source Layout

```
src/
  app/          SDL2 + GL + ImGui boot, main loop, input dispatch.
  core/         Math (mat4/vec3 PODs), seeded RNG, torus helpers.
  gl/           Thin GL wrappers: shaders, FBOs, textures, fullscreen quad.
  ecs/          EnTT World, components, systems.
  macro/        L1 macroworld core (sim, terrain, politik, items, army).
  sub/          L2 subworld (seamless 9-cell, generators, 2D/3D renderers).
  events/       L3 event bus, logic nodes, effect applicator, quests.
  content/      L4 pure data (spells, plot, encounters, quest generators).
  ui/           ImGui overlays (Diplomacy, Settlement, Quest, Codex, Map…).
```

Build glob: `src/{app,core,gl,ecs,macro,sub,events,content,ui}/**/*.cpp`
is picked up by `GLOB_RECURSE` in [CMakeLists.txt](CMakeLists.txt). New
files compile automatically.

---

## L1 — Macroworld Core

Pure simulation. No GL state, no events, no UI. Each TS module maps 1:1
to a C++ TU pair (header + optional `.cpp`).

| TS module                  | C++ target                                                            | Responsibility |
|----------------------------|------------------------------------------------------------------------|----------------|
| `game/state.ts`            | [macro/state.{h,cpp}](src/macro/state.h)                              | `GameState`, `PlayerState`, `WorldTime`, `Settlement`, `Village`, `Spire`, save version |
| `game/economy.ts`          | [macro/economy.{h,cpp}](src/macro/economy.h)                          | Per-settlement inventory, prices, daily trade tick |
| `game/attributes.ts`       | [macro/attributes.h](src/macro/attributes.h)                          | Stat block, level data, XP curves |
| `game/items.ts`            | [macro/items.{h,cpp}](src/macro/items.h)                              | `Item`, `Inventory` (count/add/remove), loot tables |
| `game/army.ts`             | [macro/army.h](src/macro/army.h)                                      | `CombatTemplate` universal stat block, `ArmyComposition`, RPS damage matrix |
| `game/npc.ts`              | [macro/npc.h](src/macro/npc.h)                                        | `NPCType` enum + `kNpcTypes[]` registry |
| `game/politik.ts`          | [macro/politik.{h,cpp}](src/macro/politik.h)                          | `KingdomDef` registry, capital + city placement, MST + extra roads, Voronoi `cellOwner` |
| `game/language.ts`         | [macro/language.{h,cpp}](src/macro/language.h)                        | Procedural per-kingdom phonotactic name generation |
| `game/pathfinding.ts`      | [macro/pathfinding.{h,cpp}](src/macro/pathfinding.h)                  | A* over traversability grid |
| `game/world-tick.ts`       | [macro/world_tick.{h,cpp}](src/macro/world_tick.h)                    | Time advancement, daily settlement / village / economy tick; subworld time proof still pending |
| `game/tree-spawner.ts`     | [macro/spawners.{h,cpp}](src/macro/spawners.h) `spawn_trees`          | FBM-density tree placement |
| `game/mountain-spawner.ts` | [macro/spawners.{h,cpp}](src/macro/spawners.h)                        | Height-threshold mountain feature |
| `game/road-spawner.ts`     | [macro/spawners.{h,cpp}](src/macro/spawners.h) `trace_roads`          | Current C++ road tracing; TS parity audit required before further claims |
| `game/road-network.ts`     | [macro/spawners.{h,cpp}](src/macro/spawners.h)                        | Corridor-snap path tracing |
| `game/dirt-road-spawner.ts`| [macro/spawners.{h,cpp}](src/macro/spawners.h) `trace_dirt_roads`     | Village → main-road dirt path |
| `game/features.ts`         | [macro/features.h](src/macro/features.h)                              | `FeatureType` enum, `FeatureLayer` byte grid, builder |
| `game/zones.ts`            | [macro/zones.{h,cpp}](src/macro/zones.h)                              | Difficulty heightmap (BFS civ + mountain interior + fBM) |
| `game/biomes.ts`           | [macro/biomes.h](src/macro/biomes.h)                                  | Biome enum, 3×3 climate matrix |
| `game/biome-textures.ts` + `tundra.ts`…`water-biome.ts` | [macro/macro_renderer.cpp](src/macro/macro_renderer.cpp) (GLSL `kFS`) | Procedural macroworld ground rendering: 10 per-biome `bt_<biome>(wp,sd)`, neighbour-aware shore, climate overlay |
| `game/flag-generator.ts`   | [macro/flag_generator.{h,cpp}](src/macro/flag_generator.h)            | Procedural 128×128 RGBA8 heraldic flag bitmaps |
| `game/movement-cost.ts`    | [macro/movement_cost.h](src/macro/movement_cost.h)                    | Data-driven SP costs per biome / feature |
| `game/npc-ai.ts`           | [macro/npc_ai.{h,cpp}](src/macro/npc_ai.h)                            | NPC AI tick: reusable behaviour functions shared by NPC types |
| `game/rng.ts`              | [core/rng.h](src/core/rng.h)                                          | Seeded xorshift32 RNG |
| `game/torus.ts`            | [core/torus.h](src/core/torus.h)                                      | Toroidal map geometry helpers (wraparound, distance, step) |
| `game/audio.ts`            | `macro/audio.{h,cpp}` *(planned)*                                     | SDL_mixer track loading / playback |
| `game/renderer.ts`         | [macro/macro_renderer.{h,cpp}](src/macro/macro_renderer.h)            | Single fragment shader: biome + features + zones + cell-grid + time tint |
| `game/markers.ts`          | [macro/markers.h](src/macro/markers.h)                                | Universal POI/quest/danger/waypoint marker list |
| `character/`               | [character/](src/character/) *(planned)*                              | Sprite atlas, animation, palette, character generation |
| `webgl/map-generator.ts`   | [macro/map_generator.{h,cpp}](src/macro/map_generator.h)              | GPU master texture pipeline (heights, moisture, temperature, mask) |
| `webgl/shaders.ts`         | inline `kVS` / `kFS` strings in renderer / generator TUs              | GLSL sources |
| `webgl/webgl-context.ts`   | [gl/](src/gl/)                                                        | GL context wrappers, FBO, textures, fullscreen quad |

### Spell System

Modular spell framework in [content/spells/](src/content/spells). Core
infrastructure + pluggable spell modules — adding a spell means adding one
file, no engine changes.

| TS module                    | C++ target                                                                    | Role |
|------------------------------|--------------------------------------------------------------------------------|------|
| `game/spells/spell-types.ts` | [content/spells/spell_types.h](src/content/spells/spell_types.h)              | Spell type definitions, registry |
| `game/spells/spell-casting.ts`| [content/spells/spell_book.{h,cpp}](src/content/spells/spell_book.h)         | Cast logic, cooldowns, mana cost |
| `game/spells/spell-renderer.ts`| `content/spells/spell_renderer.{h,cpp}` *(planned)*                         | Visual effects rendering for active spells |
| `game/spells/index.ts`       | [content/spells/registry.cpp](src/content/spells/registry.cpp)                | Re-exports + spell registration |
| `game/spells/fireball.ts`    | `content/spells/fireball.{h,cpp}`                                              | AoE damage projectile |
| `game/spells/ice-shard.ts`   | `content/spells/ice_shard.{h,cpp}`                                             | Targeted frost projectile |
| `game/spells/lightning-chain.ts` | `content/spells/lightning_chain.{h,cpp}`                                  | Bouncing arc damage |
| `game/spells/energy-beam.ts` | `content/spells/energy_beam.{h,cpp}`                                           | Sustained directional beam |
| `game/spells/magic-bolt.ts`  | `content/spells/magic_bolt.{h,cpp}`                                            | Basic ranged attack |
| `game/spells/armageddon.ts`  | `content/spells/armageddon.{h,cpp}`                                            | Screen-wide damage |
| `game/spells/flight.ts`      | `content/spells/flight.{h,cpp}`                                                | Movement mode toggle |
| `game/spells/haste.ts`       | `content/spells/haste.{h,cpp}`                                                 | Speed buff |

```
SpellBook { known, active, cooldowns, mana, maxMana, manaRegenPerSec }
spellbook_learn / set_active / can_cast / cast / tick
```

### Politics System ([macro/politik.{h,cpp}](src/macro/politik.h))

Kingdom-driven world generator. Politics is the **source of truth** for
where capitals sit, which cities belong to whom, and how roads connect
them. Pure data: no rendering, no events, no UI.

**Pipeline:**

1. **`generate_politik(seed, mapW, mapH)`** — iterates `kingdom_defs()`
   (data registry of `{id, lineage, region, minCities, maxCities,
   capital_requires_lake, color_rgb, priority}`), places one capital per
   kingdom, scatters child cities around it, then per kingdom builds a
   **Prim's MST** seeded at the capital + one extra nearest non-connected
   edge per city for redundancy (cap 4 connections), and finally one
   inter-kingdom **bridge road** between every kingdom pair whose closest
   city pair is within `0.35 ×` the half-diagonal.
2. **`snap_cities_to_land(politik, terrain, seaLevel8, radius)`** — bounded
   spiral BFS that nudges every city onto the nearest land cell. Required
   because `generate_politik` is terrain-agnostic; without this pass cities
   placed in coastal regions can land in water.
3. **`finalize_politik(politik, terrain, seaLevel8)`** — per kingdom whose
   def has `capital_requires_lake`, nudges the capital to the nearest
   coastal cell with ≥ 4 water neighbours; then runs a multi-source
   4-neighbour BFS Voronoi over **land cells only**, writing per-cell
   `cellOwner` (`0xff` = unowned). Territories never jump the sea, so
   the political map respects coastlines exactly. Consumed by the
   Diplomacy / map overlays.

**Lineages** (`Empire | Magika | Timaert | Barbarians`) drive faction
relations and city aesthetics. Adding a new kingdom = one entry in
`kingdom_defs()` + one language seed; placement, naming, roads, and
territory all adapt automatically.

**Output type `Politik`:**
```cpp
struct Politik {
    std::vector<City>    cities;
    std::vector<Kingdom> kingdoms;
    std::vector<std::uint8_t> cellOwner; // mapW * mapH ; 0xff = unowned
    int mapW = 0, mapH = 0;
};
```
Stored in `GameState`, serialised with saves, consumed by:
- `ui::draw_map_overlay` — political map mode
- `ui::draw_diplomacy_overlay` — kingdom roster + relations
- `ui::draw_settlement_overlay` — capital banner + lineage label

### Combat System

Combat is **unified across the whole game** — there is no separate "battle
mode". Player, NPCs, garrison units, and bandits share **one stat block**
(`CombatTemplate` in [macro/army.h](src/macro/army.h)) and **one engine**
([sub/ai.cpp](src/sub/ai.cpp) + [sub/engine.cpp](src/sub/engine.cpp)).
Macroworld interactions hand off to the subworld when a fight starts.

**Universal stat block — `CombatTemplate`:**
```cpp
struct CombatTemplate {
    int   hp, damage;
    float speed, attackRange, cooldown;
    const char* label;
    AttackKind  attackKind;          // Melee | Missile
    float       missileSpeed;
    float       missileBlast;
    std::uint32_t missileColor;
};
```
Used by **both** `kUnitStats[]` (army units: Swordsman, Archer, Spearman,
Horseman) and `kNpcTypes[i].combat`. There is no second combat schema —
all entities are interchangeable participants.

**Rock-paper-scissors damage matrix** (`damage_multiplier` in `army.h`):
- Swordsman → 1.5× vs Archer
- Archer → 1.5× vs Spearman
- Spearman → 1.8× vs Horseman
- Horseman → 1.4× vs Swordsman

Other matchups = 1.0×. Lookup via `damage_multiplier(attacker, defender)`.

**Engine constants ([sub/engine.h](src/sub/engine.h)):**

| Constant                  | Effect                                                  |
|---------------------------|---------------------------------------------------------|
| `kHostileThreshold = -50` | Faction reputation below this → auto-aggro              |
| `kHitRepPenalty = -1`     | Player attacks on neutral cost reputation               |
| `kCrowdPenalty = 40`      | Damage falloff per extra attacker on one target         |
| `kDetectionRadius = 200`  | NPC awareness range (subworld units)                    |

Hostility is **faction-driven**, not entity-driven: any NPC's hostility
toward the player is derived from `factions[npc.factionId].relation`. When
the player attacks a friendly NPC, `kHitRepPenalty` deducts 1 reputation;
crossing `kHostileThreshold` flips the entire faction hostile.

**Combat AI** uses `tick_combat_move` for both melee and missile attackers.
Multiple attackers ganging one target suffer the `kCrowdPenalty` distance
spread, naturally creating combat formations.

**Recruitment & garrisons** ([macro/army.h](src/macro/army.h)):
- `hire_unit(playerArmy, garrison, type, gold)` — atomic recruit from city
- `kHireCost` / `kUpkeepCost` per unit type
- City `garrison: ArmyComposition` regenerates daily in `world_tick.cpp`

**Survivors** are counted post-fight via `count_survivors(army)` — feeds
back into the macroworld army composition for the next encounter.

### Feature Layer

Features are static, persistent visual elements placed on macroworld cells.
They sit between the terrain biome (GPU-computed) and landmarks/entities
(cities, NPCs). Features do not alter the underlying biome.

**Data-driven architecture:** all feature classification happens once during
generation. `build_feature_layer()` stamps each cell with a `FeatureType`.
The resulting byte grid is uploaded to the GPU as `u_featureMap`. All GLSL
renderers read that single texture to decide what to draw — no feature
logic is re-derived at render time. Water cells are filtered out at build
time so roads / trees / mountains never appear on water.

| Feature  | Module                                                              | Rendering         | Placement                           |
|----------|---------------------------------------------------------------------|-------------------|--------------------------------------|
| Road     | [macro/spawners.cpp](src/macro/spawners.cpp) `trace_roads`         | GLSL overlay      | In commit `0866bb4`, C++ road generation used budgeted torus A* with reusable scratch and dry/short Bresenham fallback. TS `road-network.ts` uses corridor-guided Bresenham over `tData.roadData`; classification is `UNKNOWN` until parity audit. |
| DirtRoad | [macro/spawners.cpp](src/macro/spawners.cpp) `trace_dirt_roads`    | GLSL overlay      | Spiral search up to 60 tiles → torus-aware lerp trace, skips villages already on roads, never overwrites main road, `landMaskA` filters water/ice |
| Tree     | [macro/spawners.cpp](src/macro/spawners.cpp) `spawn_trees`         | Feature byte + GLSL overlay | Domain-warped multi-scale FBM density (large×0.40 + med×0.35 + fine×0.25), biome-gated, shoreline buffer + mountain cap |
| Mountain | [macro/zones.cpp](src/macro/zones.cpp) / spawners                  | GLSL overlay      | Height threshold                    |

**Cell structure** (bottom → top, identical to TS):
1. **Biome** — terrain type from 3×3 climate matrix (temperature × moisture),
   or `Biome::Water` when `macroHeight < seaLevel` (GPU-computed in
   `map_generator.cpp` fragment shader)
2. **Feature** — road, tree, mountain, dirt road (`FeatureType`,
   data-driven byte grid)
3. **Zone** — difficulty level 0-9 (`ZoneLayer`, see below)
4. **Landmark** — settlement, dungeon, etc. (full entity object)

Every cell's context (biome + feature + zoneLevel + landmark + macroHeight)
is passed to the subworld as `CellContext`. The subworld never re-derives
this data — it reads the macroworld as the single source of truth.

### Difficulty Zones

Universal per-cell **danger heightmap** ([macro/zones.{h,cpp}](src/macro/zones.h)).
Every macroworld cell carries a continuous **danger altitude**
`level ∈ [0, 1]`, conceptually identical to a terrain heightmap — peaks of
danger emerge from noise + bias, just as mountains emerge from elevation
noise + bias.

```
danger(x, y) = clamp01(
    fbmNoise(x, y)            // organic base relief (5-octave fBM)
  - civInfluence(x, y)        // cities / villages / roads pull DOWN
  + mountainInfluence(x, y)   // mountain mass pushes UP
)
```

A quantised byte `0..9` is stored alongside the float field for systems
that prefer integer thresholds:

| Byte | Label       | Typical placement                          |
|------|-------------|---------------------------------------------|
| 0    | Safe Haven  | City cores                                  |
| 1    | Settled     | Around cities, villages                     |
| 2    | Patrolled   | Roads, near villages                        |
| 3    | Frontier    | Open countryside, fringes                   |
| 4    | Wild        | Remote land                                 |
| 5    | Untamed     | Deep wilderness, foothills                  |
| 6    | Perilous    | Forest interiors, mountain slopes           |
| 7    | Forsaken    | Mountain interiors, deep wilds              |
| 8    | Cursed      | Rare wilderness pockets                     |
| 9    | Hellgate    | Mountain peaks, deepest wilds               |

**Pure data — not stored in saves.** Deterministic from world seed +
civilization layout. Regenerated on every load (mirrors `Politik`).

**Generation pipeline** (`generate_zones()` — three-stage compose):

1. **Civilization potential field** (BFS, "max strength wins"). Each city
   (`+1.10`), village (`+0.55`), road (`+0.35`), dirt road (`+0.22`) seeds
   a strength value. 8-connected diffusion subtracts `0.012` per step
   (~`0.017` diagonal). Frontier stops once strength hits 0 → smooth
   organic falloff that naturally clamps to 0 in remote land. Subtracted
   from the noise base to pull civilized regions toward 0.

2. **Mountain interior depth** (BFS from non-mountain cells). Distance to
   the nearest non-mountain cell. Cells get a base `+0.08` mountain boost
   plus `+0.04 × depth`, capped at `+0.45` total. Mountain peaks become
   the natural high-danger ridges of the world.

3. **fBM noise base** — 5-octave value noise (persistence 0.5, lacunarity
   2, base wavelength 96 cells), bilinear-interpolated and smoothstepped
   per octave. Toroidally wrapped. Provides the organic `[0,1]` base
   relief — exactly the same construction used for terrain elevation.

After composition the field is clamped to `[0, 1]` and quantised to bytes
`floor(field * 10)`.

**Tunables** are top-of-file `constexpr` constants (`kCiv*`, `kMountain*`,
`kWaterBoost`, `kForestBoost`, `kNoiseBaseCells`, `kNoiseOctaves`).
Reshape the entire world by editing one number — no engine changes.

**Generation order** (`boot_world` in [app/main.cpp](src/app/main.cpp)):
```
generate_terrain
  → generate_politik → snap_cities_to_land → finalize_politik (lake-snap + multi-source BFS Voronoi over land)
  → populate_landmarks_from_politik
  → spawn_trees
  → trace_roads (current C++ road pass; TS parity audit required)
  → trace_dirt_roads
  → build_feature_layer
  → generate_zones
  → generate_spires
```
Roads are the **last** connectivity step before feature compositing.
Corrected 2026-05-11: Windows boot success proves the current C++ road pass
does not hang that build; it does not prove TS parity. TS road generation must
be compared against `C:\Timaert\src\game\road-network.ts` before further road
claims or rewrites.
Zones come **after** every civilization layer and **before** any zone-gated
landmark.

**Consumers:**

| System                              | How it reads zones                                       |
|-------------------------------------|----------------------------------------------------------|
| `state.cpp` `generate_spires()`     | `is_allowed` predicate → spires require zone ≥ 5         |
| [sub/spawn.cpp](src/sub/spawn.cpp) `derive_context_scale()` | Each level above 2 adds +1 monster level + 18% hp/damage |
| `ui::draw_map_overlay`              | "Difficulty Zones" map mode with green→red palette + legend |
| (future) Encounter triggers         | Higher zone → higher ambush probability                  |

### Procedural Biome Textures (Macroworld Render)

The macroworld is **not** a pre-baked texture. Every screen pixel is
synthesised live in the map fragment shader from cell data + 3×3 neighbours
+ scalar fields (height, moisture, temperature, mask). No raster art is
sampled; the only inputs are data textures.

**Composition pipeline** (single fragment shader `kFS` in
[macro/macro_renderer.cpp](src/macro/macro_renderer.cpp), in order):

```
biomeTextureOverlay(mapUV)             ← biome ground + shore + climate
   ↓
roadOverlay / dirtRoadOverlay          ← FeatureLayer (roads)
   ↓
treeOverlay / mountainOverlay          ← FeatureLayer (trees, mountains)
   ↓
zoneTint                               ← ZoneLayer (zone > 4)
   ↓
cellGrid                               ← torus visibility (zoom ≥ 8)
   ↓
nightDarken                            ← time-of-day tint
```

`biomeTextureOverlay()` is the **universal pixel synth** for any cell.
For each pixel it:

1. Resolves the cell's biome from `u_master` (R=height vs `u_seaLevel`,
   G=moisture, B=temperature → 3×3 matrix lookup → `Biome` enum, with
   biome `9` = water).
2. Samples the 8 neighbours (`bt_biome(cell ± offset)`).
3. Computes the per-biome procedural texture `bt_<biome>(wp, sd)` on a
   16×16 pixel-art sub-grid (matches road/tree/mountain pattern).
4. Blends with neighbouring **land** biomes near edges (5 px reach,
   smooth transitions, no tile seams).
5. Applies a **shore band** at every water↔land boundary (per-cell-local
   noise via `bt_edgeNoise`; crisp wet-sand on water side ≤4.5 px,
   noisy contour-less fade on land side ≤12 px).
6. Applies a **climate overlay** (`bt_climateOverlay`) driven by
   `u_master.b` (temperature): patchy snow on cold land, drift ice with
   crack pattern on cold water — purely procedural, no extra data.

**Per-biome modules** (TS `tundra.ts` … `tropics.ts`, `water-biome.ts`)
are inlined as 10 GLSL functions inside `kFS`: `bt_tundra`, `bt_taiga`,
`bt_snow`, `bt_valley`, `bt_meadow`, `bt_swamp`, `bt_desert`, `bt_steppe`,
`bt_tropics`, `bt_water`. Each returns a colour modulation vector around
`1.0`. Adding a new biome = one new GLSL function + one branch in
`bt_tex()` dispatch + one entry in `bt_baseColor()`. **No engine code,
no atlas, no PNG.**

**Universal data inputs** (always available to overlays):

| Uniform                  | Source                                              | Channels                        |
|--------------------------|-----------------------------------------------------|---------------------------------|
| `u_master`               | `macro/map_generator.cpp` (RGBA8 FBO readback + GPU texture) | R=height, G=moisture, B=temperature, A=mask |
| `u_featureMap`           | `macro/features.h` `FeatureLayer` (R8 texture)      | R=`FeatureType` byte            |
| `u_zoneMap`              | `macro/zones.cpp` (R8 texture)                      | R=zone byte (0..9)              |
| `u_seaLevel`, `u_seed`, `u_mapSize`, `u_zoom`, `u_viewSize`, `u_cam`, `u_timeOfDay` | game settings | scalars |

Any future overlay can read these without touching the data pipeline.

**Why this is universal & expandable:**

- Each overlay is a pure function `vec3 overlay(vec2 uv, vec3 color)`.
- Overlays compose in a fixed order; adding one requires only:
  1. Write `myOverlay()` GLSL (read whatever uniforms it needs).
  2. Inject the snippet into `kFS` and call it in the composition chain.
- No tile cache, no canvas, no CPU rasterisation — every pixel is fresh
  per frame at any zoom.

**Future expansion (same pattern, no new architecture):**

| Future system        | Data source                                  | Procedural overlay                        |
|----------------------|----------------------------------------------|--------------------------------------------|
| Rivers               | `u_riverTexture` (already built by `map_generator.cpp`) | `riverOverlay()` — sample as blue path |
| Hillshade            | `u_master.r` (4-tap derivative)              | `hillshadeOverlay()` — multiply by `dot(n, sunDir)` |
| Water depth gradient | `u_master.r` vs `u_seaLevel`                 | extend `bt_water()` to darken with depth   |
| Faction zones        | `u_zoneMap` (extended) or `u_factionMap`     | `zoneOverlay()` — tint by faction at edges |
| Magic auras / corruption | `u_auraMap` (R8 / R16F)                  | `auraOverlay()` — chromatic noise modulation |
| Weather (rain, fog)  | scalar field per cell                        | `weatherOverlay()` — animated noise tint   |
| Borders / political  | computed from `City[]`                       | `borderOverlay()` — line at zone-id transitions |
| Seasonal foliage     | `WorldTime.season` + biome                   | swap palette in `bt_<biome>()`              |

Each is one new GLSL snippet + one uniform — never a refactor.

### Marker System

Universal point-of-interest overlay for the macroworld
([macro/markers.h](src/macro/markers.h)). Markers are placed and removed
at runtime — quests, POIs, danger zones, and waypoints all use the same
system.

**Four marker styles:** `quest` (gold `?`), `poi` (blue `★`), `danger`
(red `!`), `waypoint` (green `◆`). Each has a colour and glyph defined
in `kMarkerColors` and `kMarkerGlyphs`.

**Stored in:** `GameState::markers : std::vector<Marker>` — serialised
with save data.

**Rendering:** ImGui foreground draw list, positioned via the macro
camera transform; drawn above the GL canvas. Not sprite-based — uses
ImGui text styling with glow effects.

**Quest integration:** When a quest is accepted, markers are added for
each spatial objective. On completion / failure / abandonment, markers
are removed by `remove_markers_by_prefix(markers, "quest_<id>")`.

### Quest UI

| Component                              | Trigger                | Purpose                                             |
|----------------------------------------|------------------------|-----------------------------------------------------|
| `ui::draw_settlement_overlay` (Quests tab) | Visit settlement [E]   | Browse and accept quests at current settlement      |
| `ui::draw_quest_overlay`               | Press [Q]              | View active quest journal, track objectives, abandon |

**Quest generation flow:**
1. Player opens settlement → `generate_quests_for_settlement()` called
2. Deterministic RNG seeded from `worldSeed + settlementId + day`
3. Context includes `biome_name(...)` for enriched descriptions
4. Quests shown in Quests tab with accept buttons
5. On accept → quest added to `PlayerState::activeQuests`, markers placed

---

## L2 — Microworld (Subworld)

**Core idea: the subworld is the macroworld, detailed.**

Each macroworld cell becomes a 1024×1024 tile map when the player enters
it. The subworld is not an isolated room — it is a continuous 3×3 grid of
such maps (3072×3072 total) where every cell inherits its terrain,
climate, features, and landmarks directly from the macroworld. The result
is a seamless zoom-in: forests are forests, rivers are rivers, mountains
are mountains — because the macroworld *says* they are.

Dual rendering: 2D top-down (default) and OpenGL first-person 3D
(Might & Magic style), toggled at runtime.

| TS module                              | C++ target                                              | Responsibility |
|----------------------------------------|----------------------------------------------------------|----------------|
| `subworld/engine.ts`                   | [sub/engine.{h,cpp}](src/sub/engine.h)                  | Subworld game loop, input, AI / system tick dispatch |
| `subworld/map-data.ts`                 | [sub/map_data.h](src/sub/map_data.h)                    | `CellContext`, `SubworldMapData`, `Structure`, tile constants |
| `subworld/map-factory.ts`              | [sub/map_factory.{h,cpp}](src/sub/map_factory.h)        | Session-local subworld snapshot cache; runtime save persistence is out of v4 scope |
| `subworld/seamless-manager.ts`         | [sub/seamless_manager.{h,cpp}](src/sub/seamless_manager.h) | 3×3 cell grid, composite tile / heightmap, boundary re-centre |
| `subworld/gen-worker.ts` (Web Worker)  | `std::thread` worker pool *(deferred)*                  | Off-thread cell generation |
| `subworld/map-renderer.ts`             | [sub/renderer_2d.{h,cpp}](src/sub/renderer_2d.h)        | 2D tile-map renderer |
| `subworld/renderer.ts`                 | [sub/renderer_2d.cpp](src/sub/renderer_2d.cpp)          | 2D entity renderer (same TU) |
| `subworld/renderer-3d.ts`              | [sub/renderer_3d.{h,cpp}](src/sub/renderer_3d.h)        | First-person 3D: terrain mesh + water + sun shading |
| `subworld/camera.ts`                   | [sub/camera.h](src/sub/camera.h)                        | First-person camera (yaw/pitch, fov) |
| `subworld/math3d.ts`                   | [core/math.h](src/core/math.h)                          | mat4/vec3 PODs |
| `subworld/textures.ts`                 | [sub/textures.{h,cpp}](src/sub/textures.h)              | 64×64 procedural pixel-art atlas (9 biome + water) |
| `subworld/base-generator.ts`           | [sub/base_generator.{h,cpp}](src/sub/base_generator.h)  | Universal foundation: heightmap, `BiomeConfig`, coastal sculpting |
| `subworld/city-generator.ts` … `subworld/road-generator.ts`, `subworld/spire.ts` | [sub/gens/dispatch.{h,cpp}](src/sub/gens/dispatch.h) (and per-biome `.cpp`) | One self-contained generator per landmark / mode |
| `subworld/sky.ts`                      | [sub/sky.{h,cpp}](src/sub/sky.h)                        | Procedural sky shader: gradient, sun, moons, stars, FBM clouds |
| `subworld/lighting.ts`                 | [sub/lighting.h](src/sub/lighting.h)                    | `compute_sun(WorldTime)` → direction, colour, intensity |
| `subworld/spawn.ts`                    | [sub/spawn.{h,cpp}](src/sub/spawn.h)                    | Per-biome NPC respawn from fauna table |
| `subworld/ai.ts`                       | [sub/ai.{h,cpp}](src/sub/ai.h)                          | Local NPC AI tick (chase + cooldown attack, missile / melee) |
| `subworld/fauna.ts`                    | [sub/fauna.{h,cpp}](src/sub/fauna.h)                    | Per-biome `FaunaEntry` density tables |
| `subworld/citizen-sprites.ts`          | `sub/citizen_sprites.{h,cpp}` *(planned)*               | NPC type → sprite mapping for cities |
| `subworld/spatial-hash.ts`             | [sub/spatial_hash.h](src/sub/spatial_hash.h)            | Bucketed grid for proximity |

### Seamless 9-Cell Architecture

The subworld is a **3×3 grid of macroworld cells** (`kCellSize=1024` each,
3072×3072 total) stitched into one continuous surface. The player's
current macroworld cell sits at the centre; all 8 neighbours are generated
around it. Walking across a cell boundary triggers re-centring: the grid
shifts, new neighbours are generated, and the player experiences
uninterrupted movement.

```
┌────────┬────────┬────────┐
│  NW    │   N    │   NE   │
├────────┼────────┼────────┤
│   W    │ CENTER │   E    │   ← player is here
├────────┼────────┼────────┤
│  SW    │   S    │   SE   │
└────────┴────────┴────────┘
```

Each of the 9 cells carries a **`CellContext`** — a snapshot of everything
the macroworld knows about that cell:
```cpp
struct CellContext {
    int   cx, cy;
    float macroHeight;            // 0..1
    Biome biome;                  // resolved climate; Water if h < seaLevel
    FeatureType feature;
    int   landmarkSettlementId;   // -1 if none
    int   landmarkSize;
    std::uint32_t seed;
};
```

`SeamlessSubworldManager` owns the composite tile array
(`std::vector<std::uint8_t>`) and heightmap (`std::vector<float>`),
re-builds on boundary cross. Off-thread generation uses `std::thread`
(replaces TS Web Worker dispatch).

### Generation Pipeline

Every cell follows the same universal pipeline. The key principle:
**what matters is not just the centre cell, but all 9 neighbours.**

```
   Macroworld (height, climate, features, landmarks)
                          │
                          ▼
              resolve_cell(cx, cy) × 9       ← SubworldEngine queries
                          │                    macroworld data for each
                          ▼                    cell in the 3×3 window
 ┌──────────────────────────────────────┐
 │  NeighborGrid (3×3 CellContexts)    │  Row-major: [NW,N,NE,W,C,E,SW,S,SE]
 └───────────────────┬──────────────────┘
                     │
    ┌────────────────┼────────────────┐
    ▼                ▼                ▼
 LAYER 1          LAYER 2         LAYER 3
 Heightmap        Features        Landmarks
```

**Layer 1 — Heightmap** (neighbour-aware terrain)

The local 1024×1024 heightmap is derived from the macroworld, not invented
from scratch. `generate_heightmap()` in `base_generator.cpp`:

1. **Macro blend** — samples all 9 macroworld heights, interpolates a
   smooth base elevation field (70% macro + 30% local detail noise).
2. **Multi-octave detail** — 3 value-noise octaves (0.008, 0.02, 0.06)
   add terrain texture. Global coordinates ensure seamless noise across
   cells.
3. **`BiomeConfig` scaling** — each biome has a `heightScale` multiplier
   (0.3 for Water, 1.0 for mountains) controlling terrain amplitude.
4. **Mountain amplification** — cells with `Mountain` feature get a
   2.5× base amplifier + 0.4× per adjacent mountain neighbour. Non-mountain
   cells near mountains get gradual spillover (1 + 0.15×count) → naturally
   rising foothills.
5. **Coastal sculpting** — when a neighbour is `Biome::Water`, terrain is
   pulled down in a 25% shore band from that edge. Fully-water cells get
   70% of terrain pushed below `waterLevel`. Land between two water cells
   creates natural rivers.
6. **Biome-specific** — deserts get rolling dune noise; swamps get
   terrain dips (pools) where noise < 0.35.

**Layer 2 — Features** (modular, neighbour-aware)

Features (roads, forests, mountains) are placed based on both the centre
cell's `FeatureType` and its 8 neighbours:

- **Roads** connect toward cell edges via *edge anchors* computed from
  neighbouring road cells. A road in the centre always exits toward any
  adjacent road cell → seamless road network.
- **Forests** are denser at edges bordering other forest cells and thin
  out at edges bordering open biomes.
- **Mountains** amplify terrain (see heightmap above) and spill foothills
  into flat neighbours.
- **Water** is a universal plane rendered at `waterLevel` from `BiomeConfig`.
  Coastal cells sculpt terrain down to meet the plane → shorelines emerge.

**Layer 3 — Landmarks** (self-contained generators)

Each landmark type has a dedicated generator that fills the tile grid with
domain-specific content: streets and walls (city), farms and houses
(village), scattered ruins, etc. Landmarks sit on top of the heightmap
and features; they do not re-derive terrain — they read it.

**Output of each cell** (fed into renderers):
1. **Heightmap** — `std::vector<float>`, continuous elevation for terrain mesh.
2. **Tile grid** — `std::vector<std::uint8_t>`, biome-specific ground tiles.
3. **Structures** — 2D shapes (houses, walls, trees) with 3D render height.
4. **Water level** — per-biome threshold from `BiomeConfig`.
5. **NPC spawns** — entity list with position, sprite, AI state.

### `BiomeConfig` (data-driven terrain)

Each of the 10 biomes (Tundra…Tropics + Water) has a config in
`base_generator.h`. Adding a new biome means adding one config entry —
no engine code changes.

| Property      | Effect                                                  |
|---------------|---------------------------------------------------------|
| `treeDensity` | Forest scatter density (0–0.3)                          |
| `treeStep`    | Grid spacing for tree scatter (2–16)                    |
| `treeSize`    | `[min, max]` billboard width for trees                  |
| `heightScale` | Terrain amplitude multiplier (0.3–1.0)                  |
| `waterLevel`  | Height of the universal water plane (0.05–0.7)          |
| `swampPools`  | Enables terrain dips for swamp pools                    |
| `duneNoise`   | Enables rolling dune hills for desert                   |

### Generator Self-Containment

Each generator (`city_generator.cpp`, `village.cpp`, `forest.cpp`, etc.)
is a **fully self-contained TU**. All generation logic — street growth,
wall building, tree gradients, house placement — lives as `static`
free functions inside the generator TU. Generators do not include each
other. Duplication between generators is intentional: each is an
independent module.

`base_generator.{h,cpp}` provides only the minimal foundation shared by
all: grid allocation, neighbour-aware heightmap generation (with coastal
sculpting, mountain amplification, biome-specific terrain),
`to_map_data()` serialisation, and low-level grid primitives.

### 3D Rendering Pipeline

The 2D tile grid is the source of truth. The 3D renderer reads the same data:

- **Sky**: fullscreen gradient quad — procedural sun, moons, stars,
  FBM clouds (`sub/sky.cpp`).
- **Terrain**: heightmap (`std::vector<float>`) + tile grid
  (`std::vector<std::uint8_t>`) → 192×192 quad mesh sampled from the
  seamless heightmap, central-difference normals, indexed `GL_TRIANGLES`,
  per-tile texture from atlas (9 biome grounds + water). Lit by sun
  (4-band quantised NdotL) + point lights.
- **Water plane**: flat alpha-blended quad at `waterLevel × kHeightScale`,
  depth-write disabled. Sun-direction specular, wave animation. Water
  level comes from `BiomeConfig` via `seamless_manager::composite_water_level()`.
- **Structures**: `Structure[]` (2D shapes + height) → instanced boxes /
  cylinders. Derivative-based face normals, same lighting as terrain.
- **Billboard shadows**: projected sprite silhouettes on ground — stretched
  along sun direction, length inversely proportional to sun elevation.
  Drawn before normal billboards; translucent, depth-write disabled.
- **Billboards**: tree / prop structures → camera-facing alpha-tested quads.
  Ambient + sun intensity modulation (no per-pixel normals).
- **NPCs**: EnTT entities → per-frame billboard sprites (same shader as trees).

Render order: **Sky → Terrain → Water → Structures → Billboard Shadows → Billboards.**

Both 2D and 3D views share the same engine tick, EnTT registry, and game
state. Switching view only changes which renderer draws the frame.

### Lighting System

Pure graphics — does not affect game state, AI, or any non-rendering
system. Computed per-frame from `WorldTime` in `sub/lighting.h`,
consumed by `renderer_3d.cpp` exclusively.

**Directional light (sun/moon):**
- Sun direction matches `sky.cpp`: `sunAng = (tod − 0.25) × 2π`.
- Intensity ramps with `smoothstep(−0.05, 0.3, elevation)` — zero at night.
- Sun colour warms near horizon (dawn/dusk orange), neutral white overhead.
- Ambient colour: cool blue moonlight at night → neutral during day.
- All diffuse lighting quantised to 4 bands for pixel-retro aesthetic.

**Sprite shadows:**
- Each billboard is rendered in two passes:
  1. Shadow pass: project sprite onto ground along sun direction. Uses
     the same sprite texture → tree-shaped shadow for trees,
     character-shaped for NPCs. Dark translucent
     (`alpha × 0.35 × sunIntensity`).
  2. Normal pass: standard camera-facing billboard.
- Shadow length = `spriteHeight / max(sunElevation, 0.15)` — long at
  dawn/dusk, short at noon, zero at night.

**Point lights (modular — torches, campfires, etc.):**
- Up to `kMaxPointLights` (8) set via `renderer_3d::set_point_lights()`.
- Applied in terrain and structure fragment shaders. Quadratic falloff,
  radius-limited, quantised attenuation (4-band retro).
- `PointLight` POD: position (`x, y, z`), colour (`r, g, b`), `radius`.
- No gameplay dependency — any system can provide light positions.

---

## L3 — Event System

Tag-indexed event bus + condition-vector logic engine. Nodes react to
events and emit new ones — the core control-flow mechanism.

| TS module                       | C++ target                                                    | Responsibility |
|---------------------------------|----------------------------------------------------------------|----------------|
| `game/event-bus.ts`             | [events/event_bus.{h,cpp}](src/events/event_bus.h)            | Tick-buffered emit / subscribe, world history |
| `game/event-types.ts`           | [events/event_types.h](src/events/event_types.h)              | Discriminated union of all event types + `EventTag` enum |
| `game/logic-nodes.ts`           | [events/logic_nodes.{h,cpp}](src/events/logic_nodes.h)        | `LogicNode`, `ConditionSlot`, `LogicNodeEngine` |
| `game/node-registry.ts`         | [events/node_registry.{h,cpp}](src/events/node_registry.h)    | Built-in system nodes (encounters, level-up, greeting, clock) |
| `game/effect-applicator.ts`     | [events/effect_applicator.{h,cpp}](src/events/effect_applicator.h) | `GameEvent[] → mutate PlayerState` |
| `game/quests/quest-types.ts`    | [events/quests/quest_types.h](src/events/quests/quest_types.h) | `Quest`, `Objective`, `Reward` |
| `game/quests/quest-engine.ts`   | [events/quests/quest_engine.{h,cpp}](src/events/quests/quest_engine.h) | Objective evaluation, reward application, lifecycle |

### Quest System

Modular quest framework in [events/quests/](src/events/quests/).
Data-driven quest evaluation — adding a new objective type = one checker
entry, adding a new reward type = one applier entry. No hardcoded
if-chains.

**Three quest categories:**
1. **Main** — linear storyline with rare bifurcations (designed `.cpp` files)
2. **Procedural** — context-aware quests generated from game state
   (economy, geography, difficulty)
3. **Side** — human-designed quests placed in the world procedurally

**Architecture:** Quest engine (L3) owns lifecycle + objective evaluation.
Quest generators (L4) produce quest data from game context. Quests are
plain POD structs — serialised in `PlayerState::activeQuests`.

**Six universal objective verbs:**

| Verb             | Description                       | Checked Against                       |
|------------------|-----------------------------------|---------------------------------------|
| `visit_cell`     | Go to macroworld coordinates      | Player position (torus distance)      |
| `find_location`  | Enter subworld area               | `PlayerMove` to target cell           |
| `deliver_items`  | Bring items to settlement         | Inventory + position                  |
| `destroy_npc`    | Kill N hostiles                   | `NpcDeath` events                     |
| `wait_at`        | Stay in zone for N hours          | Position + `TimeAdvance` events       |
| `interact_cell`  | Trigger cell change               | `LandmarkChangeOwner` / `WorldCellChange` |

**Five reward types:** `gold`, `xp`, `item`, `reputation`, `event`
(any `GameEvent`).

**Reward appliers** are a data-driven registry — each type has one entry
in `kRewardAppliers`. Adding a new reward = one function, no engine changes.

**Quest tick pipeline:**
```
QuestEngine::tick() → for each active quest:
  1. Check expiry → emit QuestFail if expired
  2. Check objectives via kObjectiveCheckers[type]
  3. If all complete → apply kRewardAppliers → emit QuestComplete
```

**Procedural generation** uses settlement context:
- Economy (scarce resources → delivery quests)
- Distance (far targets → higher rewards)
- Settlement mood (poor villages → protect quests)
- Cities get 2–4 quests, villages get 1–2, at least 1 guaranteed

---

## L4 — Plot Content

Pure data modules. Each exports `LogicNode[]` arrays and optionally
active-node IDs. Imported through `content/plot/index.h`. Any file here
can be deleted and the game continues to run.

| TS module                          | C++ target                                                      | Responsibility |
|------------------------------------|------------------------------------------------------------------|----------------|
| `game/plot/index.ts`               | [content/plot/index.h](src/content/plot/index.h)                | Single import point — aggregates all plot modules |
| `game/plot/intro.ts`               | `content/plot/intro.{h,cpp}` *(planned)*                         | Intro: 9 slides, sex choice, realm choice |
| `game/plot/chapter-1.ts`           | `content/plot/chapter_1.h` *(planned)*                           | Chapter 1 placeholder (dormant) |
| `game/plot/encounters.ts`          | [content/plot/encounters.h](src/content/plot/encounters.h)      | Random encounter content table |
| `game/quests/quest-generators.ts`  | [content/quests/procedural.{h,cpp}](src/content/quests/procedural.h) | Procedural quest factories |

### Adding new plot content

1. Create `src/content/plot/my_quest.cpp` exporting
   `const std::vector<LogicNode>& my_quest_nodes();`.
2. Include in `content/plot/index.h`, register in
   `content/plot/index.cpp` aggregator.
3. Done. No engine files touched.

### Adding new quest types

- **New objective verb:** add one entry to `kObjectiveCheckers` in
  `events/quests/quest_engine.cpp` and one discriminant to `Objective`
  union in `quest_types.h`.
- **New reward type:** add one entry to `kRewardAppliers` in
  `quest_engine.cpp` and one discriminant to `Reward` union in
  `quest_types.h`.
- **New procedural quest:** add one generator function to
  `kQuestGenerators` in `procedural.cpp`. The function receives full
  game context (settlement economy, distances, world time) and returns
  `std::optional<Quest>`.

---

## UI Layer (`src/ui/`)

ImGui overlays. Thin orchestration — delegates all logic to the four
layers above.

| TS source                           | C++ target                                              | Role |
|-------------------------------------|----------------------------------------------------------|------|
| `screens/GameScreen.svelte`         | [app/main.cpp](src/app/main.cpp) `Playing` branch       | Main game loop, renders map, delegates to overlays |
| `screens/SubworldScreen.svelte`     | [sub/engine.cpp](src/sub/engine.cpp)                    | Subworld view (city/battle/exploration) |
| `screens/TitleScreen.svelte`        | [ui/screens.cpp](src/ui/screens.cpp) `draw_title_menu`  | Title menu with New / Custom / Load / Quit |
| `screens/LoadScreen.svelte`         | [ui/screens.cpp](src/ui/screens.cpp) `draw_load_screen` | Single-slot save browser for `save.bin`; runtime evidence is listed in README |
| `screens/SandboxSetup.svelte`       | [ui/screens.cpp](src/ui/screens.cpp) `draw_custom_new_game` | Custom world parameter screen |
| `screens/StoryOverlay.svelte`       | pending                                                | Universal narrative overlay (slides + choices); `ShowStory` consumer missing |
| `screens/EventOverlay.svelte`       | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_encounter_modal` | Encounter modal; `ShowDialog` / full story overlay pending |
| `screens/StatOverlay.svelte`        | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_character_panel` | Character stats / inventory / army / equipment / spells panel; tabs are runtime-evidenced, equipment slots are placeholder text |
| `screens/MapOverlay.svelte`         | `ui::draw_map_overlay`                                  | Full-screen minimap |
| `screens/CodexOverlay.svelte`       | `ui::draw_codex_overlay`                                | In-game encyclopedia / lore |
| `screens/DiplomacyOverlay.svelte`   | `ui::draw_diplomacy_overlay`                            | Faction relations and diplomacy |
| `screens/SettlementOverlay.svelte`  | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_settlement` | Settlement info, recruit, inventory, trade, quests; trade and quest accept are runtime-evidenced, Build tab is placeholder text |
| `screens/TradeOverlay.svelte`       | pending                                                | Separate trade overlay pending; buy/sell exists inside the settlement tab |
| `screens/QuestOverlay.svelte`       | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_quest_log` | Active quest journal |
| `screens/SpellOverlay.svelte`       | character panel Spells tab; casting UI pending         | Spell book surface only |
| `screens/InteractionOverlay.svelte` | pending                                                | Full NPC interaction dialog pending |
| `screens/NpcProximityPanel.svelte`  | `ui::draw_npc_proximity_panel`                          | Right-edge nearby-NPC awareness panel; NPC Talk flow is runtime-evidenced |
| `screens/DebugOverlay.svelte`       | [app/main.cpp](src/app/main.cpp) `draw_debug_ui`; `TIMAERT_DEBUG_UI` is extra-debug only | Minimal FPS / camera / world counters; full tools / cheats / entity inspector pending |
| `screens/DeathOverlay.svelte`       | `ui::draw_death_overlay`                                | Death screen with retry |
| `screens/PauseOverlay.svelte`       | `ui::draw_pause_overlay`                                | Pause menu |
| `ui/theme.ts`                       | [ui/theme.h](src/ui/theme.h)                            | Shared ImGui style: colours, padding, layout helpers |

`app/main.cpp` is the **controller** — it owns the SDL2 + GL + ImGui boot,
the main loop, the camera, and input dispatch, then delegates to extracted
modules (`world_tick`, `spawn_trees`, `effect_applicator`) for actual
computation. This is acceptable because:
- It does one thing (orchestrate the main loop).
- All pure logic lives in includable modules.
- Splitting it into sub-files would create tight bi-directional coupling.

The top status bar (`Day | HH:MM | HP/MP/SP bars | Gold | Items | Pos |
Name + Lv + EXP`) and bottom command toolbar (`II  >  >>  Z | Inv Map Bld
Qst Par Eq | Cdx Dip In/Out | – +`) live in
[ui/screens.cpp](src/ui/screens.cpp) and emit `ToolbarResult` flag bundles
consumed by the `Playing` branch of `main.cpp`.

Runtime evidence exists for Load, character tabs, settlement trade/quest
accept, and NPC Talk (see README). Equipment slots, the Build tab, and Attack
action are not complete parity claims. Combat resolver work is not a current
objective.

---

## Save / Load

| TS pattern                        | C++ target                                                         |
|----------------------------------|--------------------------------------------------------------------|
| `state.ts` save / load           | [macro/save.{h,cpp}](src/macro/save.h)                            |
| `subworld/map-factory.ts` regen  | inline in subworld load path                                       |

Magic-gated, version-gated, regenerate-from-seed. **No save compatibility:**
bump `kSaveVersion` for any breaking change to serialised data; existing
saves are silently invalidated.

Current save schema is `kSaveVersion = 4` in
[macro/state.h](src/macro/state.h). `save_game`, `load_game`, and
`inspect_save` are built, and `save.bin` is the app slot path. The v4 binary
writer/reader and harness evidence are verified (`save.bin`,
`build-msvc/runtime_save_load.err`, `save_roundtrip_test`); GUI evidence
currently proves pause-menu save and valid-slot load screens, but still needs
one canonical end-to-end GUI round-trip proof before being called complete.

---

## Key Patterns

### EventBus + LogicNodeEngine
```
emit(event) → bus buffer → flush() → listeners fire
                                    → engine.tick() checks active nodes
                                    → matching node fires effect() → emits more events
```

### Condition Vector
Each node has a `conditions[]` array and a `mask[]` bitmask. The node fires
only when all mask-required conditions are satisfied in a single tick.

### Story System
`ShowStory` and `ShowDialog` are still missing native consumers. The planned
story overlay will render `StoryPhase[]` generically, and the plot module that
emitted the event will own the interpretation of results via `sourceNodeId`
routing.

### Quest System
Quests are POD structs tracked in `PlayerState`. The `QuestEngine` ticks
alongside the logic node engine, checking objectives against events.
Procedural quests are generated on demand from settlement context
(economy, geography, mood). Each quest is a self-contained package:
objectives, rewards, and optional `onAccept` events (e.g. spawn bandits
for protect quests).

`quest_lifecycle_test` is the native objective/reward lifecycle proof. Some
objective producers still need runtime coverage through the UI/game loop, and
passing this test does not prove full TS quest parity.

---

## Rules

1. **No upward includes.** L1 never includes from L2/L3/L4. L3 never
   includes from `ui/`. Layer hygiene is enforced by review.
2. **Plot is pure data.** `content/plot/*` files export `LogicNode[]` —
   no subscriptions, no side effects at translation-unit load.
3. **Encounter content lives in `content/`.** `events/node_registry.cpp`
   includes it; the encounter *node* (L3) is separate from encounter
   *data* (L4).
4. **Effect application is centralised.** All `GameEvent[]` →
   player-state mutations go through `events/effect_applicator.cpp`.
5. **One file = one responsibility.** Don't split unless there's a
   genuine architectural seam.
6. **Max ~1000 lines** per file, relaxed for naturally encapsulated
   modules (renderers, generators).
7. **Subworld = detailed macroworld.** Every subworld property (terrain,
   biome, features) is derived from macroworld data — never invented
   locally.
8. **Neighbour-aware generation.** All subworld generators receive the
   full 3×3 NeighborGrid. Terrain, features, and landmarks must respect
   adjacent cells (roads connect, forests blend, coasts emerge,
   mountains spill).
9. **Data-driven extensibility.** Adding a new biome = one `BiomeConfig`
   entry + one ground tile texture. Adding a new feature = one
   `FeatureType` + one handler in the pipeline. No hardcoded if-chains.
10. **No exceptions, no RTTI.** Disabled in CMake (`-fno-exceptions
    -fno-rtti`). EnTT built with `ENTT_NOEXCEPTION`.
11. **Performance first.** Favour better algorithms, contiguous data
    layouts, EnTT views over pointer chasing. No per-frame allocation
    in hot paths.
12. **`GLOB_RECURSE`.** Drop a `.cpp` in `src/<layer>/`, it is compiled.
    Do not edit `CMakeLists.txt` for individual files.
13. **No save compatibility.** Bump `kSaveVersion` for any breaking
    change. No legacy code paths.

Modular, elegant, generalised, optimised — minimal systems, maximal
functionality and universality.

**ALL SYSTEMS ARE DATA DRIVEN. DATA ORIENTED PROGRAMMING.**
