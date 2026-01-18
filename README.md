# Samosbor

A 2D tile-based procedural world simulation built with SDL2 and modern C++.

## Features

- **Procedural terrain generation** with multi-octave noise + diffusion smoothing
- **Toroidal 1024×1024 world** that wraps seamlessly in all directions
- **Terrain palette**: water, sand, grass, dirt, mountains
- **Settlements & economy**: cities, towns, villages with simulated population/capital growth
- **NPC simulation**: peasants, merchants, caravans, bandits, guards roaming the world
- **Player controller** with inventory, HP, and settlement trade UI
- **World map view** rendered from the generated terrain
- **Kinetic camera panning + smooth zoom** (mouse wheel or pinch gestures)
- **Pause menu** with resume/save/load (desktop) and on-screen control buttons
- **Touch + mouse input** for desktop and mobile/web builds

## Project Structure

```
src/
├── sac.cpp              # Main entry point and game loop
├── game_context.h       # Core game state, world grid, and utilities
├── game_state.h         # Abstract base class for game states
├── menu_state.h         # Main menu (New Game / Load / Exit)
├── gen_state.h          # Procedural world generation
├── load_state.h         # Load saved game
├── play_state.h         # Main gameplay and HUD
├── map_state.h          # World map overview
├── pause_state.h        # Pause menu
├── world_manager.h      # Settlements, NPCs, player controller
├── landmark.h           # Settlement data + pathing fields
├── npc.h                # NPC simulation + inventory
├── player.h             # Player movement/trading
├── economy.h            # Resource definitions + pricing model
├── entity_manager.h     # Entity pooling and serialization
├── texture_manager.h    # Sprite and texture loading
├── tergen.h             # Terrain generation algorithms
├── input.h              # Text input dialog utility
├── ui_button.h          # On-screen button helpers
├── sprites/             # Tile and object sprites (16×16 PNG)
├── backgrounds/         # Menu background images
└── Roboto-Black.ttf     # UI font
```

## Dependencies

- CMake 3.16+
- SDL2
- SDL2_image
- SDL2_ttf
- C++23 compatible compiler (GCC, Clang, MSVC)

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

## Building

### Native Build

```bash
mkdir build && cd build
cmake .. -GNinja
ninja
```

Or with Make:
```bash
mkdir build && cd build
cmake ..
make
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

### WebAssembly Build (Emscripten)

```bash
# Activate Emscripten SDK (adjust path as needed)
source /path/to/emsdk/emsdk_env.sh

# Build
mkdir build-web && cd build-web
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
emmake make -j$(nproc)
```

This produces:
- `samosbor.html` - Main HTML file
- `samosbor.js` - JavaScript glue code
- `samosbor.wasm` - WebAssembly binary
- `samosbor.data` - Preloaded assets

To test locally:
```bash
python3 -m http.server 8080
# Open http://localhost:8080/samosbor.html
```

### Cloudflare Pages Deployment

The project includes a GitHub Actions workflow that automatically builds and deploys to Cloudflare Pages on push to `main`/`master`.

To enable:
1. Create a Cloudflare Pages project named `samosbor` (or update the project name in `.github/workflows/deploy.yml`)
2. Get your Cloudflare Account ID from the dashboard URL or Workers & Pages overview
3. Create an API token with "Cloudflare Pages: Edit" permissions
4. Add repository secrets in GitHub:
   - `CLOUDFLARE_ACCOUNT_ID` - Your Cloudflare account ID
   - `CLOUDFLARE_API_TOKEN` - Your API token
5. Push to main branch

The game will be available at `https://samosbor.pages.dev/` (or your custom domain)

## Running

Run from the build directory:

```bash
./samosbor
```

Resources (sprites, backgrounds, fonts) are automatically copied to the build directory during compilation.

## Controls

### Menu
- **Mouse click / tap** — Select menu option
- **0** — Toggle fullscreen

### Gameplay
- **Mouse drag / touch drag** — Pan camera with kinetic scrolling
- **Mouse wheel / pinch** — Zoom in/out (0.25× to 4×)
- **Arrow keys** — Nudge camera (tile-by-tile)
- **Space** — Pause/unpause simulation
- **P** — Toggle free camera mode
- **M** — Open world map view
- **C** — Open text input dialog (debug)
- **K** — Take screenshot (saves as `save.png`)
- **0** — Toggle fullscreen
- **Escape** — Open pause menu

On-screen buttons:
- **|| / ▶ / ▶▶** — Pause, play, fast-forward
- **Arrow pad + center** — Move player and center camera
- **$** — Toggle trade UI when at a settlement
- **=** — Open pause menu

### Map View
- **Mouse drag / touch drag** — Pan the map
- **Enter** — Return to gameplay
- **K** — Take screenshot
- **Escape** — Open pause menu

### Pause Menu
- **Resume** — Return to gameplay
- **Save / Load** — Save or restore entity state (`objects.dat`)
- **Exit** — Quit (desktop builds only)

## Save Files

- `field.dat` — Terrain heightmap data
- `objects.dat` — Entity + NPC states
- `save.png` — Screenshot capture (when triggered)

## Technical Details

- **World size**: 1024×1024 tiles (`WORLD_WIDTH`)
- **Tile size**: 16×16 pixels (`TILE_SIZE`)
- **Entity pool**: 128×128 = 16,384 (`MAX_OBJECTS²`)
- **NPC pool**: 4,096 (`NPCManager::MAX_NPCS`)
- **Terrain generation**: 6 octaves, 64 diffusion steps per octave
- **Frame-rate independent**: Delta-time based physics and scrolling
