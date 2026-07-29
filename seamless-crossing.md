# Seamless Crossing — Timaert

Source of truth for **how a subworld cell-boundary crossing stays invisible**:
no hitch, no texture/lighting pop, no vanishing structures. This is a
cross-cutting system — it spans the seamless manager ([microworld.md](microworld.md)),
the Vulkan terrain upload ([render.md](render.md)), and one GPU-texture
primitive ([vulkan.md](vulkan.md)) — so it gets its own doc. Read those three
for the surrounding subsystems; read this for the crossing itself.

> **Status (2026-07-28).** SHIPPED. The player crosses 1024-tile cell
> boundaries with the 3×3 window re-centring underneath them and there is no
> perceptible frame. Verified in-game plus by self-check (below). The crossing
> is now **O(new content)**, not O(whole 3×3 grid).

---

## The problem

The player stands at the centre of a seamless **3×3 grid** of 1024×1024 cells
(3072×3072 tiles total — see [microworld.md](microworld.md)). When they walk
across a cell boundary the window must **re-centre by one cell** so the player is
always in the middle cell with eight generated neighbours around them.

The naïve re-centre regenerates the exposed neighbours and re-uploads the
**entire 3072² composite** to the GPU every crossing — the terrain height mesh
*and* the per-tile material image. On this project that is a ~11 ms upload spike
(a dropped frame) plus, historically, three visible artefacts. Four distinct
defects, four fixes:

| # | Defect | Root cause | Fix | Lives in |
|---|--------|-----------|-----|----------|
| **Inc 1** | Ground texture / lighting **pops** at the crossing | procedural ground synth was keyed to composite-*local* coords, which jump ±1 cell on re-centre | anchor the synth to **absolute world coords** (composite origin packed into unused `sunDir.w`/`sunColor.w` push lanes) | [mesh.frag](shaders/mesh.frag), `groundOrigin*` in [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp) |
| **Inc 2** | An adjacent city **vanishes then rebuilds** | structures were re-spawned relative to the moving window | spawn each cell's structures from its **absolute macro context**; on a crossing rebase/evict/spawn-only-new | [seamless_manager.cpp](src/sub/seamless_manager.cpp) |
| **Inc 3b** | Multi-frame **microfreeze cascade** as async cells drain in | the whole composite re-uploaded on *every* async drain | make the upload **incremental per dirty cell** via `CompositeDirty` | [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp) |
| **Inc 3c** | Residual **~11 ms full-upload hitch** on the crossing frame itself | the crossing frame still did one full 3072² height + material rebuild | **GPU toroidal shift**: relocate the unchanged overlap, rebuild only the fresh cells | [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp), [vk_texture.cpp](src/gpu/vk_texture.cpp), [seamless_manager.cpp](src/sub/seamless_manager.cpp) |

This doc focuses on **Inc 3c** — the toroidal shift — because it is the hardest
and the one with the most reusable insight. Inc 1/2/3b are one-liners by
comparison and are covered where they live.

---

## The contract: `CompositeDirty`

