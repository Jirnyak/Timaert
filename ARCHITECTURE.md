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
| `game/army.ts` | Army composition, `CombatTemplate` (universal stat block), unit rock-paper-scissors |
| `game/npc.ts` | NPC types + `NPC_TYPE_DEFS` registry (stats, AI, combat, label, portrait, talkLines) |
| `game/politik.ts` | Kingdom registry, capital/city placement, intra/inter-kingdom roads, Voronoi territory |
| `game/language.ts` | Procedural per-kingdom phonotactic name generators |
| `game/pathfinding.ts` | A* over traversability grid |
| `game/world-tick.ts` | Time advancement, daily settlement + village + economy simulation |
| `game/tree-spawner.ts` | Feature: tree placement (FBM noise) + pixel-art shader |
| `game/mountain-spawner.ts` | Feature: mountain pixel-art overlay |
| `game/road-spawner.ts` | Feature: road surface overlay (GLSL) |
| `game/road-network.ts` | Road tracing: corridor-guided Bresenham on GPU corridors |
| `game/dirt-road-spawner.ts` | Feature: dirt-road paths from villages to main roads (GLSL overlay) |
| `game/features.ts` | FeatureType enum, FeatureLayer grid, builder |
| `game/biomes.ts` | Biome definitions: 3×3 temperature × moisture matrix, GPU lookup texture |
| `game/biome-textures.ts` | Procedural macroworld ground rendering: biome dispatch, neighbour-aware shore, climate overlay (snow/ice). Aggregates per-biome GLSL modules. |
| `game/tundra.ts` … `game/tropics.ts` | Per-biome procedural texture (`bt_<biome>(wp, sd)` GLSL). One file per biome. |
| `game/water-biome.ts` | Procedural water texture (animated GLSL). |
| `game/flag-generator.ts` | Procedural heraldic flag generation |
| `game/movement-cost.ts` | Data-driven SP costs per biome/feature for player movement |
| `game/npc-ai.ts` | NPC AI tick logic: reusable behaviour functions shared by NPC types |
| `game/rng.ts` | Seeded xorshift32 RNG for deterministic generation |
| `game/torus.ts` | Toroidal map geometry helpers (wraparound, distance, step) |
| `game/audio.ts` | Track loading / playback (thin Web Audio wrapper) |
| `game/renderer.ts` | WebGL entity renderer (sprite batching) |
| `game/spells/` | Spell system: types, casting, rendering + individual spell modules |
| `game/markers.ts` | Universal macroworld marker system (quests, POI, waypoints) |
| `character/` | Sprite atlas, animation, palette, character generation |
| `webgl/map-generator.ts` | WebGL-based macroworld terrain + feature generation pipeline |
| `webgl/shaders.ts` | GLSL shader sources for macroworld rendering |
| `webgl/webgl-context.ts` | GL context management, layer parameters, terrain data readback |

### Spell System

Modular spell framework in `game/spells/`. Core infrastructure + pluggable
spell modules — adding a spell means adding one file, no engine changes.

| File | Responsibility |
|------|----------------|
| `game/spells/spell-types.ts` | Spell type definitions, spell registry |
| `game/spells/spell-casting.ts` | Cast logic, cooldowns, mana cost |
| `game/spells/spell-renderer.ts` | Visual effects rendering for active spells |
| `game/spells/index.ts` | Re-exports + spell registration |
| `game/spells/fireball.ts` | Fireball: AoE damage projectile |
| `game/spells/ice-shard.ts` | Ice Shard: targeted frost projectile |
| `game/spells/lightning-chain.ts` | Lightning Chain: bouncing arc damage |
| `game/spells/energy-beam.ts` | Energy Beam: sustained directional beam |
| `game/spells/magic-bolt.ts` | Magic Bolt: basic ranged attack |
| `game/spells/armageddon.ts` | Armageddon: screen-wide damage |
| `game/spells/flight.ts` | Flight: movement mode toggle |
| `game/spells/haste.ts` | Haste: speed buff |

### Politics System (`game/politik.ts`)

