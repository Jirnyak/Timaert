# Vulkan Migration — Completion Plan (`vulkan_plan.md`)

> **Audience:** any agent (even a cheaper one) or human continuing the
> OpenGL→Vulkan cutover of the game `timaert`. This plan is written to be
> executed **mechanically, in small green steps**. If you follow it top to
> bottom and build after every step, you will finish the migration without
> needing to re-derive the architecture.

---

## 0. Golden rules (do not skip)

1. **Slow is fast.** One change → build → next. Never chain unverified edits.
2. **Keep the build GREEN after every step.** Known-good build:
   `cmake --build build` (or `--target timaert`). Zero warnings (`-Wall
   -Wextra`; warnings are errors during review).
3. **Additive first.** Write the new Vulkan renderer as a **new file that
   compiles unused** next to the GL one; wire it and delete GL only at the
   flip. This is exactly how `src/macro/vk_macro_renderer.{h,cpp}` was done.
4. **Do NOT delegate this to an autonomous subagent.** (A subagent attempt
   deleted needed source — `src/sub/textures.{cpp,h}`, still used by
   `renderer_3d` — and left a broken tree. Recovering cost more than by hand.)
5. **No exceptions/RTTI. C++23. Vulkan C API only.** MoltenVK on macOS.
6. **Hand visual/runtime checks to the human** (the game opens a window).

---

## 1. Where we are

**Done ✅**
- `map_generator.cpp`: master climate texture is CPU-synthesised into
  `TerrainData::rgba` (RGBA8) + `riverData` (R8). It still creates GL textures
  `td.texture`/`td.riverTexture` — those two `gl_make_texture_*` lines get
  deleted at the flip (renderers upload `td.rgba`/`riverData` themselves).
- Subworld is **always 3D**; the 2D subworld renderer is deleted.
- `shaders/macro.frag`: full camera synth (52-byte push `{resolution, mapSize,
  cam, viewSize, zoom, seaLevel, seed, timeOfDay, nightDarken}`), biomes +
  rivers + roads + zone tint + night. Landmark **glyphs + night-lights** are the
  only deferred parity gap. Compiles under `timaert_shaders`.
- **`src/macro/vk_macro_renderer.{h,cpp}` = `sm::MacroRendererVk`** — the Vulkan
  macro-map renderer, **compiles green, not yet wired.** `init(dev,pass)` /
  `upload(dev,td,features,zones)` / `record(cmd,ext,td,camX,camY,zoom,seaLevel,
  tod)`. Mirrors `tests/gpu_smoke.cpp` exactly.
- All subworld shaders already exist in `shaders/` and are proven in
  `tests/gpu_smoke3d.cpp`: `mesh`, `billboard`, `sky`, `water`, `struct`, `npc`,
  `shadow_mesh`, `shadow_bb`, `shadow_struct`, `shadow_npc`, plus the shared
  includes `tree_sprite.glsl` / `npc_sprite.glsl`. Compiled by `timaert_shaders`.

**Remaining**
- **PHASE A (P6-3b):** port the subworld 3D renderer to Vulkan
  (`renderer_3d` + `sky` + `textures`/`TileAtlas` + `tree_atlas`/`TreeAtlas` +
  `character_paperdoll_gl`). This is the big piece.
- **PHASE B (P6-4):** the atomic flip — `main.cpp` window/ImGui/loop → Vulkan,
  CMake backend swap, delete `src/gl` + WASM.
- **PHASE C:** polish (push→UBO for AMD 128-byte limit; docs; landmark lights).

---

## 2. The two reference implementations (SOURCE OF TRUTH — copy from these)

- **`tests/gpu_smoke.cpp`** — the macro map on Vulkan. Already distilled into
  `MacroRendererVk`. Use it as the template for descriptor sets, pipelines,
  ImGui-Vulkan init, and the frame loop.
- **`tests/gpu_smoke3d.cpp`** — the ENTIRE subworld scene on Vulkan: instanced
  terrain mesh, trees (billboard), water, sky, shadow mapping (depth pass +
  PCF), structures (walls/houses), NPC billboards. **Every pass you need in
  PHASE A already exists and runs here.** Your job is to move each pass into a
  class method and feed it the game's real data instead of the harness's CPU
  stand-ins.

The mental model for PHASE A:

```
  Vulkan HOW  (per pass: pipeline, push, descriptor, draw)  ← tests/gpu_smoke3d.cpp
  +  data WHAT (heightmap mesh, tiles, Structures, NPCs)    ← src/sub/renderer_3d.cpp (current GL)
  =  the Vulkan Renderer3D
```

