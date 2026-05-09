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

> The C++ project lives in the `samosbor_nolod/` subfolder of the repo
> root. All commands below assume you are inside `samosbor_nolod/`.

```bash
cd samosbor_nolod
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/samosbor
```

Or from the repo root, without `cd`:

```bash
cmake -S samosbor_nolod -B samosbor_nolod/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build samosbor_nolod/build
./samosbor_nolod/build/samosbor
```

WebAssembly (Emscripten):

```bash
cd samosbor_nolod
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server -d build-web 8080
# http://localhost:8080/samosbor.html
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

## Controls

| Key            | Action                                    |
|----------------|-------------------------------------------|
| WASD / Arrows  | Move player                               |
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
