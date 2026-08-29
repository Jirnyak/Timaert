# Seamless Crossing — Timaert

> **A CELL IS A PLACE (2026-08-20).** The crossing is only half the promise; the
> other half is that the cell you cross INTO is the same cell however you got
> there. It was not: `CellContext.cx` was the window's running counter and never
> wrapped, so walking east off the last column named the cell 1024 while it read
> macro cell 0's biome — and the detail-noise offset, the road anchor seed and
> the tile hash all keyed off that number. The same place therefore had two
> subworlds (measured: 100 % of tiles differing, up to 63 m of height, road
> anchors 146 m apart), and the session cache hid half of it by restoring
> heights but not tiles. The name is the WRAPPED macro index now, the window
> keeps its own running coordinate for composite geometry, and every
> global-coordinate noise octave in `base_generator.cpp` closes on the world's
> tile span (1024 × 1024) so the wrap costs nothing at the seam. Held by
> `subworld_cell_identity_test` with a negative control.

Source of truth for **how a subworld cell-boundary crossing stays invisible**:
no hitch, no texture/lighting pop, no vanishing structures. This is a
cross-cutting system — it spans the seamless manager ([microworld.md](microworld.md)),
the Vulkan terrain upload ([render.md](render.md)), and one GPU-texture
primitive ([vulkan.md](vulkan.md)) — so it gets its own doc. Read those three
for the surrounding subsystems; read this for the crossing itself.

> **Status (2026-08-05, end of the pass).** Inc 4-8 took the crossing frame from
> ~9-11 ms to **6-8 ms**, and its `upload3d` half from 6.9-8.4 ms to **3.0-5.0
> ms**. The OWNER CONFIRMED IN PLAY that the sub-freeze at a crossing is no
> longer felt. A structure-shade pop at every seam was found and fixed on the way
> (Inc 6) — a defect, not a cost.
>
> ~~What remains, measured and deliberately NOT done: every `VulkanBuffer::update`
> and `create_device_local` ends in `vkQueueWaitIdle`...~~ **DONE 2026-08-11
> (Session 19), and further than the batching sketched here:** the seam path no
> longer submits AT ALL — `upload()` is the CPU stage (runs in the sim tick),
> and the GPU writes are recorded onto the frame's own command buffer through a
> per-frame-in-flight staging arena, ordered by queue-scope barriers instead of
> stalls. Zero `vkQueueWaitIdle`, zero mid-frame submits; the crossing's CPU
> stage measured 5.5 → 1.35 ms and destroyed-in-flight hazards went with it
> (deferred-destruction graveyard). See render.md §The real fence contract and
> problems.md §22б.
>
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
| **Inc 4** | The crossing was O(new content), but **new content is a CONSTANT** and still cost full price — 3.1 ms of height resample | a fresh cell is a placeholder: one height across all 1024² of its tiles, and the vertex resample box-averaged 289 copies of it per vertex | **do not integrate a constant**: fill the cell's interior vertex block with the value, sample only the four shared edges | [seamless_manager.cpp](src/sub/seamless_manager.cpp), [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp) |
| **Inc 5** | The same mistake in the material's dialect — 1.9 ms in a meadow world, up to **19.7 ms in a mountainous one** | a placeholder's 1024² tiles are all the same id, walked one at a time through a biome pick + treeline + LUT | a flat cell resolves to ONE value (memset) or, inside the treeline dither band, to exactly TWO chosen per tile by the hash | [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp), [material.h](src/sub/material.h) |
| **Inc 6** | **Every building in view changed shade** when the player crossed a boundary (a BUG, not a cost) | the per-instance shade hashed the cell's COMPOSITE coordinate, which a crossing moves by a whole cell | key it to ABSOLUTE world coords — `structure_shade()`, next to the ground dither it shares a hash with | [material.h](src/sub/material.h), [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp) |
| **Inc 7** | Road smoothing marked the whole 3×3 dirty ⇒ a full 3 ms resample a few frames AFTER every crossing | it declared no reach at all, so the caller assumed the worst | a pass has an INPUT reach (92 tiles) and an OUTPUT reach (**1 tile**); dirty-marking wants the second | [base_generator.h](src/sub/base_generator.h), [seamless_manager.cpp](src/sub/seamless_manager.cpp) |
| **Inc 8** | Instance rebuilds cost 1.6 ms per crossing — and **not for the reason predicted** | 87-97% was destroy+create of the device-local buffer; the CPU loop over 10896 trees is 0.08 ms | reuse the allocation and overwrite in place, growing by half again; `*Count_` bounds the draw so spare capacity is never read | [vk_renderer_3d.cpp](src/sub/vk_renderer_3d.cpp), [vk_renderer_3d.h](src/sub/vk_renderer_3d.h) |

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

