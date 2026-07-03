# Vulkan Backend & GPU-Assisted Computation — Timaert

Source of truth for the **Vulkan backend** (`src/gpu/`) and the **GPU-driven
simulation** that it exists to enable. Companion to [render.md](render.md) (the
graphics passes) and [ARCHITECTURE.md](ARCHITECTURE.md) §Rendering & Compute
Backend / §GPU-Driven Simulation.

> **Why Vulkan, not "faster GL".** The game's core goal — thousands of macro
> squads and thousands of microworld combatants — is a **compute-shader**
> problem. OpenGL 3.2 Core has no compute, and Apple caps macOS OpenGL at 4.1, so
> GL compute is impossible on Mac. Vulkan gives explicit compute + memory control
> and far lower per-draw overhead. **SDL2 is demoted to platform only** (window,
> input, timing, audio) — never the graphics API.

Native, single-player, ships on Steam. **macOS = primary/dev** (MoltenVK),
Windows + Linux to follow. The browser / WASM / OpenGL paths are dropped.

---

## Backend module map (`src/gpu/`)

Game logic (`core/`, `ecs/`, `macro/`, `sub/`, `events/`, `content/`) stays
backend-agnostic and never includes Vulkan headers — everything Vulkan is behind
these modules. C API only (`<vulkan/vulkan.h>`); no `vulkan.hpp` because
exceptions + RTTI are disabled.

| Module | Type | Responsibility |
|--------|------|----------------|
| [vk_common.h](src/gpu/vk_common.h) | header | Vulkan include, `VK_TRY` macro, `vk_result_str` |
| [vk_device.h](src/gpu/vk_device.h) | `VulkanDevice` | Instance, surface, physical/logical device, queues, MoltenVK ICD auto-detect, validation opt-in |
| [vk_swapchain.h](src/gpu/vk_swapchain.h) | `VulkanSwapchain` | FIFO swapchain, image views, recreate on resize |
| [vk_renderer.h](src/gpu/vk_renderer.h) | `VulkanRenderer` | Colour+depth render pass, framebuffers, command pool/buffers, per-frame sync, split frame API |
| [vk_pipeline.h](src/gpu/vk_pipeline.h) | `VulkanPipeline` | Graphics pipelines: `create` / `create_mesh` / `create_shadow` (see [render.md](render.md)) |
| [vk_buffer.h](src/gpu/vk_buffer.h) | `VulkanBuffer` | Device-local vertex/index/instance buffers via staging copy |
| [vk_texture.h](src/gpu/vk_texture.h) | `VulkanTexture` | Sampled RGBA8 images via staging + layout barriers |
| [vk_shadow.h](src/gpu/vk_shadow.h) | `VulkanShadowMap` | Depth-only shadow target + render pass (see [render.md](render.md) §Shadow mapping) |

New GPU code goes here (replacing the retired `src/gl/`). `src/gpu/*.cpp` is
auto-globbed into the build — do not edit [CMakeLists.txt](CMakeLists.txt) for
individual files.

---

## Device bring-up & MoltenVK portability

`VulkanDevice::init()` follows the MoltenVK portability rules so the same code
runs on desktop drivers and MoltenVK:

- **Instance** — on `__APPLE__` enable `VK_KHR_portability_enumeration` +
  `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` and
  `get_physical_device_properties2`.
- **Device** — enable `VK_KHR_portability_subset` when the physical device
  advertises it.
- **Surface** — created by SDL: `SDL_WINDOW_VULKAN` +
  `SDL_Vulkan_GetInstanceExtensions` / `SDL_Vulkan_CreateSurface`. SDL headers are
  included as `<SDL.h>` / `<SDL_vulkan.h>`.
- **Queues** — single graphics+present family on M2 Pro (`g=0 p=0`).

### ICD auto-detect

The Vulkan loader does **not** auto-find the Homebrew MoltenVK ICD. `vk_device.cpp`
(`ensure_moltenvk_icd`, `__APPLE__`) sets `VK_ICD_FILENAMES` if unset — bundled
path first (for a shipped `.app`), then `/opt/homebrew/etc/vulkan/icd.d`. No
manual env var is needed to run the harnesses.

