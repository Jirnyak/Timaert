# Debugging & Profiling — Timaert

Optimization and CPU/GPU time are the top priority for this game. This is the
practical playbook for hunting bottlenecks, crashes, and memory bugs — tuned for
**macOS (primary/dev, Apple Silicon + MoltenVK)**, with Linux/Windows notes.

When reporting back, paste the **exact command** you ran and the **raw output**
(top stacks / sanitizer report / `[vk]` lines / timings). Template at the bottom.

## 0. One-time shell setup (macOS)

```bash
# Makes the Homebrew Vulkan validation layer loadable (its manifest names the
# dylib bare, and dyld doesn't search /opt/homebrew/lib by default).
export DYLD_LIBRARY_PATH=/opt/homebrew/lib
```
The game auto-detects the MoltenVK ICD, so you do **not** need `VK_ICD_FILENAMES`.
Add the export to `~/.zshrc` if you want validation always on.

## 1. Build variants

Compile speed is irrelevant; runtime speed is everything. Optimized configs are
`-O3 + LTO`, and the game binary also uses **fast-math** (`-ffast-math
-fno-finite-math-only`, so NaN/Inf checks still work). We do not target TS-seed
or cross-build float identity — a seed still reproduces the same world within one
build. Default build type is **Release**.

```bash
# Fastest (shipping-like):
cmake -S . -B build -G Ninja && cmake --build build

# Profiling — optimized + debug symbols (use this for Instruments/perf):
cmake -S . -B build-prof -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-prof

# Memory / UB bugs — Address + UB sanitizers:
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTIMAERT_ASAN=ON && cmake --build build-asan

# Squeeze this exact CPU (dev only, NOT shippable):
cmake -S . -B build -G Ninja -DTIMAERT_NATIVE=ON && cmake --build build
```
Targets: `timaert` (game), `gpu_smoke` (Vulkan bring-up), and the `*_test` execs.
Build one target only with `cmake --build build --target gpu_smoke`.

## 2. Vulkan validation (correctness)

```bash
export DYLD_LIBRARY_PATH=/opt/homebrew/lib
./build/gpu_smoke                 # validation on; issues print as "[vk] ...VUID..."
```
Report any `[vk] ... Error` / `VUID` lines verbatim. Performance-hint warnings
are usually fine to ignore — flag them only if perf is the problem.

Extra MoltenVK knobs:
```bash
MVK_CONFIG_DEBUG=1 ./build/gpu_smoke               # extra MoltenVK logging
MVK_CONFIG_LOG_LEVEL=3 ./build/gpu_smoke           # verbose
```

## 3. CPU profiling (where the frame time goes)

macOS — Instruments Time Profiler (best):
```bash
xctrace record --template "Time Profiler" --output prof.trace --launch -- ./build-prof/timaert
open prof.trace           # heaviest call stacks bubble to the top
```
Quick sample of an already-running build:
```bash
sample $(pgrep -n timaert) 5 -f /tmp/timaert.sample && open /tmp/timaert.sample
```
Linux: `perf record -g ./build/timaert` then `perf report` (or a flamegraph).
**Report:** the top ~10 self-time functions and the % each takes.

## 4. Memory debugging

**Valgrind does NOT work on Apple Silicon** — use ASan/Instruments instead.

```bash
# ASan build catches use-after-free, overflow, leaks, UB at runtime:
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./build-asan/timaert
# On the first error it prints a full stack — paste it verbatim.
```
Live leak snapshot of any build:
```bash
leaks $(pgrep -n timaert)
```
Instruments templates: **Allocations** (growth/leaks over time), **Leaks**.
Linux alternative: `heaptrack ./build/timaert` or `valgrind --tool=massif`.
**Report:** the ASan header line (e.g. `heap-use-after-free`) + the first stack.

## 5. GPU profiling (MoltenVK → Metal)

```bash
# Average GPU frame time to stderr (over 60 frames):
MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_PERFORMANCE_LOGGING_FRAME_COUNT=60 ./build-prof/timaert
```
Deep GPU capture: Instruments → **Metal System Trace**, or Xcode → **GPU frame
capture** (MoltenVK exposes real Metal work). RenderDoc does **not** support
Metal/MoltenVK — don't use it here.
**Report:** avg GPU ms/frame and which pass dominates (clear / terrain / UI).

## 6. Crashes

```bash
lldb -- ./build-asan/timaert
(lldb) run
# after the crash:
(lldb) bt all            # all-thread backtrace — paste this
(lldb) frame variable    # locals in the crashing frame
```
If it crashes on load, also send the `save.bin` (app-data path). Symbolicate a
raw address with `atos -o ./build-prof/timaert <addr>`.

## 7. In-engine timing

`gpu_smoke` shows live FPS/ms in its ImGui overlay (toggle the demo window to
stress the UI). The game HUD (F3) shows FPS/camera/world counters. For a quick
apples-to-apples, run `GPU_SMOKE_FRAMES=600 ./build-prof/gpu_smoke` and report
the printed frame count / any stalls.

### 7.-1 Render-diagnostic console commands

* `fpshud [on|off]` — persistent framerate chip.
* `sunfreeze [on|off]` — pin the sun/moon for RENDERING only (sim keeps
  running). Separates "flickers with the moving sun" from "flickers on its
  own".
