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
