# Agent Instructions — Samosbor / Timaert (C++ Port)

## Hard Rules

- **No exceptions. No RTTI.** Disabled in CMake (`-fno-exceptions -fno-rtti`).
  Do not use `try`/`catch`/`throw`/`dynamic_cast`/`typeid`. EnTT is built
  with `ENTT_NOEXCEPTION`.
- **Performance first.** Favour better algorithms, contiguous data layouts,
  EnTT views over pointer chasing. Do not allocate per-frame in hot paths.
- **Data-driven by default.** Adding a biome / feature / spell / NPC type /
  quest objective / reward must be one new entry in the appropriate table —
  never an `if` chain in the engine.
- **No save compatibility.** Bump `kSaveVersion` for any breaking change to
  serialized data; existing saves are silently invalidated.
- **No legacy code.** Delete deprecated paths immediately. The project is
  pre-release; there is nothing to keep alive.
- **GLOB_RECURSE.** New `.cpp` files under `src/{app,core,gl,ecs,macro,sub,
  events,content,ui}` are auto-picked-up. Do **not** edit `CMakeLists.txt`
  for individual files.

## Source Authority

- `C:\Timaert\src` (TypeScript/Svelte) is the gameplay behavior authority.
  Before changing a gameplay system, read the matching TS module and callers.
- Windows/MSVC is a verification target for this workspace, not a gameplay
  authority. A passing Windows build proves compilation only.
- Do not implement a combat resolver as cleanup work. Treat combat resolution
  as out of scope unless a later prompt explicitly changes that objective.
- Road generation must be audited against `C:\Timaert\src\game\road-network.ts`
  and its callers before further claims or rewrites.

## File Organization

- One file = one responsibility.
- Do not split files to satisfy an arbitrary line count. A 500-line module
  that does one thing well is better than five 100-line files that import
  from each other.
- Split when there is a real architectural seam (pure logic vs. GL code,
  shared utilities used by 3+ consumers, dedicated `*_types.h`).
- Files exceeding ~800 lines should be reviewed; never let one exceed 1000
  unless it is a naturally encapsulated module (renderer, generator).

## C++ Style

- C++23. Prefer `std::uint8_t` / `std::int32_t` etc. — never `unsigned int`.
- POD components (`struct Foo { int x, y; };`). No virtuals on hot data.
- Headers minimal — forward-declare in headers, include in `.cpp`.
- No global state. Pass `GameState&`, `ecs::World&`, `EventBus&` explicitly.
- Use `constexpr` for tunables; group at top of file.
- For RNG, use the seeded `Rng` from `core/rng.h` — never `std::rand`.
- Math: use the `vec2/vec3/vec4/mat4` POD helpers in `core/math.h`. Do not
  pull in GLM or Eigen.

## ECS Conventions (EnTT)

- Components are POD structs in [src/ecs/components.h](src/ecs/components.h).
  Keep them under ~64 bytes; split larger blobs into separate components.
- Systems are free functions in `src/ecs/systems.{h,cpp}` operating on
  views (`reg.view<A, B>()`). They take `World&` and `dt`.
- Spawning: free factory functions per subsystem (e.g. `respawn_subworld_npcs`
  in [src/sub/spawn.cpp](src/sub/spawn.cpp)). Never construct entities ad-hoc
  outside a factory.
- Tag types (`PlayerTag`, `Active`, `Dead`) carry no data — use `view<Tag>`.

## Build

Known-good Windows / MSVC build for this workspace:

```cmd
cmd /d /s /c "\"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build build-msvc"
```

Launch from repo root with `.\build-msvc\timaert.exe`.

SDL2 is required. Do not substitute SDL3; CMake uses
`find_package(SDL2 REQUIRED)` and links `SDL2::SDL2`.

Portable native build when SDL2 is available from the system package manager:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

WASM:

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

After any non-trivial Windows change run the `build-msvc` command above.
For portable native changes, run `cmake --build build`. Ensure **zero warnings**
(compiled with `-Wall -Wextra` off MSVC; `/W3` on MSVC). Treat warnings as
errors during review.

## Layer Discipline

The four-layer rule from `ARCHITECTURE.md` is enforced by include hygiene,
not tooling. Before adding an `#include`, verify the target lives in the
same layer or below:

```
L4 content/  →  may include events/, macro/, sub/, ecs/, core/, gl/
L3 events/   →                       macro/, sub/, ecs/, core/, gl/
L2 sub/      →                                 macro/, ecs/, core/, gl/
L1 macro/    →                                          ecs/, core/, gl/
```

`ui/` (ImGui overlays) sits above everything and may read from any layer
but never own game logic.

## Workflow Checklist

1. Make the smallest change that solves the problem.
2. Run the known-good Windows `build-msvc` command. It must compile clean
   (no warnings).
3. If new shader: verify it links (any GLSL error appears at runtime in
   stderr).
4. Update [ARCHITECTURE.md](ARCHITECTURE.md) only if you added a real new
   subsystem; do not document trivial edits.
5. Do not create stand-alone notes / changelogs / "summary of changes" markdown.
