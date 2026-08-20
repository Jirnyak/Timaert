# Samosbor / Timaert — MANIFEST

> **This is the engineering register of the project** — the orchestrating
> document that references every system doc, the build recipes, the
> Integration Ledger and the test roster. It was `README.md` until
> 2026-07-30, when the README became the public showcase page; nothing was
> dropped in the move. Agents and contributors: treat THIS file as what
> older docs (MASTER_PROMPT.md, AGENTS.md, memory notes) call "the README".

**`timaert_c/` is the final game.** C++23 + EnTT 3.14 + ImGui native port of the
Timaert TypeScript prototype: a procedural macro-world simulation with a
seamless 1024-cell subworld zoom-in, dual 2D / first-person 3D rendering, an
event/quest engine, and a modular spell system. Gameplay is still being
translated from the TS/Svelte source (`C:\Timaert\src`), but the C++ port is
what ships.

> **Backend direction.** Rendering + compute target **Vulkan** (MoltenVK on
> macOS). The legacy **OpenGL 3.2 Core / WebGL2 / Emscripten-WASM** paths are
> being retired and the browser build is dropped, because the game's core goal —
> thousands of macro squads and thousands of microworld combatants — is a
> compute-shader problem GL 3.2 cannot express (and macOS GL is capped at 4.1,
> so GL compute is impossible on Mac). **SDL2 is demoted to platform/input/audio
> only — not the graphics API.** The migration is **complete in `src/`**: 0 GL
> call sites, 0 `emscripten` references, no `src/gl/` directory; the Vulkan
> backend lives in `src/gpu/` and the window is created `SDL_WINDOW_VULKAN`. The
> Build and Dependencies sections below already describe the Vulkan toolchain.
> (Leftover `EMSCRIPTEN` guards still linger in `CMakeLists.txt` — build-system
> scaffolding to prune, not live code.) See [ARCHITECTURE.md](ARCHITECTURE.md)
> §Rendering & Compute Backend and §GPU-Driven Simulation.

## Documentation

[ARCHITECTURE.md](ARCHITECTURE.md) is the layered design source of truth;
[AGENTS.md](AGENTS.md) holds contributor / agent rules. Each core system has a
focused doc in this directory alongside the README, which orchestrates them.

| System | Doc | What it covers |
|---
<p align="center">
  <a href="https://twitter.com/intent/tweet?text=Check%20out%20Timaert%20on%20GitHub!&url=https%3A%2F%2FJirnyak.github.io%2FTimaert%2F"><img src="https://img.shields.io/badge/Share-Twitter%2FX-1DA1F2?style=for-the-badge&logo=x" alt="Share on X"/></a> &nbsp;
  <a href="https://news.ycombinator.com/submitlink?u=https%3A%2F%2FJirnyak.github.io%2FTimaert%2F&t=Check%20out%20Timaert%20on%20GitHub!"><img src="https://img.shields.io/badge/Submit-Hacker%20News-FF6600?style=for-the-badge&logo=y-combinator" alt="Submit to HN"/></a> &nbsp;
  <a href="https://reddit.com/submit?url=https%3A%2F%2FJirnyak.github.io%2FTimaert%2F&title=Check%20out%20Timaert%20on%20GitHub!"><img src="https://img.shields.io/badge/Post-Reddit-FF4500?style=for-the-badge&logo=reddit" alt="Post on Reddit"/></a>
</p>
--------|-----|----------------|
| Lore | [lore.md](lore.md) | THE canonical fiction: dead gods, magic vs black energy, the powers, the named figures, the ten-year clock, the endings — each tied to the mechanic that produces it |
| Time | [time.md](time.md) | ONE integer tick ladder: fixed simulation step, derived calendar, slower subworld day, every rate in game time |
| Macroworld | [macroworld.md](macroworld.md) | World state, terrain gen, time, politik, pathfinding |
| Microworld | [microworld.md](microworld.md) | Seamless 3×3 subworld, generators, 2D/3D renderers |
| Seam crossing | [seamless-crossing.md](seamless-crossing.md) | Hitch-free cell-boundary crossing: GPU toroidal shift, O(new content) upload |
| Dungeons & props | [dungeons.md](dungeons.md) | The interior layer (houses, cellars, caves) as pocket subworlds behind a door — identity not storage, storeys as portals — and the ONE prop table whose columns decide look, light and what pressing E does |
| Biomes | [biomes.md](biomes.md) | 3×3 climate matrix + Water/Mountain by elevation, procedural GPU biome textures |
| Landmarks | [landmarks.md](landmarks.md) | Settlements, spires, dungeons, markers |
| Features | [features.md](features.md) | Roads, dirt roads, trees (feature layer; mountains are a biome) |
| Spells | [spells.md](spells.md) | Spell book, cooldowns, mana, effect modules |
| RPG system | [rpg.md](rpg.md) | Attributes, XP, items, inventory, equipment |
| Economy | [economy.md](economy.md) | Settlement inventories, prices, trade tick |
| Zones | [zones.md](zones.md) | Difficulty heightmap 0–9, danger scaling |
| Microcombat | [microcombat.md](microcombat.md) | Sword-and-magic ARPG combat (unified, in-subworld) |
| Possession | [possession.md](possession.md) | Player = one `PlayerTag` flag on an ordinary body; вселение moves the flag; body-native stats; macro↔subworld projection; exit *as* the possessed lord (position + identity remap), save-stable via a spawn ordinal |
| Monsters & Loot | [monsters.md](monsters.md) | ONE global monster table + ONE loot table (source of truth), spawn/XP |
| Macrosim | [macrosim.md](macrosim.md) | Mount-&-Blade / Dwarf-Fortress macro simulation |
| Resources | [resources.md](resources.md) | THE resource-field registry: fields over macro cells, two storage dialects behind one door, the ONE growth law, the ledger every harvest settles through |
| Quests | [quests.md](quests.md) | Objective/reward registries, procedural generation, world-map quest markers |
| Progression | [progression.md](progression.md) | Levels, spell unlocks, plot/events, game arc |
| Rendering | [render.md](render.md) | Vulkan render passes, dynamic lighting, shadow mapping, sky/stars, water |
| Sprites | [sprites.md](sprites.md) | THE one sprite law: a kind is a row, drawn art wins, procedural archetype is the floor, one coverage = one shadow; the paper-doll composite's retirement and the future голыш+palette+clothes tier |
| Macro lighting | [macro-lighting.md](macro-lighting.md) | ONE celestial light for the 2D world map: day/night relief hillshade (sun→moon sweep), water sun/moon glint, baked night-glow field with feature- **and elevation-**occluded spread |
| Map & knowledge | [map.md](map.md) | THE fog-of-war + map doc: knowledge layer (Unknown/Explored/Visible, save v40), sight-as-light over the one optical sweep, the drowned-memory render law, the full-screen chart page (M), marker surfaces, subworld minimap/map |
| GPU backend | [vulkan.md](vulkan.md) | Vulkan backend modules, MoltenVK, GPU-driven compute simulation |
| UI settings | [ui-settings.md](ui-settings.md) | ONE universal show/hide + resize registry for every HUD element & panel, macro + micro; global prefs file |