Kingdom-driven world generator. Politics is the **source of truth** for
where capitals sit, which cities belong to whom, and how roads connect them.
Pure data: no rendering, no events, no UI.

**Pipeline:**

1. **`generateKingdomCities(seed, mapW, mapH)`** — Iterates `KINGDOM_DEFS`
   (data registry of `{id, lineage, region, minCities, maxCities, capital
   spawn rules, priority}`). For each kingdom: places its capital inside its
   region predicate, scatters `minCities..maxCities` member cities, and
   assigns each a procedural name via per-kingdom `Language` (see
   `language.ts`). Builds an intra-kingdom MST + a small number of inter-
   kingdom edges (trade roads).
2. **`finalizePolitik(cities, terrain)`** — Snaps the Lake Duchy capital to
   the nearest lake tile (`capitalRequires: 'lake'`). Computes a Voronoi
   `cellOwner: Uint8Array` over land cells using `torusDist` to any city —
   this is the territory map consumed by `MapOverlay` (political tint) and
   `DiplomacyOverlay` (faction list).

**Lineages** (`empire | magika | timaert | barbarians`) drive faction
relations and city aesthetics. Adding a new kingdom = one entry in
`KINGDOM_DEFS` + one language seed; placement, naming, roads, and
territory all adapt automatically.

**Output type `Politik`:**
```ts
{ kingdoms: Record<KingdomId, Kingdom>, cellOwner: Uint8Array }
```
Stored in `GameState`, serialised with saves, consumed by:
- `screens/MapOverlay.svelte` — political map mode
- `screens/DiplomacyOverlay.svelte` — kingdom roster + relations
- `screens/SettlementOverlay.svelte` — capital banner + lineage label

### Combat System

Combat is **unified across the whole game** — there is no separate "battle
mode". Player, NPCs, garrison units, and bandits share **one stat block**
(`CombatTemplate` in `army.ts`) and **one engine** (`SubworldEngine` in
`subworld/engine.ts`). Macroworld interactions hand off to the subworld
when a fight starts.

**Universal stat block — `CombatTemplate`:**
```ts
{ hp, damage, speed, attackRange, cooldown, label,
  attackKind?: 'melee' | 'missile',
  missileSpeed?, missileBlast?, missileColor? }
```
Used by **both** `UNIT_STATS` (army units: Swordsman, Archer, Spearman,
Horseman) and `NPC_TYPE_DEFS[type].combat`. There is no second combat
schema — all entities are interchangeable participants.

**Rock-paper-scissors damage matrix** (in `army.ts`):
- Swordsman → 1.5× vs Archer
- Archer → 1.5× vs Spearman
- Spearman → 1.8× vs Horseman
- Horseman → 1.4× vs Swordsman

Other matchups = 1.0×. Lookup via `getDamageMultiplier(attacker, defender)`.

**Engine (`subworld/engine.ts`):**

| Constant | Effect |
|----------|--------|
| `HOSTILE_THRESHOLD = -50` | Faction reputation below this = auto-aggro |
| `HIT_REP_PENALTY = -1`    | Player attacks on neutral cost reputation |
| `CROWD_PENALTY = 40`      | Damage falloff per extra attacker on one target |
| `DETECTION_RADIUS = 200`  | NPC awareness range (subworld units) |

Hostility is **faction-driven**, not entity-driven: any NPC's hostility
toward the player is derived from `factions[npc.factionId].relation`. When
the player attacks a friendly NPC, `HIT_REP_PENALTY` deducts 1 reputation;
crossing `HOSTILE_THRESHOLD` flips the entire faction hostile.

**Combat AI** uses `tickCombatMove` for both melee and missile attackers.
Multiple attackers ganging one target suffer the `CROWD_PENALTY` distance
spread, naturally creating combat formations.

**Recruitment & garrisons** (`army.ts`):
- `hireUnit(playerArmy, garrison, type, gold)` — atomic recruit from city
- `HIRE_COST` / `UPKEEP_COST` per unit type
- City `garrison: ArmyComposition` regenerates daily in `world-tick.ts`

