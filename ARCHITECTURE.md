# Architecture

Four strict layers. Each depends only on layers below it — never sideways,
never up. Removing any Layer 4 file must leave the game fully functional.

```
┌──────────────────────────────────────────────────┐
│  L4 — Plot Content                               │
│  Pure data modules: quests, encounters, story     │
│  slides. No engine imports, no bus subscriptions. │
├──────────────────────────────────────────────────┤
│  L3 — Event System                               │
│  EventBus, LogicNodeEngine, effect-applicator.    │
│  Condition → effect graph that drives the game.   │
├──────────────────────────────────────────────────┤
│  L2 — Microworld (Subworld)                      │
│  Per-cell detail: city interiors, forests, ruins.  │
│  Tile maps, local NPC AI, battle engine.          │
├──────────────────────────────────────────────────┤
│  L1 — Macroworld Core                            │
│  World state, settlements, time, terrain gen,     │
│  pathfinding, NPCs, attributes, items, army.      │
└──────────────────────────────────────────────────┘
```

## Dependency Rule

> A file may import from its own layer or any layer below.
> Never import upward. Never import from the UI layer (screens/).

The UI layer (Svelte components in `screens/`) sits above everything and
orchestrates layers via thin wrappers — it never owns game logic.

---

## L1 — Macroworld Core

Pure game-state types and simulation logic. No rendering, no events, no UI.

| File | Responsibility |
|------|----------------|
| `game/state.ts` | PlayerState, GameState, WorldTime, Settlement, Village, save/load |
| `game/economy.ts` | Resource/goods/price engine, trade routes, player trade helpers |
| `game/attributes.ts` | Stat formulas, levelling, XP curves |
| `game/items.ts` | Item types, inventory operations |
| `game/army.ts` | Army composition, unit counts |
| `game/npc.ts` | NPC types, spawn/tick logic, AI |
| `game/pathfinding.ts` | A* over traversability grid |
| `game/world-tick.ts` | Time advancement, daily settlement + village + economy simulation |
| `game/tree-spawner.ts` | Feature: tree placement (FBM noise) + pixel-art shader |
| `game/mountain-spawner.ts` | Feature: mountain pixel-art overlay |
| `game/road-spawner.ts` | Feature: road surface overlay (GLSL) |
| `game/road-network.ts` | Road tracing: corridor-guided Bresenham on GPU corridors |
| `game/features.ts` | FeatureType enum, FeatureLayer grid, builder |
| `game/flag-generator.ts` | Procedural heraldic flag generation |
| `game/monster-generator.ts` | Monster stat generation |
| `game/audio.ts` | Track loading / playback (thin Web Audio wrapper) |
| `game/renderer.ts` | WebGL entity renderer (sprite batching) |
| `character/` | Sprite atlas, animation, palette, character generation |
| `webgl/` | Map generator, shaders, GL context |

### Feature Layer

Features are static, persistent visual elements placed on macroworld cells.
They sit between the terrain biome (GPU-computed) and landmarks/entities
(cities, NPCs). Features do not alter the underlying biome.

**Data-driven architecture:** all feature classification happens once during
generation. `buildFeatureLayer()` stamps each cell with a `FeatureType`.
The resulting byte grid is uploaded to the GPU as `u_featureMap`. All GLSL
renderers read that single texture to decide what to draw — no feature logic
is re-derived at render time.

| Feature | Module | Rendering | Placement |
|---------|--------|-----------|----------|
| Road | `road-network.ts` + `road-spawner.ts` | Map pass GLSL overlay | Corridor-guided Bresenham along GPU corridors |
| Tree | `tree-spawner.ts` | Instanced sprite (GLSL) | CPU density-weighted sampling |
| Mountain | `mountain-spawner.ts` | Map pass GLSL overlay | Height-threshold (CPU, via FeatureLayer) |

`road-network.ts` walks Bresenham lines between connected settlements and
snaps each point to the nearby cell with the strongest GPU road corridor
signal. This produces 1-cell-width roads that naturally follow terrain.
Connectivity comes from pre-computed `City.connections[]` (MST + extras).

`features.ts` provides `FeatureType` enum and `FeatureLayer` — a CPU-side
byte grid built after world generation. It is uploaded as a GPU texture
(`u_featureMap`) for rendering and queried on the CPU for game logic
(encounters, NPC behaviour).

**Cell structure** (bottom → top):
1. **Biome** — terrain type from height/moisture/temperature (GPU-computed)
2. **Feature** — road, tree, or mountain (`FeatureType`, data-driven)
3. **Landmark** — settlement, dungeon, etc. (full entity object)

