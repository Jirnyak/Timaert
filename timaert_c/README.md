# Samosbor / Timaert

C++23 + OpenGL 3.2 Core + EnTT 3.14 + ImGui port of the Timaert TypeScript
prototype. Procedural macro-world simulation with a seamless 1024-cell
subworld zoom-in, dual 2D / first-person 3D rendering, an event/quest engine,
and a modular spell system.

This project is a **full rewrite** of the TS/Vite/WebGL2 original.
Architecture and ideas are preserved 1:1 — the implementation is native.

## Highlights

- Procedural toroidal macro-world: terrain (height/moisture/temperature),
  10 biomes (3×3 climate matrix + Water), rivers, kingdoms, capitals, MST
  road network, dirt roads to villages, trees, mountains, difficulty zones.
- Politik: kingdom-driven world generation with capitals, MST + extra
  inter-kingdom roads, Voronoi territory, procedural per-kingdom languages
  and heraldic flags.
- Seamless 3×3 subworld (3072×3072) with neighbour-aware heightmap, coastal
  sculpting, mountain amplification, biome-specific terrain shaping.
- Dual subworld rendering: top-down 2D and first-person 3D (sky, terrain,
  water, structures, billboards) — toggle in-game.
- Universal combat: one stat block (`Combat`), one engine for player / NPCs
  / units / bandits. Faction-driven hostility.
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

The repo uses SDL2. The current Windows CMake cache points at an SDL2 2.x
development package (`SDL2_DIR=...\SDL2-2.32.10\cmake`). An SDL3 zip is not
valid for this repo: CMake calls `find_package(SDL2 REQUIRED)` and the app
links `SDL2::SDL2` / `SDL2::SDL2main`.