## Highlights

- Procedural toroidal macro-world: terrain (height/moisture/temperature),
  11 biomes (3×3 climate matrix + Water + Mountain by elevation), rivers,
  kingdoms, capitals, MST road network, dirt roads to villages, trees,
  difficulty zones.
- **Rivers are honest water cells**, not a painted overlay: generation traces
  least-cost channels (binary-heap A\* hugging climate-biome edges, gentle
  downhill bias) and carves each below sea level, so a river classifies as
  `Biome::Water` and renders through the exact sea-water path — crisp banks, no
  blue-halo overlay — and in the subworld the terrain descends smoothly into a
  naturally sub-kilometre river. Invariants locked by `river_generation_test`.
- Politik: kingdom-driven world generation with capitals, MST + extra
  inter-kingdom roads, Voronoi territory, procedural per-kingdom languages
  and heraldic flags.
- **Fields are a real peasant system, and resources come before settlement**
  (the field track, 2026-08-09). Macro: `FT_Field` farmland stamped on the
  wettest cells around villages renders as per-cell furrow patches whose
  orientation the subworld matches underfoot (`field_furrows_vertical`, the
  C++ twin of `macro.frag`'s hash). Subworld: an organic module like roads —
  furlong districts (jittered-grid Voronoi, one plough direction each),
  whole rotated parcels gated by fertility, grass balks, knee-high dry-stone
  walls (`Structure::Fence`) with honest gaps, farm lanes meeting the
  neighbouring road at the symmetric edge anchor. Wheat stands are
  harvestable props through the ONE loot registry (per-kind prop table
  `kStructureKindRows`), and every harvest — player sickle or farmer AI —
  settles the **wheat resource field**: `macro/resource_field.h` is the ONE
  registry (rows: Wheat, Fauna, Trees, Clay, Iron, Stone). The owner's
  causality law — мир → рельеф → климат → ресурсы → заселение — is built
  end to end: R2 (2026-08-13) made placement READ the resources through one
  suitability door, `macro/settlement_score.h` (weights as data, capacity
  deciding where a place stands, how many souls live there and how many
  villages a city's land feeds), and moved trees and deposits ahead of
  politics in the boot so it can. The living-fields track (same day)
  finished the unification: two storage dialects behind the one door
  (sparse scars / whole carriers, save v36–v37) and the ONE growth law —
  forest plants forest, beasts breed where beasts are, iron is born where
  scarce. Full write-up: [resources.md](resources.md).
- Seamless 3×3 subworld (3072×3072) with neighbour-aware heightmap, coastal
  sculpting, mountain amplification, biome-specific terrain shaping. Cell-boundary
  crossings are **hitch-free**: the 3×3 window re-centres via a GPU toroidal shift
  (relocate the unchanged overlap, rebuild only the 3–5 fresh cells) so a crossing
  costs O(new content), not a full 3072² re-upload — see
  [seamless-crossing.md](seamless-crossing.md).
- First-person 3D subworld rendering (sky, terrain, water, structures,
  billboards). The flat top-down 2D view is the macro map / minimap, not a
  subworld mode.
- **Honest 3D subworld simulation.** World generation is 2D (terrain heightmap +
  decorations like trees and buildings), and the seamless 3×3 window shifts in 2D,
  but **all entity simulation is full 3D** — X, Y, Z are equal coordinates:
  - Ground-walking entities are pinned to the terrain surface each tick
    (`pos.z = sample_height_m(x, y)`).
  - Flying entities own their Z through pitch-based movement.
  - Projectiles fly in 3D: spawn direction = camera `(yaw, pitch)` for the player
    or 3D aim vector for NPCs; velocity is `(vx, vy, vz)`; hit detection (sphere,
    blast, beam) is 3D distance.
  - Point lights, spell VFX (trails, impacts), NPC sprites — all render at
    `Position.z`, the entity's actual world-space altitude.
  - **One vertical authority — [`src/sub/height.h`](src/sub/height.h)**:
    `kHeightScaleM` (1500 m per 1.0 of normalised heightmap), the sea-level
    water plane `kSeaLevelM` (≈ 600 m) and the flight envelope. Flying
    entities and the flying player clamp to [terrain surface, highest
    terrain of the loaded 3×3 window + 120 m] — an absolute ceiling that can
    never sit below mountain ground (the old sea-relative ceiling pushed
    flyers underground in high massifs). Projectiles have **no** vertical
    clamp: they arc freely and die on honest terrain collision (ground
    blast at `z < sample_height_m`).
  - All distance checks (melee targeting, NPC AI chase, proximity scans, hostile
    detection, corpse interaction) use 3D Euclidean distance.
- **Universal subworld dynamic lighting (day + night).** One time-of-day scalar
  drives a single directional light: the sun by day and, at night, **whichever
  moon dominates the sky** — since the sky-submodule track (2026-08-09) the
  moons are procedural (`macro/celestial.h`: a moon lags the sun by its phase,
  so a full moon is anti-solar *emergently*, not by the old `-sunDir` decree)
  and `night_light(day, tod)` picks the lit-and-up dominant one; an
  all-new-moon night honestly goes dark to the ambient floor. One
  `lit_surface()` ([shaders/lighting.glsl](shaders/lighting.glsl)) lights
  terrain, structures and every billboard identically — and dims every direct
  sun term under the ONE drifting cloud field
  ([shaders/clouds.glsl](shaders/clouds.glsl)) the sky pass draws, so the
  cloud overhead IS the shadow underfoot. The water carries a sun/moon
  "glitter road" (лунная дорожка) that follows the actual dominant moon. The
  subworld sky itself is an isolated pure-shader submodule behind ONE door
  (`sub/sky.h` `SkyContext`): sun, 1–3 phased moons with geometric crescents,
  authored constellations, churning procedural clouds, seasonal day tint, and
  reserved weather lanes (cloudiness/wind/precip) for the future macro
  weather field. See [render.md](render.md), [celestial.md](celestial.md).
- **One celestial relief light for the 2D world map.** A single time-of-day
  bearing (`mapCelestial()` in `macro.frag`, mirroring the subworld's
  sun/moon fold: sun rises +X, sets −X; at night the slot re-points at the
  anti-solar moon at the same 0.42 gain) drives three things at once:
  - a **universal land hillshade** from the climate heightmap — long eastern
    shadows at dawn sweeping to western by evening, faint moon-shadows at
    night; normalised so flat ground keeps its exact base colour, and the
    mountain massif relief + its cast shadows re-aim off the same bearing
    (dawn/dusk throw long range shadows);
  - a **water glint + THE reflection** — micro-sparkles on every sea and
    river, plus **one physical mirror image of the light per frame**: the
    viewer is an eye above the screen centre and the map is the mirror, so
    the sun/moon's image sits offset along its azimuth by
    `eyeHeight·tan(zenith)` — near the centre at noon, sliding toward the
    horizon side at dawn/dusk — zoom-invariant (viewport-relative), drawn
    only where that point actually is water, gold at sunset, a cool moon
    road at night (added after the night darkening);
  - danger zones read as a **wispy crimson haze** (air, not ground): zone
    data sampled bilinearly across cells and broken into drifting procedural
    patches, identical over land and water, deliberately thin — the player
    senses the country differs without being told exactly how (replaced the
    flat tint that muddied land and stained water brown);
  - the data-driven **night glow**: a baked per-cell light field turns every
    settlement, village and active spire into a population-scaled warm glow
    that spreads over open ground and along roads, is smothered by forest,
    and is **walled off by elevation** — the bake's Dijkstra pays a climb
    cost per unit of rise (downhill free), so bare massifs occlude glow from
    the same heightmap the day hillshade reads. One director knob
    (`kMacroGlowGain`) sets master brightness; adding a glowing landmark type
    is one data column. Baked on world-change only — never per frame. See
    [macro-lighting.md](macro-lighting.md).
- **One faction registry** (`macro/faction.h`): every faction — kingdoms
  included — is one data row (id, name, colour, temperament, player-reputation
  seed). Relations come from a temperament×temperament band matrix plus authored
  pair overrides; `NPCKind.factionIdx` indexes the registry for humanoids and
  monsters alike. Adding a faction is one row — its relations to everyone,
  reputation seed and UI identity all follow. Settlements fight under their
  kingdom's faction. Locked by `faction_relations_test`.
- **Mass battles on one algorithm** (`sub/battle.{h,cpp}`): O(N) steering for
  one bandit or 16 384 bodies through the same code — per-tick interned faction
  hostility masks, a per-faction influence field with a comrade alert chain
  (rear ranks charge because the front saw the enemy; scattered animals don't
  swarm), dual bucket grids sized from the crowd's own body/weapon data,
  separation + engagement ring + acceleration limit (no collapse into a point),
  and terrain as data (`kTileMovementSpeed` per ground type; slopes cost speed
  and only un-walkable grades steer). The influence field is read as a FLOW all
  the way to weapon contact; on the last mile (site within one field cell) a
  body rescans the pick grid for its OWN nearest enemy instead of homing on the
  shared site point — the fix for the "всасываются в точки" clumping, where
  every victor within 32 units funnelled onto each surviving enemy pocket
  (mean-neighbours-within-2 spiked 3.6 → 11). The flow bearing itself aims at
  the nearest point of the seed cell's REGION (its bounds), not at the site
  point: a solid enemy front is otherwise a lattice of one centroid per
  32-unit cell, and steering at lattice points sheared a `test_battle 8192`
  army into ~4 self-reinforcing lanes that fought as 4 separate knots — a
  facing column now has zero lateral pull, so a wide front meets as one wall
  and only true flanks curl inward. ~3–5 ms/tick at 16k bodies; the O(N) bound
  (one shared visit budget covers the rescan), the no-collapse/no-implosion
  invariants, the line-holds-through-attrition invariant and the
  wide-front-one-wall invariant (8192/side with the engine kill model, empty
  frontage gap < 3 bins vs 5 red on the lattice read) are *measured* by
  `battle_ai_test`, with negative controls that reproduce each shipped bug.
- **Entry-side context** (`macro/entry_context.h`): every macro walker — the
  player and any NPC alike — carries two bytes: the packed signed step of its
  last macro cell change (`MacroNpcRuntime.entryDir`, stamped by the one
  `try_move` path; `PlayerState::entryDir`, stamped on the macro-walk cell
  crossing, **`kSaveVersion` 14→15**) and a saturating count of AI ticks since
  (`entryTicks`). On subworld enter the spawn position derives from a
  *reachable band*: uniform between the entry edge and how far the walker could
  have walked since entering, so an army that chased another across a border
  materialises at that border behind it, while a local that has been in the
  cell forever degrades to exactly the old whole-cell scatter — no special
  case, no actor-kind branch anywhere. Consumed by `SubworldEngine::enter`
  (player, deterministic mid-band; sentinel/saturation = the old 1536,1536
  centre) and `project_macro_npcs_into_subworld` (per-NPC band + water dodge).
  Pinned by `entry_context_test` (pack/unpack, band geometry, uniform
  degradation), the v15 `save_roundtrip_test` fields, and the
  `TIMAERT_SMOKE_ENTRYDIR="dx,dy,ticks"` app-smoke hook, which asserts the
  spawn lands in the stamped side's quarter of the centre cell end-to-end.
- **Friendly fire is real**: projectiles and spells are faction-agnostic — they
  strike whoever stands in their path, ally or enemy (owner design decision;
  the old same-faction shield is deleted).
- Universal combat on one curve: humanoids and the player derive ECS
  `Health`/`Combat` from a shared `CharacterSheet` via `project_combat` (the
  per-role `CombatTemplate` is the authored base — HP/damage floor + attack
  identity); monsters stay sheet-less on the raw `FaunaEntry` row. One engine
  for player / NPCs / soldiers / bandits. Faction-driven hostility. **All combat
  is 3D** — melee range, projectile trajectories, spell blasts, NPC missile aim,
  and hit detection operate in full XYZ space.
- The player is **an NPC with a flag**: a single `PlayerTag` component rides an
  ordinary ECS body (a minimal marker on the overworld, a full combat actor in a
  subworld), with one system-wide invariant — exactly one `PlayerTag` at all
  times. *Possession* (вселение) moves that one flag onto a body you look at
  (subworld reticle, keybind **V**) or name (`control <id>`); the body fights on
  its OWN character sheet (possess a lord ⇒ strong, a rat ⇒ weak) and the one you
  left reverts to a normal NPC. Overworld NPCs are projected into the subworld as
  real bodies where they actually stand, and leaving *as* a possessed body lands
  you on its overworld cell **as that NPC** — an identity that survives save/load
  (a deterministic spawn ordinal, since the ECS is regenerated each boot). No
  player special-case in any universal path. See [possession.md](possession.md).
- One global monster table + one loot table (single sources of truth): every
  creature (rabbit → dragon) is one `FaunaEntry` row with a stable id; every
  drop — monster or NPC — resolves through one `roll_loot_profile(lootId, …)`.
  Console `spawn <id>` spawns any creature; adding content is one data row.
- Event bus + logic nodes + procedural quests (data-driven objective and
  reward registries — adding a verb = one entry). Active quests project onto the
  world map as gold "!" pins — a *derived* overlay on the universal marker layer
  (one pin per incomplete world-anchored objective, all targets of every quest),
  rebuilt only when the quest set changes and toggled/scaled from UI settings.
- Modular spell system: spell book, cooldowns, mana regen.
- ImGui debug HUD + Diplomacy / Settlement / Quest / Codex / Map overlays.
- One universal UI settings registry (macro + micro): every HUD element and
  pop-up panel toggles on/off and resizes from a single "Interface" pause-menu
  panel, and the choices persist in a global `ui_prefs.cfg`. Adding an element
  is one enum + one table row — no per-world or per-widget code. See
  [ui-settings.md](ui-settings.md).

## Build

The canonical C++ project is `timaert_c/`. Commands below assume you are
inside that directory.

### Windows / MSVC

Known-good local build tree: `build-msvc` (Ninja, Debug). Build it from a
Visual Studio developer environment:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
```

Launch:

```cmd
.\build-msvc\timaert.exe
```

The repo uses SDL2 plus SDL2_mixer. The current Windows CMake cache points at
an SDL2 2.x development package (`SDL2_DIR=...\SDL2-2.32.10\cmake`) and must
also resolve SDL2_mixer with MP3 support. An SDL3 zip is not valid for this
repo: CMake calls `find_package(SDL2 REQUIRED)`, probes SDL2_mixer, and links
`SDL2::SDL2` / `SDL2::SDL2main` plus the discovered mixer target.

If `build-msvc` must be regenerated on this machine, use SDL2 and SDL2_mixer,
not SDL3. With vcpkg, install `sdl2-mixer:x64-windows` or set
`SDL2_mixer_DIR` to the package config directory:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSDL2_DIR=C:\dev\SDL2-devel-2.32.10-VC\SDL2-2.32.10\cmake && cmake --build build-msvc"
```

### Portable Native

Non-Windows builds still use the normal CMake/Ninja flow when SDL2 and
SDL2_mixer are available through the system package manager:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/timaert
```

> **No browser build.** The WebAssembly / Emscripten target has been dropped
> (the core goal is a compute-shader workload GL/WebGL2 cannot express). There
> is no `emcmake` flow; the CMake `project()` name is `timaert`, so there is no
> `samosbor.*` or `timaert.html` artifact. Stale `EMSCRIPTEN` guards remain in
> `CMakeLists.txt` and should be pruned.

## Dependencies

- C++23 compiler (Clang 17+ / GCC 13+ / AppleClang 15+)
- CMake 3.16+
- Ninja
- **Vulkan SDK** — headers + loader + `glslc` (GLSL→SPIR-V). CMake calls
  `find_package(Vulkan REQUIRED)` and compiles every shader with `glslc`.
- **MoltenVK** on macOS (Vulkan-on-Metal; provided by the Vulkan SDK / Homebrew
  `molten-vk`).
- SDL2 (system package) — **window, input, timing, audio only; never the
  graphics API.**
- SDL2_mixer with MP3 support (native builds hard-fail if missing)
- EnTT 3.14.0 (FetchContent — no install)
- Dear ImGui 1.91.5 (FetchContent — no install; the **Vulkan** ImGui backend
  `imgui_impl_vulkan` + `imgui_impl_sdl2`)

> The OpenGL 3.2 Core / WebGL2 / GLES3 / Emscripten dependencies listed in
> earlier revisions are **removed**: there is no GL or WASM code left in `src/`
> (0 `gl*` call sites, 0 `emscripten` references), the ImGui backend is
> Vulkan, and the browser target is dropped.

### macOS (Homebrew)

```bash
brew install cmake ninja sdl2 sdl2_mixer molten-vk vulkan-headers vulkan-loader shaderc
```

`molten-vk` provides the Vulkan-on-Metal ICD; `shaderc` provides `glslc`. At
runtime the loader finds MoltenVK via `VK_ICD_FILENAMES`
(`/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json`).

### Ubuntu / Debian

```bash
sudo apt install cmake ninja-build libsdl2-dev libsdl2-mixer-dev \
                 vulkan-tools libvulkan-dev vulkan-validationlayers glslc
```

## Integration Ledger

Windows/MSVC build evidence dates to 2026-05-15; the logic-test suite was last
re-verified **43/43 green on macOS 2026-08-05** (`ctest --test-dir build`, from a
clean reconfigure).
Windows/MSVC evidence is a build and smoke verification target only. Gameplay
behavior authority remains the TypeScript/Svelte source under `C:\Timaert\src`.

Known-good Windows verification command:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
```

Launch path:

```cmd
.\build-msvc\timaert.exe
```

### Build / Platform Enablement

| Change area | Status | Evidence |
|-------------|--------|----------|
| Windows/MSVC build | VERIFIED | Canonical `build-msvc` command passes as of 2026-05-15 and links `timaert.exe` plus the executable test targets. This is not gameplay parity evidence. |
| SDL stack | VERIFIED | CMake requires SDL2 and native SDL2_mixer with MP3 support; SDL3 is invalid for this repo. |
| No exceptions / no RTTI | VERIFIED BY BUILD FLAGS | CMake applies `/GR- /EHs-c-` on MSVC and `-fno-rtti -fno-exceptions` elsewhere. |
| Runtime smoke artifacts | LOCAL ONLY | Root `runtime_*`, `smoke_*`, and `save.bin` artifacts belong under ignored `artifacts/runtime-smoke/`; `.gitignore` also keeps legacy root patterns ignored. |

### Test / Smoke Infrastructure

| Flow | Status | Evidence |
|------|--------|----------|
| Title / New Game / macro walking | VERIFIED | Existing root artifacts were archived under `artifacts/runtime-smoke/`; representative proofs include `runtime_title*.png`, `runtime_boot_final.err`, `runtime_playing_newgame.png`, and `runtime_playing_after_w.png`. |
| Load screen and GUI save/load | VERIFIED | `save_roundtrip_test` passes on schema **v12** (11→12: faction registry unification) (8→9 for the mountains→biome refactor, then 9→10 for the possessed-identity ordinal `PlayerState::possessedMacroSpawnId`, Inc 5e-2 — no back-compat, old v8/v9 saves hard-rejected by the version gate); native smoke `new_game,wait_boot_done,save_game,open_load,load_game,wait_boot_done,quit` (seed 12345) passed `[smoke] PASS` with a 51733-byte v10 slot. |
| Settlement trade / quests | VERIFIED | `runtime_settlement_*`, `runtime_settlement_trade_*`, `runtime_quest_accept_*`; procedural quest lifecycle is covered by `quest_lifecycle_test`. |
| NPC panel / trade / attack | VERIFIED | `smoke_04_ui.png`, `smoke_07_ui.png`, `smoke_10_attack_ui.png`; smoke script routes selected macro NPCs into subworld combat. |
| Character paper-doll | VERIFIED | `character_paperdoll_test` and boot smoke load `atlas.bin` / `atlas.png` once. |
| Spell book / casting | VERIFIED | `spell_casting_effects_test`; smoke opens Spells tab, casts projectile spell, toggles Haste, and toggles Flight pathing. |
| Subworld time / combat handoff | VERIFIED | `subworld_time` smoke passes on seed 42; combined `trigger_battle_start,subworld_time` smoke passes and checks death XP flush plus subworld entity cleanup. |
| NPC-as-soldier / loot / exit gate | VERIFIED | `combat_squad_test` covers concrete NPC-kind soldiers, hire price/upkeep, garrison generation, and squad projection. Seed-42 app smoke `subworld_exit_gate,subworld_loot_xp` proves zone-9 exit blocking, corpse interaction, XP attribution (`0->25`), and inventory loot transfer (`misc_gem 0->2`). |
| Subworld spawn parity | VERIFIED | `subworld_spawn_parity_test` locks TS-fauna count/placement from `roll_fauna`, the shared RNG stream after table rolling, `baseLevel + floor(rng()*2)`, 15% per-level HP/damage scaling, zone context multipliers, sprite tint/type IDs, AI mode, and all-water squad placement fail-closed. It also now locks the possession-era paths: settlement city projection, carry-across re-centre persistence, re-entry determinism, macro→subworld NPC projection (`MacroOrigin` backlinks, data-driven hostility, reaper-spared), and the exit-remap query (origin cell / torus wrap / no-backlink fallback / null / degenerate dims / stale-handle-after-reap no-crash), and the identity remap (adopt moves the flag onto the origin + returns its spawn ordinal; reattach re-finds it by ordinal on a fresh identically-seeded world; ordinal/id negative paths no-op). Latest direct run: `fauna=3 seed=610795520 zone=5 water_squad_blocked=1 city_projection=1 carry_across=1 reentry_determinism=1 macro_projection=1 exit_remap=1 identity_remap=1`. |
| Player as entity / possession | VERIFIED | The player is a single movable `ecs::PlayerTag` flag (minimal `Position + PlayerTag` on the overworld via `ensure_macro_player_entity`; a full `Health + Combat + BodyRadius + SubworldTag` combat actor in a subworld), invariant **exactly one PlayerTag at all times**. Subworld player takes/deals damage + casts through the universal paths (Inc 4a–4d; entity `Position` authoritative post-5a). `possess_entity` moves the one flag onto an aimed body (`aim_target` cone, keybind **V** / console `possess` / `possess_by_id`); combat is **body-native** (the inhabited body's own `CharacterSheet`, `gs.player` preserved as revert target). `project_macro_npcs_into_subworld` materialises ±1-cell macro NPCs as combat bodies with a runtime `MacroOrigin` backlink; `leave()` remaps the macro player onto the possessed body's origin cell **and** hands over its identity — `adopt_possessed_macro_as_player` moves the one flag onto the origin macro NPC itself, so you exit *as* the lord (Inc 5e-2). That identity is save-stable via a deterministic spawn ordinal `ecs::MacroSpawnId` (stamped by the sole `make_npc` path, never serialized itself); only the chosen ordinal persists, in `PlayerState::possessedMacroSpawnId` (**`kSaveVersion` 9→10**), and `reattach_player_to_macro_spawn` re-finds the regenerated NPC by ordinal on load. Proven by `subworld_spawn_parity_test` (above, incl. `identity_remap=1`) + the `player_entity` / `macro_player_entity` (macro→sub→macro `PlayerTag=1 not_npc=1`) / `possess` / `subworld_exit_remap` (`onOrigin=1 off_centre=1` + `adopt tags=1 on_macro_npc=1 rides_origin=1 spawnId=3`) console smokes + the GUI save/load round-trip (`51733`-byte v10 slot, `[smoke] PASS`); 28/28 ctest green; validated seed-12345 smokes `[smoke] PASS`, `validation=1`, only the benign 05137 teardown leak. See [possession.md](possession.md). |
| ShowDialog / ShowStory | VERIFIED | `draw_show_dialog`, `draw_story_overlay`, `trigger_level_dialog`, `trigger_count_only_dialog`, `trigger_story_overlay`, and `complete_story_overlay` are wired; `quest_lifecycle_test` covers `ShowDialog` and `ShowStory` payloads. Dialog `nodeId` choices route through app-layer logic activation. |
| Feature layer / pathfinding guards | VERIFIED | `feature_layer_parity_test` and `pathfinding_parity_test` pass; malformed feature storage fails closed and pathfinding semantics remain locked. Mountains are no longer a `FeatureType` (see the *Mountains as a biome* row); the feature enum is now `FT_None / FT_Road / FT_Tree / FT_DirtRoad`. |
| Mountains as a biome | VERIFIED | `FT_Mountain` removed and reborn as the elevation-classified `Biome::Mountain` (id 10; land at height ≥ `kMountainBiomeLevel` `0.75f`). One `biome_at()` CPU classifier mirrors the shader's `bt_biome`, so mountain↔forest / mountain↔road borders are clean iso-height boundaries and trees/roads compose *on top* of the massif base. `FT_DirtRoad` renumbered 4→3; `kSaveVersion` bumped 8→9. `biome_classifier_test` locks the Water/Mountain/climate cascade; `pathfinding_parity_test`, `feature_layer_parity_test`, and `subworld_generator_parity_test` were re-pinned to biome semantics. Build green (zero warnings), 28/28 CTest. Validated seed-12345 `subworld_enter` smoke over a Mountain cell confirmed Mountain-mode terrain shaping + fauna select via biome and no OOB on `kConfigs[11]`. See [biomes.md](biomes.md), [features.md](features.md). |
| Forests are a COUNT, not a feature | VERIFIED | Owner follow-up to the tree-count layer ("слишком густо" + "откажемся от фичи леса"): `FT_Tree` removed from the feature enum (`FT_DirtRoad` 3 → 2, stale byte 3 fails closed; features now carry man-made structures only), and the layer derives from the `spawn_trees` FBM **massif mask** (the original organic лесные массивы) + small biome ambience capped UNDER the map-sprite threshold — so the map draws massifs again, not biome carpets. All former FT_Tree consumers ported to the count: subworld Forest mode + forest fauna via `kForestClassTreeCount` (8192 = 2^13, `is_forest_cell`), macro path/travel cost (forest drag 3.0, roads still win), night-glow canopy occlusion as continuous `kCanopyOpticalCost·density` (full forest = the old 2.5×), danger-zone forest boost scaled by density, tooltip "Forest (N trees)" from the live layer. `kSaveVersion` 13→14 (derived world changed). Verified: build zero-error, ctest **39/39** (feature_layer_parity re-pinned road-only + byte-3 renumber guard; macro_lighting forest occlusion via density; generator parity via `nbTreeCount`; pathfinding forest-weight rows), validated seed-12345 console smoke `chop cell=122,143 trees=14814->14813 override_recorded=1` `[smoke] PASS` exit 0 (benign 05137 only), macro map capture shows organic massifs with clearings. |
| Tree-count layer (contextual cells) | VERIFIED | Every macro cell carries a scalar tree count (`macro/tree_layer.h`, golden max `kMaxTreesPerCell = 16384` = FT_Tree cell with 8 forested neighbours; derivation `biomeBase + 16384·forest₃ₓ₃/9`, smooth by construction). ONE authority, three consumers: `u_treeMap` (binding 5) drives the density-based map tree sprite; `scatter_universal_trees` derives subworld tree rates from the neighbours' counts (`kTreeScatterYield`-calibrated so placed ≈ count, bilinear ring blend keeps seams smooth); felling a tree (no-target melee swing / console `chop`) removes the Structure from cell+composite and decrements the owning macro cell through `set_tree_count` — persisted as sparse `GameState.treeOverrides` (**`kSaveVersion` 12→13**), re-applied over the derived layer on load, delta-only so untouched visits never drift counts. Verified: `tree_layer_test` (formula/torus/clamp/override round-trip/scatter calibration, ratios 1.018 & 0.996), `save_roundtrip_test` v13 overrides, console-smoke `chop cell=122,143 trees=11273->11272 override_recorded=1` at seed 12345 with `validation=1`, `[smoke] PASS`, exit 0, only the benign 05137 teardown leak; ctest 39/39. See [features.md](features.md). *(Superseded 2026-08-13: the forest is now the Trees CARRIER row of the resource-field registry — the grid rides the save whole (v36), `treeOverrides` died, `u_treeMap` = binding 4; see [resources.md](resources.md).)* |
| Road / river invariants | VERIFIED | `road_river_generation_test` enforces rejected-water pruning for surviving Politik road connections. **Rivers are now honest water cells** (owner intent: *«честно как воды клеточки»* — so there is no river-coastline problem in-game): `generate_river_data` traces least-cost channels with a binary-heap **A\*** (heuristic = BFS steps-to-sea `waterDist`, consistent ⇒ first pop optimal + lazy-deletion valid; ties break on cell index for MSVC/libc++ determinism) that hugs the Voronoi climate-biome edges with a `kRiverClimbShift` downhill bias, then **carves each cell below sea level** (`carveH=94` < `seaLevel8=102`) so `bt_biome()` classifies it `Biome::Water`. The translucent `riverOverlay()`/`riverVisualValue()` shader path (a 9-tap MAX dilation) is **deleted** from `macro.frag` — this fixed the *blue-halo bank bug* — so a river now renders through the identical sea-water path (crisp banks, no halo); in the subworld the `base_generator` remap + bilinear blend + `kLandMargin` make terrain descend smoothly land→water *within* the cell, a naturally sub-km river with **no width knob**. `river_generation_test` (CTest-registered) locks determinism, drains-to-sea, no-cell-above-sea, the `maxrun < 120` anomaly bound (negative control: the pre-heap baseline hit **494**), a sane density band, fail-closed, and the honest submerged descent (bed below `WATER_LEVEL`, adjacent land dry, cliff-free monotone). Standalone compile+run green under the canonical flags; the from-scratch `cmake` build + in-game glance are the usual GPU/network-gated human sign-off. Rivers regenerate on load (not persisted). See [macroworld.md](macroworld.md) § Rivers, [biomes.md](biomes.md). |
| Subworld height system / flight envelope | VERIFIED | ONE vertical authority `src/sub/height.h` (`kHeightScaleM`, `kSeaLevelM`, flight margin); renderer no longer owns the scale. Flight clamps to [terrain, window-max terrain + 120 m] (`Renderer3DVk::max_height_m`, refreshed on every height upload/shift) — fixed the flying-player-underground bug in mountains (old ceiling was sea + 120 m ≈ 720 m, below mountain ground). Projectiles have no vertical clamp (terrain collision only). Mountain slope rebalance (p50 43°→31°, p90 70°→49°) + micro-crag octave; universal `TerrainMod` flattening under roads/settlements (city on a level walled plateau, seam selfcheck `mismatch=0`); alpine treeline 0.72→0.92 + 35° tree slope rule. Verified: seed-12345/42 mountain captures (walk + flight), seam smoke selfcheck clean, ctest **37/37** green (`mountain_mesh_smoothness_test` re-pinned for the approved look). |
| Async subworld seam / water plane | VERIFIED | `subworld_async_seam_test` covers axis, diagonal, reversal, snapshot, placeholder, saved-restore, saved-structure, sparse road-mask proofs, and the 3x3 water-plane invariant. Latest focused run: `roadGen=31.578ms`, `plainGen=23.261ms`, `diagonalGen=29.785ms`, `reversalGen=24.892ms`, `smooth=0.000ms`; water scan reported `water=3145728`, `land=6291456`, `badWater=0`, `badLand=0`, `maxWater=0.40000`, `minLand=0.42000`. `subworld_seam` app smoke crosses a real 3D seam; latest freshly rebuilt Debug timing was `gen=38.989ms upload3d=118.795ms upload2d=0.000ms total=157.938ms`, while the best accepted 1024-mask Debug timing remains `gen=22.695ms upload3d=51.785ms upload2d=0.000ms total=74.603ms`; terrain-payload shader-grid and GL sub-update trials were measured and rejected. |
| Seamless crossing (no hitch) | VERIFIED | Cell-boundary crossings re-centre the 3×3 window with no perceptible frame (confirmed in-game) and as **O(new content)**: a GPU toroidal shift relocates the unchanged overlap and rebuilds only the 3–5 fresh cells. Validated smoke `new_game,wait_boot_done,subworld_seam,quit` (seed 12345, `validation=1`, `TIMAERT_SEAM_SELFCHECK=1`, `TIMAERT_SEAM_SETTLE_MS=15`) crossed a real seam (`center 122,143->123,143`) and printed `[smoke] PASS`, exit 0, with all self-checks clean: `material shift mismatch=0` (GPU-readback vs from-scratch recompute), `height incremental mismatch=0/37249 maxdiff=8.5e-4` (FP tolerance — the TU is `-ffast-math`), `material incremental mismatch=0`; the only validation finding is the pre-existing benign teardown leak (VUID-vkDestroyDevice-device-05137). Shipping-path crossing `upload3d` fell 11.2ms → 6.5ms. Full design + gotchas: [seamless-crossing.md](seamless-crossing.md). |
| Audio | VERIFIED | `audio_contract_test` and `audio_runtime_test` cover SDL_mixer metadata, dummy-driver decode/play/stop, and one-time asset loading. Dedicated `new_game,wait_boot_done,subworld_audio,quit` smoke passed on seed 42 with the SDL dummy audio driver, proving `explore -> subworld -> explore` music transitions. |
| Global monster table + unified loot | VERIFIED | The 19-row `FaunaEntry` catalog is now a global monster registry with stable ids (`creature_catalog` / `creature_def` / `creature_def_from_kind`); the subworld bakes `NPCKind.type = 0x100 \| catalogIndex`. All death-path drops (NPC + monster) route through one `roll_loot_profile(lootId, …)` registry (8 NPC roles + wildlife/demons/bandits faction defaults); `spawn_hostile_npc` resolves any creature id or NPC role; `FaunaEntry.xpReward` gives per-creature XP. Validated seed-12345 smoke `new_game,wait_boot_done,console,subworld_loot_xp,subworld_time,quit` → `[smoke] PASS`, exit 0, `validation=1`, `spawned_creatures=1`, `subworld_loot_xp exp=0->25 misc_gem=0->2`. Defaults are behavior-preserving; see [monsters.md](monsters.md). |
| Macro celestial relief light + water glint | VERIFIED | ONE `mapCelestial()` bearing in `macro.frag` (sun +X→−X, anti-solar moon at 0.42 — mirrors `sub/lighting.h`) drives the universal land hillshade (flat-ground-invariant, mountains + cast shadows re-aimed, shadow length follows light elevation), the water sparkle glint (spreads at grazing light, warm at sunset / cool moon road at night, added after night darkening), and the night-glow bake's NEW elevation occlusion (increment C: uphill Dijkstra steps cost `kGlowClimbCost`·rise, downhill free — bare massifs wall glow off; flat heights byte-identical to the heights-free bake). `macro_lighting_test` extended (ridge-blocks / valley-spills / flat≡no-heights), ALL PASS; seed-12345 map captures at 07/13/17/18/23 show the E→W shadow sweep, sea sparkle at sunset, and night town pools dying at the massif wall; `TIMAERT_SMOKE_HOUR`/`TIMAERT_SMOKE_MACROPOS` now work for bare macro `capture_frame` (mutations staged one frame before arming — the stale-frame rule). ctest **38/38**; validated smoke PASS (benign 05137 only). |
| Macroworld night lighting | VERIFIED (unit); in-game pending | `macro_lighting_test` (CTest-registered) locks radial + terrain-occluded falloff, torus wrap, colour fidelity, the `kMacroGlowGain` **anti-saturation lock** (a lone city core encodes `< 128` — the regression guard for the "cities blow out to white" bug), stacked-clamp, and forest solid-block occlusion. `upload_light_field` (surgical binding-4 re-upload) compiles clean in isolation. Full end-to-end/in-game verification is pending an in-game pass: the mountains→biome refactor that shared `main.cpp` has now landed (binary links, all 28 CTest targets green), so this row is unblocked. See [macro-lighting.md](macro-lighting.md). |
| Universal UI settings (macro + micro) | VERIFIED | One `kUiElementSpec` registry drives one **Interface** panel (Esc → Interface), one global `ui_prefs.cfg` (its own `# … v1` header, independent of `save.bin`/`kSaveVersion`), and per-element visibility/scale honoured at every HUD/panel call-site. `ui_settings_test` (CTest-registered) covers spec-seeded defaults, the forgiving text-KV load/save round-trip, unknown-key / comment / partial-line tolerance, scale clamping, non-scalable handling, and `reset_defaults()`. Validated seed-12345 smoke `new_game,wait_boot_done,subworld_time,quit` → `[smoke] PASS`, exit 0, `validation=1`, exercising the gated + scaled subworld HUD path. Opening Interface releases subworld mouse-capture through the shared `gameplay_panel_open` predicate so the cursor stays clickable. See [ui-settings.md](ui-settings.md). |
| THE time ladder (one integer tick) | VERIFIED | The world runs on ONE integer quantum. `core/time.h` owns the ladder — 64 ticks = 1 real second (the fixed simulation step), **8192 = a game day** (2^13, 128 real seconds), 32 days = a season, **128 days = a year = 2^20 ticks exactly**. The four old rhythms (frame `dt`, a float minute accumulator, a 0.5 s AI cadence, a per-DRAWN-FRAME daily queue) are gone. The minute is NEVER STORED: `1440/8192 = 45/256` and `24/8192 = 3/1024` make `minute = (t*45)>>8` and `hour = (t*3)>>10` exact integer arithmetic, and `floor(floor(a/b)/c) == floor(a/(b*c))` makes the two derivations unable to disagree. **THE TICK IS PRIMARY:** one turn of the main loop is one tick AND one drawn frame, so the frame rate and the world's rate are the same number — a low frame rate is not a choppier picture of a world moving at its usual pace, it is the world living slower. The wall clock is consulted for a single purpose — if the turn was quicker than a tick is worth, WAIT out the remainder so the world can never run faster than nominal (the swapchain prefers MAILBOX for the same reason: FIFO would let a 60 Hz display cap the world at 60 ticks/s). It is never consulted to decide that ticks are OWED, so there is no accumulator and no debt: a slow turn is one late tick, a machine that cannot sustain the rate runs a slower world rather than a shorter one, and a SUSPENDED process (closed laptop, breakpoint) ran no turns, advanced no ticks, and simply carries on — no gap to detect, no threshold to tune. Real time can only make the game WAIT; it can neither add a tick nor take one away. Subworld physics, AI and projectiles became deterministic without one file under `sub/` changing. Underground `kSubworldTickDivisor = 16` steps buy one tick (a game hour costs 85 real seconds) and the whole macro world, macro AI included (`kAiTicks = 32` world ticks), slows with it: 24186 → 852 NPC thinks per 1000 steps. EVERY rate is game-time denominated (march 32 cells/game hour, recovery 10/game hour in BOTH worlds since 2026-08-20 — a body standing still underground mends at the same rate per game hour, which is sixteen times slower by the wall clock), so day length is FEEL and cannot move the economy. The subworld has its own denominator for the things you LIVE through rather than plan: `kSubworldWalkTilesPerSecond`, and the combat/spell cooldowns and sustained drain, which are counted in integer simulation STEPS (`kStepsPerSecond`) — see [time.md](time.md) for why literal ticks were measured and rejected there. `kSaveVersion` 17→18 — three ints became one `uint64`, a tick number rather than a duration. `time_ladder_test` walks all 8192 ticks of a day; `world_tick_parity_test` proves **no drift** (10000 one-tick advances == one 10000-tick advance: same instant, same elapsed, same daily queue) and the subworld divisor keeping its remainder across a split. Two accepted consequences: the march costs 28% more stamina per game day, and 0 HP now means dead reliably (the coarse pre-tick frame used to let a player at zero round back to 1). ctest 43/43; smokes PASS. See [time.md](time.md). |
| Seam crossing, second pass | VERIFIED | Crossing frame **~9-11 ms → 6-8 ms**, its `upload3d` half **6.9-8.4 → 3.0-5.0 ms**; owner confirmed in play that the sub-freeze is no longer felt. One law, four consumers: **do not integrate a constant** — a placeholder cell is one height and one tile id, so the height path fills its interior vertex block (3.10 → 0.19 ms) and the material path memsets it, or picks between two bytes inside the treeline dither band (up to 19.7 → 2.60 ms in a mountainous world). A pass has **two radii** — input reach 92 tiles, output reach 1 — and dirty-marking wants the second (road smooth 3.0-3.2 → 2.0-2.5 ms). Instance buffers are **reused, not re-created** (the CPU loop over 10896 trees is 0.08 ms; 87-97% was buffer churn). Plus a BUG fixed: every building in view changed shade at a crossing, because the per-instance hash was keyed to the composite coordinate — now `structure_shade(absX, absY)`, proven by `material_seam_test` invariant 7 **with a negative control**. Verified by `TIMAERT_SEAM_SELFCHECK` on five pinned seeds: height incremental `0/37249`, material incremental `0`, material shift `0` (GPU readback), flat cells `tileMismatch=0/1048576 valMismatch=0`. Deliberately NOT done: batching the per-resource `vkQueueWaitIdle` (0.39-0.60 ms each, size-independent, 4 per upload) — worth ~1.25x, not free. See [seamless-crossing.md](seamless-crossing.md) and problems.md entries 14-15. |
| Universal rebindable keymap | VERIFIED | One `kActionSpec` registry ([src/ui/keymap.h](src/ui/keymap.h)) drives every game key across both worlds: scancode-based (layout-proof) bindings with `UiScope` Macro/Sub/Both, a **Controls** panel (menu → Controls, press-to-rebind, Esc fixed as menu/cancel), a global `keymap.cfg` (same forgiving text-KV idiom as `ui_prefs.cfg`), and the one-key-one-meaning-per-world steal rule. Consumers: the `handle_event_playing` dispatch and the `poll_movement` held-key polls read `Keymap::get`; toolbar tooltips and the pause badge quote the LIVE binding so no hint can go stale (the hardcoded hint bar died with this). `keymap_test` (CTest) covers defaults / steal / scopes / round-trip / tolerance; seed-12345 smokes green. See [controls.md](controls.md). |
| Quest markers (macro "!" pins) | VERIFIED | Active quests project onto the universal `markers.h` layer as gold "!" pins — `rebuild_quest_markers` adds one `MarkerStyle::Quest` pin per incomplete world-anchored objective (cell resolver mirrors `eval_objective`; a `destroy_npc` kill-count has no fixed cell so it gets none), for **all** targets of every active quest. `QuestEngine` stays pure; the allocating rebuild is gated by a per-frame integer `quest_marker_signature` in `process_world_events` (cache reset on new-game/load, which also reconciles stale pins from a save). Rendered by the universal by-style pass in `draw_macro_overlay`, gated + scaled by the new **QuestMarkers** UI element. Validated seed-12345 smoke `new_game,wait_boot_done,console,subworld_time,quit` → `[smoke] PASS`, exit 0, `validation=1`, asserting `quest_markers pin@42,17 style=quest killcount=nopin complete->removed sig_changed=1`; 28/28 CTest targets green (incl. `quest_lifecycle_test`, `ui_settings_test`, `biome_classifier_test`). See [quests.md](quests.md). |

The **43 CTest-registered** logic-test targets (run under `ctest --test-dir
build --output-on-failure`, verified **43/43 green** on 2026-08-05 from a clean
reconfigure) are:

- `time_ladder_test` — THE time ladder: the calendar derived exactly from one
  integer tick ([time.md](time.md))
- `quest_lifecycle_test`
- `world_tick_parity_test` — clock rollover, **no drift**, subworld divisor
- `player_recovery_parity_test`
- `save_roundtrip_test`
- `spell_casting_effects_test`
- `combat_squad_test`
- `audio_contract_test`
- `audio_runtime_test`
- `npc_spawn_contract_test`
- `macro_npc_ai_parity_test`
- `macro_travel_parity_test`
- `item_use_parity_test`
- `pathfinding_parity_test`
- `character_paperdoll_test`
- `road_river_generation_test`
- `river_generation_test` — river-gen invariants + honest subworld descent (verified green under `ctest` from a clean reconfigure, 2026-07-29)
- `biome_classifier_test` — mountains-as-biome Water/Mountain/climate cascade
- `tree_layer_test` — per-cell tree counts: derivation formula (16384 golden
  cap), torus build, override clamps/round-trip, scatter calibration
- `feature_layer_parity_test`
- `subworld_generator_parity_test`
- `subworld_async_seam_test`
- `subworld_spawn_parity_test`
- `rpg_loot_test`
- `targeting_test`
- `fauna_registry_test`
- `ui_settings_test`
- `keymap_test` — the ONE rebindable keymap: spec-seeded defaults, the
  one-key-one-meaning-per-world steal rule, scope gating, forgiving KV
  round-trip, reset_defaults
- `macro_lighting_test`
- `faction_relations_test` — the ONE faction registry: unique ids, kingdoms
  resolve to rows, symmetric temperament bands, pair overrides, reputation seed
- `battle_ai_test` — mass-battle steering: measured O(N) bound, no-collapse +
  no-implosion (negative controls), alert chain, terrain table, determinism
- `structure_collide_test` — body-vs-structure solidity (sub/collide.h):
  blocking / support / lintel z-layering / oriented boxes / cylinders / slide /
  escape rule, plus a generated-city functional pass (gates BFS-walkable, walls
  block, wall tops standable)
- `seasons_test` — data-driven seasons derived purely from world time

The `enable_testing()` / `foreach` block at the tail of `CMakeLists.txt` is the
source of truth for which targets run under `ctest` (the list above is a
snapshot of it). The GPU/display harnesses (`gpu_smoke`, `gpu_smoke3d`) and
`timaert` itself need a GPU/display and are intentionally not registered.

### Current Gaps

- Standalone full-screen `TradeOverlay.svelte` shell is intentionally not
  duplicated in native UI. Current settlement and NPC trade surfaces execute
  the real buy/sell path; recreating the TS wrapper would be a UX-shell task,
  not a gameplay parity gap.
- Build tab remains an explicit non-action surface because TS does not define
  build projects, costs, construction time, or persisted building effects.
- Extended TS event tags `NpcHpChange`, `SettlementMoodChange`,
  `PlayerStatChange`, `BattleEnd`, `MagicSurge`,
  `FactionRelationChange`, `DialogStart`, and `CameraMove` are now native
  `EventTag` values and are covered by `save_roundtrip_test` in save schema
  v10. Normal gameplay producers/consumers are still partial for several of
  them; do not treat schema/save proof as full event-loop parity.

## Controls

Every row is a **default binding** in the one rebindable registry
(`kActionSpec`, [src/ui/keymap.h](src/ui/keymap.h)) — menu → **Controls**
rebinds any of them, `keymap.cfg` persists the result, and only Esc is fixed.
See [controls.md](controls.md).

| Default        | Action                                    |
|----------------|-------------------------------------------|
| Arrows         | Subworld: move (the left hand acts, it does not walk) |
| WASD           | Macro: pan the camera                     |
| Left click      | Walk to a macro-cell destination          |
| Mouse wheel    | Zoom (macro view)                         |
| Enter          | Enter / leave subworld                    |
| I              | Toggle character panel (Inventory tab)    |
| P              | Character panel → Army tab                |
| B              | Character panel → Spells tab              |
| E              | Subworld: interact; overworld: character panel → Equipment tab |
| V              | Subworld: вселение / possess the body under the reticle |
| Space          | Macro: pause / unpause the world; subworld: jump |
| Z              | Macro: rest — stop the squad, fast-forward until SP is full |
| A / Left click | Subworld: attack                          |
| S              | Subworld: cast the active spell           |
| K              | Toggle Diplomacy overlay                  |
| T              | Toggle Settlement overlay                 |
| Q              | Toggle Quest log                          |
| C              | Toggle Codex                              |
| M              | World map page (macro: full-screen chart; subworld: 3×3 map) |
| F3             | Toggle debug HUD                          |
| F5 / F9        | Quick-save / open load screen             |
| Esc (fixed)    | Open the game menu (Resume / Save / Load / Codex / **Interface** / **Controls** / Title / Quit) — the MENU, not the pause |

## Project Layout

```
src/
  app/          SDL2 (Vulkan window) + ImGui boot, main loop
  core/         math, RNG, torus helpers
  gpu/          Vulkan backend (device, swapchain, pipelines, buffers, textures, shadow)
  ecs/          EnTT World, components, systems
  macro/        L1 — macro-world simulation (state, gen, tick)
  sub/          L2 — subworld engine, generation, renderers, sky/lighting
  events/       L3 — bus, logic nodes, effect applicator, quest engine
  content/      L4 — pluggable data: spells, procedural quests
  assets/       sprite atlas and paper-doll asset loaders / GPU cache
  ui/           ImGui overlays
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for the layered design + per-system
notes, and [AGENTS.md](AGENTS.md) for contributor / agent rules.
