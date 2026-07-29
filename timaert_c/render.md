# Rendering — Timaert (Vulkan)

Source of truth for the **Vulkan render path**. Companion to
[vulkan.md](vulkan.md) (backend + GPU-assisted compute) and
[ARCHITECTURE.md](ARCHITECTURE.md) §Rendering & Compute Backend.

> **Status (2026-07-12).** The shipping game uses the Vulkan path for macro and
> subworld rendering. The subworld renderer has terrain, sky, water, structures,
> tree billboards, paper-doll NPC billboards, and one full-subworld directional
> shadow map. The 2D macro view is the macro fragment synth
> ([shaders/macro.frag](shaders/macro.frag)).

All backend objects live in `src/gpu/`; game logic never includes Vulkan
headers. Shaders are GLSL compiled to SPIR-V by `glslc` at build time (see
[vulkan.md](vulkan.md) §Shader toolchain).

---

## Frame structure

Each subworld frame is **two passes** recorded into one command buffer: a
depth-only **shadow pass** (scene from the sun's point of view) followed by the
**main pass** (scene from the camera). The split is why
[vk_renderer.h](src/gpu/vk_renderer.h) exposes `acquire_frame()` /
`begin_render_pass()` separately instead of only `begin_frame()`.

```mermaid
flowchart TD
    A[acquire_frame: wait fence, acquire image, begin cmd] --> B[shadowMap.begin]
    B --> C[object casters only]
    C --> D[shadow_bb: tree silhouettes from sun]
    D --> Ds[shadow_struct: wall/house boxes from sun]
    Ds --> Dn[shadow_npc: paper-doll alpha silhouettes from sun]
    Dn --> E[shadowMap.end -> DEPTH_STENCIL_READ_ONLY]
    E --> F[begin_render_pass: main color+depth]
    F --> G[Sky: fullscreen, depth OFF]
    G --> H[Terrain: mesh + PCF shadow sample]
    H --> I[Trees: instanced billboards]
    I --> Is[Structures: instanced boxes + PCF shadow sample]
    Is --> In[NPCs: paper-doll atlas billboards]
    In --> J[Water: transparent plane, depth read / no write]
    J --> K[end_frame: end pass, submit, present]
```

**Draw order and depth policy** (main pass):

| # | Pass | Pipeline | Depth test | Depth write | Blend |
|---|------|----------|-----------|-------------|-------|
| 1 | Sky | `create()` fullscreen | off | off | off |
| 2 | Terrain | `create_mesh()` | LESS | on | off |
| 3 | Trees | `create_mesh()` instanced | LESS | on | off (alpha-test discard) |
| 4 | Structures | `create_mesh()` instanced | LESS | on | off |
| 5 | NPCs | `create_mesh()` instanced | LESS | on | alpha |
| 6 | Water | `create_mesh()` stride 0 | LESS | **off** | alpha |

Sky is drawn first as a backdrop; everything else depth-tests over it. Water is
last so it reads existing depth and blends without occluding terrain, billboards,
or structures.

---

## Dynamic lighting

Lighting is driven entirely by a **time-of-day** scalar `tod ∈ [0,1)` and is
**extensible** — every pass takes the same sun/ambient inputs, so adding a light
consumer is a push-constant field, not an engine change.

- **Sun direction** — `sunAng = (tod − 0.25)·2π`; `sunDir = (cos, sin, 0)`.
  Shaded receivers use this as `L`, the direction **from the world toward the
  sun**, for `N·L`. Do not negate it when filling `MeshPush::sunDir`; negating it
  makes horizontal terrain receive no direct sun term, so ground shadows become
  invisible.
- **Sun colour** — warm orange near the horizon, neutral white overhead, scaled
  by a day-intensity `smoothstep` of the sun elevation (zero at night).
- **Ambient** — cool blue moonlight at night lerping to neutral grey by day, so
  night is **moonlit, never black**. Ambient is applied *unshadowed* (shadows
  only attenuate the sun term). The night floor is kept deliberately **low** so
  the directional moonlight (next) does the sculpting, not a flat ambient wash.
- **Moonlight (directional).** At night the moon is not merely ambient fill — it
  is a *weak directional light in its own right*, the anti-solar point
  `moonDir = -sunDir`. `compute_light_parameters`
  ([src/sub/lighting.h](src/sub/lighting.h)) folds it onto the **same**
  `sunDir`/`sunColor` slot the sun uses: as the sun sinks, a cool blue term
  (`{0.60, 0.70, 1.00}` × `kMoonDirGain`, currently `0.42`) fades up and the
  direction flips to `-sunDir`, so the one "sun" slot every shader already reads
  carries **whichever body is up**. The world stays *directionally sculpted* at
  night (relief, not a uniform grey), and because the light now arrives from the
  moon *above* rather than the sun *below the horizon*, it does **not**
  re-introduce the night-glow the contract below guards against. This one bearing
  is exactly what the visible moon disc ([sky.frag](shaders/sky.frag)) and the
  water specular ([water.frag](shaders/water.frag)) use — a **single celestial
  direction**, so the moon you see, the moonlight that lights the ground, and the
  reflection on the water all agree.
- **Point lights (positional).** On top of the one directional body, the world
  carries up to `kSubworldMaxLights` (32) **positional** lights — torches, the
  player's carried lantern, spell / projectile glows, lit windows. They live in
  a per-frame **storage buffer** at **set 0 / binding 1** (the same set every lit
  pipeline already binds for the shadow sampler), summed by `point_lights()` in
  [lighting.glsl](shaders/lighting.glsl) *additively* over `lit_surface()`:
  `col += base · Σ light.color · gain · atten · N·L`, with a smooth quadratic
  `atten = clamp(1 − d/radius, 0, 1)²`. The sum is **unshadowed** (the sun
  shadow map does not gate it) and returns exactly `vec3(0)` when the count is
  zero, so the whole feature is provably inert until an emitter exists.
  - *One universal source.* Any subworld entity may carry a `LightEmitter`
    ([src/ecs/components.h](src/ecs/components.h)); the renderer's
    `gather_point_lights()` packs **every** `view<Position, LightEmitter,
    SubworldTag>` entity into the buffer each frame with no per-emitter code —
    the player lantern, an NPC torch and a fireball glow all take the identical
    path. Positions are built in the same window/composite space as terrain
    `vWorld` (`tile_to_world` for XZ, `sample_height_m` for ground Y) plus the
    emitter's metres offset, so a light lines up with the surface it lights.
  - *No-stall upload.* The buffer is a **persistently-mapped host-visible ring**
    (one per frame in flight, `create_host_mapped`). It is written straight
    through the mapping in `record_main` *after* `acquire_frame` reset the
    frame's fence — the slot is GPU-idle, so there is no staging copy, no barrier
    and no queue stall (the no-stall transfer contract).
  - *First emitter — the player lantern.* The player entity carries a warm
    `LightEmitter` (radius 16 m, intensity 1.35, RGB `{1.00, 0.72, 0.42}`, seated
    1.2 m up). Because it is additive over the directional term it reads as a
    warm pool at night and is washed out by daylight on its own — verified by eye
    at 01:00 (clear warm ground pool against the cool moonlit distance) and 08:00
    (indistinguishable from no lantern). Possession moves only `PlayerTag`, so
    lighting simply follows whatever body is possessed with no special case.
  - *Second emitter — travelling spell bolts.* Every spell projectile from the
    shared `emplace_projectile()`
    ([src/content/spells/registry.cpp](src/content/spells/registry.cpp) —
    fireball, ice shard, magic bolt, lightning chain) carries a `LightEmitter`
    built by `bolt_light()` from data the spell already passes: the light colour
    is the bolt's **sprite element tint** normalised so the brightest channel is
    1 (a pale ice tint still reads bright), and the reach/intensity scale from
    the projectile radius (a fat fireball throws a wider, brighter pool than a
    thin bolt). One formula, no per-spell code — a new spell lights in its own
    colour for free. Because the light rides the bolt's `Position`, which the
    projectile sim advances every tick, the pool **travels with the bolt** and
    sweeps the ground as it flies. Note this is also the bolt's *only* visual
    presence in the 3D subworld: the sprite pass skips `archetype == 0xFF`
    (projectiles), so the glow is what you see. Verified at 01:00 (settlement
    walls bathed in warm fireball light; `cast_bolt_capture` reports `lit=1`
    and, after a short pre-capture flight, `alive=1`).

The shaded surface colour — **defined once** for every lit object in
[shaders/lighting.glsl](shaders/lighting.glsl) as `lit_surface()`:

```
col = base · (ambient.rgb + sunColor.rgb · sunTerm · shadowFactor)
```

Every lit fragment stage — terrain ([mesh.frag](shaders/mesh.frag)), structures
([struct.frag](shaders/struct.frag)), and the tree / NPC / creature billboards
([billboard](shaders/billboard.frag), [npc](shaders/npc.frag),
[creature](shaders/creature.frag)) — does `#include "lighting.glsl"` and calls
`lit_surface()`, so the day/night response lives in **one place** and cannot
drift or be re-implemented (subtly wrong) per shader. Only the `sunTerm`
differs: terrain and structures quantise `N·L` to 4 bands for a pixel-retro
look; billboards pass a flat constant (`0.7` trees/creatures, `0.75` NPCs) since
they have no meaningful per-pixel normal.

**Night-glow contract (universal day/night switch).** Because `sunColor` carries
the day-intensity — the **sun's** contribution scaled to zero as it drops below
the horizon in `compute_light_parameters`
([src/sub/lighting.h](src/sub/lighting.h)) — the direct term
`sunColor · sunTerm · shadow` falls away together for *every* object as the sun
sets, with no per-object drift. At night that same slot is **repurposed to carry
the moon** (the weak cool term from `-sunDir`, above), so the world is still lit
directionally, just from overhead instead of from a sun below the horizon. This
is deliberately the single switch. An earlier build left the intensity out of
`sunColor` and open-coded the combine in each shader, so billboards (flat sun
term) and vertical wall faces (`N·L` still catches the below-horizon sun's large
horizontal component) "glowed" at night, while flat terrain escaped only by
geometry (upward `N·L ≤ 0` against a sun that is down). Fold intensity in at the
source + combine in one place and all object classes track day/night together —
and the moon, riding the same slot, inherits that safety for free (its light
comes from *above*, so it lights without glowing). Do **not** re-scale `sunColor`
in a shader or add a per-shader ambient floor — either re-introduces the glow.

The subworld game maps `tod` from `WorldTime`; see
[src/sub/lighting.h](src/sub/lighting.h) `compute_sun()` for the production
sun/colour curves this harness mirrors.

---

## Macro night lighting (2D map)

The flat top-down macro view ([shaders/macro.frag](shaders/macro.frag)) has its
own night-glow system, separate from the subworld sun/shadow path above. A
per-cell **light field** is baked on the CPU whenever the world changes and
sampled once in the macro synth's `nightDarken` stage:

- **Emitters** are enumerated data-drivenly from world state
  (`collect_macro_lights`): settlements/villages glow population-scaled, active
  spires at a fixed strength, off each type's `LandmarkDef.lightColor`.
- **Spread** is terrain-occluded — a bounded Dijkstra over the feature grid's
  per-feature optical cost (roads carry light furthest, forest canopy smothers
  it), so light flows over open ground and *around* dense stands. With no feature
  layer it falls back to an exact Euclidean radial.
- **Brightness** has one director knob, `kMacroGlowGain` (`macro_lighting.h`),
  applied in the bake before the `kMacroGlowCeil` clamp/encode.
- **Upload** is surgical: `vk_macro_renderer::upload_light_field` rewrites only
  descriptor **set 0 / binding 4**, leaving the master/feature/zone/river synth
  inputs live.
- **Re-baked** on world-gen, save-load, and daily population drift only — never
  per frame.

The shader decodes (`· kMacroGlowCeil`) and adds the glow scaled by the same
`nightDarken` day/night curve that darkens the base map. Full pipeline, cost
table, rebake triggers and the mountains→biome occlusion caveat:
**[macro-lighting.md](macro-lighting.md)**.

---

## Shadow mapping

This is the answer to the standing requirement: **no floating shadow blobs — cast
shadows must land on the terrain and on other objects.** We render a real
depth-map from the sun and sample it with PCF.

### Shadow resource — [vk_shadow.h](src/gpu/vk_shadow.h)

`gpu::VulkanShadowMap` owns one square depth target:

| Property | Value |
|----------|-------|
| Size | 4096×4096 |
| Format | `VK_FORMAT_D32_SFLOAT` |
| Usage | `DEPTH_STENCIL_ATTACHMENT` + `SAMPLED` |
| Sampler | `NEAREST`, clamp-to-edge |
| Render pass | depth-only, `CLEAR → STORE`, final layout `DEPTH_STENCIL_READ_ONLY_OPTIMAL` |
| Sync | two subpass dependencies (fragment-read ↔ depth-write) |

`begin(cmd)` opens the depth pass (clear depth = 1, viewport/scissor = size),
`end(cmd)` closes it and transitions the image to read-only so the main pass can
sample it.

### Light matrix

The sun is directional, so the shadow camera is an **orthographic** box in light
space. The production subworld uses one full-3×3-subworld shadow map instead of a
camera-centred bubble:

```
toSun    = normalize(sunDir)                  // world -> sun
fromSun  = -toSun                              // sun -> world
center   = (0, kHeightScale·0.45, 0)
lightEye = center - fromSun · 4200
lightMvp = vk_ortho(-2300..2300, -2300..2300, 0.5..9000) · lookAt(lightEye, center, lightUp)
```

`vk_ortho`/`vk_perspective` are the subworld-local **Vulkan-depth (0..1)** helpers
([src/sub/vk_camera_math.h](src/sub/vk_camera_math.h)); `core/math.h` projection
matrices are GL-style (depth −1..1) and must not be used directly for Vulkan clip
space.

### Depth-only pipeline — `create_shadow()` in [vk_pipeline.h](src/gpu/vk_pipeline.h)

- Colour blend `attachmentCount = 0` (no colour target).
- Depth test + write, compare `LESS`.
- Raster depth bias is **disabled**. Bias is applied in receiver shaders in the
  same normalized light-depth space as the shadow lookup; large raster bias on a
  full-subworld frustum erases small casters (NPCs/trees) via peter-panning.
- Push constants `VERTEX | FRAGMENT`.
- Optional descriptor set layout is supported for alpha-tested casters such as
  paper-doll NPCs.

### Casters

| Caster | Vertex | Fragment |
|--------|--------|----------|
| Trees | [shadow_bb.vert](shaders/shadow_bb.vert) — expand instance quad along sun-derived `lightRight` | [shadow_bb.frag](shaders/shadow_bb.frag) — shared `treeCoverage()` `discard` (real silhouette) |
| NPCs | [shadow_npc.vert](shaders/shadow_npc.vert) — expand instance quad along sun-derived `lightRight` | [shadow_npc.frag](shaders/shadow_npc.frag) — samples paper-doll `sampler2DArray` alpha and discards transparent pixels |
| Structures | [shadow_struct.vert](shaders/shadow_struct.vert) — expand the per-instance box (cube from `gl_VertexIndex`) by `lightMvp` | [shadow_struct.frag](shaders/shadow_struct.frag) — empty (depth only) |

**Universal billboard shadows (no bespoke silhouettes).** A billboard's shadow is
cast from its *own* visible coverage, not a hand-authored blob. Procedural trees
share `treeCoverage()` between lit and depth-only passes. Paper-doll NPCs use the
same composited 48×48 texture-array layer for lit rendering and shadow alpha, so
macro and micro NPCs come from the same character atlas and cast the current frame
silhouette. The same pattern should be reused for future sprite classes: lit pass
samples/draws coverage, shadow pass samples the same coverage and only writes
depth for opaque pixels.

### Receivers (PCF sampling)

Terrain and structures bind the shadow map at **set 0, binding 0** and sample it
with a 3×3 PCF kernel. A fragment is lit when its light-space depth (minus bias)
is nearer than the stored depth:

- **Terrain** ([mesh.frag](shaders/mesh.frag)) samples **per-fragment** at
  `lightMvp · vWorld` with normalized receiver bias
  `max(0.00008, 0.00035·(1−NdotL))`.
- **Structures** ([struct.frag](shaders/struct.frag)) sample **per-fragment** at
  `lightMvp · vWorld` with the same slope-scaled receiver bias as terrain.
- **Billboards** currently **cast but do not receive** shadow-map lighting. A flat
  billboard receiver sampled at one base point made whole sprites pop to black
  near other billboards. Future per-pixel billboard normals or sprite-depth cards
  can add receiving back without changing the shadow-map resource.

Result: terrain and structures receive PCF shadows; trees, structures, and NPCs
cast into the shared depth map. Terrain is intentionally not rendered as a caster
in this single full-subworld shadow map: self-shadowing the coarse receiver mesh
creates tile-scale zebra bands instead of useful object shadows. Shadows swing
through the day/night cycle because `lightMvp` tracks `sunDir` every frame.

### Extending shadows

- **New caster** — add a `create_shadow()` pipeline for the mesh and draw it
  inside `shadowMap.begin()/end()`. Give it the same `lightMvp`.
- **New receiver** — bind the shadow set (set 0, binding 0), push `lightMvp`,
  copy the `shadowFactor`/`treeShadow` helper, multiply the sun term by it.
- **Softer shadows / cascades** — widen the PCF kernel, or add a second
  `VulkanShadowMap` at a tighter ortho box and select by view distance. No API
  change beyond another descriptor binding.

---

## Sky and stars

[shaders/sky.frag](shaders/sky.frag) renders a **texture-free** procedural
celestial dome from a camera-basis push (`forward/right/up`, resolution, fov,
`tod`, fog colour, time). One fullscreen triangle
([fullscreen.vert](shaders/fullscreen.vert)), depth off, drawn first.

Layers: day/night/twilight gradient, sun disc + glow + horizon scatter, a
**prominent two-lobe moon** (near-white disc + tight core bloom + wide cool halo,
sized to read as the night's light source) fixed at the anti-solar point
`-sunDir` so it sits over the exact bearing the terrain is lit from and the water
road points back toward, **three equirectangular star densities** + a Milky-Way
band (`dot(rd, mwN)`) + per-star twinkle and colour temperature, and FBM clouds.
This is intentionally richer than the old baked star texture.

---

## Terrain and trees

- **Terrain** — a heightmap quad mesh (shipping: 192×192 quads, harness:
  128×128) with central-difference normals, indexed triangles, lit + shadowed as
  above. Vertex = `{vec3 pos, vec3 normal, vec2 uv}`, where `uv` is the
  normalised grid position (`0..1`). Ground colour is a **procedural per-biome
  synth** ([shaders/mesh.frag](shaders/mesh.frag) `groundColor`/`materialBase`),
  but the **material id that drives it is sampled per-fragment, not interpolated
  from the mesh vertices**. The renderer bakes a full-resolution R8 tile-material
  texture (`u_material`, descriptor **set 1**) in `upload()` — one texel per
  world tile, independent of the terrain tessellation — and the fragment stage
  looks it up at `vUv`.

  This is the fix for roads / field bands / shorelines rendering as
  **disconnected blobs (пятна)** instead of connected lines. The mesh is coarse
  — ~16 world tiles between vertices at 192² (more at the harness's 128²) — so a
  1-tile-wide road carried as a *per-vertex* material attribute simply falls
  between vertices and dissolves. Sampling the id *per-fragment* from the
  full-res grid keeps thin features crisp and continuous, exactly like the TS
  authority's per-fragment `u_tileGrid` lookup (`v_uv = a_pos` in renderer-3d).
  The synth then layers the quantised pixel-art per-material variation on top (no
  atlas, same philosophy as the macro synth). In the shipping game the ids come
  from the seamless tile grid ([src/sub/renderer_3d.h](src/sub/renderer_3d.h)),
  resolved once per biome cell while baking; the harness fakes a small grid
  (biome-by-height + a cross road) so the standalone smoke drives the identical
  path. The set-1 material descriptor is allocated once and rewritten on each
  `upload()` (load-time / seam-cross only — never per frame) — and, on a seam
  crossing, re-pointed to the ping-pong sibling image after an on-GPU relocation
  (see §Seam crossing below and [seamless-crossing.md](seamless-crossing.md)).

  *Next polish:* the per-material **surface** variation still reads
  "fabric-like" (a woven micro-pattern) rather than natural ground. The material
  routing above is correct and shipped; it is the texture synth inside
  `groundColor` that still needs a pass.
- **Trees** — **instanced procedural billboards**. One `vkCmdDraw(6, treeCount)`
  draws the whole forest: the quad corners come from `gl_VertexIndex`, and a
  per-instance buffer supplies `{vec3 pos, size, species, seed}`. The fragment
  stage draws 7 species (pine/birch/willow/jungle/oak/cherry/autumn) **per pixel**
  keyed by species+seed — no atlas, no per-tree CPU cost, full variety. Camera
  facing is cylindrical (world-up stays vertical, right follows the camera).
  The macro-map tree decor in [macro.frag](shaders/macro.frag) intentionally keeps
  the old irregular single-cell blob as the visual base, then uses the project's
  3×3 context rule only to add small neighbouring crown caps across shared
  forest edges/corners. That preserves crisp organic tree shapes, avoids square
  forest fills or one-direction smears, and lets neighbouring biomes/temperatures
  mix their tree colours naturally on forest borders.
- **NPCs** — **instanced paper-doll billboards**. The micro-world uses the same
  composited character atlas frames as the macro overlay via
  `character::PaperdollAtlas` + `gpu::SpriteArray` (48×48 `sampler2DArray`).
  `prepare_frame()` resolves the current `AnimationState` (`Idle`/`Walk` plus
  direction from `SubworldAi::vx/vy`), uploads any newly composed layers before
  the render pass, and fills the NPC instance buffer with `{pos, size, layer}`.
  The lit pass samples [shaders/npc.frag](shaders/npc.frag); the shadow pass
  samples the same layer alpha in [shaders/shadow_npc.frag](shaders/shadow_npc.frag).

## Water

[shaders/water.vert](shaders/water.vert) builds a flat quad at `waterLevel`
straight from `gl_VertexIndex` (no vertex buffer — the pipeline is created with
`vertexStride = 0`). [shaders/water.frag](shaders/water.frag) animates a wave
normal from two drifting noise fields, then adds a Fresnel sky reflection, a
**sun/moon specular highlight**, and a depth tint; output alpha `0.82`.
Depth-test on, depth-write off, alpha blend — so it fills valleys below the water
line while hills poke through.

The specular is a **half-vector two-lobe** model sharing the one
`sunDir`/`sunColor` slot, so it serves the sun by day and the moon by night with
no branch: a tight core glint (`pow(N·H, 80)`) is the compact daytime highlight,
and a far wider lobe whose spread is gated by `1 − |L.y|` opens **only when the
light sits low** over the horizon — smearing the reflection into the long
shimmering "glitter road" (the *лунная дорожка*) that points back at the viewer
for a setting sun or a risen moon, while an overhead midday sun keeps a compact
spot (the daytime look is unchanged). The road stages at real shorelines; a
landlocked or massif-occluded spawn (e.g. seed `12345`) may show no open water
along the celestial bearing — use `TIMAERT_SMOKE_WATERSCAN` (see §Frame capture)
to find a coast.

## Structures (walls & houses)

City walls and houses render as **instanced boxes** — the same
geometry-from-`gl_VertexIndex` trick as the trees/water, so one
`vkCmdDraw(36, structCount)` draws the whole settlement with **no geometry vertex
buffer**. The per-instance buffer supplies `{vec3 centre, vec3 halfExtent, type,
seed}`; [struct.vert](shaders/struct.vert) builds a unit cube with outward face
normals and scales/translates it per instance. [struct.frag](shaders/struct.frag)
keys colour off `type` (stone wall vs. tan house body + red-brown roof band) and
lights it with the shared sun + ambient + PCF shadow. Walls and houses both cast
([shadow_struct.vert](shaders/shadow_struct.vert)) and receive shadows.

**Extensible by design:** a new structure kind (tower, gate, ruin, bridge) is one
more `type` value + one branch in the fragment stage — no new pipeline, no new
geometry. The shipping game feeds the real `Structure[]` records
([src/sub/map_data.h](src/sub/map_data.h)) — which already exist for walls,
houses and bridges — in place of the harness settlement, and can swap the box for
a pitched-roof or arbitrary mesh without touching the pass wiring.

---

## Seam crossing (incremental terrain upload)

`Renderer3DVk::upload()` rebuilds the terrain mesh + material image from the
seamless manager. It runs **load-time / on seam-cross only, never per frame**,
and is *scoped* by a `CompositeDirty` struct so a boundary crossing costs
**O(new content)** instead of a full 3072² rebuild. Full design +
verification + gotchas are in **[seamless-crossing.md](seamless-crossing.md)**;
the renderer-side mechanics in brief:

- **Three modes**, chosen from `CompositeDirty`: **full** (first upload, height
  smooth, or the two-crossing fallback), **shift** (a re-centre), **per-cell** (an
  async worker drain stitched one 1024-tile cell at a time).
- **Height (shift)** — the persistent `Nv×Nv` vertex grid `heightVtxM_` is slid
  in place by `std::memmove` (toroidal, in vertices), then only the clamped 1-vertex
  border ring + the fresh cells are resampled. The vertex buffer is rebuilt whole
  from the grid (trivial) so all normals stay correct.
- **Material (shift)** — a **GPU ping-pong**: two R8 images
  (`materialTex_ ↔ materialTexAlt_`). One `vkCmdCopyImage` relocates the unchanged
  6/9 (axis) or 4/9 (diagonal) overlap on the GPU, `vkCmdCopyBufferToImage` fills
  only the fresh cells, then the two images `std::swap` and the set-1 descriptor is
  rewritten to the new front (`gpu::blit_shift_r8`,
  [vk_texture.cpp](src/gpu/vk_texture.cpp)). Valid because
  `material_new[cell] == material_old[shifted-from cell]` over the overlap.
- **Fence contract** — `upload()` runs at the same fenced point as the in-place
  image updates, so the swap + `vkUpdateDescriptorSets` are safe with no in-flight
  frame sampling the image; validation reports zero new barrier/layout errors.

Result: the crossing frame's `upload3d` dropped from **11.2 ms → 6.5 ms** — one
frame, well under the 16.6 ms / 60 fps budget — with GPU-readback and
FP-tolerance self-checks proving the incremental result byte/precision-identical
to a full rebuild.

---

## Push-constant layouts

All matrices + lighting travel as push constants (`VERTEX | FRAGMENT`).

| Pass | Struct | Bytes | Contents |
|------|--------|-------|----------|
| Macro synth | `Push` (macro.frag) | 32 | resolution, mapSize, viewCells, seaLevel, seed, time |
| Sky | `SkyPush` | 80 | forward, right, up, (resX,resY,fov,tod), (fogRGB,time) |
| Terrain | `MeshPush` | 176 | mvp, sunDir, sunColor, ambient, lightMvp |
| Trees | `BbPush` | 176 | mvp, camRight, sunColor, ambient, lightMvp |
| Structures | `MeshPush` (reused) | 176 | mvp, sunDir, sunColor, ambient, lightMvp |
| Water | `WaterPush` | 128 | mvp, camPos, sunDir, sunColor, (time,ambient,waterLevel,extent) |
| Shadow (mesh) | `ShadowPush` | 64 | lightMvp |
| Shadow (trees) | `ShadowBbPush` | 80 | lightMvp, lightRight |
| Shadow (struct) | `ShadowPush` (reused) | 64 | lightMvp |
| Shadow (NPC) | `ShadowBbPush` | 80 | lightMvp, lightRight |

> **Portability.** MoltenVK allows 4096-byte pushes, so 176 B is fine on macOS.
> **AMD desktop caps `maxPushConstantsSize` at 128**, so before broad Windows/GPU
> support the per-frame matrices (mvp / lightMvp) should move into a per-frame UBO,
> keeping only small per-draw data in push constants.

---

## Pipeline factory — [vk_pipeline.h](src/gpu/vk_pipeline.h)

One `gpu::VulkanPipeline` type, three constructors:

| Method | Use | Notes |
|--------|-----|-------|
| `create()` | Fullscreen fragment passes (macro synth, sky) | No vertex input; depth-stencil state present but disabled (valid against the depth render pass); optional descriptor-set layout |
| `create_mesh()` | Terrain, trees, structures, NPCs, water | Vertex input binding (rate VERTEX or INSTANCE); depth test/write, optional blend, optional back-face cull, one or more descriptor-set layouts. **`vertexStride == 0` ⇒ no vertex input** (geometry from `gl_VertexIndex`, used by water) |
| `create_shadow()` | Depth-only casters | No colour attachment; optional descriptor-set layout for alpha-tested sprite casters; used inside the shadow pass |

Adding a pass = pick the right constructor, add the SPIR-V pair to the `glslc`
`foreach` in [CMakeLists.txt](CMakeLists.txt), record the draw in the correct
phase. No new pipeline abstraction needed.

---

## Frame capture (visual self-check)

The renderer can dump a rendered frame to a PNG so an agent (or a human) can
**look at the actual image** instead of trusting that a smoke "passed". It is
test/tooling only and degrades to a clean no-op where unsupported.

- **Swapchain** — [vk_swapchain.cpp](src/gpu/vk_swapchain.cpp) adds
  `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` to the presentable images when the surface
  supports it and records that in `VulkanSwapchain::transferSrc`; capture is a
  no-op when absent.
- **Renderer** — [vk_renderer.h](src/gpu/vk_renderer.h): arm with
  `request_capture()` **before** `end_frame()`; `end_frame()` copies the presented
  image into a persistent host-visible buffer inside the same command buffer;
  drain with `take_capture(px, w, h, fmt)` **after** `end_frame()`. Pixels come
  back in the swapchain's native format (BGRA on MoltenVK).
- **App smoke** — [main.cpp](src/app/main.cpp) `write_smoke_frame_png()` swizzles
  BGRA→RGBA, forces opaque alpha, and writes via `stb_image_write`. Triggered from
  a smoke script by the **`capture_frame`** action token.

Environment knobs (all opt-in; the smoke composes them, so one run can pose the
camera, set the hour, and dump a frame):

| Env var | Effect |
| --- | --- |
| `TIMAERT_SMOKE_SCRIPT` | comma-separated action tokens; include `capture_frame` to dump |
| `TIMAERT_SHOT_PATH` | output PNG path (else `/tmp/timaert_shot_<NN>_<label>.png`) |
| `TIMAERT_SMOKE_SEED` | world seed |
| `TIMAERT_SMOKE_HOUR` | force the game clock to `0..23` (picks day vs night lighting) |
| `TIMAERT_SMOKE_YAW` / `_PITCH` | camera aim in degrees (yaw `0` = `+X`) |
| `TIMAERT_SMOKE_SUBPOS` | teleport the player to `"x,y"` in the subworld |
| `TIMAERT_SMOKE_WATERSCAN` | report the longest east–west open-water run (find a coast to stage the moon road) |
| `cast_bolt_capture` (action) | cast a spell, assert the bolt spawned with a `LightEmitter`, optionally fly it clear, then arm capture in the same step |
| `TIMAERT_SMOKE_SPELL` | which spell `cast_bolt_capture` casts; default `fireball` |
| `TIMAERT_SMOKE_BOLT_FLIGHT` | seconds to fly the bolt clear of the caster before the shot (clamped `0..0.30`) |

Example — a night frame looking back along the moon's bearing:

```
TIMAERT_SMOKE_HOUR=1 TIMAERT_SMOKE_YAW=180 TIMAERT_SHOT_PATH=/tmp/moon.png \
  TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,subworld_enter,capture_frame,quit" \
  ./build/timaert
```

The app smoke self-terminates on the `quit` token. (The separate GPU harness
[gpu_smoke3d](tests/gpu_smoke3d.cpp) instead auto-exits after `GPU_SMOKE_FRAMES`
frames, default `600`; `GPU_SMOKE_FRAMES=0` is its unbounded interactive mode.)

## Design requirements (standing)

- **3D relief must match the 2D map.** The 2D view *is* the map / minimap (the
  macro synth — [shaders/macro.frag](shaders/macro.frag)) and must stay
  beautiful; the first-person 3D relief has to read as the **same** world,
  **especially mountains**. In the game both views consume the same macro
  heightmap, so the correspondence is structural — keep it that way (never invent
  3D relief the map does not show). Mountains are the sensitive case: tall and
  readable, never spiky aliasing.
- **Everything is extensible.** More biomes, landmarks and features are expected,
  and each adds render passes + context. Keep passes data-keyed (biome id,
  structure `type`, feature id) so growth is new table rows / new `type` values,
  not new engine branches.

## What is not implemented yet

Do not cite these as current visual evidence:

- **Terrain surface synth** — the material *id* is now sampled per-fragment from
  the full-resolution tile grid (see §Terrain and trees), so roads/fields/
  shorelines are crisp and connected. What remains is the per-material **surface
  texture**: it still reads "fabric-like" (a woven micro-pattern) rather than
  natural ground. This is a `groundColor` synth polish, not a data-routing gap.
- **Billboard shadow receiving** is intentionally disabled for now. Billboards cast
  silhouettes into the shadow map, but only terrain/structures receive PCF shadows;
  the old flat base-point receive path made whole sprites pop to black.
- **Richer structures** — walls + houses render as lit, shadowed boxes; pitched
  roofs, bridges and arbitrary `Structure` meshes still map onto the same pass.
- **Bare-mountain light occlusion (macro map)** — the macro night-light bake
  (§Macro night lighting) occludes glow through *forest* (the feature grid) but
  not through treeless mountain massifs, which are now a **biome** (elevation),
  not a feature. Restoring it needs an elevation sample in the bake — a planned
  follow-up. See [macro-lighting.md](macro-lighting.md) §7.

The **2D view is the map / minimap**, not a separate tile renderer to port — it
is already the macro synth ([shaders/macro.frag](shaders/macro.frag)); the
first-person 3D view is the subworld renderer.

See [vulkan.md](vulkan.md) for the backend module map and the GPU-driven
simulation plan.