## L2 — Microworld (Subworld)

Detail layer for individual map cells — entered when the player walks into a
settlement or triggers a battle. Dual rendering: Canvas2D top-down (default)
and WebGL2 first-person 3D (Might & Magic style), toggled at runtime.

| File | Responsibility |
|------|----------------|
| `game/subworld/engine.ts` | Subworld game loop, input, physics |
| `game/subworld/map-data.ts` | Tile-map types, Structure, heightmap, SubworldMapData |
| `game/subworld/map-factory.ts` | Creates subworld from mode + seed, save/load/regeneration |
| `game/subworld/seamless-manager.ts` | 9-cell seamless grid manager, Web Worker dispatch |
| `game/subworld/gen-worker.ts` | Web Worker entry point for off-thread generation |
| `game/subworld/map-renderer.ts` | Canvas2D tile-map renderer (2D view) |
| `game/subworld/renderer.ts` | Canvas2D entity renderer (2D view) |
| `game/subworld/renderer-3d.ts` | WebGL2 first-person 3D renderer (3D view) |
| `game/subworld/camera.ts` | First-person camera: position, yaw/pitch, height tracking |
| `game/subworld/math3d.ts` | mat4/vec3 operations for 3D rendering |
| `game/subworld/textures.ts` | Procedural 64×64 pixel-art texture atlas |
| `game/subworld/base-generator.ts` | Minimal foundation: grid, heightmap, grid primitives, `toMapData` |
| `game/subworld/city-generator.ts` | City generator — fully self-contained package |
| `game/subworld/village.ts` | Village generator — fully self-contained package |
| `game/subworld/forest.ts` | Forest generator — fully self-contained package |
| `game/subworld/grassland.ts` | Grassland generator — fully self-contained package |
| `game/subworld/ruin.ts` | Ruin generator — fully self-contained package |
| `game/subworld/road-generator.ts` | Road generator — fully self-contained package |
| `game/subworld/spawn.ts` | NPC spawning for subworlds |
| `game/subworld/ai.ts` | Local NPC AI within subworlds |
| `game/subworld/citizen-sprites.ts` | NPC sprite mapping for cities |
| `game/subworld/types.ts` | Shared subworld types |
| `game/subworld/index.ts` | Re-exports for the subworld subsystem |

### Seamless 9-Cell Architecture

The subworld is not a single isolated tile map — it is a **3×3 grid of
macroworld cells** (CELL_SIZE=1024 each, 3072×3072 total) stitched into one
continuous surface. The player's current macroworld cell sits at the center;
all 8 neighbours are generated around it. Walking across a cell boundary
triggers re-centering: the grid shifts, new neighbours are generated, and the
player experiences uninterrupted movement.

```
┌────────┬────────┬────────┐
│  NW    │   N    │   NE   │
├────────┼────────┼────────┤
│   W    │ CENTER │   E    │   ← player is here
├────────┼────────┼────────┤
│  SW    │   S    │   SE   │
└────────┴────────┴────────┘
```

`SeamlessSubworldManager` owns the composite buffer and dispatches generation
to **Web Workers** (`gen-worker.ts`) so the main thread never stalls. When
the player approaches a cell edge, it pre-generates the next row/column.

### Generation Pipeline

Every subworld cell follows the same universal pipeline:

```
Macroworld 3×3 context
        │
        ▼
  ┌─────────────┐
  │  Heightmap   │  Derived from macroworld elevation of the center cell
  │              │  and its 8 neighbours (smooth interpolation).
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │  Biome       │  One generator per biome type (city, village, forest,
  │  Generator   │  grassland, ruin, road). Fills the tile grid with
  │              │  roads, buildings, walls, fields, vegetation, etc.
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │  Feature     │  Per-feature generator (trees, roads, decorations).
  │  Generator   │  Each feature owns its placement + rendering data.
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │  Landmark    │  Per-landmark generator (squares, ruins, structures).
  │  Generator   │  Placed on top of biome + feature layers.
  └──────┬──────┘
         ▼
  ┌─────────────┐
  │  NPC Spawn   │  spawn.ts populates the cell with NPCs appropriate
  │              │  to the biome/landmark type.
  └─────────────┘
```

**Output of each cell** (fed into renderers):
1. **Heightmap** — `Float32Array`, continuous elevation for terrain mesh.
2. **Tile grid** — `Uint8Array`, procedural terrain textures per tile.
3. **Structures** — 2D shapes (houses, walls, trees) with 3D render height.
4. **NPC sprites** — entity list with position, sprite, AI state.

### Generator Self-Containment