**Survivors** are counted post-fight via `countSurvivors(army)` — feeds
back into the macroworld army composition for the next encounter.

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
1. **Biome** — terrain type from 3×3 climate matrix (temperature × moisture),
   or `Biome.Water` when `macroHeight < seaLevel` (GPU-computed)
2. **Feature** — road, tree, mountain, dirt road (`FeatureType`, data-driven)
3. **Landmark** — settlement, dungeon, etc. (full entity object)

Every cell's context (biome + feature + landmark + macroHeight) is passed to
the subworld as `CellContext`. The subworld never re-derives this data — it
reads the macroworld as the single source of truth.

### Procedural Biome Textures (Macroworld Render)

The macroworld is **not** a pre-baked texture. Every screen pixel is
synthesised live in the map fragment shader from cell data + 3×3 neighbours
+ scalar fields (height, moisture, temperature, mask). No raster art is
sampled; the only inputs are data textures.

**Composition pipeline (single fragment shader, in order):**

```
biomeTextureOverlay(mapUV)             ← biome ground + shore + climate
   ↓
roadOverlay / dirtRoadOverlay          ← FeatureLayer (roads)
   ↓
treeOverlay / mountainOverlay          ← FeatureLayer (trees, mountains)
   ↓
nightDarken                            ← time-of-day tint
```

`biomeTextureOverlay()` lives in `game/biome-textures.ts` and is the
**universal pixel synth** for any cell. For each pixel it:

1. Resolves the cell's biome from `u_masterTexture` (R=height vs `u_seaLevel`,
   G=moisture, B=temperature → 3×3 matrix lookup → `Biome` enum).
2. Samples the 8 neighbours (`bt_biome(cell ± offset)`).
3. Computes the per-biome procedural texture `bt_<biome>(wp, sd)` on a
   16×16 pixel-art grid (matches road/tree/mountain pattern).
4. Blends with neighbouring land biomes near edges (smooth transitions, no
   tile seams).
5. Applies a **shore band** at every water↔land boundary (per-cell-local
   noise; crisp wet-sand on water side, noisy contour-less fade on land).
6. Applies a **climate overlay** (`bt_climateOverlay`) driven by
   `u_masterTexture.b` (temperature): patchy snow on cold land, drift ice
   on cold water — purely procedural, no extra data needed.

**Per-biome modules** (`tundra.ts` … `tropics.ts`, `water-biome.ts`) export
a single GLSL function `bt_<biome>(wp, sd)` returning a colour modulation
vector. Adding a new biome = create one file, add it to the `bt_tex()`
dispatch and the `bt_baseColor()` palette in `biome-textures.ts`.
**No engine code, no atlas, no PNG.**

**Universal data inputs** (always available to overlays):

| Uniform | Source | Channels |
|---------|--------|----------|
| `u_masterTexture` | `webgl/map-generator.ts` | R=height, G=moisture, B=temperature, A=mask |
| `u_featureMap` | `features.ts` (`FeatureLayer`) | R=`FeatureType` byte |
| `u_seaLevel`, `u_worldSeed`, `u_mapSize`, `u_tileSize` | game settings | scalars |

Any future overlay can read these without touching the data pipeline.

**Why this is universal & expandable:**

- Each overlay is a pure function `vec3 overlay(vec2 uv, vec3 color)`.
- Overlays compose in a fixed order; adding one requires only:
  1. Write `myOverlay()` GLSL (read whatever uniforms it needs).
  2. Inject the snippet into `mapFrag` and call it in the composition chain.
- No tile cache, no canvas, no CPU rasterisation — every pixel is fresh
  per frame at any zoom.

**Future expansion (same pattern, no new architecture):**

| Future system | Data source | Procedural overlay |
|---------------|-------------|--------------------|
| Rivers | `u_riverTexture` (already built by `map-generator.ts`) | `riverOverlay()` — bind texture, sample as blue path |
| Hillshade | `u_masterTexture.r` (4-tap derivative) | `hillshadeOverlay()` — multiply by `dot(n, sunDir)` |
| Water depth gradient | `u_masterTexture.r` vs `u_seaLevel` | extend `bt_water()` to darken with depth |
| Faction zones | `u_zoneMap` (Uint8 per cell) | `zoneOverlay()` — tint by faction colour at edges |
| Magic auras / corruption | `u_auraMap` (Float per cell) | `auraOverlay()` — chromatic noise modulation |
| Weather (rain, fog, storm) | scalar field per cell | `weatherOverlay()` — animated noise tint |
| Borders / political maps | computed from `Settlement[]` | `borderOverlay()` — line at zone-id transitions |
| Seasonal foliage | `WorldTime.season` + biome | swap palette in `bt_<biome>()` |