* `lightdbg [march|clouds|map|nl|off]` — bisect the sun-visibility product
  (lighting.glsl `lit_surface`): each call toggles ONE term off — terrain
  march, cloud shadows, object shadow maps, or the N·L response — so the eye
  can name which member draws a given darkening. No argument prints state;
  `off` restores all.

RULE (owner, 2026-08-11): these are per-run tools, not settings. `boot_world`
resets them on every new game and every load — the game has no graphics
options at this stage; every session starts with everything on, working as
designed. Never make a diagnostic persist.

### 7.0 What a low frame rate now MEANS

One turn of the loop is one tick **and** one frame, so the frame rate and the
world's tick rate are the same number. 33 FPS is not a choppier picture of a
world moving at its usual pace — it is the world living at half speed, because
half as many ticks happened.

That is a change of meaning, not of numbers. Under the old variable-`dt` loop a
low frame rate covered the gap with a bigger `dt`, so the world kept its pace
and only the picture suffered. Now the picture and the world share one clock.

The debug HUD (**F3**) therefore prints the rate with its meaning attached:

```
FPS: 64.0
World: 64.0 / 64 ticks/s
Present: mailbox (loop paces itself)
```

`World` below nominal (highlighted, with `SLOW MOTION`) means the machine is not
keeping up and the world is genuinely running slower — chase the tick cost.
The two lines differ only when `simspeed` is not 1, which runs several ticks per
turn or none.

`Present: fifo (display paces the world)` means the surface did not offer
MAILBOX, so the display's refresh is also the world's tick ceiling: on a 60 Hz
screen the world runs at 60 ticks a second by construction. That is a platform
limit, not a bug to hunt.

### 7.05 The smoke harness — and its seed

```bash
sh smoke.sh                       # the default scenario (macro travel stamina)
sh smoke.sh subworld_enter        # one scenario
sh smoke.sh a,b,c                 # several, in order
sh smoke.sh cast_spell 1,7,999    # one scenario across three worlds
```

The boot prefix (`new_game,wait_boot_done`) and the trailing `quit` are added for
you, and the environment lives inside that one run — your next plain
`./build/timaert` still opens the menu.

**One world by default, and that is deliberate.** `choose_new_game_seed` takes
`SDL_GetTicks()`, so before `smoke.sh` pinned a seed **every run was a different
planet**: a red scenario could not be reproduced, and a regression was
indistinguishable from bad luck. A stale note claiming "five red smokes" turned
out to be nine — the earlier measurement had simply landed on a kinder world. The
script now pins **12345**, the seed the graphics captures already use.

**Run the sweep before you believe a green.** Naming several seeds is how
world-DEPENDENT behaviour surfaces on purpose instead of by accident, and it pays
immediately: one scenario landed a falling body on a rooftop (legitimately
resting 6 m above the terrain it was comparing against), another had a live
peasant wander into a bolt's line of fire. Both were real; both were invisible on
seed 12345.

### 7.1 Seam-crossing profiling (the one that bites)

A subworld cell crossing is the spikiest frame in the game. Two env vars:

```bash
# Label every terrain upload with its MODE and print a per-section breakdown.
TIMAERT_SMOKE_SEED=12345 TIMAERT_SEAM_TRACE=1 \
  TIMAERT_SMOKE_SCRIPT="new_game,wait_boot_done,subworld_seam,quit" ./build/timaert
```

```
shift=0,0 fullH=1 cells=9  height=3.102  matFill=36.015  TOTAL=46.974  <- enter
shift=1,0 fullH=0 cells=3  height=0.216  matFill=1.922   TOTAL=6.242   <- CROSSING
shift=0,0 fullH=0 cells=1  height=0.332                  TOTAL=8.493   <- async drain
shift=0,0 fullH=0 cells=7  height=3.166  matFill=0.000   TOTAL=5.584   <- road smooth
```

**PIN THE SEED.** The harness does not fix one unless `TIMAERT_SMOKE_SEED` is
set, so five runs are five different worlds — and the same measurement ranged
from 0.2 ms to 19.7 ms across them purely by terrain. Unseeded comparisons are
worthless here.

**Read the label before believing the number.** Four upload modes are
interleaved in that log (enter / crossing / async drain / road smooth). Taking
`tail -1` and calling it "the crossing" charged the road smooth's 3.1 ms to the
wrong frame during this system's own optimisation pass.

`TIMAERT_SEAM_SELFCHECK=1` proves the incremental paths against a from-scratch
recompute (including a GPU readback of the material image). It **doubles the
work and inflates the timings**, so never measure performance with it on. And
note what it cannot do: it recomputes through the same functions the fast paths
live in, so it would happily agree with itself — a fast path that skips work
needs its ASSUMPTION checked directly (see `verifyFlatCell`).

What a new feature costs a crossing — a flat ~0.45 ms per newly uploaded GPU
resource whatever its size, up to 3 ms for a whole-composite post-process, and
so on — is tabulated in **problems.md entry 15**.

## 8. Report template (paste this back to me)

```
- Doing:      <e.g. macro walk with ~5k NPCs / entering subworld / idle>
- Build:      build / build-prof / build-asan   (config)
- Command:    <exact command line>
- Symptom:    <freeze | crash | low FPS | growing memory | validation error>
- FPS / ms:   <CPU ms, GPU ms if known>
- Raw output:
  <top stacks / ASan report / [vk] VUID lines / MVK timings>
```