### Validation layers

`VK_LAYER_KHRONOS_validation` (Homebrew `vulkan-validationlayers`) loads only when
the brew lib dir is on the dynamic path:

```bash
export DYLD_LIBRARY_PATH=/opt/homebrew/lib   # makes the layer load -> validation=1
```

`init()` retries without validation if the layer is absent, so the code degrades
gracefully. Every increment is verified with `validation=1` and **zero VUIDs**.

---

## Frame lifecycle & synchronisation

`VulkanRenderer` runs `kMaxFramesInFlight = 2` with per-frame fence +
acquire/submit semaphores and an images-in-flight guard, and recreates the
swapchain (and the depth attachment) on resize.

The frame API is **split** so a shadow pass can be recorded before the main pass:

| Call | Does |
|------|------|
| `acquire_frame(win)` | Wait fence, acquire swapchain image, begin command buffer — **no** render pass. Returns false to skip (resize). |
| `begin_render_pass(r,g,b)` | Open the main colour+depth pass (clears both). |
| `begin_frame(win,r,g,b)` | Convenience = `acquire_frame` + `begin_render_pass` (used by the 2D macro harness). |
| `current_command_buffer()` | The recording command buffer. |
| `end_frame(win)` | End pass, submit, present. |

See [render.md](render.md) §Frame structure for how the subworld harness uses
`acquire_frame` → shadow pass → `begin_render_pass` → main pass.

---

## Buffers & textures (no-stall staging)

- **`VulkanBuffer::create_device_local(dev, data, bytes, usage)`** — allocates a
  host-visible staging buffer, copies the payload, then issues a one-time copy
  into a `DEVICE_LOCAL` buffer through a transient command pool. Used for terrain
  vertex/index buffers and the instanced-tree buffer.
- **`VulkanTexture::create_rgba8(dev, w, h, pixels, linearFilter, repeat)`** —
  staging buffer → `DEVICE_LOCAL` sampled image with
  `UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY` barriers + sampler. Used for the
  macro synth data textures (master / feature / zone / river maps).

These are the primitives the compute simulation will reuse for SSBOs. The
**no-stall rule** (below) governs how results come back.

---

## Shader toolchain

GLSL in [shaders/](shaders) is compiled to SPIR-V by **`glslc`** (Homebrew
`shaderc`) at build time. Add a new shader to the `glslc` `foreach` list in
[CMakeLists.txt](CMakeLists.txt); the `.spv` is emitted next to the binary and
loaded at runtime via `SDL_GetBasePath`. GLSL compile errors surface at build
time; pipeline/link errors surface at runtime on stderr.

Toolchain (macOS dev, Homebrew): `molten-vk`, `vulkan-headers`, `vulkan-loader`
(`libvulkan` in `/opt/homebrew/lib`), `vulkan-tools` (`vulkaninfo`), `shaderc`
(`glslc`), `sdl2`, `sdl2_mixer`.

---

## GPU-driven simulation (the payload)

The rendering backend exists to enable this. The organising principle: **NPCs are
always real, always simulated — never faked, frozen, or LOD-cheated.** Only their
*execution unit* changes.

### Residency & embodiment (воплощение)

| Tier | Who | Runs on | Representation |
|------|-----|---------|----------------|
| **GPU-resident** | The mass (distant squads; micro combatants outside the engagement set) | Compute over SSBOs | Packed SoA / bit-packed |
| **CPU-embodied** | The few the player can touch | EnTT/ECS | Full gameplay logic, events, loot, dialogue |

**Embodiment** promotes a GPU-resident NPC to a CPU ECS entity the instant the
player can act on it; **de-embodiment** returns it to the GPU pool. Identity (id,
packed stats) is preserved across the transition.

### No-stall transfer rule

