# Samosbor / Timaert

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
> only — not the graphics API.** Build instructions below still describe the
> current pre-Vulkan baseline. See [ARCHITECTURE.md](ARCHITECTURE.md)
> §Rendering & Compute Backend and §GPU-Driven Simulation.

## Documentation

[ARCHITECTURE.md](ARCHITECTURE.md) is the layered design source of truth;
[AGENTS.md](AGENTS.md) holds contributor / agent rules. Each core system has a
focused doc in this directory alongside the README, which orchestrates them.

| System | Doc | What it covers |
|--------|-----|----------------|
| Macroworld | [macroworld.md](macroworld.md) | World state, terrain gen, time, politik, pathfinding |
| Microworld | [microworld.md](microworld.md) | Seamless 3×3 subworld, generators, 2D/3D renderers |
| Biomes | [biomes.md](biomes.md) | 3×3 climate matrix, procedural GPU biome textures |
| Landmarks | [landmarks.md](landmarks.md) | Settlements, spires, dungeons, markers |
| Features | [features.md](features.md) | Roads, dirt roads, trees, mountains (feature layer) |
| Spells | [spells.md](spells.md) | Spell book, cooldowns, mana, effect modules |
| RPG system | [rpg.md](rpg.md) | Attributes, XP, items, inventory, equipment |
| Economy | [economy.md](economy.md) | Settlement inventories, prices, trade tick |
| Zones | [zones.md](zones.md) | Difficulty heightmap 0–9, danger scaling |
| Microcombat | [microcombat.md](microcombat.md) | Sword-and-magic ARPG combat (unified, in-subworld) |
| Monsters & Loot | [monsters.md](monsters.md) | ONE global monster table + ONE loot table (source of truth), spawn/XP |
| Macrosim | [macrosim.md](macrosim.md) | Mount-&-Blade / Dwarf-Fortress macro simulation |
| Quests | [quests.md](quests.md) | Objective/reward registries, procedural generation |
| Progression | [progression.md](progression.md) | Levels, spell unlocks, plot/events, game arc |
| Rendering | [render.md](render.md) | Vulkan render passes, dynamic lighting, shadow mapping, sky/stars, water |
| GPU backend | [vulkan.md](vulkan.md) | Vulkan backend modules, MoltenVK, GPU-driven compute simulation |

## Highlights

- Procedural toroidal macro-world: terrain (height/moisture/temperature),
  10 biomes (3×3 climate matrix + Water), rivers, kingdoms, capitals, MST
  road network, dirt roads to villages, trees, mountains, difficulty zones.
- Politik: kingdom-driven world generation with capitals, MST + extra
  inter-kingdom roads, Voronoi territory, procedural per-kingdom languages
  and heraldic flags.
- Seamless 3×3 subworld (3072×3072) with neighbour-aware heightmap, coastal
  sculpting, mountain amplification, biome-specific terrain shaping.
- First-person 3D subworld rendering (sky, terrain, water, structures,
  billboards). The flat top-down 2D view is the macro map / minimap, not a
  subworld mode.
- Universal combat: one source stat block (`CombatTemplate`, projected to ECS
  `Combat`), one engine for player / NPCs / soldiers / bandits.
  Faction-driven hostility.
- One global monster table + one loot table (single sources of truth): every
  creature (rabbit → dragon) is one `FaunaEntry` row with a stable id; every
  drop — monster or NPC — resolves through one `roll_loot_profile(lootId, …)`.
  Console `spawn <id>` spawns any creature; adding content is one data row.
- Event bus + logic nodes + procedural quests (data-driven objective and
  reward registries — adding a verb = one entry).
- Modular spell system: spell book, cooldowns, mana regen.
- ImGui debug HUD + Diplomacy / Settlement / Quest / Codex / Map overlays.

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

WebAssembly (Emscripten):

```bash
cd timaert_c
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server -d build-web 8080
# http://localhost:8080/timaert.html
```

## Dependencies