If `build-msvc` must be regenerated on this machine, use SDL2, not SDL3:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSDL2_DIR=C:\dev\SDL2-devel-2.32.10-VC\SDL2-2.32.10\cmake && cmake --build build-msvc"
```

### Portable Native

Non-Windows builds still use the normal CMake/Ninja flow when SDL2 is
available through the system package manager:

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
- EnTT 3.14.0 (FetchContent — no install)
- Dear ImGui 1.91.5 (FetchContent — no install)
- OpenGL 3.2 Core (built-in on macOS, available everywhere)
- WebGL2 / GLES3 on Emscripten

### macOS (Homebrew)

```bash
brew install cmake ninja sdl2
```

### Ubuntu / Debian

```bash
sudo apt install cmake ninja-build libsdl2-dev
```

## Integration Ledger

Corrected 2026-05-11 after Masum review. Windows/MSVC evidence is a build and
smoke verification target only. Gameplay behavior authority remains the
TypeScript/Svelte source under `C:\Timaert\src`.

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
| Windows/MSVC build | VERIFIED | `build-msvc` links `timaert.exe` with SDL2. This is not gameplay parity evidence. |
| SDL version | VERIFIED | CMake uses `find_package(SDL2 REQUIRED)`; SDL3 zip is invalid for this repo. |
| No exceptions / no RTTI | VERIFIED BY BUILD FLAGS | CMake applies `/GR- /EHs-c-` on MSVC and `-fno-rtti -fno-exceptions` elsewhere. |
| Runtime smoke artifacts | KEEP | `.gitignore` covers root `runtime_*`, `smoke_*`, `save.bin`, and temp save files. |

### Test / Smoke Infrastructure

| Flow | Status | Evidence |
|------|--------|----------|
| Title screen | VERIFIED | `runtime_title*.png`, `runtime_topmost_title.png`, `runtime_round2*_title.png`. |
| New Game boot | VERIFIED | `runtime_boot_final.err` reaches `[boot] done`; `runtime_playing_newgame.png` / `runtime_topmost_playing_newgame.png`. |
| Macro walking | VERIFIED | `runtime_playing_after_w.png`, `runtime_topmost_after_w.png`. |
| Load screen | VERIFIED | `runtime_load_valid_ready.png`, `runtime_load_valid_loaded_world.png`, `runtime_round2*_load_ready.png`. |
| Save v4 binary | VERIFIED | `save_roundtrip_test` is committed and passes; `save.bin` exists from runtime smoke; `macro/save.{h,cpp}` implements v4 magic/version/checksum and atomic write. |
| GUI save/load | PARTIAL | `runtime_pause_menu_before_save.png`, `runtime_pause_menu_after_save.png`, `runtime_load_valid_ready.png`, `runtime_load_valid_loaded_world.png`; exact canonical end-to-end GUI round trip still needs one dedicated proof log. |
| Settlement / trade / quests | VERIFIED | `runtime_settlement_*`, `runtime_settlement_trade_*`, `runtime_quest_accept_*`. |
| NPC Talk | VERIFIED | `runtime_round2e_npc_talk.png`. |
| Character tabs | VERIFIED | `runtime_character_*`, `runtime_topmost_character_*`, `runtime_toolbar*_equipment.png`. |
| Equipment / Build / Attack actions | PARTIAL / PLACEHOLDER | Equipment slots and Build tab remain placeholder surfaces. Attack is not a current combat-resolver objective. |
| Subworld time | UNVERIFIED | Time code is instrumented, but no reliable runtime proof is recorded. |
| ShowDialog / ShowStory | MISSING | Event docs list these as absent consumers/overlays. |

Native test targets currently present:

- `quest_lifecycle_test` (`cmake --build build-msvc --target quest_lifecycle_test`).
- `save_roundtrip_test` (`cmake --build build-msvc --target save_roundtrip_test`).
- No `add_test`/CTest registration is present yet.

### Gameplay Parity Progress

Confirmed facts from commit `0866bb4`:

- `quest_lifecycle_test` is committed and passes. It proves selected native
  quest objective/reward paths, not full TS quest parity.
- `save_roundtrip_test` is committed and passes. It proves native binary
  save/load invariants for its fixture, not complete TS save-field parity.
- Runtime screenshots/logs prove reachability of title, New Game, walking,
  load screen, settlement trade/quest accept, NPC Talk, and character tabs.

Not proven by those facts:

- Windows/MSVC build success does not prove gameplay progress.
- Road generation in commit `0866bb4` differs from the TS `road-network.ts`
  corridor-guided Bresenham model until a TS parity audit says otherwise.
- Equipment, Build, Attack/combat resolver, `ShowDialog`, `ShowStory`, and
  subworld time are not complete gameplay parity claims.

### Speculative / TS-Divergent Work

Treat these as `UNKNOWN` or `KEEP WITH FIX` until reviewed against TS:

- `0866bb4` road routing added bounded A* plus fallback for
  boot safety. TS road generation uses corridor-guided Bresenham over
  `tData.roadData`; compare before keeping or rewriting.
- `src/events/*` and quest/event graph additions need TS event producer and
  consumer parity review.
- `src/ui/overlays.cpp` UI additions need TS screen parity review; do not use
  placeholder buttons as proof of backend completion.
- `src/app/main.cpp` smoke hooks and save/load shell paths need portability
  review so Windows-only assumptions do not enter core logic.

### Reviewer Packet: `5b16b69..0866bb4`

Compare:

```powershell
git diff --stat 5b16b69..0866bb4
git diff --name-only 5b16b69..0866bb4
git diff 5b16b69..0866bb4 -- src/macro/spawners.cpp src/macro/save.cpp src/app/main.cpp src/ui/overlays.cpp src/events tests
```

Inspect first:

- `src/macro/spawners.cpp`
- `src/macro/save.cpp`
- `src/app/main.cpp`
- `src/ui/overlays.cpp`
- `src/events/*`
- `tests/quest_lifecycle_test.cpp`
- `tests/save_roundtrip_test.cpp`

Classify every reviewed change as:

- `KEEP`: tested and TS-compatible.
- `KEEP WITH FIX`: useful, but needs parity/perf/portability correction.
- `REVERT`: wrong or TS-divergent with no compensating value.
- `UNKNOWN`: insufficient evidence; state the missing evidence.

Manual-only checks still required: canonical GUI save/load round trip,
subworld time advance proof, Equipment/Build/Attack parity, and
ShowDialog/ShowStory overlay delivery.

## Controls

| Key            | Action                                    |
|----------------|-------------------------------------------|
| WASD / Arrows  | Pan macro camera; move player in subworld |
| Left click      | Walk to a macro-cell destination          |
| Mouse wheel    | Zoom (macro view)                         |
| Enter          | Enter / leave subworld                    |
| F              | Toggle 2D ↔ first-person 3D in subworld   |
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
  ui/           ImGui overlays
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for the layered design + per-system
notes, and [AGENTS.md](AGENTS.md) for contributor / agent rules.