Run either harness to sanity-check Vulkan on this machine:
```
export DYLD_LIBRARY_PATH=/opt/homebrew/lib   # loads validation layer
GPU_SMOKE_FRAMES=120 ./build/gpu_smoke3d 2>&1 | grep -iE "validation|VUID|error|OK"
# expect: validation=1, 0 VUIDs, "... loop OK", exit 0
```

---

## 3. `gpu/` backend API cheat-sheet (namespace `gpu`)

- `VulkanDevice` — `.device` (VkDevice), `.physical`, `.graphicsQueue`,
  `.families.graphics`, `.instance`, `.props.deviceName`. `init(SDL_Window*,
  bool validation)` / `destroy()`.
- `VulkanRenderer` (`kMaxFramesInFlight=2`) — has a **DEPTH attachment**
  (D32_SFLOAT). Public: `renderPass`, `swapchain` (`.extent`, `.images`),
  `framebufferResized`. Split frame API:
  `bool acquire_frame(win)` → `void begin_render_pass(r,g,b)` →
  `VkCommandBuffer current_command_buffer()` → `bool end_frame(win)`.
  `begin_frame(win,r,g,b)` = acquire+begin (used by macro-only path).
- `VulkanPipeline` — `create(dev,rp,vspv,fspv,pushBytes,setLayout=NULL)`
  (fullscreen, no vtx input, FRAGMENT push); `create_mesh(dev,rp,vspv,fspv,
  pushBytes,stride,attrs,attrCount,instanced,depthTest,depthWrite,blend,
  cullBack,setLayout=NULL)` (**stride==0 ⇒ geometry from `gl_VertexIndex`, no
  vtx buffer**; push is VERTEX|FRAGMENT); `create_shadow(dev,shadowPass,vspv,
  fspv,pushBytes,stride,attrs,attrCount,instanced)` (depth-only, bias baked in).
- `VulkanBuffer::create_device_local(dev,data,bytes,usage)` — staging→device.
- `VulkanTexture::create_rgba8(dev,w,h,pixels,linearFilter,repeat)` → `.view`,
  `.sampler`; `.destroy(dev)`.
- `VulkanShadowMap` — 2048² D32 sampled depth map + depth-only pass;
  `init/begin(cmd)/end(cmd)/destroy`. See `src/gpu/vk_shadow.h`.

**Push-constant limit:** MoltenVK allows ≥256 B, so the 176 B `MeshPush` works
on macOS. **AMD desktop caps at 128 B** → PHASE C moves per-frame matrices to a
UBO. Fine to ship macOS first with 176 B pushes.

---

## 4. Target frame architecture (what `main.cpp::frame()` becomes)

Shadow maps must be rendered **before** the main render pass. So the subworld
renderer exposes **two** record calls. The App owns `VulkanDevice device` +
`VulkanRenderer renderer`.

```cpp
if (!app.renderer.acquire_frame(app.window)) return;          // handles resize
VkCommandBuffer cmd = app.renderer.current_command_buffer();

// 1. Shadow pass(es) — subworld only, BEFORE the main pass.
if (app.worldLoaded && app.subworld.active())
    app.subworld.record_shadow(cmd);

// 2. Main pass.
app.renderer.begin_render_pass(0.02f, 0.02f, 0.04f);
if (app.worldLoaded) {
    if (app.subworld.active())
        app.subworld.record_main(cmd);                        // sky+terrain+...
    else
        app.macro.record(cmd, app.renderer.swapchain.extent, app.terrain,
                         app.camX, app.camY, app.zoom,
                         app.gs.mapParams.seaLevel, tod(app.gs.worldTime));
}
// 3. ImGui NewFrame calls stay; only the backend prefix changes to Vulkan.
ImGui::Render();
ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
app.renderer.end_frame(app.window);
```

Device/cmd flow rule: **`init(dev, pass)` takes the device + main render pass
once; `upload(dev, …)` (re)builds device-local buffers/textures at load;
`record_*(cmd, …)` only records commands** (no device work per frame).

---

## PHASE A — Subworld 3D renderer → Vulkan (P6-3b)

Do this as **new files that compile unused**, one pass at a time, building green.
Suggested new files: `src/sub/vk_renderer_3d.{h,cpp}` (class `Renderer3DVk`) and
`src/sub/vk_sky.{h,cpp}` (class `SkyVk`). Leave the GL `renderer_3d`/`sky`
untouched until the flip (PHASE B step B4).

