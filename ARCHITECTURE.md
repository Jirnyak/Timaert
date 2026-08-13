# Architecture — Timaert (C++ / Vulkan / EnTT port)

Native rewrite of the Timaert TS/Vite/WebGL2 prototype in **C++23 + SDL2
(platform/input/audio) + Vulkan + EnTT + ImGui**. `timaert_c/` is the **final
game** — the product that ships, not a throwaway port. This document is the
**single source of truth** for it and is a faithful translation of
`../ARCHITECTURE.md` (the TS reference build) into native idioms — same
four-layer model, same patterns, same rules. The only differences are language
(TS → C++23) and runtime (WebGL2 → Vulkan, Svelte → ImGui, Web Worker →
`std::thread`, `Uint8Array`/`Float32Array` →
`std::vector<std::uint8_t>`/`std::vector<float>`). Rendering runs on **Vulkan**
(MoltenVK on macOS): the OpenGL→Vulkan raster migration is **complete in `src/`**
(0 GL call sites, no `src/gl/`, the backend lives in `src/gpu/`). The GPU
*compute* half of the backend (mass NPC simulation) is **not yet built** — see
*Rendering & Compute Backend* and *GPU-Driven Simulation* below for what is
shipped vs planned.

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
│  Tile maps, local NPC AI, unified combat.         │
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

## Performance & Algorithmic Bounds

> **Strict O(N) simulation bound.** During any active simulation tick (subworld ECS or macroworld), **nothing greater than O(N) is permitted**.
> Never write O(N²) scans for proximity, line-of-sight, or AI targeting.
> **This is exactly why we bake paths and use bucket grids.** For radius
> queries use the battle grids (`sub/battle.h` `UnitGrid` — the AI's budgeted
> `contact_scan`, the spell broad phase `SpellNeighborsFn`) or the collision
> bins (`sub/collide.h`), and precomputed grids for navigation. The last
> violator — four full-registry scans per projectile per tick in
> `spell_effects.cpp` — fell 2026-08-13; `spell_broadphase_parity_test` holds
> the door.

## Discreteness & Number Style

This is a **cellular game**: the world is cells, and the house style is
*discrete, integer, narrow* — everywhere it doesn't cost functionality.

- **Data is where the performance lives.** Arrays, grids and serialized
  state use the narrowest integer type that fits: tiles `u8`, `trav` `u8`,
  tree counts `u16` (golden max 16384 = 2^14), collision-bin CSR `i32`,
  packed entry-dir in 2 bytes. This is cache lines, memory bandwidth and
  SIMD lanes — the wins here are real and measured, and they compound with
  the O(N) bound above.
- **Scalar float constants: the VALUE doesn't buy speed** — an FPU multiply
  costs the same for 9.81 and 8, and we don't divide by tunables. Prefer
  **powers of two / round numbers anyway**, for three non-perf reasons:
  a po2 multiply is rounding-EXACT (pure exponent shift → bit-stable
  determinism, which seeded generation and TS-parity depend on), the mental
  math collapses (tuning and review without a calculator), and honest
  simulation only needs the right ORDER of magnitude — this world owes
  Earth nothing past that. Precedent: the subworld physics family
  (`sub/height.h`) — g = 8 m/s², jump = 4 m/s (apex exactly 1 m), safe
  landing = 8 m/s (exactly a 4 m free drop), terminal = 64 m/s; fall
  damage = `4 · radius · (dropMetres − 4)`, computable in your head.
- **Never sacrifice function for discreteness.** The principle applies
  "where it practically doesn't hurt": positions/velocities stay float
  (the sim is continuous in space), and a constant that genuinely needs a
  fraction keeps it. Discrete is the default, not a straitjacket.

## Source Layout

```
src/
  app/          SDL2 (Vulkan window) + ImGui boot, main loop, input dispatch.
  core/         Math (mat4/vec3 PODs), seeded RNG, torus helpers.
  gpu/          Vulkan backend (device/swapchain/pipelines/buffers/textures/shadow).
  ecs/          EnTT World, components, systems.
  macro/        L1 macroworld core (sim, terrain, politik, items, army).
  sub/          L2 subworld (seamless 9-cell, generators, first-person 3D renderer).
  events/       L3 event bus, logic nodes, effect applicator, quests.
  content/      L4 pure data (spells, plot, encounters, quest generators).
  assets/       Sprite atlas and paper-doll asset loaders / GPU cache.
  ui/           ImGui overlays (Diplomacy, Settlement, Quest, Codex, Map…).
```

There is **no `src/gl/`** — the OpenGL backend was fully removed; all GPU code
lives in `src/gpu/`. Build glob (verified in `CMakeLists.txt:138-140`, and it is
already `gl`-free): `src/{app,core,gpu,ecs,macro,sub,events,content,ui,assets}`
`/**/*.cpp` is picked up by `GLOB_RECURSE`. New files compile automatically.

---

## Rendering & Compute Backend (Vulkan)

> **Decision (2026-07-02); raster migration COMPLETE (2026-07).** `timaert_c/`
> is the **final game**. The rendering and compute backend is **Vulkan**
> (native), with **MoltenVK** on macOS. The legacy **OpenGL 3.2 Core / WebGL2 /
> Emscripten-WASM** paths are **removed** — `src/` has 0 GL call sites, no
> `src/gl/`, and the browser target is dropped. The **raster** backend is fully
> ported and shipping from `src/gpu/`. The GPU **compute** half (mass NPC sim) is
> **still unbuilt** — see *GPU-Driven Simulation* (**STATUS: NOT YET
> IMPLEMENTED**). Sections below that describe GL/GLSL uniform semantics are
> retained as the *algorithmic reference* the SPIR-V pipelines were ported from,
> not a description of a live GL path.

**Why the move was required, not cosmetic.** OpenGL is not merely "slower"; the
old target (**OpenGL 3.2 Core**) has **no compute shaders**, and Apple caps macOS
OpenGL at **4.1**, so GL compute is impossible on macOS *at all*. The game's core
goal — simulating **thousands of macro NPCs/squads** and **thousands of
microworld combatants** — is a compute-shader problem. GL 3.2 could not express
it; Vulkan can, with explicit control over CPU↔GPU work and far lower per-draw
driver overhead. (The raster migration that unblocks this is done; the compute
work itself is still pending — *GPU-Driven Simulation*.)

**SDL2 is demoted to a platform layer, not the graphics API.** SDL owns:
window creation (`SDL_Vulkan_CreateSurface`), input events, timing, and audio
(SDL_mixer). It **does not** touch rendering. All draw + compute goes through
Vulkan directly. This keeps CPU time budgeted for simulation and avoids GL
driver overhead and hidden allocations/stalls.

**Backend isolation.** All Vulkan lives in `src/gpu/` (replacing `src/gl/`).
Game logic (`core/`, `ecs/`, `macro/`, `sub/`, `events/`, `content/`) stays
backend-agnostic and never includes Vulkan headers. `ui/` uses
`imgui_impl_vulkan` + `imgui_impl_sdl2`.

**Migration is a dedicated pass** (not folded into feature work): stand up the
Vulkan device/swapchain/frame-graph in `gpu/`, port the macro fragment synth
and subworld terrain/billboard passes to SPIR-V pipelines, then land the compute
simulation kernels. Saves are unaffected (rendering is never serialised).

## GPU-Driven Simulation

> **⛔ STATUS: NOT YET IMPLEMENTED (planned — `vulkan.md` P7).** This entire
> section describes the *target design*, not shipped behaviour. As of 2026-07-29
> the code contains **zero compute shaders, zero `vkCmdDispatch`, zero compute
> pipelines, and zero SSBOs** (`rg` over `src/` — the only hit is a comment). The
> "mass of NPCs" today runs on the **CPU** via a time-sliced budgeted tick
> (`macro/npc_ai.cpp` `tick_macro_npc_ai_budgeted`) that raises a `backlog` flag
> and skips updates when overloaded, and the subworld renderer is hard-capped at
> **512** visible NPCs + 512 creatures. Read every present-tense verb below as
> **"is intended to"**, not "does". This is the project's biggest doc-vs-code gap
> and the headline of `audit.md` §6.0 / §2.6. Do not cite it on a store page as an
> existing feature.