**Fence contract (rewritten 2026-08-11, Session 19).** The paragraph that stood
here claimed `upload()` ran "at a fenced point"; it did not (audit III.9/III.14
— blocking submits mid-frame, a destroy the in-flight frame could read, a
descriptor rewritten in use). The real contract: `upload()` is CPU-only and
queues ops; `flush_uploads` records them onto the FRAME's command buffer via a
staging-arena ring; write-after-read against the frame in flight is one
queue-scope `VERTEX_INPUT→TRANSFER` barrier per frame; the material swap flips
between two descriptor sets written once at image birth; retired resources die
in the fence-tracked graveyard. Full write-up: render.md §The real fence
contract. A frameless smoke must drain via
`SubworldEngine::debug_flush_gpu_uploads()` or the incremental paths degrade to
the full rebuild and are not exercised.

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


---

## Inc 4 — do not integrate a constant

Inc 3c made the crossing **O(new content)**. Inc 4 is the observation that the
new content is not content at all.

A freshly exposed cell is a **placeholder** until its real terrain finishes
generating on the worker: `place_placeholder` fills all 1024² of its composite
tiles with ONE height (`placeholder_height_for`). The vertex resample then did
what it does to real terrain — box-average a 17×17 tile footprint per vertex,
4225 vertices per cell. Three fresh cells on an axis crossing came to **3.7
million scattered reads of a 37 MB array to recover a number we already had.**

The mean of a constant is the constant. Only the cell's four shared EDGES need
honest sampling: a vertex's ±half footprint reaches into the neighbour there,
and `half < step` guarantees it reaches no further than the first interior
vertex. Everything strictly inside gets the value written straight in.

Measured on the shipping build, seed-locked `subworld_seam`:

| | before | after |
|---|---|---|
| height resample, crossing frame | 3.10 ms | **0.20 ms** |
| whole crossing (`gen` + `upload3d`) | 9.3–11.3 ms | **7.9–8.5 ms** |

**The flatness is tracked, not inferred.** `LoadedCell::heightIsFlat` /
`flatHeight` are set by `place_placeholder` and cleared by every write that puts
real terrain into the composite (`blit_cell_into_composite`,
`blit_into_composite`, the road-smoothing drain). Deriving it from
`cell.placeholder` would have been true today and quietly false the first time
someone smoothed or blitted under a placeholder — the flag says exactly what the
consumer needs to know, and is maintained where the knowledge is.

Verified by the existing `TIMAERT_SEAM_SELFCHECK` harness: height incremental
mismatch **0/37249**, max delta 6e-4 (the fast-math noise floor; the tolerance
is 1e-2).

### A note on why the box filter itself cannot be made cheaper

The obvious idea — separate the 17×17 box into two 1-D passes — buys nothing
here. Vertices sit every `step` = 16 tiles while the footprint is 17 wide, so
adjacent footprints overlap by a single tile: there is no redundancy to factor
out. For REAL terrain the resample is already near the memory-bandwidth floor
for what it computes. That is why the win had to come from not computing it.

### Reading the trace

`TIMAERT_SEAM_TRACE=1` now labels each upload with its mode:

```
shift=0,0 fullH=1 cells=9  height=3.102  matFill=36.015  TOTAL=46.974  <- enter
shift=1,0 fullH=0 cells=3  height=0.216  matFill=1.922   TOTAL=6.242   <- crossing
shift=0,0 fullH=0 cells=1  height=0.332                  TOTAL=8.493   <- async drain
shift=0,0 fullH=1 cells=9  height=3.166  matFill=0.000   TOTAL=5.584   <- road smooth
```

Without those labels the four modes are indistinguishable in a log, and it is
very easy to attribute one frame's cost to another — which is exactly what
happened while measuring Inc 4.


---

## Inc 5-8 — the same law, four more consumers