The GL `Renderer3D` public surface the engine relies on (keep equivalents):
`init()`, `destroy()`, `upload(const SeamlessSubworldManager&)`,
`render(w,h,cam,worldTime,waterLevel,&mgr,ecs,haste,flight,px,py,elapsed)`,
`sample_height_m(px,py)`, `static tile_to_world(px,py,&wx,&wz)`. Sky:
`init()`, `render(w,h,worldTime,cam,elapsed,seed,fogR,fogG,fogB)`, `destroy()`.

New Vulkan API (what the engine will call after the flip):
```cpp
class Renderer3DVk {
  void init(const gpu::VulkanDevice&, VkRenderPass mainPass);
  void destroy(const gpu::VulkanDevice&);
  void upload(const gpu::VulkanDevice&, const SeamlessSubworldManager&); // rebuild buffers
  void record_shadow(VkCommandBuffer cmd, /*light matrices from cam+sun*/);
  void record_main(VkCommandBuffer cmd, VkExtent2D ext, const Camera&,
                   const WorldTime&, float waterLevel,
                   const SeamlessSubworldManager*, ecs::World*,
                   bool haste, bool flight, float px, float py, float elapsed);
  float sample_height_m(float,float) const;
  static void tile_to_world(float,float,float&,float&);
};
```
`Renderer3DVk` **owns its `gpu::VulkanShadowMap`** (created in `init`).

### A-camera/matrix note
`core/math.h` mat4 helpers are GL-style. Use the Vulkan-correct
`vk_perspective` (depth 0..1, Y-flip) and `vk_ortho` helpers that already live
in `tests/gpu_smoke3d.cpp` — copy them into an anonymous namespace or a small
`sub/vk_camera_math.h`. Light matrix for shadows: `lightEye = center +
sunDir*20`, `lookAt` up `(0,0,1)`, `vk_ortho(-14..14, 1..45)` (see gpu_smoke3d).
Feed the **real** sun from `sub/lighting.h` `compute_sun(worldTime)` (the harness
fakes it).

### Port order (each step = copy the pass from gpu_smoke3d, feed real data, build)

1. **Terrain mesh.** Copy the mesh pipeline + `Vtx`/`MeshPush`(176B) from
   gpu_smoke3d. Build the vertex/index buffers from the **real** composite
   heightmap in `SeamlessSubworldManager` exactly as GL `Renderer3D::upload`
   does today (192×192 grid, central-diff normals). `mesh.frag` already has the
   procedural biome ground; you may later feed the real tile-grid biome id.
   Shadow-receive: bind the shadow-map descriptor set (see step 6).
2. **Sky** (`SkyVk`). Fullscreen `sky.frag` (SkyPush 80B) drawn FIRST in the
   main pass (depth off). Copy from gpu_smoke3d. Feed `worldTime`+`cam`.
3. **Water.** `water.vert/frag` flat quad from `gl_VertexIndex` (stride==0),
   WaterPush 128B, drawn last (depthTest, depthWrite off, blend). `waterLevel`
   from `BiomeConfig` via `composite_water_level()`.
4. **Trees.** Instanced `billboard.vert/frag` (`#include tree_sprite.glsl`).
   Build the per-instance buffer `{vec3 pos, float size, species, seed}` from
   the real `Structure::Tree` records (GL renderer already does this). BbPush
   176B. Receives shadows (flat `vLightClip`).
5. **Structures** (walls/houses). Instanced `struct.vert/frag` unit cube from
   `gl_VertexIndex`, per-instance `{centre, half, type, seed}` from real
   `Structure` records (walls/houses/bridges exist in `sub/map_data.h`).
   MeshPush 176B.
6. **Shadow pass** (`record_shadow`). Depth-only casters into the shadow map:
   `shadow_mesh` (terrain), `shadow_struct` (boxes), `shadow_bb` (trees, real
   silhouette via `tree_sprite.glsl`), `shadow_npc` (NPCs). Use
   `VulkanShadowMap::begin(cmd)`/`end(cmd)` around them. The terrain/tree/struct
   main pipelines then sample the shadow map (PCF) — pass its descriptor set to
   `create_mesh(...,shadowSetLayout)` and bind it before those draws.
7. **NPC billboards.** Instanced `npc.vert/frag` (`#include npc_sprite.glsl`),
   per-instance `{vec3 pos, size, seed}` from the ECS NPC view (GL renderer
   iterates `ecs::World` today). Casts + receives shadows.