The organising principle (as designed): **NPCs are always real, always
data-oriented, always simulated — never faked, frozen, or LOD-cheated.** They are
*intended to* live where their fidelity is indistinguishable to the player: the
**GPU** for the mass, the **CPU** only for the few the player can actually touch.
No behaviour is to be skipped; only the *execution unit* changes. *(Caveat: the
current CPU fallback described in the status banner above **does** LOD-skip under
load — the one place today's build violates this principle.)*

### Residency & Embodiment (воплощение)

Two residency tiers, one entity identity:

| Tier | Who | Where it runs | Fidelity |
|------|-----|---------------|----------|
| **GPU-resident** | The mass (distant macro squads; micro combatants outside the player's engagement set) | Compute shaders over SSBOs | Full simulation, packed representation |
| **CPU-embodied** | The few the player can meaningfully interact with | EnTT/ECS on CPU | Full-fidelity gameplay logic, events, loot, dialogue |

**Embodiment** is the promotion of a GPU-resident NPC to a CPU ECS entity the
instant the player can act on it (aims at it / talks / attacks in the
microworld; enters its cell chunk in the macroworld). **De-embodiment** returns
it to the GPU pool when interaction ends. The identity (id, packed stats) is
preserved across the transition — the same NPC, embodied or not.

- **Macroworld:** the CPU-embodied set = the chunk of cells around the player.
  Everything beyond the chunk is GPU-resident mass simulation.
- **Microworld:** the CPU-embodied set = NPCs inside the player's engagement
  radius / under the reticle. The rest of the crowd is GPU billboards driven by
  compute, promoted the moment they enter the engagement set.

### No-stall transfer rule

GPU↔CPU transfer is the enemy. Every embodiment/de-embodiment either happens at
a **load/transition boundary** or is **amortised** across frames via
double-buffered, fenced staging — **never** a synchronous per-frame readback
stall. The target is zero micro-freezes: no blocking `vkQueueWaitIdle` in the
frame loop, no per-frame full-buffer readback. If data must come back this
frame, only the embodied few come back, never the mass.

### The four GPU-crowd techniques

Adopted as design rules for every compute simulation kernel:

1. **Data packing (SoA + bit-packing).** NPC state is packed into a few 32-bit
   words in GPU SSBOs, Structure-of-Arrays. E.g. one `uint32`:
   `level(8) | kindOrWeaponId(8) | hp(16)`; positions/velocities in parallel
   `float` buffers. The shader reads one word and bit-shifts (nanoseconds) to
   recover identity. **No AoS structs, no pointers on the GPU.**
2. **Lookup buffers (data-driven on the GPU).** Weapon / armour / faction / NPC
   kind stats live as **flat GPU arrays indexed by id**. One universal kernel
   reads stats by index; there is no per-kind shader. Adding a weapon/kind =
   one row in a buffer, exactly like the CPU registries — the same
   data-oriented rule, moved to VRAM.
3. **Branchless math (no warp divergence).** Replace `if (melee) … else …` with
   a single formula evaluated for all: e.g.
   `dmg = meleeDmg * proximityCoef + rangedDmg * visibilityCoef`, where a melee
   weapon's ranged coefficient is simply `0` in the lookup buffer. The GPU
   multiplies by zero and gets the right answer for both, divergence-free.
4. **Cohort sorting.** When behaviour genuinely can't collapse to one formula,
   the CPU (or a GPU radix sort) **sorts the crowd by behaviour class** before
   dispatch, then runs one homogeneous compute dispatch per cohort (all melee
   `[0..N)`, all missile `[N..M)`). Each warp sees identical control flow.

### What stays on the CPU

Only what the player is actually resolving: the embodied entities, their events
(loot, XP, dialogue, faction reputation), quest evaluation, save/load, and world
generation. These are latency-bound, branchy, and low-count — a poor GPU fit and
a natural CPU fit. The dividing line is **interactivity**, not entity type: an
NPC is CPU-embodied *because the player can touch it*, not because it is special.

This model is **not a cheat**: an off-screen macro squad and an embodied one run
the same rules; the only difference is the execution unit and the representation
width. The player never observes a discontinuity because embodiment happens
before any interaction is possible.

---

## L1 — Macroworld Core

Pure simulation. No GL state, no events, no UI. Each TS module maps 1:1
to a C++ TU pair (header + optional `.cpp`).

| TS module                  | C++ target                                                            | Responsibility |
|----------------------------|------------------------------------------------------------------------|----------------|
| `game/state.ts`            | [macro/state.{h,cpp}](src/macro/state.h)                              | `GameState`, `PlayerState`, `WorldTime`, `Settlement`, `Village`, `Spire`, save version |
| `game/economy.ts`          | [macro/economy.{h,cpp}](src/macro/economy.h)                          | Per-settlement inventory, prices, daily trade tick |
| `game/attributes.ts`       | [macro/attributes.h](src/macro/attributes.h)                          | Stat block, level data, XP curves |
| `game/items.ts`            | [macro/items.{h,cpp}](src/macro/items.h)                              | `Item`, `Inventory` (count/add/remove), unified loot registry (`roll_loot_profile` keyed by `lootId`) |
| `game/army.ts`             | [macro/army.h](src/macro/army.h)                                      | `CombatTemplate`, `SoldierRecord`, and `SoldierSquad` universal NPC-as-soldier records. Current source has no legacy 4-unit/RPS schema (`UnitType`, `kUnitStats`, `damage_multiplier`, `kHireCost`, `kUpkeepCost`, `hire_unit` are absent). |
| `game/npc.ts`              | [macro/npc.h](src/macro/npc.h), [macro/npc_spawn.cpp](src/macro/npc_spawn.cpp) | `NPCType` enum + `kNpcTypes[]` registry; macro NPC spawning treats invalid or mismatched terrain as absent terrain, fails closed on invalid map dimensions, and keeps spawn fallback positions inside map bounds |
| `game/politik.ts`          | [macro/politik.{h,cpp}](src/macro/politik.h)                          | `KingdomDef` registry, capital + city placement, MST + extra roads, Voronoi `cellOwner`; malformed or mismatched terrain storage is ignored as absent terrain |
| `game/language.ts`         | [macro/language.{h,cpp}](src/macro/language.h)                        | Procedural per-kingdom phonotactic name generation |
| `game/pathfinding.ts`      | [macro/pathfinding.{h,cpp}](src/macro/pathfinding.h)                  | A* over traversability grid |
| `game/world-tick.ts`       | [macro/world_tick.{h,cpp}](src/macro/world_tick.h)                    | Time advancement, daily settlement / village / economy tick; `subworld_time` smoke proves runtime advance on seed 42 |
| `game/tree-spawner.ts`     | [macro/spawners.{h,cpp}](src/macro/spawners.h) `spawn_trees`          | FBM-density tree placement with TS-style 2-cell river exclusion |
| `game/mountain-spawner.ts` | [macro/biomes.h](src/macro/biomes.h) `biome_at`                       | Superseded — mountains are the elevation-classified `Biome::Mountain`, not a spawned feature |
| `game/road-spawner.ts`     | [macro/spawners.{h,cpp}](src/macro/spawners.h) `trace_roads`          | Native terrain-cost A* baseline with component pre-prune and large-map step cap; TS corridor-snap divergence is documented and invariant-tested |
| `game/road-network.ts`     | [macro/spawners.{h,cpp}](src/macro/spawners.h)                        | Audited reference; native keeps A* topology unless same-seed A/B proof justifies rewrite |
| `game/dirt-road-spawner.ts`| [macro/spawners.{h,cpp}](src/macro/spawners.h) `trace_dirt_roads`     | Village → main-road dirt path; invalid dimensions, short road masks, mismatched village arrays, and short supplied land-mask byte counts fail closed |
| `game/features.ts`         | [macro/features.h](src/macro/features.h)                              | `FeatureType` enum, `FeatureLayer` byte grid, native land/water guard, builder, and `FeatureLayer::decode()` fail-closed handling for malformed feature bytes |
| `game/zones.ts`            | [macro/zones.{h,cpp}](src/macro/zones.h)                              | Difficulty heightmap (BFS civ + mountain interior + fBM) |
| `game/biomes.ts`           | [macro/biomes.h](src/macro/biomes.h)                                  | Biome enum, 3×3 climate matrix |
| `game/biome-textures.ts` + `tundra.ts`…`water-biome.ts` | [macro/vk_macro_renderer.cpp](src/macro/vk_macro_renderer.cpp) + [shaders/macro.frag](shaders/macro.frag) | Procedural macroworld ground rendering: 11 per-biome `bt_<biome>(wp,sd)` (incl. `bt_mountain`), neighbour-aware shore, climate overlay |
| `game/flag-generator.ts`   | [macro/flag_generator.{h,cpp}](src/macro/flag_generator.h)            | Procedural 128×128 RGBA8 heraldic flag bitmaps |
| `game/movement-cost.ts`    | [macro/movement_cost.h](src/macro/movement_cost.h)                    | Data-driven SP costs per biome / feature |
| `game/npc-ai.ts`           | [macro/npc_ai.{h,cpp}](src/macro/npc_ai.h)                            | NPC AI tick: reusable behaviour functions shared by NPC types |
| `game/rng.ts`              | [core/rng.h](src/core/rng.h)                                          | Seeded xorshift32 RNG |
| `game/torus.ts`            | [core/torus.h](src/core/torus.h)                                      | Toroidal map geometry helpers (wraparound, distance, step) |
| `game/audio.ts`            | [macro/audio.{h,cpp}](src/macro/audio.h)                              | SDL_mixer audio subsystem for native builds: CMake requires SDL2_mixer outside Emscripten and links the discovered mixer target. The C++ no-mixer backend exists only for configurations that do not define `TIMAERT_HAS_SDL_MIXER`; native CMake does not silently enter it. Stable MP3 music registry, one-shot SFX registry, volume/mute controls, fade play/stop, RAII no-copy handle ownership, and app-level state music hooks with same-desired-track failure latch. `audio_contract_test` locks stable IDs, asset filenames, and the control contract; `audio_runtime_test` verifies dummy-driver init/decode/playback with the native mixer backend |
| `game/renderer.ts`         | [macro/vk_macro_renderer.{h,cpp}](src/macro/vk_macro_renderer.h)      | Single fragment shader: biome + rivers + feature painter overlay + zones + cell-grid + time tint |
| `game/markers.ts`          | [macro/markers.h](src/macro/markers.h)                                | Universal POI/quest/danger/waypoint marker list |
| `character/`               | [assets/character_paperdoll.{h,cpp}](src/assets/character_paperdoll.h), [assets/character_paperdoll_gl.{h,cpp}](src/assets/character_paperdoll_gl.h) | Sprite atlas manifest, animation, palette, deterministic character generation, and GL texture cache |
| `webgl/map-generator.ts`   | [macro/map_generator.{h,cpp}](src/macro/map_generator.h)              | GPU master texture pipeline (heights, moisture, temperature, mask) |
| `webgl/shaders.ts`         | [shaders/](shaders/) `.vert`/`.frag`/`.glsl` compiled to SPIR-V by `glslc` | GLSL shader sources (compiled offline, not inline strings) |
| `webgl/webgl-context.ts`   | [gpu/](src/gpu/) `vk_device` / `vk_swapchain` / `vk_pipeline`        | Vulkan device/swapchain/pipeline setup (replaced the removed `src/gl/` GL wrappers) |

### Spell System

Modular spell framework in [content/spells/](src/content/spells). Core
infrastructure + pluggable spell modules — adding a spell means adding one
file, no engine changes.

| TS module                    | C++ target                                                                    | Role |
|------------------------------|--------------------------------------------------------------------------------|------|
| `game/spells/spell-types.ts` | [content/spells/spell_types.h](src/content/spells/spell_types.h)              | Spell type definitions, registry, tags, status metadata |
| `game/spells/spell-casting.ts`| [content/spells/spell_book.{h,cpp}](src/content/spells/spell_book.h)         | Cast logic, cooldowns, mana cost |
| `game/spells/spell-renderer.ts`| [sub/vk_renderer_3d.{h,cpp}](src/sub/vk_renderer_3d.h) spell visual pass     | Native 3D billboards/ribbons for active spell effects |
| `game/spells/index.ts`       | [content/spells/registry.cpp](src/content/spells/registry.cpp)                | Re-exports + spell registration |
| `game/spells/fireball.ts`    | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/spell_effects.{h,cpp}](src/sub/spell_effects.h) | AoE damage projectile |
| `game/spells/ice-shard.ts`   | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/spell_effects.{h,cpp}](src/sub/spell_effects.h) | Targeted frost projectile |
| `game/spells/lightning-chain.ts` | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/spell_effects.{h,cpp}](src/sub/spell_effects.h) | Bouncing arc damage |
| `game/spells/energy-beam.ts` | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/spell_effects.{h,cpp}](src/sub/spell_effects.h) | Sustained directional beam |
| `game/spells/magic-bolt.ts`  | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/spell_effects.{h,cpp}](src/sub/spell_effects.h) | Basic ranged attack |
| `game/spells/armageddon.ts`  | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/spell_effects.{h,cpp}](src/sub/spell_effects.h) | Bounded meteor swarm with expiry AoE blasts |
| `game/spells/flight.ts`      | [content/spells/registry.cpp](src/content/spells/registry.cpp) + [sub/engine.{h,cpp}](src/sub/engine.h) | Sustained macro path bypass plus pitch-based subworld flight height |
| `game/spells/haste.ts`       | [content/spells/registry.cpp](src/content/spells/registry.cpp)                  | Sustained speed buff |