- C++23 compiler (Clang 17+ / GCC 13+ / AppleClang 15+)
- CMake 3.16+
- Ninja
- SDL2 (system package)
- SDL2_mixer with MP3 support (native builds hard-fail if missing)
- EnTT 3.14.0 (FetchContent — no install)
- Dear ImGui 1.91.5 (FetchContent — no install)
- OpenGL 3.2 Core (built-in on macOS, available everywhere)
- WebGL2 / GLES3 on Emscripten

### macOS (Homebrew)

```bash
brew install cmake ninja sdl2 sdl2_mixer
```

### Ubuntu / Debian

```bash
sudo apt install cmake ninja-build libsdl2-dev libsdl2-mixer-dev
```

## Integration Ledger

Updated 2026-05-15. Windows/MSVC evidence is a build and smoke verification
target only. Gameplay behavior authority remains the TypeScript/Svelte source
under `C:\Timaert\src`.

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
| Load screen and GUI save/load | VERIFIED | `save_roundtrip_test` passes on schema v8; native smoke `new_game,wait_boot_done,save_game,open_load,load_game,wait_boot_done,quit` passed with a 51256-byte slot. |
| Settlement trade / quests | VERIFIED | `runtime_settlement_*`, `runtime_settlement_trade_*`, `runtime_quest_accept_*`; procedural quest lifecycle is covered by `quest_lifecycle_test`. |
| NPC panel / trade / attack | VERIFIED | `smoke_04_ui.png`, `smoke_07_ui.png`, `smoke_10_attack_ui.png`; smoke script routes selected macro NPCs into subworld combat. |
| Character paper-doll | VERIFIED | `character_paperdoll_test`, `character_paperdoll_gl_smoke_test`, and boot smoke load `atlas.bin` / `atlas.png` once. |
| Spell book / casting | VERIFIED | `spell_casting_effects_test`; smoke opens Spells tab, casts projectile spell, toggles Haste, and toggles Flight pathing. |
| Subworld time / combat handoff | VERIFIED | `subworld_time` smoke passes on seed 42; combined `trigger_battle_start,subworld_time` smoke passes and checks death XP flush plus subworld entity cleanup. |
| NPC-as-soldier / loot / exit gate | VERIFIED | `combat_squad_test` covers concrete NPC-kind soldiers, hire price/upkeep, garrison generation, and squad projection. Seed-42 app smoke `subworld_exit_gate,subworld_loot_xp` proves zone-9 exit blocking, corpse interaction, XP attribution (`0->25`), and inventory loot transfer (`misc_gem 0->2`). |
| Subworld spawn parity | VERIFIED | `subworld_spawn_parity_test` locks TS-fauna count/placement from `roll_fauna`, the shared RNG stream after table rolling, `baseLevel + floor(rng()*2)`, 15% per-level HP/damage scaling, zone context multipliers, sprite tint/type IDs, AI mode, and all-water squad placement fail-closed. Latest direct run after the TS-style float weighted roll fix: `fauna=6 seed=324478056 zone=5 water_squad_blocked=1`. |
| ShowDialog / ShowStory | VERIFIED | `draw_show_dialog`, `draw_story_overlay`, `trigger_level_dialog`, `trigger_count_only_dialog`, `trigger_story_overlay`, and `complete_story_overlay` are wired; `quest_lifecycle_test` covers `ShowDialog` and `ShowStory` payloads. Dialog `nodeId` choices route through app-layer logic activation. |
| Feature layer / pathfinding guards | VERIFIED | `feature_layer_parity_test` and `pathfinding_parity_test` pass; malformed feature storage fails closed and TS pathfinding semantics remain locked. |
| Road / river invariants | VERIFIED | `road_river_generation_test` enforces rejected-water pruning for surviving Politik road connections. |
| Async subworld seam / water plane | VERIFIED | `subworld_async_seam_test` covers axis, diagonal, reversal, snapshot, placeholder, saved-restore, saved-structure, sparse road-mask proofs, and the 3x3 water-plane invariant. Latest focused run: `roadGen=31.578ms`, `plainGen=23.261ms`, `diagonalGen=29.785ms`, `reversalGen=24.892ms`, `smooth=0.000ms`; water scan reported `water=3145728`, `land=6291456`, `badWater=0`, `badLand=0`, `maxWater=0.40000`, `minLand=0.42000`. `subworld_seam` app smoke crosses a real 3D seam; latest freshly rebuilt Debug timing was `gen=38.989ms upload3d=118.795ms upload2d=0.000ms total=157.938ms`, while the best accepted 1024-mask Debug timing remains `gen=22.695ms upload3d=51.785ms upload2d=0.000ms total=74.603ms`; terrain-payload shader-grid and GL sub-update trials were measured and rejected. |
| Audio | VERIFIED | `audio_contract_test` and `audio_runtime_test` cover SDL_mixer metadata, dummy-driver decode/play/stop, and one-time asset loading. Dedicated `new_game,wait_boot_done,subworld_audio,quit` smoke passed on seed 42 with the SDL dummy audio driver, proving `explore -> subworld -> explore` music transitions. |
| Global monster table + unified loot | VERIFIED | The 19-row `FaunaEntry` catalog is now a global monster registry with stable ids (`creature_catalog` / `creature_def` / `creature_def_from_kind`); the subworld bakes `NPCKind.type = 0x100 \| catalogIndex`. All death-path drops (NPC + monster) route through one `roll_loot_profile(lootId, …)` registry (8 NPC roles + wildlife/demons/bandits faction defaults); `spawn_hostile_npc` resolves any creature id or NPC role; `FaunaEntry.xpReward` gives per-creature XP. Validated seed-12345 smoke `new_game,wait_boot_done,console,subworld_loot_xp,subworld_time,quit` → `[smoke] PASS`, exit 0, `validation=1`, `spawned_creatures=1`, `subworld_loot_xp exp=0->25 misc_gem=0->2`. Defaults are behavior-preserving; see [monsters.md](monsters.md). |