8. **Paper-doll NPCs (real atlas).** GL `renderer_3d.cpp:1563` samples
   `characterCache.texture_for(...)->tex`. For Vulkan: port
   `character_paperdoll_gl` so `CharacterTexture` holds a `gpu::VulkanTexture`
   (compose stays CPU: `compose_rgba8`; `gl_make_texture_rgba8` →
   `VulkanTexture::create_rgba8`; `glDeleteTextures` → `.destroy`). Draw the
   paper-doll billboards sampling that texture (a per-NPC descriptor set, or an
   atlas). **GOTCHA:** the SAME cache is used by the UI (`ui/macro_overlay.cpp`
   `paperdoll_cache()`); see PHASE B step B5.

Engine change (do at the flip, not before): split
`SubworldEngine::render(w,h)` into `record_shadow(cmd)` + `record_main(cmd)`
that call `Renderer3DVk`/`SkyVk`; change `init()` to take `(dev, pass)` and be
called after `boot_window`.

---

## PHASE B — The flip (P6-4). This is atomic; do it as ONE green step.

Only start when PHASE A compiles green. Exact anchors are current as of
2026-07-03 (re-grep if the file moved).

### B1. `main.cpp` — App struct (~L196)
```cpp
// remove:  SDL_GLContext gl = nullptr;
// add:
gpu::VulkanDevice   device;
gpu::VulkanRenderer renderer;
VkDescriptorPool    imguiPool = VK_NULL_HANDLE;
```
Replace `app.macro` type `sm::MacroRenderer` → `sm::MacroRendererVk`.

### B2. `boot_window` (~L947)
Drop all `SDL_GL_SetAttribute`, `SDL_GL_CreateContext`, `SDL_GL_MakeCurrent`,
the `_WIN32 gl_load_functions` block, `SDL_GL_SetSwapInterval`. Create the
window with `SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI`,
then:
```cpp
if (!app.device.init(app.window, /*validation*/ want_validation())) return false;
if (!app.renderer.init(app.device, app.window)) return false;
SDL_Vulkan_GetDrawableSize(app.window, &app.width, &app.height);
```
Then init the Vulkan renderers (device now exists):
`app.macro.init(app.device, app.renderer.renderPass);` and (after the engine
change) `app.subworld.init(app.device, app.renderer.renderPass);`.

### B3. `boot_imgui` (~L977) — mirror `tests/gpu_smoke.cpp`
`ImGui_ImplSDL2_InitForVulkan(app.window);` then create `app.imguiPool` (a
`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` pool, maxSets ~16, flags
`FREE_DESCRIPTOR_SET_BIT`), fill `ImGui_ImplVulkan_InitInfo` from `device`
/`renderer` (Instance, PhysicalDevice, Device, QueueFamily=`families.graphics`,
Queue=`graphicsQueue`, DescriptorPool=`imguiPool`, RenderPass=
`renderer.renderPass`, MinImageCount=2, ImageCount=`swapchain.images.size()`,
MSAASamples=1), `ImGui_ImplVulkan_Init(&info)`. (v1.91.5 auto-builds fonts on
first frame — no explicit font upload needed; match gpu_smoke.cpp.)

### B4. `frame()` (~L5241) — replace the render section
Per §4 above. Replace `ImGui_ImplOpenGL3_NewFrame()` →
`ImGui_ImplVulkan_NewFrame()`, and `ImGui_ImplOpenGL3_RenderDrawData(...)` +
`SDL_GL_SwapWindow(...)` → `ImGui_ImplVulkan_RenderDrawData(dd, cmd)` +
`renderer.end_frame(win)`. Keep all the ImGui overlay code in between.
`tod(worldTime) = (hour*60+minute)/1440.0f`.

### B5. Paper-doll cache for the UI
`ui/macro_overlay.cpp` draws paper-dolls with ImGui. After the cache stores
`gpu::VulkanTexture`, register each with
`ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_
OPTIMAL)` → returns a `VkDescriptorSet` you cast to `ImTextureID` for
`ImGui::Image`. Cache that descriptor alongside the texture.

### B6. `main()` shutdown (~L5512)
`vkDeviceWaitIdle(app.device.device);` then destroy world/subworld/macro
(they free Vulkan objects), `ImGui_ImplVulkan_Shutdown()` + `ImGui_ImplSDL2_
Shutdown()` + `ImGui::DestroyContext()`, `vkDestroyDescriptorPool(imguiPool)`,
`app.renderer.destroy(); app.device.destroy();`. Remove `SDL_GL_DeleteContext`.

### B7. `map_generator.cpp`
Delete the two `gl_make_texture_*` lines + the `td.texture`/`riverTexture`
fields (`map_generator.h`) + `destroy_terrain`'s `glDeleteTextures`. The macro
renderer uploads `td.rgba`/`riverData` itself now.