```
SpellBook { learned, activeSpellId, cooldowns, sustainedActive, sustainedDrainCarry }
spellbook_learn / set_active / can_cast / cast / tick
```

### Politics System ([macro/politik.{h,cpp}](src/macro/politik.h))

Kingdom-driven world generator. Politics is the **source of truth** for
where capitals sit, which cities belong to whom, and how roads connect
them. Pure data: no rendering, no events, no UI.

**Pipeline:**

1. **`generate_politik(seed, mapW, mapH, terrain, seaLevel8, target, site)`**
   — iterates `kingdom_defs()` (data registry of `{id, lineage, region,
   minCities, maxCities, capital_requires_lake, color_rgb, priority}`),
   places one capital per kingdom, scatters child cities around it, then
   per kingdom builds a **Prim's MST** seeded at the capital + one extra
   nearest non-connected edge per city for redundancy (cap 4 connections),
   and finally one inter-kingdom **bridge road** between every kingdom
   pair whose closest city pair is within `0.35 ×` the half-diagonal.

   **Politics says how many and whose; the ground says where** (R2).
   Every candidate cell is priced by `settlement_site_score` through the
   `site` context, and the best valid draw is settled — a capital takes
   the best cell within its own spacing disk, child cities the best of
   their anchor-jitter draws. Populations derive from that same score
   (`capital_population` / `city_population`), never from dice. Passing
   `site = nullptr` prices every cell at zero, which degrades to the old
   first-valid placement — that arm is the negative control in
   `settlement_placement_test`.

   **One distance law.** `derive_city_spacing(terrain, seaLevel8, w, h, n)`
   is the single door: nearest-neighbour distance of *n* points over the
   land area, times 0.6. City rejection uses it, anchor jitter is `2 ×`
   it (so a child lands in the annulus `[spacing, 2 × spacing]` of its
   parent), and village hinterlands are **half** of it. No other
   placement distance exists.
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
mode" and no second combat schema. Player, NPCs, garrison units, bandits,
and macroworld army squads share **one stat block** (`CombatTemplate` in
[macro/army.h](src/macro/army.h)) and **one engine**
([sub/ai.cpp](src/sub/ai.cpp) + [sub/engine.cpp](src/sub/engine.cpp)).
Macroworld interactions hand off to the subworld when a fight starts —
the same NPC entities that already exist in the cell are the
participants. The danger zone level (see *Difficulty Zones* below)
controls whether the player can leave the subworld at all (yellow/red →
no exit), so resolution is just normal subworld play, not a modal screen.

**All combat is 3D.** Melee range, projectile trajectories, spell blasts,
NPC missile aim, and hit detection all operate in full XYZ space. Projectiles
carry `(vx, vy, vz)` and the player's spell direction is
`(cos(yaw)*cos(pitch), sin(yaw)*cos(pitch), sin(pitch))` — the camera
look vector. NPCs aim missiles at the 3D position of their target. Distance
checks for targeting, detection, and chase use `dist3sq()` (3D Euclidean).
Ground-walking combatants are terrain-pinned each tick; flying combatants and
projectiles own their Z. A projectile leaves the caster's **eye**, not his feet
(`player_muzzle_z()` = feet + `kBodyEyeM`) — the same point the look vector is
taken from, so the crosshair and the bolt share one line. Its collision is
**swept**: the segment crossed this tick, earliest hit first, never a single
sampled point (a bolt strides several units per tick and would otherwise step
straight over anything closer than its stride). It dies on terrain, on masonry,
or on any face of the 3×3 window — walls, floor, and ceiling alike.

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
Every NPC kind (`kNpcTypes[i].combat`) carries this block. There is no
separate "unit" type - the historical `kUnitStats[]` (Swordsman / Archer /
Spearman / Horseman), rock-paper-scissors `damage_multiplier()`, and separate
hire/upkeep tables are gone from the current source. In the universal model:

- An "army" is a list of NPC ids the player has hired (or a settlement
  has garrisoned). When the squad enters a subworld it spawns those
  exact NPCs as soldiers using their normal AI and `CombatTemplate`.
- Any NPC kind can be a soldier. Designers tag a kind as hireable in the
  registry, set its base stats (one row), done.
- Daily upkeep is **a single number per NPC kind** (`upkeepGoldPerDay`
  in the kind row). Designers can set 0 (free), 1 (cheapest peasant
  levy), or 1000 (elite). The balance baseline is `1 gold/day` for the
  weakest hireable NPC.
- `soldier_upkeep(SoldierRecord)` applies the kind upkeep and level factor;
  `calculate_squad_upkeep(SoldierSquad, charisma)` folds the squad and charisma
  discount. No RPS table, no per-pair matchups, no separate hire-cost table.

**Combat is derived from the character sheet (`CombatTemplate` = the base).**
For **humanoids and the player**, `kNpcTypes[i].combat` is not the final stat
block — it is the authored *base*. Each humanoid NPC (and the player) carries a
`CharacterSheet` (Attributes + Skills + Perks + LevelData; see
[rpg.md](rpg.md)), and its ECS `Health`/`Combat` are projected from that sheet
by `project_combat(sheet, base)` in
[macro/character_sheet.h](src/macro/character_sheet.h). The projection reuses
the **exact** player formulas (`calculate_combat_stats` / `calculate_derived`),
so player and NPC sit on ONE combat curve: the template supplies the HP/damage
floor plus the attack identity (speed / range / cooldown / kind / missile
params), while attributes/skills/level scale hp/damage on top. Level is captured
implicitly by the sheet's spent points — spawn code applies **no** extra
per-level multiplier. **Monsters** (`NPCKind.type & 0x100`) are sheet-less and
never projected: their `Combat`/`Health` stay the raw `FaunaEntry` row.

**Death, loot, and XP — Might & Magic style:**

- Whoever lands the killing blow gets the XP. NPCs killed by other NPCs
  (e.g. a player-hired soldier kills a bandit) award XP to the killer's
  owner squad. The player gets XP only for kills they (or their hired
  party) made.
- Every NPC drops a **corpse object** containing whatever loot the
  designer set on its kind. If the kind resolves to no loot, the corpse is
  empty and not lootable (no drop, just despawn). When loot exists, the
  corpse is interactable until despawned (use → transfer to player
  inventory). This mirrors Might & Magic 6/7/8 corpse interaction.
- **One loot table, one path.** Every drop — humanoid NPC *or* monster —
  resolves through a single `roll_loot_profile(lootId, level, rng)` registry
  in `macro/items.{h,cpp}`. The `lootId` is a stable string: `npc_loot_id()`
  maps the 8 `NPCType` roles to their profiles; a monster uses its
  `FaunaEntry.lootId` override or falls back to its faction default
  (`wildlife` / `demons` / `bandits`). Unknown/empty id ⇒ no items. This
  replaced the old split (NPCType-int vs faction-string) `kNpcLoot[]` /
  `generate_fauna_loot` paths and fixed the latent Bandits-faction zero-loot
  gap. Fully data-driven — see [monsters.md](monsters.md).

**Hostility** is **faction-driven**, not entity-driven: any NPC's
hostility toward the player is derived from
`factions[npc.factionId].relation`. When the player attacks a friendly
NPC, `kHitRepPenalty` deducts 1 reputation; crossing `kHostileThreshold`
flips the entire faction hostile.

**Engine constants ([sub/engine.h](src/sub/engine.h)):**