Each is one new GLSL snippet + one uniform — never a refactor.

### Marker System

Universal point-of-interest overlay for the macroworld (`game/markers.ts`).
Markers are placed and removed at runtime — quests, POIs, danger zones, and
waypoints all use the same system.

**Four marker styles:** `quest` (gold `?`), `poi` (blue `★`), `danger` (red `!`),
`waypoint` (green `◆`). Each has a color and glyph defined in `MARKER_COLORS`
and `MARKER_GLYPHS`.

**Stored in:** `GameState.markers: Marker[]` — serialized with save data.

**Rendering:** HTML overlays positioned via `GameRenderer.worldToScreen()`,
displayed above the WebGL canvas. Not sprite-based — uses CSS text styling
with glow effects.

**Quest integration:** When a quest is accepted, markers are added for each
spatial objective. On completion/failure/abandonment, markers are removed
by `removeMarkersByPrefix(markers, 'quest_<questId>')`.

### Quest UI

| Component | Trigger | Purpose |
|-----------|---------|---------|
| `screens/SettlementOverlay.svelte` (Quests tab) | Visit settlement [E] | Browse and accept quests available at current settlement |
| `screens/QuestOverlay.svelte` | Press [Q] | View active quest journal, track objectives, abandon quests |

**Quest generation flow:**
1. Player opens settlement → `generateQuestsForSettlement()` called
2. Deterministic RNG seeded from `worldSeed + settlementId + day`
3. Context includes `getBiomeName()` for enriched descriptions
4. Quests shown in Quests tab with accept buttons
5. On accept → quest added to `PlayerState.activeQuests[]`, markers placed

## L2 — Microworld (Subworld)

**Core idea: the subworld is the macroworld, detailed.**

Each macroworld cell becomes a 1024×1024 tile map when the player enters it.
The subworld is not an isolated room — it is a continuous 3×3 grid of such
maps (3072×3072 total) where every cell inherits its terrain, climate,
features, and landmarks directly from the macroworld. The result is a
seamless zoom-in: forests are forests, rivers are rivers, mountains are
mountains — because the macroworld *says* they are.

Dual rendering: Canvas2D top-down (default) and WebGL2 first-person 3D
(Might & Magic style), toggled at runtime.

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
| `game/subworld/textures.ts` | Procedural 64×64 pixel-art texture atlas (9 biome + water) |
| `game/subworld/base-generator.ts` | Universal foundation: heightmap, BiomeConfig, coastal sculpting, grid primitives |
| `game/subworld/city-generator.ts` | City generator — fully self-contained package |
| `game/subworld/village.ts` | Village generator — fully self-contained package |
| `game/subworld/forest.ts` | Forest generator — fully self-contained package |
| `game/subworld/grassland.ts` | Grassland generator — fully self-contained package |
| `game/subworld/ruin.ts` | Ruin generator — fully self-contained package |
| `game/subworld/mountain.ts` | Mountain generator — fully self-contained package |
| `game/subworld/swamp.ts` | Swamp generator — fully self-contained package |
| `game/subworld/water.ts` | Water generator — fully self-contained package |
| `game/subworld/road-generator.ts` | Road generator — fully self-contained package |
| `game/subworld/sky.ts` | Procedural sky shader: day/night gradient, sun, moons, stars, clouds |
| `game/subworld/lighting.ts` | Sun/moon direction, ambient, point lights — pure graphics helper |
| `game/subworld/spawn.ts` | NPC spawning for subworlds |
| `game/subworld/ai.ts` | Local NPC AI within subworlds |
| `game/subworld/fauna.ts` | Biome fauna distribution: data-driven animal/monster spawn tables |
| `game/subworld/citizen-sprites.ts` | NPC sprite mapping for cities |
| `game/subworld/types.ts` | Shared subworld types |
| `game/subworld/index.ts` | Re-exports for the subworld subsystem |

