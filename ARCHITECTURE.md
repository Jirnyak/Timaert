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
| `game/state.ts` | PlayerState, GameState, WorldTime, Settlement, save/load |
| `game/attributes.ts` | Stat formulas, levelling, XP curves |
| `game/items.ts` | Item types, inventory operations |
| `game/army.ts` | Army composition, unit counts |
| `game/npc.ts` | NPC types, spawn/tick logic, AI |
| `game/pathfinding.ts` | A* over traversability grid |
| `game/world-tick.ts` | Time advancement, daily settlement simulation |
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
settlement or triggers a battle.

| File | Responsibility |
|------|----------------|
| `game/subworld/engine.ts` | Subworld game loop, input, physics |
| `game/subworld/map-data.ts` | Tile-map types and SubworldMapData |
| `game/subworld/map-factory.ts` | Creates subworld from mode + seed |
| `game/subworld/map-renderer.ts` | Renders subworld tile layers |
| `game/subworld/renderer.ts` | Subworld entity rendering |
| `game/subworld/base-generator.ts` | Shared map-gen primitives |
| `game/subworld/city-generator.ts` | Urban layout generator |
| `game/subworld/village.ts` | Village variant |
| `game/subworld/forest.ts` | Forest biome tiles |
| `game/subworld/grassland.ts` | Open field tiles |
| `game/subworld/ruin.ts` | Ruin biome tiles |
| `game/subworld/citizen-sprites.ts` | NPC sprite mapping for cities |
| `game/subworld/types.ts` | Shared subworld types |

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