Each generator (`city-generator.ts`, `village.ts`, `forest.ts`, etc.) is a
**fully self-contained package**. All generation logic — street growth, wall
building, tree gradients, house placement — lives as private methods inside
the generator class. Generators do not import from each other. Duplication
between generators is intentional: each is an independent module.

`base-generator.ts` provides only the minimal foundation shared by all:
grid allocation, heightmap from 9 neighbours, `toMapData()` serialisation,
and low-level grid primitives (`markOrganicMainRoad`, `markLineOnGrid`,
`markStreetAndRemoveHouses`, `hasNearbyTile`).

### 3D Rendering Pipeline

The 2D tile grid is the source of truth. The 3D renderer reads the same data:

- **Terrain**: heightmap (Float32Array) + tile grid (Uint8Array) → terrain
  mesh with per-tile texture from atlas (roads, grass, fields, squares).
- **Structures**: Structure[] (2D shapes + height) → instanced boxes/cylinders.
- **Sprites**: tree/prop structures → camera-facing billboarded quads.
- **NPCs**: engine entities → per-frame billboard sprites.

Both 2D and 3D views share the same engine tick, entities, and game state.
Switching view only changes which renderer draws the frame.

## L3 — Event System

Tag-indexed event bus + condition-vector logic engine. Nodes react to events
and emit new ones — the core control-flow mechanism.

| File | Responsibility |
|------|----------------|
| `game/event-bus.ts` | Tick-buffered emit/subscribe, world history |
| `game/event-types.ts` | All event type definitions (discriminated union + EventTag enum) |
| `game/logic-nodes.ts` | LogicNode, ConditionSlot, LogicNodeEngine |
| `game/node-registry.ts` | Built-in system nodes (encounters, level-up, greeting, clock) |
| `game/effect-applicator.ts` | Pure function: GameEvent[] → mutate PlayerState |

## L4 — Plot Content

Pure data modules. Each exports `LogicNode[]` arrays and optionally active-node
IDs. Imported through `plot/index.ts`. Any file here can be deleted and the
game continues to run.

| File | Responsibility |
|------|----------------|
| `game/plot/index.ts` | Single import point — aggregates all plot modules |
| `game/plot/intro.ts` | Intro sequence: 9 slides, sex choice, realm choice |
| `game/plot/chapter-1.ts` | Chapter 1 placeholder (dormant) |
| `game/plot/encounters.ts` | Random encounter content table (15 encounters) |

### Adding new plot content

1. Create `game/plot/my-quest.ts` exporting `myQuestNodes: LogicNode[]`.
2. Import in `game/plot/index.ts`, spread into `PLOT_NODES` / `PLOT_ACTIVE_NODES`.
3. Done. No engine files touched.

---

## UI Layer (screens/)

Svelte 5 components with runes. Thin orchestration — delegates all logic to
the four layers above.

| File | Role |
|------|------|
| `screens/GameScreen.svelte` | Main game loop, renders map, delegates to overlays |
| `screens/SubworldScreen.svelte` | Subworld (city/battle) view |
| `screens/StoryOverlay.svelte` | Universal narrative overlay (slides + choices) |
| `screens/EventOverlay.svelte` | Dialog popup for logic-node events |
| Other overlays | Stat, map, codex, diplomacy, trade, settings, etc. |

GameScreen is the largest file (~1700 lines). It is a **controller** — it owns
the game loop, camera, and input, then dispatches to extracted modules
(`world-tick`, `tree-spawner`, `effect-applicator`) for actual computation.
This is acceptable because:
- It does one thing (orchestrate the main game loop).
- All pure logic lives in importable modules.
- Splitting it into sub-components would create tight bi-directional coupling.

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
`ShowStory` events carry a `StoryPhase[]` array (slides or choices).
`StoryOverlay` renders them generically. The plot module that emitted the event
owns the interpretation of results via `sourceNodeId` routing.

---

## Rules

1. **No upward imports.** L1 never imports from L2/L3/L4. L3 never imports from screens/.
2. **Plot is pure data.** `plot/*.ts` files export `LogicNode[]` — no subscriptions, no side effects at import time.
3. **Encounter content lives in plot/.** `node-registry.ts` imports it; the encounter *node* (L3) is separate from encounter *data* (L4).
4. **Effect application is centralised.** All `GameEvent[]` → player-state mutations go through `effect-applicator.ts`.
5. **One file = one responsibility.** Don't split unless there's a genuine architectural seam.
6. **Max ~1000 lines** per file, relaxed for naturally encapsulated modules (renderers, generators).

modular, elegant, generalised, optimised, minimal systems - maximal functionality and universality archeitecture.