### Seamless 9-Cell Architecture

The subworld is a **3×3 grid of macroworld cells** (CELL_SIZE=1024 each,
3072×3072 total) stitched into one continuous surface. The player's current
macroworld cell sits at the center; all 8 neighbours are generated around it.
Walking across a cell boundary triggers re-centering: the grid shifts, new
neighbours are generated, and the player experiences uninterrupted movement.

```
┌────────┬────────┬────────┐
│  NW    │   N    │   NE   │
├────────┼────────┼────────┤
│   W    │ CENTER │   E    │   ← player is here
├────────┼────────┼────────┤
│  SW    │   S    │   SE   │
└────────┴────────┴────────┘
```

Each of the 9 cells carries a **CellContext** — a snapshot of everything the
macroworld knows about that cell:
- `macroHeight` — elevation (0–1), sampled from the macroworld heightmap
- `biome` — resolved from climate (temperature × moisture → 3×3 matrix),
  or `Biome.Water` when `macroHeight < seaLevel`
- `feature` — road, tree, mountain, dirt road, or none (`FeatureType`)
- `landmark` — city, village, ruin, or none
- `landmarkParam` — population / size parameter
- `seed` — deterministic per-cell seed for reproducible generation

`SeamlessSubworldManager` owns the composite buffer and dispatches generation
to **Web Workers** (`gen-worker.ts`) so the main thread never stalls. When
the player approaches a cell edge, it pre-generates the next row/column.

### Generation Pipeline

Every cell follows the same universal pipeline. The key principle: **what
matters is not just the center cell, but all 9 neighbours.**

```
     Macroworld (height, climate, features, landmarks)
                          │
                          ▼
              resolveCell(cx, cy) × 9       ← SubworldScreen queries
                          │                    macroworld data for each
                          ▼                    cell in the 3×3 window
 ┌──────────────────────────────────────┐
 │  NeighborGrid  (3×3 CellContexts)   │   Row-major: [NW,N,NE,W,C,E,SW,S,SE]
 └───────────────────┬──────────────────┘
                     │
    ┌────────────────┼────────────────┐
    ▼                ▼                ▼
 LAYER 1          LAYER 2         LAYER 3
 Heightmap        Features        Landmarks
```

**Layer 1 — Heightmap** (neighbor-aware terrain)

The local 1024×1024 heightmap is derived from the macroworld, not invented
from scratch. `generateHeightmap()` in `base-generator.ts`:

1. **Macro blend** — samples all 9 macroworld heights, interpolates a smooth
   base elevation field (70% macro + 30% local detail noise).
2. **Multi-octave detail** — 3 Simplex octaves (0.008, 0.02, 0.06) add
   terrain texture. Global coordinates ensure seamless noise across cells.
3. **BiomeConfig scaling** — each biome has a `heightScale` multiplier
   (0.3 for Water, 1.0 for mountains) controlling terrain amplitude.
4. **Mountain amplification** — cells with `Mountain` feature get a 2.5×
   base amplifier + 0.4× per adjacent mountain neighbour. Non-mountain cells
   near mountains get gradual spillover (1 + 0.15×count) → naturally rising
   foothills.
5. **Coastal sculpting** — when a neighbour is `Biome.Water`, terrain is
   pulled down in a 25% shore band from that edge. Fully-water cells get
   70% of terrain pushed below `waterLevel`. Land between two water cells
   creates natural rivers. Land near water creates shorelines.
6. **Biome-specific** — deserts get rolling dune noise; swamps get
   terrain dips (pools) where noise < 0.35.

**Layer 2 — Features** (modular, neighbor-aware)

Features (roads, forests, mountains) are placed based on both the center
cell's `FeatureType` and its 8 neighbours:

- **Roads** connect toward cell edges via *edge anchors* computed from
  neighbouring road cells. A road in the center always exits toward any
  adjacent road cell → seamless road network.