**Inc 5, material.** `fillCellMaterial` walked all 1024² tiles of a placeholder —
every one of them the same id — through a per-tile biome pick, treeline and LUT.
A flat cell's material varies with the coordinate in exactly ONE place, the
treeline dither band, which gives three cases: an authored tile or a height
outside the band resolve to **one value** (a memset); a height *inside* the
band resolves to **exactly two**, chosen per tile by the dither, because a
uniform ring pins the biome pick to `ring[4]` and the material lookup to two
bytes precomputed once. (The ring here is the GROUND ring — since 2026-08-29
a flooded cell enters it as its unflooded climate ground, so a "water ring"
case no longer exists; see microworld.md, cross-seam ground materials.)

| matFill on the crossing frame | before | after |
|---|---|---|
| seed 12345 / 777 (meadow) | 2.55 / 2.24 ms | **0.21 / 0.12 ms** |
| seed 1 / 2 (banded mountain) | 3.74 / 9.08 ms | **0.95 / 2.60 ms** |

`tile_hash01`, `treeline_t` and `treeline_is_rock` moved out of an anonymous
namespace in `material.cpp` into `material.h` as inline — ONE copy of the
formula, in a place a hot loop can inline it. Going through the cross-TU call
instead was worth 3.7× on the banded case, and is why the first attempt at that
path saved nothing at all.

**Verification that matters here:** the whole-image self-check *cannot* test
either fast path, because it recomputes through the same function and would
agree with itself. `verifyFlatCell` (under `TIMAERT_SEAM_SELFCHECK`) checks the
two claims directly against the composite the manager handed over — every tile
of a flat cell really is `flatTile` (all 1048576), and every byte written really
is what the honest per-tile expression produces. Five seeds, both paths
exercised, `tileMismatch=0 valMismatch=0`.

**Inc 6, the shade pop.** A structure's per-instance shade hashed `s.x/s.y` —
its position *inside the composite*. A crossing reindexes the composite, so
every house and wall on screen jumped to a new brightness (a 20 % swing) at the
instant the player stepped over the boundary. Trees had the same bug, keyed the
same way, and were fixed when their species snapped at seams; structures were
missed by that fix. The rule now lives in `material.h` as
`structure_shade(absX, absY)`, and `material_seam_test` invariant 7 proves it
**with a negative control**: the old window-relative keying is kept in the test
and the test fails if it ever stops reproducing the pop.

**Inc 7, two radii.** A terrain post-process has an **input reach** (how far a
height can influence the result — box r=12 plus 80 Laplacian iterations along the
road chain ≈ 92 tiles) and an **output reach** (how far a height can actually
change — every pass writes road tiles only, pass 3 writes their 8-neighbour
shoulder: **1 tile**). Dirty-marking wants the output reach. The worker now names
the cells a finished run actually moved, from the same index list the smoothing
ran on, and `mark_composite_cell_height` leaves `dirtyFullHeight_` clear — that
flag is the difference between a 3 ms resample and a per-cell one. 3.0-3.2 →
2.0-2.5 ms. That the *input* reach is finite is also the fact that makes moving
this pass into per-cell generation possible at all (problems.md entry 14).

**Inc 8, instance buffers.** The prediction was that instances were expensive
because a crossing recomputes window-relative positions. Measured, the CPU loop
building 10896 tree instances costs **0.08 ms** and 87-97 % of the time was
`destroy` + `create_device_local`. So the fix was to keep the allocation and
overwrite it in place. Which then exposed the real remaining cost — see the
Status note above.

## Reading the trace, and pinning the seed

`TIMAERT_SEAM_TRACE=1` labels every upload with its mode:

```
shift=0,0 fullH=1 cells=9  height=3.102  matFill=36.015  TOTAL=46.974  <- enter
shift=1,0 fullH=0 cells=3  height=0.216  matFill=1.922   TOTAL=6.242   <- crossing
shift=0,0 fullH=0 cells=1  height=0.332                  TOTAL=8.493   <- async drain
shift=0,0 fullH=0 cells=7  height=3.166  matFill=0.000   TOTAL=5.584   <- road smooth
```

Without those labels the four modes are indistinguishable in a log, and it is
very easy to attribute one frame's cost to another — which is exactly what
happened while measuring Inc 4.

**Always pin `TIMAERT_SMOKE_SEED` before comparing numbers.** The harness does
not fix a seed by default, so five runs are five different worlds — and matFill
ranged from 0.2 ms to 19.7 ms across them purely by terrain. Unseeded
comparisons here are meaningless, and several early ones were.
