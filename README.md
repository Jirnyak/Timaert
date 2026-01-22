# Samosbor

Samosbor is a 2D, tile-based world sim built with SDL2 and modern C++. It generates a wraparound world full of settlements and roaming NPCs, then lets you explore, trade, and watch the simulation evolve.

## Highlights

- Procedural 1024×1024 toroidal terrain with water, sand, grass, dirt, and mountains.
- Cities, towns, and villages that grow population and capital over time.
- NPC simulation: peasants wander, merchants/caravans trade between settlements, bandits roam.
- Player controller with tap-to-move pathfinding and a settlement trade panel.
- Kinetic camera panning with smooth zoom, plus a world map overview.
- Save/load support, screenshots, and touch/mouse-friendly controls.

## Dependencies

- CMake 3.16+
- SDL2, SDL2_image, SDL2_ttf
- C++23-compatible compiler

### Installing dependencies

**Ubuntu/Debian:**
```bash
sudo apt install cmake ninja-build libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

**Fedora:**
```bash
sudo dnf install cmake ninja-build SDL2-devel SDL2_image-devel SDL2_ttf-devel
```

**Arch Linux:**
```bash
sudo pacman -S cmake ninja sdl2 sdl2_image sdl2_ttf
```

**macOS (Homebrew):**
```bash
brew install cmake ninja sdl2 sdl2_image sdl2_ttf
```

**Windows (vcpkg):**
```bash
vcpkg install sdl2 sdl2-image sdl2-ttf
```

## Build

Native build:
```bash
mkdir build && cd build
cmake .. -GNinja
ninja
```

WebAssembly build (Emscripten):
```bash
source /path/to/emsdk/emsdk_env.sh
mkdir build-web && cd build-web
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc)
```

To test locally:
```bash
python3 -m http.server 8080
# Open http://localhost:8080/samosbor.html
```

### Build types

Release build (optimized with `-O3`):
```bash
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
```

Debug build:
```bash
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug
```

(jirnyak)
Mac build terminal:
cd ~/Mirror/samosbor
mkdir build
cd build
cmake ..
cmake --build .
./samosbor

Github update

git status

# if modified
git add .
git commit -m "msg"

# if diverged
git pull --rebase

git push

If you do not want some files from your folder to git:

nano .gitignore 

git add .gitignore 

If your folder is bracnhing from git for some reason and you want to merge it wih main:

git config pull.rebase false                  

## Run

```bash
./samosbor
```

Resources (sprites, backgrounds, fonts) are copied to the build directory during compilation.

## Controls

Gameplay:
- **Mouse/touch drag** — Pan camera
- **Mouse wheel/pinch** — Zoom in/out
- **Tap/click** — Move player (pathfinding)
- **Arrow keys** — Nudge camera
- **Space** — Pause/unpause simulation
- **M** — World map view
- **P** — Toggle free camera
- **K** — Screenshot (`save.png`)
- **0** — Toggle fullscreen
- **Esc** — Pause menu

On-screen buttons:
- **|| / > / >>** — Pause, play, fast-forward
- **D-pad + center** — Move player, center camera
- **$** — Toggle trade UI
- **=** — Pause menu

Map view:
- **Mouse/touch drag** — Pan
- **Enter** — Return to gameplay
- **Esc** — Pause menu

## Save Files

- `persistent.dat` — Window preferences (desktop builds)
- `save.dat` — World + entities
- `save.png` — Screenshot capture