- **Forests** are denser at edges bordering other forest cells and thin
  out at edges bordering open biomes.
- **Mountains** amplify terrain (see heightmap above) and spill foothills
  into flat neighbours.
- **Water** is a universal plane rendered at `waterLevel` from BiomeConfig.
  Coastal cells sculpt terrain down to meet the plane → shorelines emerge.

**Layer 3 — Landmarks** (self-contained generators)

Each landmark type has a dedicated generator that fills the tile grid with
domain-specific content: streets and walls (city), farms and houses (village),
scattered ruins, etc. Landmarks sit on top of the heightmap and features;
they do not re-derive terrain — they read it.

**Output of each cell** (fed into renderers):
1. **Heightmap** — `Float32Array`, continuous elevation for terrain mesh.
2. **Tile grid** — `Uint8Array`, biome-specific ground tiles per cell.
3. **Structures** — 2D shapes (houses, walls, trees) with 3D render height.
4. **Water level** — per-biome threshold from `BiomeConfig`.
5. **NPC sprites** — entity list with position, sprite, AI state.

### BiomeConfig (data-driven terrain)

Each of the 10 biomes (Tundra…Tropics + Water) has a config in
`base-generator.ts`. Adding a new biome means adding one config entry —
no engine code changes.

| Property | Effect |
|----------|--------|
| `treeDensity` | Forest scatter density (0–0.3) |
| `treeStep` | Grid spacing for tree scatter (2–16) |
| `treeSize` | [min, max] billboard width for trees |
| `heightScale` | Terrain amplitude multiplier (0.3–1.0) |
| `waterLevel` | Height of the universal water plane (0.05–0.7) |
| `swampPools` | Enables terrain dips for swamp pools |
| `duneNoise` | Enables rolling dune hills for desert |

### Generator Self-Containment

Each generator (`city-generator.ts`, `village.ts`, `forest.ts`, etc.) is a
**fully self-contained package**. All generation logic — street growth, wall
building, tree gradients, house placement — lives as private methods inside
the generator class. Generators do not import from each other. Duplication
between generators is intentional: each is an independent module.

`base-generator.ts` provides only the minimal foundation shared by all:
grid allocation, neighbor-aware heightmap generation (with coastal sculpting,
mountain amplification, biome-specific terrain), `toMapData()` serialisation,
and low-level grid primitives.

### 3D Rendering Pipeline

The 2D tile grid is the source of truth. The 3D renderer reads the same data:

- **Sky**: fullscreen gradient quad — procedural sun, moons, stars, FBM clouds (`sky.ts`).
- **Terrain**: heightmap (`Float32Array`) + tile grid (`Uint8Array`) →
  mesh with per-tile texture from atlas (9 biome grounds + water).
  Lit by sun (derivative normals, 4-band quantised NdotL) + point lights.
- **Water plane**: flat semi-transparent quad at `waterLevel × HEIGHT_SCALE`,
  alpha-blended, depth-write disabled. Sun-direction specular, wave animation.
  Water level comes from `BiomeConfig` via `seamless-manager.compositeWaterLevel()`.
- **Structures**: `Structure[]` (2D shapes + height) → instanced boxes/cylinders.
  Derivative-based face normals, same lighting as terrain.
- **Billboard shadows**: projected sprite silhouettes on ground — stretched
  along sun direction, length inversely proportional to sun elevation.
  Drawn before normal billboards; translucent, depth-write disabled.
- **Billboards**: tree/prop structures → camera-facing alpha-tested quads.
  Ambient + sun intensity modulation (no per-pixel normals).
- **NPCs**: engine entities → per-frame billboard sprites (same shader as trees).

Render order: Sky → Terrain → Water → Structures → Billboard Shadows → Billboards.

Both 2D and 3D views share the same engine tick, entities, and game state.
Switching view only changes which renderer draws the frame.

### Lighting System

Pure graphics — does not affect game state, AI, or any non-rendering system.
Computed per-frame from `WorldTime` in `lighting.ts`, consumed by
`renderer-3d.ts` exclusively.