### B8. `write_smoke_frame_ppm` (glReadPixels)
Test-only capture. Either implement a Vulkan swapchain-image readback
(`vkCmdCopyImageToBuffer` into a HOST_VISIBLE buffer after `end_frame`), or stub
it to `return true` for now (note it in the plan/commit). Do not let it block
the build.

### B9. `CMakeLists.txt`
- `IMGUI_SOURCES`: remove `imgui_impl_opengl3.cpp`, add
  `${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp`.
- Remove the game's OpenGL: `find_library(OPENGL_FRAMEWORK ...)`,
  `find_package(OpenGL ...)`, and both `${TIMAERT_OPENGL_LIB}` links. Keep
  `Vulkan::Vulkan`.
- `add_dependencies(timaert timaert_shaders)` and ensure the built `.spv` land
  next to the binary (same mechanism the harness uses via `SDL_GetBasePath`).
- Delete the `EMSCRIPTEN`/WASM branches and the `IMGUI_IMPL_OPENGL_ES3` define.

### B10. Delete GL
`rm -r src/gl`. Remove every `#include "gl/gl.h"` / `"gl/helpers.h"` and the
`sm::gl_load_functions` reference. Delete the GL `renderer_3d`/`sky`/`textures`
/`tree_atlas` bodies (now replaced by the `vk_*` versions) — or, if you ported
in place, they're already Vulkan. Delete `src/macro/macro_renderer.{h,cpp}`
(replaced by `MacroRendererVk`).

### B11. Build + run
`cmake --build build` clean (0 warnings). Then **hand the human a launch**:
`DYLD_LIBRARY_PATH=/opt/homebrew/lib ./build/timaert` — expect the title menu +
(new game) the macro map, and a 3D subworld on Enter. Validation clean.

---

## PHASE C — Polish (after it runs)

1. **Push→UBO for AMD.** Move the per-frame matrices out of the 176 B pushes
   into a per-frame UBO (descriptor set) so `maxPushConstantsSize=128` GPUs
   work. macOS/MoltenVK is fine without this; do it before Windows/Linux ship.
2. **Macro landmark glyphs + night lights** (`macro.frag`): the deferred parity
   gap — city/village glyphs + the culled 64-light night glow from the GL kFS.
   Add a 5th `u_landmarkMap` texture + a lights UBO.
3. **Docs.** Update `ARCHITECTURE.md` (drop GL/2D-subworld language, describe the
   Vulkan passes), `README.md`, `microworld.md`. Delete `matwej.md`'s stale
   `renderer_.upload` note.

---

## 5. Gotchas & invariants (read before you touch code)

- **`macro.frag` push is 52 B, FRAGMENT stage.** `MacroRendererVk` already
  matches it. Don't change one without the other.
- **Feature/zone/river are R8 byte grids uploaded as RGBA8 (byte in R).** The
  shader does `round(tex.r*255)`. `MacroRendererVk::upload` shows the expansion.
- **`u_seed` for the macro map is hardcoded 1.0** (matches GL).
- **Vulkan `gl_FragCoord.y` is top-down** — `macro.frag` already flips
  (`uv.y = 1.0 - uv.y`). 3D shaders use `vk_perspective` (Y-flip baked in).
- **Shadow pass is a SEPARATE render pass, recorded before `begin_render_pass`.**
  That is why the subworld renderer needs `record_shadow` + `record_main`.
- **Paper-doll cache has two consumers** (3D renderer + UI). Port once, feed
  both; UI path needs `ImGui_ImplVulkan_AddTexture`.
- **`textures.{cpp,h}` (TileAtlas) and `tree_atlas.{cpp,h}` are needed** by the
  GL `renderer_3d` — do not delete them until their Vulkan replacements exist.
- **Within-build same-seed reproduction must hold** (save/load regenerates from
  seed). Rendering never touches saves, so the flip can't break saves — but the
  P6-1 CPU generator must keep producing the same field it does now.
- **Run with `export DYLD_LIBRARY_PATH=/opt/homebrew/lib`** to get validation
  layers on macOS; expect `validation=1`, zero VUIDs.

---

## 6. Suggested commit sequence (each = green build)

```
A1 terrain  → A2 sky → A3 water → A4 trees → A5 structures →
A6 shadows  → A7 npc → A8 paperdoll        (PHASE A, new vk_* files, unused)
B  the flip (engine record-split + main.cpp + CMake + delete GL)   (one step)
C1 push→UBO → C2 macro landmarks/lights → C3 docs                  (polish)
```

If you must stop, stop after any lettered step with a green build and note which
step is next here.