| Constant                  | Effect                                                  |
|---------------------------|---------------------------------------------------------|
| `kHostileThreshold = -50` | Faction reputation below this → auto-aggro              |
| `kHitRepPenalty = -1`     | Player attacks on neutral cost reputation               |
| `kCrowdPenalty = 40`      | Damage falloff per extra attacker on one target         |
| `kDetectionRadius = 200`  | NPC awareness range (subworld units)                    |

**Combat AI** uses `tick_combat_move` for both melee and missile attackers.
Multiple attackers ganging one target suffer the `kCrowdPenalty` distance
spread, naturally creating combat formations.

**Recruitment & garrisons:**
- `hire_npc(playerSquad, garrison, npcKind, gold)` - atomic recruit in
  [macro/npc.h](src/macro/npc.h). It validates hireable NPC kind, charges
  `hire_price_for(SoldierRecord)`, moves the concrete soldier record from the
  settlement garrison into the player squad, and is wired to the Settlement
  Recruit tab. `combat_squad_test` covers charge, denial, stable IDs, garrison
  generation, upkeep, squad spawn projection, and death removal.
- City garrison is a list of NPC entities, not a `{Sword:n, Arc:n, ...}`
  histogram. Daily regen in `world_tick.cpp` adds NPCs by kind from the
  city's hireable pool.
- Survivors are just the NPCs still alive after a fight; no
  `count_survivors(army)` over a histogram.

### Feature Layer

Features are static, persistent visual elements placed on macroworld cells.
They sit between the terrain biome (GPU-computed) and landmarks/entities
(cities, NPCs). Features do not alter the underlying biome.

**Data-driven architecture:** all feature classification happens once during
generation. `build_feature_layer()` stamps each cell with a `FeatureType`
using the pass order (Tree -> DirtRoad -> Road). Mountains are **not** a feature:
they are the elevation-classified `Biome::Mountain` (land at height >=
`kMountainBiomeLevel`, `0.75f`), resolved in one place by `biome_at()` and mirrored
by the shader's `bt_biome`. Trees and roads compose on top of the Mountain base —
see [biomes.md](biomes.md).
Tree writes use TS flattened index semantics, and short road/dirt masks apply
their valid prefix bytes like TS typed-array reads.
`FeatureLayer::at()` and `set()` use overflow-safe torus wrapping before
touching backing storage; malformed huge extents still fail closed instead of
depending on C++ signed-overflow behaviour.
Malformed non-empty `FeatureLayer` storage fails closed: valid prefix bytes can
still be read, but out-of-backing cells return `FT_None` and ignore writes.
Movement-cost, zone generation, and feature texture upload validate complete
storage once at the call boundary; malformed feature buffers are ignored as
`FT_None` or uploaded as a 1x1 blank R8 texture instead of being read blindly.
Zone generation receives the active terrain RGBA byte count; valid water cells
get the TS `WATER_BOOST`, while short supplied water masks are ignored instead
of indexed.
Complete feature buffers with unknown byte values are sanitized through
`FeatureLayer::complete_cells_or_sanitized()` before GPU upload, so valid grids
stay zero-copy and invalid ids become `FT_None` instead of shader-visible
decorations.
Zone and landmark texture uploads validate/sanitise their byte grids and fall
back to blank R8 textures on malformed dimensions or data pointers.
The five TS `FeatureType` byte values are compile-time asserted, and
`FeatureLayer::set()` also sanitizes invalid enum casts before writing.
Subworld dispatch decodes `CellContext::feature` and the raw `nbFeature[9]`
neighborhood before mode resolution, height generation, road-axis alignment,
and settlement/ruin layout, so invalid feature bytes fail closed to `FT_None`
at the procedural boundary.
Politik placement/finalization, tree spawning, road tracing, macro NPC spawning, path-cost generation, and subworld entry also
validate terrain RGBA storage at the boundary and fail closed rather than
indexing malformed macro terrain buffers.
The resulting byte grid is uploaded to the GPU as `u_featureMap`. All GLSL
renderers read that single texture to decide what to draw — no feature
logic is re-derived at render time. Water cells are filtered out at build
time with the active map `seaLevel` so roads / trees / mountains never appear
on water, including custom-game sea-level settings. The native guard surface
is locked by `feature_layer_parity_test` and `road_river_generation_test`.

| Feature  | Module                                                              | Rendering         | Placement                           |
|----------|---------------------------------------------------------------------|-------------------|--------------------------------------|
| Road     | [macro/spawners.cpp](src/macro/spawners.cpp) `trace_roads`         | GLSL overlay      | Current C++ road generation keeps the native terrain-cost A* baseline as a documented intentional divergence from TS corridor-guided Bresenham over `tData.roadData`. `road_river_generation_test` enforces rejected-water pruning and the fixed large-map search cap. Cross-island pairs are component-pruned; same-island pairs use generation-tagged A*, block water during expansion, and prune routes not proven inside budget. No straight-line or water-stamping fallback exists in the current source. |
| DirtRoad | [macro/spawners.cpp](src/macro/spawners.cpp) `trace_dirt_roads`    | GLSL overlay      | Spiral search up to 60 tiles → torus-aware lerp trace, skips villages already on roads, never overwrites main road, `landMaskA` filters water/ice; malformed dimensions, road masks, village arrays, or supplied land-mask byte counts return an empty mask |
| Tree     | [macro/spawners.cpp](src/macro/spawners.cpp) `spawn_trees`         | Feature byte + GLSL overlay | Domain-warped multi-scale FBM density (large×0.40 + med×0.35 + fine×0.25), biome-gated, shoreline buffer + high-elevation treeline (`h > 0.80`) + 2-cell river exclusion |
| Mountain | [macro/biomes.h](src/macro/biomes.h) `biome_at` / GLSL `bt_biome`   | GLSL biome ground | **Not a feature** — the elevation-classified `Biome::Mountain` (land ≥ `kMountainBiomeLevel`); trees/roads compose on top |

**Cell structure** (bottom → top, identical to TS):
1. **Biome** — terrain type from 3×3 climate matrix (temperature × moisture),
   with two elevation overrides outside the matrix: `Biome::Water` when
   `macroHeight < seaLevel` and `Biome::Mountain` when
   `macroHeight >= kMountainBiomeLevel` (GPU-computed in the `map_generator.cpp`
   fragment shader, mirrored on the CPU by `biome_at()`)
2. **Feature** — road, tree, dirt road (`FeatureType`, data-driven byte grid;
   composes *on top* of the biome, so a forested mountain is `Biome::Mountain`
   + `FT_Tree`)
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
generate_terrain (height/moisture/temp + riverData/riverTexture post-pass)
  → generate_politik → snap_cities_to_land → finalize_politik (lake-snap + multi-source BFS Voronoi over land)
  → populate_landmarks_from_politik
  → spawn_trees
  → trace_roads (native A* baseline; component-pruned, capped, water-blocking TS corridor-snap divergence)
  → trace_dirt_roads
  → build_feature_layer
  → generate_zones
  → generate_spires
```
Roads are the **last** connectivity step before feature compositing.
Corrected 2026-05-15: TS road generation was compared against
`C:\Timaert\src\game\road-network.ts`. Native keeps terrain-cost A* as the
production baseline; same-seed A/B proof is required before replacing it with
TS corridor snapping. The required invariant is enforced: surviving Politik
connections are pruned when the selected path crosses rejected water.
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

**Composition pipeline** (single fragment shader [shaders/macro.frag](shaders/macro.frag),
driven by [macro/vk_macro_renderer.cpp](src/macro/vk_macro_renderer.cpp), in order):

```
biomeTextureOverlay(worldPx)           ← biome ground + shore + climate + rivers*
   ↓
roadOverlay / dirtRoadOverlay          ← FeatureLayer (roads)
   ↓
decorationOverlay                      ← 3×3 painter order: trees, mountains, landmarks
   ↓
zoneTint                               ← ZoneLayer (zone > 4)
   ↓
cellGrid                               ← torus visibility (zoom ≥ 8)
   ↓