**Directional light (sun/moon):**
- Sun direction matches `sky.ts`: `sunAng = (tod − 0.25) × 2π`.
- Intensity ramps with `smoothstep(−0.05, 0.3, elevation)` — zero at night.
- Sun color warms near horizon (dawn/dusk orange), neutral white overhead.
- Ambient color: cool blue moonlight at night → neutral during day.
- All diffuse lighting quantised to 4 bands for pixel-retro aesthetic.

**Sprite shadows:**
- Each billboard is rendered in two passes:
  1. Shadow pass: project sprite onto ground along sun direction. Uses the
     same sprite texture → tree-shaped shadow for trees, character-shaped
     for NPCs. Dark translucent (`alpha × 0.35 × sunIntensity`).
  2. Normal pass: standard camera-facing billboard.
- Shadow length = `spriteHeight / max(sunElevation, 0.15)` — long at
  dawn/dusk, short at noon, zero at night.

**Point lights (modular — torches, campfires, etc.):**
- Up to `MAX_POINT_LIGHTS` (8) set via `renderer.setPointLights()`.
- Applied in terrain and structure fragment shaders. Quadratic falloff,
  radius-limited, quantised attenuation (4-band retro).
- `PointLight` type: position (`x, y, z`), color (`r, g, b`), `radius`.
- No gameplay dependency — any system can provide light positions.

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
| `game/quests/quest-types.ts` | Quest, QuestObjective, QuestReward type definitions |
| `game/quests/quest-engine.ts` | QuestEngine: objective evaluation, reward application, quest lifecycle |
| `game/quests/index.ts` | Re-exports for quest subsystem |

### Quest System

Modular quest framework in `game/quests/`. Data-driven quest evaluation —
adding a new objective type = one checker entry, adding a new reward type =
one applier entry. No hardcoded if-chains.

**Three quest categories:**
1. **Main** — linear storyline with rare bifurcations (designed `.ts` files)
2. **Procedural** — context-aware quests generated from game state (economy, geography, difficulty)
3. **Side** — human-designed quests placed in the world procedurally

**Architecture:** Quest engine (L3) owns lifecycle + objective evaluation.
Quest generators (L4) produce quest data from game context. Quests are plain
data structs — serialized in `PlayerState.activeQuests[]`.

**Six universal objective types:**

| Type | Verb | Checked Against |
|------|------|----------------|
| `visit_cell` | Go to macroworld coordinates | Player position (torus distance) |
| `find_location` | Enter subworld area | PlayerMove to target cell |
| `deliver_items` | Bring items to settlement | Inventory + position |
| `destroy_npc` | Kill N hostiles | NpcDeath events |
| `wait_at` | Stay in zone for N hours | Position + TimeAdvance events |
| `interact_cell` | Trigger cell change | LandmarkChangeOwner / WorldCellChange |

**Five reward types:** `gold`, `xp`, `item`, `reputation`, `event` (any GameEvent).

**Reward appliers** are a data-driven registry — each type has one entry
in `REWARD_APPLIERS`. Adding a new reward = one function, no engine changes.

**Quest tick pipeline:**
```
QuestEngine.tick() → for each active quest:
  1. Check expiry → emit QuestFail if expired
  2. Check objectives via OBJECTIVE_CHECKERS[type]
  3. If all complete → apply REWARD_APPLIERS → emit QuestComplete
```

**Procedural generation** uses settlement context:
- Economy (scarce resources → delivery quests)
- Distance (far targets → higher rewards)
- Settlement mood (poor villages → protect quests)
- Cities get 2–4 quests, villages get 1–2, at least 1 guaranteed

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
| `game/quests/quest-generators.ts` | Procedural quest factories (delivery, visit, destroy, protect, fetch, scout, sanctuary) |

### Adding new plot content

1. Create `game/plot/my-quest.ts` exporting `myQuestNodes: LogicNode[]`.
2. Import in `game/plot/index.ts`, spread into `PLOT_NODES` / `PLOT_ACTIVE_NODES`.
3. Done. No engine files touched.

### Adding new quest types

**New objective verb:** Add one entry to `OBJECTIVE_CHECKERS` in `quest-engine.ts`
and one discriminant to `QuestObjective` union in `quest-types.ts`.