The manager and the renderer never share code; they talk through one struct,
[`CompositeDirty`](src/sub/seamless_manager.h). Each frame the engine pulls
`consume_composite_dirty_cells()` (which clears the manager's dirty state) and
hands the result to `Renderer3DVk::upload()`. The struct says *exactly* what
changed so the renderer rebuilds only that:

| Field | Meaning | Renderer response |
|-------|---------|-------------------|
| `fullHeight` / `fullMaterial` | rebuild everything of that kind | full 3072² resample / rebuild (first build, height smooth, fallback) |
| `heightCells[9]` / `materialCells[9]` | which 1024-tile cells changed (idx = `oy*3+ox`) | rebuild only those cell rects |
| `shiftX` / `shiftY` | the composite CPU buffers were just toroidally shifted by this many cells (±1 each) | **slide** the GPU material image + CPU height grid the same way, then rebuild only the fresh cells |
| `structs` | the tree/structure instance set changed | rebuild instance buffers |

Three upload modes fall out of this: **full** (first upload / smooth /
fallback), **shift** (a crossing), **per-cell** (an async worker drain). The
gate logic is at the top of `upload()`
([vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp) `doFullHeight` /
`doShiftMaterial` / …).

### Deferred uploads and `merge()`

`consume_*` clears the manager's state, but the engine may defer an upload
across frames (`pendingUpload3d_`). So a deferred upload must hold the **union**
of every consume since — `CompositeDirty::merge()`. A shift can only be
represented cleanly against an empty pending set (its cell flags are in the
pre-shift coordinate frame), so **two undrained crossings, or a shift landing on
already-pending work, fall back to a full rebuild**. Always correct, occasionally
not the fast path — the fallback re-reads the manager's already-shifted source
arrays, which are the ground truth.

---

## Why a crossing is O(new content)

On a crossing the manager toroidally shifts its CPU composite buffers
(`composite_tiles_`, `composite_height_`) by `(shiftX,shiftY) ∈ {−1,0,1}²` and
fills **only the newly exposed cells** with deterministic placeholders
(`mark_composite_shift`, `shift_composite_buffers` in
[seamless_manager.cpp](src/sub/seamless_manager.cpp)). Worker threads generate
the real exposed cells and stitch them in later as ordinary per-cell drains.

The consequence the renderer exploits:

- an **axis** crossing keeps **6 of 9** cells (2 full columns/rows) → **3 fresh**
- a **diagonal** crossing keeps **4 of 9** cells (a 2×2 block) → **5 fresh**

The kept cells are **byte-identical** — they are the same world, just at a new
index in the window. So instead of rebuilding all 9 cells, the renderer
**relocates the overlap** (cheap) and **rebuilds only 3–5 fresh cells**.

### The shift math (shared by height and material)

Both the height memmove and the material GPU copy use the same rectangle,
computed in **tiles**, matching `shift_buffer()` / `shift_composite_buffers()`
exactly:

```
px = -shiftX * kCellSize        py = -shiftY * kCellSize          // TILES
copyW = kFullSize - |px|        copyH = kFullSize - |py|
srcX = px > 0 ? 0 : -px         dstX = px > 0 ? px : 0            // (same for Y)
```

Because `|shift| ∈ {0,1}`, `|px|,|py| ∈ {0, kCellSize}` — the overlap is always a
**single cell-aligned rectangle**, it always exists, and **overlap ∪ fresh cells
= the whole image**. The height grid uses the same formula in *vertices* (one
cell = `cellVerts = kCellSize/step = 64` vertices).

---

## Height: CPU toroidal memmove

`heightVtxM_` is the persistent `Nv×Nv` (193×193) vertex grid in metres that
`sample_height_m()` reads. On a shift it is slid in place by `std::memmove`
(mirrors `shift_buffer`), then only the parts that are genuinely new are
resampled:

1. `memmove` the overlap block by `(vpx,vpy) = (-shiftX,-shiftY)·cellVerts`
   vertices (row-by-row, direction chosen so source and destination don't clobber).
2. Resample the **1-vertex border ring**. `sampleVertex` box-averages a `±half`
   footprint and *clamps* at the composite edge, so an outer-ring vertex that the
   memmove slid inward now has the wrong (interior-sized) footprint. `half < step`
   ⇒ only the outermost ring is affected — ~4·Nv vertices, idempotent with the
   fresh-cell pass.
3. Resample the **fresh cells** flagged in `heightCells`.

The vertex buffer is then rebuilt *whole* from `heightVtxM_` (trivial ~0.1 ms) so
every central-difference normal reads the corrected grid; the savings are in the
resample, not the buffer build.

---

## Material: GPU ping-pong (`blit_shift_r8`)

The material image is a full-resolution R8 grid (one byte per tile = material id)
sampled per-fragment by [mesh.frag](shaders/mesh.frag) at **set 1** so roads and
field bands stay crisp under the coarse terrain mesh (see [render.md](render.md)
§Terrain and trees). At 3072² it is 9 MB — the expensive thing to rebuild.

The relocation is done **entirely on the GPU** with a two-image ping-pong
(`materialTex_` ↔ `materialTexAlt_`, both `TRANSFER_SRC|TRANSFER_DST`). One call
to [`gpu::blit_shift_r8`](src/gpu/vk_texture.cpp):

1. **Barrier** `src: SHADER_READ→TRANSFER_SRC`, `dst: UNDEFINED→TRANSFER_DST`
   (discard — see below).
2. **`vkCmdCopyImage`** the overlap `src → dst` (one `VkImageCopy`, no host
   round-trip). This is the 6/9 or 4/9 that didn't change.
3. **`vkCmdCopyBufferToImage`** each fresh cell's 1024² rect into `dst` from a
   host staging buffer (the CPU rebuilds only these, via a 256-entry
   `terrain_material_for` LUT per cell).
4. **Barrier** both `→SHADER_READ`, submit, fence-wait.

Then the caller **`std::swap(materialTex_, materialTexAlt_)`** and rewrites the
sampler descriptor (set 1, binding 0) to the new front image. The old front
becomes next crossing's scratch.

### The identity that makes the GPU copy valid

> `material_new[cell] == material_old[shifted-from cell]` over the overlap.

On a crossing **both** `composite_tiles_` **and** the per-cell biome shift
together (the manager slides them consistently), and `terrain_material_for(tile,
biome)` is a pure function. So the overlap is the *same bytes* after the same
in-tile slide — copying it on the GPU is exact, not an approximation. Proven by
GPU readback (below), so the invariant is enforced, not assumed.

### Two design constraints, made explicit

- **Why two images.** `vkCmdCopyImage` may not copy overlapping regions of the
  *same* image (aliasing is undefined). The destination must be distinct — hence
  the ping-pong, not an in-place slide.
- **Why `dst` starts `UNDEFINED`.** overlap ∪ fresh covers *every* texel of the
  destination, so its prior contents are fully overwritten → the layout
  transition can discard (`UNDEFINED`) instead of preserving. The overlap copy
  and the fresh copies are disjoint, so no barrier is needed between them.

---

## Verification & self-checks

All gated behind env vars (perf must be measured with them **off**):

| Env var | Effect |
|---------|--------|
| `TIMAERT_SEAM_TRACE` | per-section wall-clock, printed as `[upload3d-prof] height=… matFill=… matGpu=…` |
| `TIMAERT_SEAM_SELFCHECK` | recompute a full reference and compare the incremental result; for material, **read the image back off the GPU** and compare. Roughly doubles the work. |
| `TIMAERT_SEAM_SETTLE_MS` | real-time pacing so the async worker drains actually complete inside the headless smoke (without it the drains never fire and the incremental paths aren't exercised) |

**Material — GPU readback (definitive).** `VulkanTexture::read_back` copies the
live sampled image back to host and the self-check compares it byte-for-byte to a
from-scratch recompute from the manager's shifted tiles + biome. This catches a
wrong copy rectangle, a missed fresh cell, **or** a MoltenVK copy fault — none of
which a CPU-only mirror would see. Material ids are exact `u8` (no fast-math), so
any nonzero mismatch is a real defect. Result: `material shift mismatch=0`.

**Height — FP tolerance (not bit-exact).** The shipped TU is built with
`-ffast-math`, so `sampleVertex`'s float reduction **reassociates** across its
inlined call sites and is *not* bit-reproducible. A bit-exact `==` would report
phantom mismatches on the resampled border ring even when the grid is correct.
The self-check compares to a floating-point tolerance instead — far below any
perceptible or structural error, but a real regression (wrong cell / off-by-one
footprint ⇒ ≥1 world-unit) still stands out against the ~1e-4 noise floor.
Result: `height incremental mismatch=0/37249 maxdiff≈8.5e-4`.

Recipe for the validated headless run — set `VK_LAYER_PATH` + `DYLD_LIBRARY_PATH`, then
`TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,subworld_seam,quit"` with
`TIMAERT_VK_VALIDATION=1 TIMAERT_SEAM_SELFCHECK=1 TIMAERT_SEAM_SETTLE_MS=15`.

---

## Gotchas (hard-won)

1. **`-ffast-math` non-determinism.** Float self-checks must use a tolerance;
   only integer/byte results (material ids) may be compared exactly. This bit us
   as phantom height mismatches before the tolerance was added.
2. **Validation layers massively inflate copy/readback timings.** In a
   `validation=1` + `selfcheck=1` run, `matGpu` reads **~145 ms** — that is the
   validation layer instrumenting the image-copy and the 9 MB readback, **not the
   shipping path**. Isolated: selfcheck-on / validation-off → `matGpu = 1.35 ms`.
   **Always measure perf with BOTH validation and selfcheck OFF.**
3. **`vkCmdCopyImage` aliasing** → the ping-pong (two images), never in-place.
4. **`merge()` two-crossing fallback** is a *correctness* path, not a perf path —
   don't be alarmed to see a full rebuild if two crossings stack before a drain.
5. **`SETTLE_MS` is required** to exercise async drains in the headless smoke;
   without real-time pacing the workers never finish inside the scripted frames.

---

## Perf (shipping path — validation + selfcheck OFF)

| Metric | pre-3c | 3c-2 (height) | 3c-3 (material) |
|--------|-------:|--------------:|----------------:|
| crossing `upload3d` | 11.178 ms | 8.227 ms | **6.460 ms** |
| material `matFill` (CPU) | — | — | 1.55 ms (was 3.34) |
| material `matGpu` (copy) | — | — | 1.32 ms (was 2.05) |

Seam-cross total ≈ 8.879 ms — one frame, comfortably under the 16.6 ms / 60 fps
budget, so no frame is dropped.

---

## Extending

A new composite field that must survive a crossing (say a moisture or danger
grid) follows the same shape:

1. Add its dirty flags to `CompositeDirty` and mark them in the manager
   (`mark_composite_*`).
2. Give `upload()` a **full / shift / per-cell** path keyed on those flags.
3. Reuse the **shift math** above (tiles, `px/py/copyW/copyH/srcX/dstX`).
4. Add a self-check: exact compare if the payload is integer, FP-tolerance if it
   is float under `-ffast-math`.

**Fence contract.** `upload()` runs at the same fenced point as the existing
in-place image updates (engine `prepare_frame` / `record_shadow`), so no
in-flight frame samples the image — the `std::swap` + `vkUpdateDescriptorSets`
are safe under exactly the contract the create/`update_region` paths already
rely on. Validation confirms zero new barrier/layout errors.

---

## Constants

| Name | Value | Meaning |
|------|------:|---------|
| `kCellSize` | 1024 | tiles per cell edge |
| `kFullSize` | 3072 | composite edge (3 cells) |
| `kMeshDim` (N) | 192 | terrain quads per edge |
| `Nv` | 193 | vertices per edge (`N+1`) |
| `step` | 16 | tiles between vertices (`kFullSize/N`) |
| `half` | 8 | vertex sample half-footprint (`max(1,step/2)`) |
| `cellVerts` | 64 | vertices per cell edge (`kCellSize/step`) |
| `vertexCount` | 37249 | `Nv²` |
| `kHeightScale` | 1500 | metres per unit height |

---

See [microworld.md](microworld.md) for the seamless manager and worker model,
[render.md](render.md) for the terrain/material render passes, and
[vulkan.md](vulkan.md) for the `VulkanTexture` primitives.