nightDarken                            ← time-of-day tint + baked night-glow field
```

\*Rivers are **not** a separate stage. `generate_river_data` carves each river
cell below sea level, so `bt_biome()` classifies it as `Biome::Water` and it
renders through the ordinary sea-water path *inside* `biomeTextureOverlay` —
crisp banks, no halo. The old translucent `riverOverlay`/`riverVisualValue` is
retired from the shipping `macro.frag`, and the dead `u_riverMap` binding was
deleted outright (2026-08-13) — the river mask lives on CPU-side as gameplay
state (`TerrainData.riverData`). Full write-up: [macroworld.md](macroworld.md)
§ Rivers.

The `nightDarken` stage does more than dim: at night it also **adds a baked
night-light field** sampled at the cell's map UV — settlement/village/spire glow
that spreads along roads and is smothered by forest canopy. The field is baked
on the CPU from world state (not per frame) and re-uploaded only on world-change;
one knob, `kMacroGlowGain`, sets its master brightness. Full pipeline:
[macro-lighting.md](macro-lighting.md).

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

| Overlay / extension  | Data source                                  | Procedural overlay                        |
|----------------------|----------------------------------------------|--------------------------------------------|
| ~~Rivers~~ (shipped, differently) | carved to `Biome::Water` in generation | **not** an overlay after all — honest water, rendered via the sea path in `biomeTextureOverlay()`; see [macroworld.md](macroworld.md) |
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

**Four marker styles:** `quest` (gold `!`), `poi` (blue `★`), `danger`
(red `!`), `waypoint` (green `◆`) — quest and danger share the glyph but
differ by colour. Each has a colour and glyph defined in `kMarkerColor`
and `kMarkerGlyph` (colours are `0xAARRGGBB`; ImGui renderers repack to
`IM_COL32`).

**Stored in:** `GameState::markers : std::vector<Marker>` — serialised
with save data.

**Rendering:** ImGui foreground draw list, positioned via the macro
camera transform; drawn above the GL canvas. Not sprite-based — uses
ImGui text styling with glow effects.

**Quest integration:** Quest pins are a *derived* projection, not
hand-mutated on accept/abandon. `rebuild_quest_markers(gs, active)`
([events/quests/quest_engine.h](src/events/quests/quest_engine.h))
rebuilds the whole `quest_` marker slice from the active quests — one
`quest` pin per incomplete world-anchored objective (its cell resolver
mirrors `eval_objective`; a `destroy_npc` kill-count has no cell, so no
pin), covering all targets of every active quest. `QuestEngine` stays
pure; the rebuild runs off a cheap per-frame signature guard in
`process_world_events`, so it fires only when the quest set changes and
also reconciles stale `quest_` pins from a loaded save. See
[quests.md](quests.md).

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

Rendering: the subworld is **always first-person 3D** (Might & Magic style),
drawn by the Vulkan `vk_renderer_3d`. There is **no 2D subworld renderer** — the
flat top-down 2D view is the *macro* map / minimap, not a subworld mode (see
`sub/engine.h`). *(Historical note: the TS prototype had a `renderer_2d`; it was
not ported. Rows below that map `subworld/map-renderer.ts` / `renderer.ts` are
retained only as TS-origin provenance, with no C++ counterpart.)*

**3D simulation model.** World *generation* is 2D: terrain heightmaps, biome
assignment, structures (trees, buildings), roads, and the seamless 3×3 window
all operate on a flat 2D tile grid. But **entity simulation is honest 3D** — all
three coordinates X, Y, Z are equal and every distance check, hit test, and
rendering position uses all three:

- **`ecs::Position{x, y, z}`** is the single source of truth for every entity's
  world-space location. Z is absolute altitude in metres, with `z = 0` at the
  sea-level water plane (`WATER_LEVEL * kHeightScale ≈ 600 m`).
- **Ground-walking entities** (no `ecs::Flying`, no `ecs::Projectile`) are pinned
  to the terrain each tick: `pos.z = sample_height_m(x, y)`. They do not own
  their Z — the terrain does.
- **Flying entities** own their Z through pitch-based velocity and flight-height
  clamping (`flightCamY_`).
- **Projectiles** carry `Projectile{vx, vy, vz, …}` and fly along a 3D velocity
  vector. Spawn direction comes from camera `(yaw, pitch)` for the player, or a
  3D aim vector toward the target for NPCs. Hit detection (sphere intersection,
  blast radius, beam perpendicular distance) is 3D.
- **Point lights** (`ecs::LightEmitter`) render at the entity's `Position.z`.
- **Spell VFX** (trails, impact bursts) receive the projectile's actual 3D
  position via the `SpellFxEmitFn` callback — no terrain-sampling fallback.
- **NPC sprites / billboards** are placed at `Position.z` in world space.
- **All distance checks** — melee targeting, NPC AI chase/detection, proximity
  scans, hostile range, corpse interaction, danger-zone level — use 3D Euclidean
  distance (`dist3sq`).

| TS module                              | C++ target                                              | Responsibility |
|----------------------------------------|----------------------------------------------------------|----------------|
| `subworld/engine.ts`                   | [sub/engine.{h,cpp}](src/sub/engine.h)                  | Subworld game loop, input, AI / system tick dispatch |
| `subworld/map-data.ts`                 | [sub/map_data.h](src/sub/map_data.h)                    | `CellContext`, `SubworldMapData`, `Structure`, tile constants |
| `subworld/map-factory.ts`              | [sub/map_factory.{h,cpp}](src/sub/map_factory.h)        | Session-local subworld snapshot cache; runtime save persistence is still outside the current **v10** save schema |
| `subworld/seamless-manager.ts`         | [sub/seamless_manager.{h,cpp}](src/sub/seamless_manager.h) | 3×3 cell grid, composite tile / heightmap, boundary re-centre, worker-backed exposed-cell generation |
| `subworld/gen-worker.ts` (Web Worker)  | [sub/seamless_manager.{h,cpp}](src/sub/seamless_manager.h) `std::jthread` workers | Off-thread exposed-cell generation with placeholder cells, completed-job stitching, outgoing save jobs, and async composite road smoothing |
| `subworld/map-renderer.ts`             | *(not ported — no C++ 2D subworld renderer)*            | TS-only 2D tile-map renderer; the subworld is first-person 3D only |
| `subworld/renderer.ts`                 | *(not ported — no C++ 2D subworld renderer)*            | TS-only 2D entity renderer |
| `subworld/renderer-3d.ts`              | [sub/vk_renderer_3d.{h,cpp}](src/sub/vk_renderer_3d.h)  | First-person 3D: terrain mesh + water + sun shading (the sole subworld renderer) |
| `subworld/camera.ts`                   | [sub/camera.h](src/sub/camera.h)                        | First-person camera (yaw/pitch, fov) |
| `subworld/math3d.ts`                   | [core/math.h](src/core/math.h)                          | mat4/vec3 PODs |
| `subworld/textures.ts`                 | *(removed — `sub/textures.{h,cpp}` was deleted in the Vulkan cutover)* | The old CPU pixel-art atlas is gone; subworld ground is procedural in-shader |
| `subworld/base-generator.ts`           | [sub/base_generator.{h,cpp}](src/sub/base_generator.h)  | Universal foundation: heightmap, `BiomeConfig`, coastal sculpting |
| `subworld/city-generator.ts` … `subworld/road-generator.ts`, `subworld/spire.ts` | [sub/gens/dispatch.{h,cpp}](src/sub/gens/dispatch.h) (and per-biome `.cpp`) | One self-contained generator per landmark / mode |
| `subworld/sky.ts`                      | [shaders/sky.frag](shaders/sky.frag) (drawn by `vk_renderer_3d`) | Procedural sky shader: gradient, sun, moons, stars, FBM clouds. There is **no `sub/sky.{h,cpp}`** — the sky is a shader pass inside the 3D renderer |
| `subworld/lighting.ts`                 | [sub/lighting.h](src/sub/lighting.h)                    | `compute_sun(WorldTime)` → direction, colour, intensity |
| `subworld/spawn.ts`                    | [sub/spawn.{h,cpp}](src/sub/spawn.h)                    | Per-biome ambient spawn from the global monster table; bakes `NPCKind.type = 0x100 \| catalogIndex` |
| `subworld/ai.ts`                       | [sub/ai.{h,cpp}](src/sub/ai.h)                          | Local NPC AI tick (chase + cooldown attack, missile / melee) |
| `subworld/fauna.ts`                    | [macro/fauna.{h,cpp}](src/macro/fauna.h)                | **Global monster table** (source of truth; MACRO data since 2026-08-07): per-biome `FaunaEntry` density tables + stable-id registry (`creature_catalog` / `creature_def` / `creature_def_from_kind`) + per-cell capacity for the `fauna_count` macro stock. See [monsters.md](monsters.md) |
| `subworld/citizen-sprites.ts`          | skipped                                                  | TS Canvas2D walk-strip helper; native NPC visuals use paper-doll billboards in 3D |
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

The player at the centre is materialised as a real ECS entity carrying an
`ecs::PlayerTag` — the movable *"player flag"*: any NPC can, in principle,
receive the tag, and the tagged entity is the subworld's sim-centre. That
entity is now a **full combat target**, carrying `Position + PlayerTag +
Health + Combat + BodyRadius + SubworldTag`, so it is struck by melee,
projectiles, and blasts through the *same* universal paths as any NPC — there
is no player special-case in the hit code. `BodyRadius` (1.5) is the universal
combat hit radius: any actor may carry one, and the player needs it explicitly
because it is the camera (no `Sprite`) and is input-driven (no `SubworldAi`) —
the two fields the hit code would otherwise read a radius from. Incoming damage
lands on the entity's `Health`; `gs.player.combatStats` stays authoritative
across the seam through an int↔float bridge — a tick-top PULL mirrors the macro
HP onto `Health`, damage reduces it, and a tick-end PUSH reconciles the result
back onto the scalar. Outgoing **melee** now flows from that same entity: the
tick-top PULL also refreshes its `Combat.damage` from the sheet, and
`tick_player_melee` reads the entity's `Combat` (damage/range/cooldown) instead
of recomputing — yet the NPC actor loop never auto-swings it, because
`is_player_side()` makes the player non-hostile-to-itself, so it is skipped as an
attacker there. Outgoing **spells** now carry the player's real entity id (4d):
`player_entity_id()` stamps each player-cast projectile's `ownerId` just as an
NPC missile carries its firer's, and the `ownerId == 0` sentinel is retired —
ownership is decided purely by the owner entity's tags.

On the **macro map** the player is now *also* a `PlayerTag` entity (macro-4a): a
deliberately minimal flag carrying `Position + PlayerTag` only — no `SubworldTag`
or `NPCKind`, so it is invisible to the overworld render / proximity / AI passes
and to the subworld reapers. It is maintained by `ensure_macro_player_entity(gs,
world)` (`src/macro/player_entity.cpp`), called at world boot, at save-load, and
at the top of the macro (non-subworld) tick. Because both seam crossings funnel
through `clear_player_entity()` (enter via `spawn_player_entity()`, leave
explicitly), the macro tick simply *re-heals* the flag after any `leave()` and
one-way syncs its `Position` from the macro-authoritative `gs.player` scalar. The
system-wide invariant is **exactly one `PlayerTag` at all times** — the minimal
flag on the overworld, the full combat actor in a subworld, never both. This
gives the flag a home on both sides of the seam.

**Possession / вселение (Inc 5c).** The `control` command MOVES the one
`PlayerTag` flag onto a live body: `possess_entity(reg, target)` does
`remove<PlayerTag>(old); emplace<PlayerTag>(target)` — the vacated body reverts to
an ordinary NPC. Targeting is scale-split: in the subworld you look at a body and
possess it (`possess_aim` uses the `aim_target` forward-cone primitive on the
camera yaw; keybind **V** / console `possess`), with `possess_by_id` as the debug
by-id path. Possession is **body-native**: the inhabited body fights with its OWN
`CharacterSheet`/`Combat`/`Health` (possess a lord ⇒ strong; a rat ⇒ weak), and
`gs.player` (the hero) is preserved untouched as the revert target — the
discriminator is `NPCKind`, which the hero husk lacks and every scene body has, so
the sync/reconcile paths branch on it. Because the flag simply moves, every
universal path already respects it: enemies target the inhabited body, its death
is game-over, and the renderer/minimap/AI exclude it (`entt::exclude<PlayerTag>` /
an `any_of<PlayerTag>` skip) so the camera body never billboards or self-drives.
The HUD reads a non-mutating `player_display_hp()` (the flagged body's `Health`),
never mirroring into the frozen `gs.player`.

**Macro→subworld projection (Inc 5d).** So that the lords, bandits, and peasants
who roam the overworld are physically *met* — and possessed — where they actually
are, `project_macro_npcs_into_subworld(w, mgr, centerCx, centerCy, mapW, mapH,
seed)` runs once on `enter()`. It snapshots every persistent macro NPC whose
integer cell lies within ±1 of the window centre on the torus (the same nine cells
the seamless manager loads) and CREATES a full combat body for each — the macro
entity itself is never touched (the macro tick is frozen while a subworld is
active). `NPCKind`/faction and `NpcCharacter` are copied verbatim, `Health.hp` is
carried as body-native persistent state, `Combat` is DERIVED from a fresh universal
`CharacterSheet` (citizen-path parity), hostility is data-driven off `NpcTypeDef.ai`
(`Aggressive`→fight, else flee), and placement scatters within the cell's
sub-region dodging water. Each projection carries a **`MacroOrigin{macro}`** runtime
backlink to its source. Two reapers bracket its lifetime: the seam-crossing reaper
(`clear_subworld_world_entities`) SPARES anything with `MacroOrigin` (so an
un-remapped projection survives an in-subworld re-center), while the `leave()`
reaper (`clear_subworld_entities`) takes ALL `SubworldTag` unconditionally — so
projections are session-scoped and gone on exit, the macro source persisting. It is
enter-only (a macro NPC entering a neighbour cell mid-session is not yet
materialised — accepted v1 scope, the persistent entity is never lost).

**Exit remap (Inc 5e-1).** On `leave()`, before the reaper destroys the body, the
possessed body's `MacroOrigin` decides where the macro player resurfaces. The pure
registry query `macro_exit_cell_for_body(w, body, mapW, mapH)` returns the origin's
torus-wrapped cell when `body` carries a valid backlink, else "no remap"; the engine
wrapper `remap_macro_player_to_origin()` writes `gs.player` from it, falling back to
the window centre (`sync_macro_player_to_center`) for any un-possessed exit — the
hero husk and ambient/citizen bodies carry no backlink. So possessing a lord and
leaving lands you on *the lord's* macro cell ("exit AS the lord"), while a normal
exit is unchanged. The position remap alone is runtime-only.

**Identity remap (Inc 5e-2).** The exit doesn't merely land you *where* the lord
stood but hands you the lord's *identity*: after the position remap,
`adopt_possessed_macro_as_player(reg, macro)` moves the single macro `PlayerTag`
onto the origin macro NPC itself, so the flag rides a real `MacroNpcRuntime` body
and the vacated hero husk is reaped by the normal teardown (strip-not-destroy spares
only `MacroNpcRuntime` holders, so exactly-one-`PlayerTag` still holds). Because the
ECS is never serialized — macro NPCs regenerate deterministically from `worldSeed`
in a fixed creation order every boot — a save-stable identity cannot be an
`entt::entity`; it is a deterministic **spawn ordinal**. The runtime component
`ecs::MacroSpawnId { std::uint32_t index; }` is stamped by the sole creation path
`make_npc` (the Nth NPC created gets ordinal N) and is itself never serialized; only
the *chosen* ordinal is persisted, in `PlayerState::possessedMacroSpawnId` (the one
new serialized field, **`kSaveVersion` 9→10**). On load,
`reattach_player_to_macro_spawn` re-finds the regenerated NPC by ordinal and hands
the flag over from the freshly-healed husk; a missing ordinal (the lord died before
the save, or the seed changed) falls back to the hero, changing nothing. The owner's
decision was that the possessed identity **must** survive save/load. The remaining
staged work (5e-3) is carrying possession through a *re-enter* — today re-entering a
subworld while possessing drops the flag back to the hero (the lord survives as an
autonomous NPC; nothing leaks).

The player-as-entity → possession track (Inc 4 + Inc 5) has its own focused
write-up — the flag model, the exactly-one invariant, the staged increments, and
the data-driven "anything is possessable" extension — in
[possession.md](possession.md).

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
(`std::vector<std::uint8_t>`) and heightmap (`std::vector<float>`).
Boundary crossing now installs deterministic placeholder cells immediately,
queues exposed-cell generation on owned `std::jthread` workers, stitches
completed cells back on the main thread, drains outgoing save jobs, and can
run composite road smoothing asynchronously. This replaces the TS Web Worker
dispatch without blocking the seam-crossing path on `dispatch_generate()`.

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
4. **Mountain amplification** — cells in the `Mountain` biome get a
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

Features (roads, forests) are placed based on both the centre
cell's `FeatureType` and its 8 neighbours (mountains render in the biome-ground
layer, not here):

- **Roads** connect toward cell edges via *edge anchors* computed from
  neighbouring road cells. A road in the centre always exits toward any
  adjacent road cell → seamless road network. Grassland, forest, swamp,
  and mountain wilderness cells also carve bounded center-to-anchor trails
  when neighbouring macro cells carry road connectivity, matching the TS
  `edgeAnchors` generator contract without putting unbounded work on the
  seam path.
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
3. **Structures** — 2D shape records (houses, walls, bridges, trees) with
   3D render height. Current 3D rendering consumes tree records for
   billboards; house/wall/bridge mesh rendering is still pending.
4. **Water level** — per-biome threshold from `BiomeConfig`.
5. **NPC spawns** — entity list with position, sprite, AI state.

### Neighbour-Context Blending (universal, data-driven)

Every subworld cell is generated from its macro `CellContext` **and the full
3×3 neighbour context**. Transitions between cells *emerge from neighbour
rules*, never from hardcoded special cases — so the world is seamless and new
content slots in by adding data, not `if`-chains. The organising idea: **no
seams — the mass of the cell is a blend of what its neighbours are.**

Universal rules (each declared once, applied to every seam):

- **Roads connect by feature.** A cell is *road-connectable* iff it carries a
  road feature (`FT_Road` / `FT_DirtRoad`). **Settlements always sit on a road
  feature** — macro boot ([app/main.cpp](src/app/main.cpp)) stamps every city
  into the road mask and every village into the dirt-road mask before
  `build_feature_layer`, so the subworld's feature-driven neighbour check
  (`connected_road_dirs`) makes roads reach every settlement and **adjacent
  settlements merge** with *no* per-generator landmark plumbing. Edge crossings
  use a **symmetric per-edge seed** (`symmetric_edge_seed`) so both sides of a
  seam pick the *same* crossing point and always meet.
- **Same neighbour ⇒ merge.** Two identical adjacent cells (city+city, or the
  same biome) generate as one continuous surface — two of a kind read as one
  larger thing, no wall at the seam.
- **Different neighbours ⇒ gradient.** Adjacent unlike cells blend across the
  seam: a forest next to open ground thins its trees toward the open edge while
  the open cell gains trees toward the forest edge; the same density-gradient
  rule applies to any feature.
- **Water ⇒ coast.** A water neighbour sculpts the shared edge down into a
  shoreline (Layer 1 coastal sculpting).

**Extensibility (the point).** Adding a biome, feature, or landmark = one data
entry plus its neighbour rule (a density / blend / connect declaration). Because
every rule reads the neighbour context uniformly, the new kind composites with
every existing kind automatically — the combinatorics are absorbed by the shared
blend, not by O(kinds²) special cases.

### `BiomeConfig` (data-driven terrain)

Each of the 11 biomes (Tundra…Tropics + Water + Mountain) has a config in
`base_generator.cpp` (`kConfigs[11]`). Adding a new biome means adding one
config entry — no engine code changes.

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

### Props and interactions — one table, one keypress

The world is built in two passes, and the second one is data. Generators place
**props** — every physical thing that is not terrain: trees, walls, houses,
crops, furniture, doors, lanterns, stairs. All of them are `Structure` records
in the one composite array, and everything the engine wants to know about a
kind is a **column in `kStructureKindRows`** (`sub/map_data.h`): its size
floors, whether it is solid, what it drops, *which render pass draws it*
(`Draw::Billboard` / `Draw::Solid`), whether it is wood or masonry, what light
it casts — and what pressing **E** on it does.

That last column is the interaction: `InteractId` plus `kInteractRows`
(`{verb, reachTiles}`). A prop that carries one is interactive; a prop that
does not is scenery. Per-*instance* payload rides `Structure::tag` — a door
carries the ordinal of the building it opens, a stair carries its direction.
So a new interaction is **one prop row plus one case in the single
dispatcher**, and it is visible, solid, lit, minimapped and testable the day
its row lands. A door additionally declares WHICH interior it opens
(`opens`), which is why a house leaf and a cave mouth share one entry path
with no chain in the engine.

The verbs that exist show the shape the table is meant to hold, and each one
pays through a system that was already there rather than inventing its own:

| Verb | Prop | What it does | Whose law it borrows |
|------|------|--------------|----------------------|
| `Door` | house leaf, cave mouth | raises the interior it declares | the dungeon session |
| `Stairs` | shaft block | same identity, one storey along | the dungeon session |
| `Loot` | corpse | the kill's own drop | the one loot registry |
| `Search` | chest | hands over a stack of the OWNING landmark's store, at a price in standing | `Settlement::inventory` + `add_player_reputation` |
| `Drink` | well | an hour of rest, standing | `kSpRegenPctPerHour` |
| `Read` | signboard | names the place | the settlement roster |

The chest is the pattern to copy when a verb must GIVE something. It has no
loot row: a prop that conjures goods is a prop the player farms by walking
out and back in, since interiors re-derive from their identity. So it draws
from the store of whoever owns the place — an emptied town has bare chests,
and the goods return only the way that town's goods ever return.

Targeting is **by look, never by proximity** (owner ruling 2026-08-12): the
resolver takes the prop under the reticle inside a forward cone on the camera
yaw, within that verb's own reach measured to the prop's *surface*. The HUD
prompt under the crosshair (`interact_prompt()`) runs the exact same
resolution, so it can never offer a verb the keypress will not perform.
Corpses answer the same rule with the `Loot` verb although they are ECS
bodies rather than composite props — one keypress, one mental model.

Lit props need no renderer code: the engine hangs an ordinary
`ecs::LightEmitter` body on each one (`rebuild_prop_cache`, rebuilt on the
same "structures changed" signal as the solidity index), so a street lantern
reaches the shader through the very path a carried torch does.

*Scar tissue:* the session cache's `restore_into` diffed a hardcoded five
kinds, so every prop added after `Bridge` — crops, fences, furniture, and then
doors — was silently dropped when the player walked out of a cell and back.
A count that must track a table now reads the table (`Structure::kKindCount`).

### Dungeons — the interior layer (`src/sub/dgn/`)

**A dungeon is a pocket subworld.** Behind every door there is a scene the
same engine simulates, draws and fights in — one `SubworldEngine`, one ECS,
one renderer. There is no interior engine, no second combat path, no second
persistence rule; a dungeon is the subworld with its 3×3 window pinned and
its context saying *"this is what stands behind a door"*.

**Identity, not storage.** A `DungeonRef` (`sub/map_data.h`, carried on
`CellContext`) is `{kind, level, ordinal, footprint}`; with the door's macro
cell and the world seed it hashes — through the one `dungeon_scene_seed` —
into the scene. Same door, same interior, byte for byte, forever. Nothing
below the map is saved, exactly as nothing below the map is saved for the
subworld: the interior is a projection of a projection, and every lasting
act pays UP through a macro stock.

**The scene.** `enter_dungeon_scene` installs a synthetic resolver on the
ordinary `SeamlessSubworldManager`: the door's cell resolves to the interior,
the eight ring cells to sealed `DungeonRef::Void` filler. Everything
downstream — composite, upload, collision index, battle pass — runs the
unchanged window path. The window is static (no seam check, no re-centre: the
room is walled and the ring is filler), and the interior renders with **no
sea** (the world water plane sits at `WATER_LEVEL` and would flood a floor
lower than it).

**One door, one key.** `E` is the universal interaction: outside it opens the
door prop you are looking at; inside, a stair shaft under your feet changes
storey and the threshold walks you back out to the exact tile you knocked
from. Corpse loot outranks both — the same E, ordered by what is under the
reticle.

**Two ways out, both gated by danger.** The *walked* way is the door or the
stair: it puts you back on the doorstep you came from, one layer up. The
*quick* way is the ordinary leave key, and it surfaces you straight to the
**map** from any storey — the same universal exit the open subworld has, so a
cellar is not a place the player must walk out of backwards. It is gated on
the HUD's own danger gem being green, asked of the interior itself rather
than of the cell's zone: a dungeon's macro cell is a town square, whose zone
would report the safety of the *street* while a troll stands behind you.

**Storeys.** `DungeonRef::level` is signed: 0 is the level the door opens
onto, +1 up, −1 a cellar. A stair is the same portal as the street door
pointed at the same identity one level along — a dungeon→dungeon scene swap
that never surfaces. Two shafts sit at fixed room corners (NW joins 0↔+1, NE
joins 0↔−1) so the geometry is a rule the generator stamps, the engine reads
and the test asserts, rather than data plumbed between them.

**Population comes from the same stocks as everything else.** Residents are
derived citizens carrying the settlement's `Population` loan; cellar vermin
are creatures from the one monster table carrying the cell's `FaunaCount`
loan. A kill behind a door thins the town or the cell in that tick, through
the same receipt a street kill settles, and the single regrowth law refills
it. There is no dungeon respawn system.

**Modules.** `sub/dgn/dispatch.{h,cpp}` mirrors `sub/gens/dispatch`: one
self-contained TU per interior kind, routed by `DungeonRef::kind`. Adding an
interior — a keep, a barrow, an authored dungeon from L4 `content/` — is one
module plus one row, exactly like adding a subworld generator.

Two kinds ship. **`house.cpp`** stamps a rectangle: rooms cut by partitions
with one doorway each, furniture, a household, storeys joined by shafts.
**`cave.cpp`** grows a shape instead: chambers chained by wandering
galleries carved out of solid rock, connected by construction because
consecutive carve discs always share ground. The difference is the point of
the layer — a house is understood from its doorway, a cave has to be walked.

An interior must be able to say **which tile is its floor**
(`dungeon_floor_tile`): a hall is flagged, a cavern is bare scree, and every
placement pass (residents, vermin) reads that rather than assuming a house.
The cave's first cut used one tile id for floor and walls alike and nothing
could be placed in it at all.

### 3D Rendering Pipeline

The 2D tile grid is the source of truth. The 3D renderer reads the same data:

- **Sky**: fullscreen gradient quad — procedural sun, a prominent moon, stars,
  FBM clouds ([shaders/sky.frag](shaders/sky.frag), drawn by `vk_renderer_3d`).
- **Terrain**: heightmap (`std::vector<float>`) + tile grid
  (`std::vector<std::uint8_t>`) -> 192x192 quad mesh sampled from the
  seamless heightmap, central-difference normals, indexed `GL_TRIANGLES`,
  per-tile texture from atlas (9 biome grounds + water). Lit by the
  time-of-day sun/ambient pass with 4-band quantised NdotL; point-light
  application is not wired into `Renderer3D` yet.
- **Water plane**: flat alpha-blended quad at `waterLevel × kHeightScale`,
  depth-write disabled. Sun/moon-direction specular (half-vector, with a low-light
  "glitter road"), wave animation. Water level comes from `BiomeConfig` via
  `seamless_manager::composite_water_level()`.
- **Structure meshes**: generated `Structure[]` records exist, but the current
  renderer only consumes `Structure::Tree` as billboard input. House, wall,
  bridge, and corpse mesh rendering are not implemented.
- **Billboard shadows**: not implemented. Do not cite them as current visual
  evidence until a real shadow pass exists in `renderer_3d.cpp`.
- **Billboards**: tree structures -> camera-facing alpha-tested quads.
  Ambient + sun intensity modulation (no per-pixel normals).
- **Spell effects**: ECS projectile descriptors -> additive 3D billboards /
  beam ribbons.
- **NPCs**: EnTT entities → per-frame billboard sprites (same shader as trees).

Render order: **Sky -> Terrain -> Water -> Spell Effects -> Tree Billboards -> NPC Paper-Doll Billboards.**

Both 2D and 3D views share the same engine tick, EnTT registry, and game
state. Switching view only changes which renderer draws the frame.

### Lighting System

Pure graphics — does not affect game state, AI, or any non-rendering
system. Computed per-frame from `WorldTime` in `sub/lighting.h`
(`compute_light_parameters`) and consumed by the Vulkan lit passes through one
shared `lit_surface()` in [shaders/lighting.glsl](shaders/lighting.glsl). Full
detail: [render.md](render.md) §Dynamic lighting.

**Directional light (sun *and* moon, one slot):**
- Sun direction matches the sky: `sunAng = (tod − 0.25) × 2π`, `sunDir = (cos, sin, 0)`.
- Sun intensity ramps with `smoothstep(−0.05, 0.3, elevation)` and reaches zero as
  the sun drops below the horizon.
- **The moon is a weak directional light in its own right**, not just ambient fill:
  at night a cool blue term (`kMoonDirGain`, `0.42`) fades up on the **same**
  `sunDir`/`sunColor` slot and the direction flips to the anti-solar point
  `-sunDir`, so the one slot carries whichever body is up and the night stays
  directionally sculpted (not a flat wash). This is the same bearing the moon disc
  and the water specular use — a single celestial direction.
- Sun/moon colour warms near the horizon (dawn/dusk orange), neutral white
  overhead, cool blue for the moon.
- Ambient colour: a **low** cool-blue moonlight floor at night → neutral during
  day (kept low so the directional moonlight does the sculpting).
- Terrain/structure diffuse quantised to 4 bands for the pixel-retro aesthetic;
  billboards use a flat term.

**Sprite shadows:**
- Pending. `renderer_3d.cpp` currently draws normal tree billboards and
  character paper-doll billboards only; there is no projected-shadow pass.

**Point lights (modular — torches, campfires, spells, windows):**
- `sub/lighting.h` defines the `PointLight` POD and a fixed `kMaxPointLights`, but
  no upload/API path exists yet.
- Current shipped lighting is directional sun/moon plus ambient/fog. Point lights
  are **the approved next increment** ("SSBO + тюнер"): a storage buffer of up to
  `kSubworldMaxLights` (start 32) + a `point_lights()` in `shaders/lighting.glsl`
  + a `LightEmitter` ECS component, with the player emitting through the same path
  as any NPC. Plan: MASTER_PROMPT.md "Dynamic lighting track".

---

## L3 — Event System

Tag-indexed event bus + condition-vector logic engine. Nodes react to
events and emit new ones — the core control-flow mechanism.

| TS module                       | C++ target                                                    | Responsibility |
|---------------------------------|----------------------------------------------------------------|----------------|
| `game/event-bus.ts`             | [events/event_bus.{h,cpp}](src/events/event_bus.h)            | Tick-buffered emit / subscribe, world history |
| `game/event-types.ts`           | [events/event_types.h](src/events/event_types.h)              | Flat `EventTag` enum and generic payload. Extended TS tags through `CameraMove` exist in the native serialized schema and `save_roundtrip_test`; several still need normal gameplay producers/consumers. |
| `game/logic-nodes.ts`           | [events/logic_nodes.{h,cpp}](src/events/logic_nodes.h)        | `LogicNode`, `ConditionSlot`, `LogicNodeEngine` |
| `game/node-registry.ts`         | [events/node_registry.{h,cpp}](src/events/node_registry.h)    | Built-in system nodes (encounters, settlement greeting). The level-up node was removed 2026-08-05: nothing ever emitted `PlayerLevelUp`, so it could not fire — see [progression.md](progression.md) |
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

Pure data modules. Each exports encounter data, story definitions, or
`LogicNode` factories. Plot registration is explicit in the module that owns
the content; any file here should remain removable without breaking the
engine core.

| TS module                          | C++ target                                                      | Responsibility |
|------------------------------------|------------------------------------------------------------------|----------------|
| `game/plot/index.ts`               | no native aggregate file yet                                      | TS single import point; native registration is explicit per module |
| `game/plot/intro.ts`               | [content/plot/intro.{h,cpp}](src/content/plot/intro.h)            | Intro: 9 slides, sex choice, name input, realm choice, `ShowStory` node |
| `game/plot/chapter-1.ts`           | [content/plot/chapter_1.h](src/content/plot/chapter_1.h)          | Chapter 1 placeholder (dormant) |
| `game/plot/encounters.ts`          | [content/plot/encounters.h](src/content/plot/encounters.h)      | Random encounter content table |
| `game/quests/quest-generators.ts`  | [content/quests/procedural.{h,cpp}](src/content/quests/procedural.h) | Procedural quest factories |

### Adding new plot content

1. Create `src/content/plot/my_quest.cpp` exporting
   `const std::vector<LogicNode>& my_quest_nodes();`.
2. Register it from the owning plot module or the app boot registration path.
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
| `screens/StoryOverlay.svelte`       | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_story_overlay` / `open_story_overlay` | Universal narrative overlay for slides, choices, and text input; `trigger_story_overlay` smoke captures intro phase 0 |
| `screens/EventOverlay.svelte`       | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_encounter_modal` / `draw_show_dialog` | Encounter modal plus `ShowDialog` choice/effect consumer |
| `screens/StatOverlay.svelte`        | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_character_panel` | Character stats / inventory / army / equipment / spells panel; tabs are runtime-evidenced, equipment slots are placeholder text |
| `screens/MapOverlay.svelte`         | `ui::draw_map_overlay`                                  | Full-screen minimap |
| `screens/CodexOverlay.svelte`       | `ui::draw_codex_overlay`                                | In-game encyclopedia / lore |
| `screens/DiplomacyOverlay.svelte`   | `ui::draw_diplomacy_overlay`                            | Faction relations and diplomacy |
| `screens/SettlementOverlay.svelte`  | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_settlement` | Settlement info, recruit, inventory, trade, quests; Build tab is an explicit no-op surface because no TS/native build-project data exists |
| `screens/TradeOverlay.svelte`       | settlement Trade tab + `ui::draw_npc_proximity_panel` NPC trade popup | Native intentionally does not duplicate a standalone full-screen wrapper; buy/sell gameplay exists in settlement and NPC surfaces |
| `screens/QuestOverlay.svelte`       | [ui/overlays.cpp](src/ui/overlays.cpp) `draw_quest_log` | Active quest journal |
| `screens/SpellOverlay.svelte`       | [ui/overlays.cpp](src/ui/overlays.cpp) character-panel Spells tab + [app/main.cpp](src/app/main.cpp) cast hooks | Spell book surface, active spell selection, cooldown/mana/sustained state, smoke-proven casts |
| `screens/InteractionOverlay.svelte` | `ui::draw_npc_proximity_panel` action buttons           | Full modal dialog pending; Talk, NPC Trade, and Attack actions are runtime-evidenced |
| `screens/NpcProximityPanel.svelte`  | `ui::draw_npc_proximity_panel`                          | Right-edge nearby-NPC awareness panel; NPC Talk, trade, and attack flow are runtime-evidenced |
| `screens/DebugOverlay.svelte`       | [app/main.cpp](src/app/main.cpp) `draw_debug_ui`; `TIMAERT_DEBUG_UI` is extra-debug only | Minimal FPS / camera / world counters; full tools / cheats / entity inspector pending |
| `screens/DeathOverlay.svelte`       | `ui::draw_death_overlay`                                | Death screen with retry |
| `screens/PauseOverlay.svelte`       | `ui::draw_pause_overlay`                                | Pause menu; hosts the **Interface** entry |
| — (native)                          | [ui/ui_settings.cpp](src/ui/ui_settings.cpp) `draw_ui_settings_panel` | Universal UI settings: show/hide + resize registry for every HUD element & panel, macro + micro; global `ui_prefs.cfg`. See [ui-settings.md](ui-settings.md) |
| `ui/theme.ts`                       | inline ImGui styling in `ui/*.cpp`                       | No native `ui/theme.h` exists yet |

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
accept, NPC Talk, NPC Trade, NPC Attack, Spell overlay/casting, `ShowDialog`,
and `ShowStory` (see README). Equipment slots and the Build tab are still not
complete parity claims; Build is intentionally blocked until a real persisted
building contract exists.

The **universal UI settings** system is one registry
([ui/ui_settings.{h,cpp}](src/ui/ui_settings.h)) shared by both worlds: a single
spec table drives one "Interface" pause-menu panel, one global prefs file
(`ui_prefs.cfg`, independent of `save.bin`), and the per-element
visibility/scale that each draw call-site reads. Every registered HUD element
and pop-up panel can be toggled and (where meaningful) resized; the same
`gameplay_panel_open()` predicate that releases the mouse cursor for the other
panels also covers it. Adding an element is one enum value plus one table row.
Full write-up: [ui-settings.md](ui-settings.md).

---

## Save / Load

| TS pattern                        | C++ target                                                         |
|----------------------------------|--------------------------------------------------------------------|
| `state.ts` save / load           | [macro/save.{h,cpp}](src/macro/save.h)                            |
| `subworld/map-factory.ts` regen  | inline in subworld load path                                       |

Magic-gated, version-gated, regenerate-from-seed. **No save compatibility:**
bump `kSaveVersion` for any breaking change to serialised data; existing
saves are silently invalidated.

Current save schema is `kSaveVersion = 18` in
[macro/state.h](src/macro/state.h) — the file's own comment block is the
authoritative changelog, bump by bump. The most recent: 17→18, where the world
clock became ONE integer tick (`core/time.h`), so three ints shrank to one
`uint64` and a save now states the instant exactly, to 1/64 of a real second
(see [time.md](time.md)). Per the no-compat rule the loader hard-rejects any
other version, so every earlier save is invalidated. `save_game`, `load_game`, and `inspect_save` are
built, and the app slot path is the user-writable
`AppData\Roaming\Timaert\timaert_c\save.bin` equivalent on Windows. The v10
binary writer/reader and harness evidence are verified by
`save_roundtrip_test`; GUI round-trip smoke
`new_game,wait_boot_done,save_game,open_load,load_game,wait_boot_done,quit`
passed with a 51733-byte v10 save slot.

---

## Key Patterns

### EventBus + LogicNodeEngine
```
emit(event) → bus buffer → flush() → listeners fire
                                    → engine.tick() checks active nodes
                                    → matching node fires effect() → emits more events
```

Event and logic callbacks use `core/small_function.h`: 64-byte inline,
copyable type-erased storage for `EventBus` handlers and `LogicNode`
predicates/effects/checks. The event/logic dispatch path no longer stores
callback targets through `std::function`.

### Condition Vector
Each node has a `conditions[]` array and a `mask[]` bitmask. The node fires
only when all mask-required conditions are satisfied in a single tick.

### Story System
`ShowStory` and `ShowDialog` have native consumers in
[ui/overlays.cpp](src/ui/overlays.cpp). `ShowStory` opens
`StoryOverlayState`, renders the native `StoryDef` phases, emits
`StoryResult`, and `app/main.cpp` applies `intro_main` results. `ShowDialog`
renders labels/effects from `DialogChoicePayload`; choices that carry `nodeId`
now request activation through the app layer so ImGui does not own gameplay
systems. Count-only dialogs without `DialogChoicePayload` stay visible as
disabled placeholders with the missing-backend reason instead of pretending to
be completed choices.

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
12. **`GLOB_RECURSE`.** Drop a `.cpp` in one of the configured `src/`
    module directories, including `assets/`, and it is compiled.
    Do not edit `CMakeLists.txt` for individual files.
13. **No save compatibility.** Bump `kSaveVersion` for any breaking
    change. No legacy code paths.
14. **Discrete by default.** Narrow integer types for all data (grids,
    counts, ids, serialization); powers of two / round numbers for scalar
    tunables (see *Discreteness & Number Style*). Reach for float only
    where the simulation is genuinely continuous.

Modular, elegant, generalised, optimised — minimal systems, maximal
functionality and universality.

**ALL SYSTEMS ARE DATA DRIVEN. DATA ORIENTED PROGRAMMING.**