**New reward type:** Add one entry to `REWARD_APPLIERS` in `quest-engine.ts`
and one discriminant to `QuestReward` union in `quest-types.ts`.

**New procedural quest:** Add one generator function to `QUEST_GENERATORS`
array in `quest-generators.ts`. The function receives full game context
(settlement economy, distances, world time) and returns `Quest | undefined`.

---

## UI Layer (screens/)

Svelte 5 components with runes. Thin orchestration — delegates all logic to
the four layers above.

| File | Role |
|------|------|
| `screens/GameScreen.svelte` | Main game loop, renders map, delegates to overlays |
| `screens/SubworldScreen.svelte` | Subworld (city/battle) view |
| `screens/TitleScreen.svelte` | Title menu with New / Load / Sandbox |
| `screens/TitleBackground.svelte` | Animated background for title screen |
| `screens/LoadScreen.svelte` | Save-slot browser and load logic |
| `screens/SandboxSetup.svelte` | Sandbox parameter configuration |
| `screens/StoryOverlay.svelte` | Universal narrative overlay (slides + choices) |
| `screens/EventOverlay.svelte` | Dialog popup for logic-node events |
| `screens/StatOverlay.svelte` | Character stats, skills, perks, inventory |
| `screens/MapOverlay.svelte` | Full-screen minimap |
| `screens/CodexOverlay.svelte` | In-game encyclopedia / lore |
| `screens/DiplomacyOverlay.svelte` | Faction relations and diplomacy |
| `screens/SettlementOverlay.svelte` | Settlement info, trade, quests tabs |
| `screens/TradeOverlay.svelte` | Buy/sell trade interface |
| `screens/QuestOverlay.svelte` | Active quest journal |
| `screens/SpellOverlay.svelte` | Spell book and casting UI |
| `screens/InteractionOverlay.svelte` | NPC interaction dialog |
| `screens/NpcProximityPanel.svelte` | Nearby NPC awareness panel |
| `screens/DebugOverlay.svelte` | Debug tools, cheats, entity inspector |
| `screens/DeathOverlay.svelte` | Death screen with retry |
| `screens/PauseOverlay.svelte` | Pause menu |
| `ui/theme.ts` | Shared UI theme: button styles, colors, layout utilities |

GameScreen is the largest file (~2,500 lines). It is a **controller** — it owns
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

### Quest System
Quests are plain data structs tracked in `PlayerState`. The `QuestEngine`
ticks alongside the logic node engine, checking objectives against events.
Procedural quests are generated on-demand from settlement context (economy,
geography, mood). Each quest is a self-contained package: objectives,
rewards, and optional `onAccept` events (e.g. spawn bandits for protect quests).

---

## Rules

1. **No upward imports.** L1 never imports from L2/L3/L4. L3 never imports from screens/.
2. **Plot is pure data.** `plot/*.ts` files export `LogicNode[]` — no subscriptions, no side effects at import time.
3. **Encounter content lives in plot/.** `node-registry.ts` imports it; the encounter *node* (L3) is separate from encounter *data* (L4).
4. **Effect application is centralised.** All `GameEvent[]` → player-state mutations go through `effect-applicator.ts`.
5. **One file = one responsibility.** Don't split unless there's a genuine architectural seam.
6. **Max ~1000 lines** per file, relaxed for naturally encapsulated modules (renderers, generators).
7. **Subworld = detailed macroworld.** Every subworld property (terrain, biome, features) is derived from macroworld data — never invented locally.
8. **Neighbor-aware generation.** All subworld generators receive the full 3×3 NeighborGrid. Terrain, features, and landmarks must respect adjacent cells (roads connect, forests blend, coasts emerge, mountains spill).
9. **Data-driven extensibility.** Adding a new biome = one `BiomeConfig` entry + one ground tile texture. Adding a new feature = one `FeatureType` + one handler in the pipeline. No hardcoded if-chains.

Modular, elegant, generalised, optimised — minimal systems, maximal functionality and universality.

 ALL SYSTEMS ARE DATA DRIVEN. DATA ORIENTED PROGRAMMING.