Native CMake executable targets currently present:

- `timaert`
- `quest_lifecycle_test`
- `save_roundtrip_test`
- `spell_casting_effects_test`
- `combat_squad_test`
- `audio_contract_test`
- `audio_runtime_test`
- `pathfinding_parity_test`
- `feature_layer_parity_test`
- `character_paperdoll_test`
- `character_paperdoll_gl_smoke_test`
- `road_river_generation_test`
- `subworld_generator_parity_test`
- `subworld_async_seam_test`
- `subworld_spawn_parity_test`

No `add_test`/CTest registration is present yet.

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
  v8. Normal gameplay producers/consumers are still partial for several of
  them; do not treat schema/save proof as full event-loop parity.

## Controls

| Key            | Action                                    |
|----------------|-------------------------------------------|
| WASD / Arrows  | Pan macro camera; move player in subworld |
| Left click      | Walk to a macro-cell destination          |
| Mouse wheel    | Zoom (macro view)                         |
| Enter          | Enter / leave subworld                    |
| K              | Toggle Diplomacy overlay                  |
| T              | Toggle Settlement overlay                 |
| Q              | Toggle Quest log                          |
| C              | Toggle Codex                              |
| M              | Toggle world map overlay                  |
| F3             | Toggle debug HUD                          |
| Esc            | Quit                                      |

## Project Layout

```
src/
  app/          SDL2 + GL + ImGui boot, main loop
  core/         math, RNG, torus helpers
  gl/           OpenGL helpers (shader compile/link)
  ecs/          EnTT World, components, systems
  macro/        L1 — macro-world simulation (state, gen, tick)
  sub/          L2 — subworld engine, generation, renderers, sky/lighting
  events/       L3 — bus, logic nodes, effect applicator, quest engine
  content/      L4 — pluggable data: spells, procedural quests
  assets/       sprite atlas and paper-doll asset loaders / GL cache
  ui/           ImGui overlays
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for the layered design + per-system
notes, and [AGENTS.md](AGENTS.md) for contributor / agent rules.
