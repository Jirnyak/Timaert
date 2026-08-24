<div align="center">

# Timaert (Samosbor) — Procedural World Simulation

[![License](https://img.shields.io/badge/License-True%20People's%20v2.0-red?style=for-the-badge)](LICENSE.md)
[![Engine](https://img.shields.io/badge/Engine-C%2B%2B23%20%2B%20Vulkan-blue?style=for-the-badge)]()
[![World](https://img.shields.io/badge/World-1024%C3%971024%20Toroidal-green?style=for-the-badge)]()

> **A procedural toroidal macro world of 1024×1024 cells (one cell ≈ 1 km²), simulated honestly everywhere — and a seamless first-person 3D subworld (one tile ≈ 1 m) that materialises around the player, up to 16,384 bodies in one frame.**

[🌐 Open Live Showcase](https://Jirnyak.github.io/Timaert/) &nbsp;·&nbsp; [🎮 Controls](#-controls-current-build) &nbsp;·&nbsp; [🗂 Engineering Manifest](MANIFEST.md)

> **Engineering documentation lives in [MANIFEST.md](MANIFEST.md)** — the
> orchestrating register of every system doc, build recipe, integration
> ledger and test roster for the shipping C++23/Vulkan game in this repo.

</div>

---

## 🧭 Where to Start

| Doc | Role |
|-----|------|
| [CANON.md](CANON.md) | **The design canon** — the owner's intent per system; how the game SHOULD be. Everything else is judged against it |
| [MANIFEST.md](MANIFEST.md) | The engineering register: every system doc, build recipes, integration ledger, test roster |
| [AGENTS.md](AGENTS.md) | Contributor / agent working rules |
| [ARCHITECTURE.md](ARCHITECTURE.md) | The four-layer architecture as built |

---

## 🗺️ World Illustrations

<div align="center">

<img src="assets/illustrations/illust_world_map.jpg" width="100%" alt="Timaert procedural world — top-down macro map with multiple biomes"/>

*The procedural macro world — a 1024×1024 toroidal cell map: ocean, rivers, forests, mountains, kingdoms with roads and settlements.*

</div>

---

### What is Timaert?

**Timaert** (working title *Samosbor*) is a native **C++23** game: **Vulkan**
rendering (MoltenVK on macOS), **SDL2** as the platform/input/audio layer
(never the graphics API), **EnTT** ECS, **ImGui** overlays. It plays on two
scales of the same world:

- **The macro world** — a 1024×1024-cell torus (a cell ≈ 1 km²), wrapping
  seamlessly in both axes. Terrain, climate and resources are generated first;
  kingdoms, capitals, roads and settlements are *derived* from them. The world
  simulates honestly whether the player looks or not: squads march, economies
  tick, battles auto-resolve.
- **The subworld** — press Enter and the current cell opens as a seamless 3×3
  first-person 3D window (one tile ≈ 1 m): terrain, water, structures, bodies,
  real-time sword-and-magic combat with up to **16,384** simulated and drawn
  entities. Everything below is a projection of the macro world; everything
  meaningful you do below is paid back up.

---

## 🔨 Build & Run

Dependencies (see [MANIFEST.md](MANIFEST.md#dependencies) for the full list):
a C++23 compiler, CMake 3.16+, Ninja, the **Vulkan SDK** (headers + loader +
`glslc`), **SDL2** and **SDL2_mixer** (with MP3 support), and **MoltenVK** on
macOS. EnTT and Dear ImGui are fetched automatically by CMake.

**macOS (Homebrew):**
```bash
brew install cmake ninja sdl2 sdl2_mixer molten-vk vulkan-headers vulkan-loader shaderc
```

**Ubuntu / Debian:**
```bash
sudo apt install cmake ninja-build libsdl2-dev libsdl2-mixer-dev \
                 vulkan-tools libvulkan-dev vulkan-validationlayers glslc
```

**Build and run:**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/timaert
```

**Tests** (builds the game and every test, *then* runs ctest — quote verdicts
only from this target):
```bash
cmake --build build --target check
```

**Smoke scenarios** (scripted runs of the real game; the script exits with the
game's own code):
```bash
sh smoke.sh                      # the default macro travel-stamina scenario
sh smoke.sh subworld_enter       # any scenario token
sh smoke.sh cast_spell 1,7,999   # one scenario swept over several world seeds
```

There is no browser build: the WebAssembly/Emscripten target is dropped and the
game is a native Vulkan title. Windows/MSVC build notes live in
[MANIFEST.md](MANIFEST.md#build).

---

## 🎮 Controls (current build)

Every key below is a **default, not a law**: the whole scheme is one
rebindable registry (`kActionSpec` in [src/ui/keymap.h](src/ui/keymap.h),
menu → **Controls**, persisted to `keymap.cfg`) — see
[controls.md](controls.md). Only **Esc is fixed** (menu / cancel a rebind),
so the way back can never be bound away. Consumers are
`handle_event_playing()` and the held-key poll `poll_movement()`, both in
[src/app/main.cpp](src/app/main.cpp); the same table lives in
[MANIFEST.md](MANIFEST.md#controls).

The canonical control reference is [controls.md](controls.md); the live
bindings are whatever the `kActionSpec` registry currently holds — this table
is a snapshot of the defaults.

The game has two layers and the defaults say so: on the **world map** the left
hand steers a camera, in the **subworld** it fights. Movement never shares a
key with an action.

### World map (macro)

| Default | Action |
|---|---|
| Left click | Walk to that cell (auto-travel); on a settlement, select it |
| **Space** | **Pause / unpause the world** — the clock, the AI, the march |
| WASD | Pan the camera (it eases back to the party when released) |
| Middle / right drag | Pan the camera |
| Mouse wheel | Zoom |
| Z | Rest — stop the squad and fast-forward until SP is full |
| Enter | Enter the cell — descend into the subworld |
| Esc | Game menu (fixed) |

### Subworld (micro, first person)

| Default | Action |
|---|---|
| Arrows | Move |
| Mouse | Look |
| A / left click | Attack |
| S | Cast the active spell |
| **Space** | **Jump** (1 m apex — a kerb, a crate, a low ledge) |
| E | Interact |
| V | вселение — possess the body under the reticle |
| Enter | Leave, back to the map |
| Esc | Game menu (fixed) |

### Both layers

| Default | Action |
|---|---|
| I | Character panel (Inventory) |
| P / B | Character panel → Army / Spells |
| E | On the map: character panel → Equipment |
| T · Q · C · M · K | Settlement · Quests · Codex · World map · Diplomacy |
| F3 | Debug HUD |
| F5 / F9 | Quick-save / open the load screen |
| ` | Developer console |

**Pause is one thing with several reasons.** Space stops the world map; so does
any panel you open, any event window, any story slide and the Esc menu — all
through one `world_paused()` query. The subworld is deliberately NOT stopped by
the Space/toolbar pause (its combat is real time), only by a window or the menu.

---

## 📜 License & Community Standards

Distributed under the **True People's License v2.0** / Open License — Authors: **Jirnyak** & **Adolf Petushkov** (2026). Free for all maintainers, developers, and AI research. Zero paywalls.
