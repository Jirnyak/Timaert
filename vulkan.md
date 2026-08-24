# Vulkan Backend & GPU-Assisted Computation — Timaert

Source of truth for the **Vulkan backend** (`src/gpu/`). Companion to
[render.md](render.md) (the graphics passes) and [ARCHITECTURE.md](ARCHITECTURE.md)
§Rendering & Compute Backend / §GPU is graphics; the world is CPU.

> **Why Vulkan, not "faster GL".** The game draws thousands of lit, shadowed bodies
> in one subworld frame — a per-draw-overhead and pipeline-control problem. OpenGL 3.2 Core has no compute, and Apple caps macOS OpenGL at 4.1, so
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
| [vk_texture.h](src/gpu/vk_texture.h) | `VulkanTexture` | Sampled RGBA8 / R8 images via staging + layout barriers; in-place `update_region`, `read_back`, and the `blit_shift_r8` on-GPU relocation |
| [vk_shadow.h](src/gpu/vk_shadow.h) | `VulkanShadowMap` | Depth-only shadow target + render pass (see [render.md](render.md) §Shadow mapping) |
| [vk_sprite_array.h](src/gpu/vk_sprite_array.h) | `SpriteArray` | Texture-array of resolved sprite frames: one image, one sampler, ONE descriptor set (the delivery half of the sprite bank, [sprites.md](sprites.md)) |
| [vk_gpu_timer.h](src/gpu/vk_gpu_timer.h) | `GpuTimer` | Per-frame GPU pass timing via timestamp queries (`TIMAERT_GPU_STATS`): a query-pool ring per frame in flight, read back one frame late — no stalls |

New GPU code goes here (`src/gl/` is deleted — the directory no longer exists).
`src/gpu/*.cpp` is auto-globbed into the build — do not edit
[CMakeLists.txt](CMakeLists.txt) for individual files.

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
- **`create_r8(...)`** — single-channel `R8_UNORM` variant (one byte/texel, 4×
  less memory than packing a scalar into RGBA8's red). Used for the
  full-resolution subworld tile-material grid sampled per-fragment
  ([render.md](render.md) §Terrain and trees). All sampled images are created
  with `TRANSFER_SRC` in addition to `TRANSFER_DST|SAMPLED`, so any of them can be
  a copy source or read back.
- **`update_region(dev, x,y,w,h, pixels)`** — overwrite a sub-rectangle **in
  place**, reusing the image/view/sampler (no realloc, no descriptor rewrite). The
  image is transitioned `SHADER_READ→TRANSFER_DST→SHADER_READ` around the copy;
  the caller fences against in-flight frames. Used for the per-cell material
  refresh on an async subworld drain.
- **`read_back(dev, out)`** — copy the whole image back to a host vector
  (`SHADER_READ→TRANSFER_SRC→SHADER_READ`, blocks on a fence).
  **Diagnostics/verification only, never per frame** — it powers the seam
  material self-check.
- **`blit_shift_r8(dev, src, dst, srcX,srcY, dstX,dstY, copyW,copyH, fresh,
  nFresh)`** — one-shot **on-GPU relocation** of an R8 image: `vkCmdCopyImage` of
  the overlap `src → dst` (no host round-trip) plus `vkCmdCopyBufferToImage` fills
  of the fresh rects, both ending `SHADER_READ`. This is the material half of the
  seamless-crossing ping-pong ([seamless-crossing.md](seamless-crossing.md)); the
  caller swaps `src`/`dst` and rewrites the sampler descriptor after it returns.

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

## What the GPU does NOT do — the world (owner's ruling 2026-08-20)

**GPU = graphics.** Shaders, shadows, lighting, terrain/billboard passes, sky, water,
particles, sprite banks. It may additionally carry **one-way physics** (ragdolls, debris):
the world drives them, they never drive the world.

**The world runs on the CPU** — macro squads, macro AI, the daily tick, economy, fields —
and it scales by baked fields plus the strict O(N) bound, not by compute.
GPU-resident world simulation (compute over SSBOs, residency tiers, the four crowd
techniques) was this document's headline for a year and is now **deferred to the far
future**: not a plan of record, nothing to build toward, nothing to leave room for.
See [CANON.md](CANON.md) S5 and ARCHITECTURE.md §GPU is graphics; the world is CPU.

The honesty rule survives the move to the CPU unchanged: **nothing is faked, frozen or
LOD-skipped** — only the execution unit and the representation width may change, never
the behaviour.

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

Two harness targets prove the backend, and outlived the P6 cutover as the
dependable headless capture path
(they are declared explicitly in [CMakeLists.txt](CMakeLists.txt), not globbed):

| Target | File | Exercises |
|--------|------|-----------|
| `gpu_smoke` | [tests/gpu_smoke.cpp](tests/gpu_smoke.cpp) | 2D macro fragment synth + ImGui |
| `gpu_smoke3d` | [tests/gpu_smoke3d.cpp](tests/gpu_smoke3d.cpp) | Subworld 3D: depth, terrain, instanced trees, sky+stars, shadows, water; since 2026-08-20 its crowd exercises BOTH branches of `body.frag` (every fourth body has no drawn art, so a merge that silently lost the procedural half would photograph as green as a correct one) |

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
| P5 | Subworld passes: terrain, trees, sky/stars, **shadows**, water, bodies | done as scoped (bodies landed 2026-08-20 as ONE pass, sprites.md; there is no "2D tile view" to port — the 2D view IS the map, i.e. the macro synth, [render.md](render.md); the terrain surface synth polish is tracked there) |
| P6 | `main.cpp` cutover; **delete** `src/gl/`, `imgui_impl_opengl3`, WASM; push→UBO | cutover + deletions **done** (`src/gl/` no longer exists); push→UBO still pending (see §Portability rules) |
| P7 | ~~Compute NPC mass sim + CPU↔GPU embodiment seam~~ | **cancelled** (owner's ruling 2026-08-20, CANON.md S5: the world is CPU; see §What the GPU does NOT do above) |
| P8 | Packaging (bundle MoltenVK for macOS Steam; ICD config) | pending |

See [render.md](render.md) for the implemented graphics passes in detail.