GPU↔CPU transfer is the enemy. Every embodiment either happens at a **load /
transition boundary** or is **amortised** across frames via double-buffered,
fenced staging — **never** a synchronous per-frame readback. No blocking
`vkQueueWaitIdle` in the frame loop, no per-frame full-buffer readback. If data
must return this frame, only the embodied few return, never the mass.

### The four crowd techniques (rules for every compute kernel)

1. **Data packing** — SoA + bit-packing. NPC state in a few 32-bit words
   (`level(8) | kindOrWeaponId(8) | hp(16)`), positions/velocities in parallel
   float buffers. No AoS structs, no GPU pointers.
2. **Lookup buffers** — weapon/armour/faction/kind stats as flat GPU arrays
   indexed by id. One universal kernel, no per-kind shader. Adding a kind = one
   row, same as the CPU registries.
3. **Branchless math** — replace `if (melee) … else …` with one formula
   (`dmg = meleeDmg·proximity + rangedDmg·visibility`, melee's ranged coef = 0 in
   the lookup buffer). Divergence-free.
4. **Cohort sorting** — when behaviour can't collapse to one formula, sort the
   crowd by behaviour class (CPU or GPU radix) and run one homogeneous dispatch
   per cohort. Each warp sees identical control flow.

### What stays on the CPU

Only what the player is resolving: embodied entities, their events (loot, XP,
dialogue, reputation), quest evaluation, save/load, world generation. Latency-
bound, branchy, low-count — a natural CPU fit. The dividing line is
**interactivity**, not entity type.

This is **not a cheat**: an off-screen squad and an embodied one run the same
rules; only the execution unit and representation width differ, and embodiment
always happens before any interaction is possible.

---

## Portability rules (desktop targets)

- **Push constants ≤ 128 bytes** on desktop (AMD caps `maxPushConstantsSize` at
  128; MoltenVK allows 4096). The current harness pushes 176 B matrices — the P6
  cutover moves per-frame matrices to a **per-frame UBO**. See
  [render.md](render.md) §Push-constant layouts.
- **No cross-build / cross-platform float determinism** is targeted — only
  *within-build* same-seed reproduction (worlds regenerate from seed). The game
  target may use `-ffast-math`.
- **ImGui** uses `imgui_impl_vulkan` + `imgui_impl_sdl2`.

---

## Harnesses & status

Two throwaway harness targets prove the backend before the P6 game cutover
(they are declared explicitly in [CMakeLists.txt](CMakeLists.txt), not globbed):

| Target | File | Exercises |
|--------|------|-----------|
| `gpu_smoke` | [tests/gpu_smoke.cpp](tests/gpu_smoke.cpp) | 2D macro fragment synth + ImGui |
| `gpu_smoke3d` | [tests/gpu_smoke3d.cpp](tests/gpu_smoke3d.cpp) | Subworld 3D: depth, terrain, instanced trees, sky+stars, shadows, water |

```bash
cmake --build build --target gpu_smoke3d
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./build/gpu_smoke3d
# headless check: GPU_SMOKE_FRAMES=120 ./build/gpu_smoke3d  -> validation=1, exit 0
```

### Migration phases

| Phase | Scope | State |
|-------|-------|-------|
| P0 | Decision + docs | done |
| P1 | Device bring-up (`VulkanDevice`, `gpu_smoke`) | done |
| P2 | Swapchain + render pass + sync (clear loop) | done |
| P3 | ImGui Vulkan | done |
| P4 | Macro fragment synth → SPIR-V (data textures) | done |
| P5 | Subworld passes: terrain, trees, sky/stars, **shadows**, water | in progress (ground atlas, 2D tiles, paper-doll remain) |
| P6 | `main.cpp` cutover; **delete** `src/gl/`, `imgui_impl_opengl3`, WASM; push→UBO | pending |
| P7 | Compute NPC mass sim + CPU↔GPU embodiment seam | pending |
| P8 | Packaging (bundle MoltenVK for macOS Steam; ICD config) | pending |

See [render.md](render.md) for the implemented graphics passes in detail.
