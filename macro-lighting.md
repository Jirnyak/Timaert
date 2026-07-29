# Macro night lighting — Ночное освещение макромира

The universal, data-driven **night-glow** system for the 2D top-down world map.
Every cell that emits light at night — settlements, villages, active spires, and
any future landmark type — is enumerated from the world state, rasterised into a
per-cell light field, and added by the macro fragment shader as the day/night
clock darkens the map. The result: a dark night countryside with warm-glowing
towns whose light spreads over open ground and is smothered by forest.

Design law (same as the rest of the game): **minimum systems, maximum
functionality, no hardcoding.** Adding a new kind of glowing thing is one data
column plus, once it has world instances, one loop — never an engine branch in
the renderer or the shader.

- **Code (L1 bake, pure — no Vulkan):**
  [src/macro/macro_lighting.h](src/macro/macro_lighting.h),
  [src/macro/macro_lighting.cpp](src/macro/macro_lighting.cpp)
- **Code (GPU upload):**
  [src/macro/vk_macro_renderer.cpp](src/macro/vk_macro_renderer.cpp)
  (`upload()` / `upload_light_field()`)
- **Shader (sample + combine):** [shaders/macro.frag](shaders/macro.frag)
- **Hooks (bake triggers):** [src/app/main.cpp](src/app/main.cpp)
  (`bake_macro_light_field` / `rebake_macro_lights`)
- **Companion docs:** [render.md](render.md) §Macro night lighting,
  [macroworld.md](macroworld.md), [features.md](features.md) (occlusion grid),
  [landmarks.md](landmarks.md) (`LandmarkDef.lightColor` / `lightPop`)

---

## Pipeline

Baked **on world-change only — never per frame.** Population and terrain are
static between edits, and the shader scales the same baked field by the day/night
curve as the clock advances, so the cost of a glowing world is one bake, not one
rasterisation per frame.

```
GameState ──collect_macro_lights──▶ [MacroLight…] ──bake_light_field(+features)──▶ RGBA8 field
                                                                                        │
                                                        upload_light_field (binding 4) ─┘
                                                                                        │
                                        macro.frag: sample u_lightField, decode, add at night
```

Four stages, three files, one L1→GPU handoff. The bake (`macro_lighting.cpp`) is
pure L1 — it depends only on `GameState` and the landmark table and never
includes a Vulkan header; the Vulkan renderer consumes the baked **bytes**, so
the GPU target keeps its minimal link set.

---

## 1. The emitter census — `collect_macro_lights(gs)`

Enumerates every night emitter into a flat `std::vector<MacroLight>`. Fully
data-driven off the landmark table ([landmarks.md](landmarks.md)):

- **Inhabited landmarks** (settlements, villages) glow with their type's
  `LandmarkDef.lightColor`, scaled by **live population** through the TS
  `renderer.ts` curve:
  - `intensity = min(1, 0.18 + log10(max(1,pop))·0.32)` — soft log-scaled peak.
  - `radius    = 1.5 + sqrt(max(1,pop))·0.35` — sqrt-scaled reach in cells.
  A hamlet and a capital differ smoothly with **no per-type tuning**.
- **Fixed POIs** (active spires) emit their table tint at a synthetic strength
  `LandmarkDef.lightPop`; a depleted spire emits nothing.
- **The single data-driven gate:** `push_light` emits nothing when
  `lightColor == 0`. Setting a landmark type's `lightColor` to `0` opts it out of
  night light — no code change.

`MacroLight` is `{ nx, ny (normalized cell-centre UV), radius (cells), intensity
0..1, r,g,b (linear tint) }`.

> **Adding a glowing landmark type = one table row** (`LandmarkDef` with a
> non-zero `lightColor`) plus, once that type has world instances, one loop in
> `collect_macro_lights`. Never a renderer or shader branch.

---

## 2. The bake — `bake_light_field(w, h, lights, out, features)`

Rasterises the summed glow of every emitter into `w·h·4` RGBA8 bytes (RGB = glow,
A = 255). Two propagation modes selected by the `features` argument:

### Increment A — isotropic radial (`features == nullptr` or empty layer)
Exact Euclidean falloff `f = max(0, 1 − D/radius)`, contribution
`color · intensity · f²`, summed, torus-wrapped. This is **bit-for-bit** the
original radial bake — callers with no feature layer get identical output. It is
an exact falloff, not an octile approximation.

### Increment B — terrain-occluded propagation (`features != nullptr`)
A **bounded Dijkstra** from each emitter over *optical distance*: the settled
distance of a cell is its cheapest optical path from the source, so light **bends
around obstacles instead of shining through them.** 8-neighbourhood with
Euclidean (octile) step lengths so open-terrain optical distance tracks true
distance; torus-wrapped; the frontier is pruned as soon as `D ≥ radius` (`f ≤ 0`).
Per-light scratch (`dist`/`touched`) is reused and reset only over the cells that
light actually touched — never a full `w·h` clear per light.

Each traversed cell spends a **per-feature optical cost** — how much "reach
budget" one step through that feature burns. This is the one data table; a new
feature's light behaviour is one row, never a branch in the bake loop. Indexed by
`FeatureType`, so row order tracks the `features.h` byte contract:

| Feature | Cost | Behaviour |
|---|---|---|
| `FT_None` (open) | 1.00 | baseline |
| `FT_Road` | 0.65 | clear + reflective — carries light **furthest** |
| `FT_Tree` (canopy) | 2.50 | dims light: edges transmit, interiors go dark |
| `FT_DirtRoad` | 0.85 | mostly clear — light runs along it, near-open |

So light **carries far over open ground and along roads, and is smothered by
forest,** flowing *around* dense stands rather than through them. (The
forest-occlusion behaviour is the effect the owner specifically called out as
good — preserve it.)

### Encode
Per channel: `g = acc · kMacroGlowGain`, clamp to `kMacroGlowCeil = 1.5`, store
`uint8 = g · (255/1.5) + 0.5`. The ceiling is the TS `renderer.ts` `totalGlow`
clamp; the shader decodes by multiplying back by `1.5`.

---

## 3. The one director knob — `kMacroGlowGain`

`inline constexpr float kMacroGlowGain` in
[macro_lighting.h](src/macro/macro_lighting.h) (**currently `0.45`**) is the
**single master brightness tunable.** It multiplies every emitter's summed
contribution *before* the clamp/encode, so it scales settlements, villages,
spires and any future emitter **uniformly** — without touching the population
curves, the falloff, the occlusion, or the shader.

It is tuned **below 1** because `macro.frag` adds the decoded glow *on top of* the
night-darkened scene: at full strength a city core saturates to pure white (the
symptom that motivated this knob). `0.45` pulls a lone city core down from a
~white ~0.85 add to a warm ~0.38 pool of light. **Raise toward 1 for brighter
towns, lower for a darker night.** This is the one number a director turns.

> The shader has a *second*, independent strength multiplier — the `· 0.85` on
> the glow add in `macro.frag` (see §5). `kMacroGlowGain` is the data/bake-side
> knob (affects the stored field and the unit test); the `0.85` is the
> render-side knob. Prefer `kMacroGlowGain` for global brightness — it is pure
> C++, needs no shader recompile, and is unit-tested.

---

## 4. GPU upload — surgical binding-4 re-upload

The macro renderer's descriptor **set 0** is a small fixed array of combined
image samplers (master / feature / zone / river synth inputs + the light field at
**binding 4**). `vk_macro_renderer.cpp` exposes two paths:

- **`upload(dev, …, lightFieldRgba, w, h)`** — the full boot/world-change upload;
  builds the light-field texture alongside the other synth inputs. A light field
  is **always** bound (a 1×1 black texture if none is supplied) so binding 4 is
  never invalid.
- **`upload_light_field(dev, rgba, w, h)`** — the mid-game **surgical** re-upload:
  `vkDeviceWaitIdle` → destroy the old `lightField_` → recreate it → rewrite
  **only** descriptor binding 4. Bindings 0–3 (master/feature/zone/river) stay
  live and untouched, so a rebake costs one texture, not a full descriptor
  rebuild.

---

## 5. Shader integration — `macro.frag`

The macro fragment synth samples the field once and adds it at night, scaled by
the same `nightDarken` day/night curve that darkens the base map
([shaders/macro.frag](shaders/macro.frag)):

```glsl
layout(set = 0, binding = 4) uniform sampler2D u_lightField; // RGB night glow

if (pc.nightDarken > 0.0) {
    col = mix(col, vec3(0.05, 0.05, 0.15), pc.nightDarken * 0.82); // darken to night blue
    vec3 glow = texture(u_lightField, mapUV).rgb * 1.5;            // * kMacroGlowCeil (decode)
    col += glow * pc.nightDarken * 0.85;                          // 0.85: render-side strength
}
```

The glow is additive and gated on `nightDarken`, so it is invisible by day and
peaks at midnight — dark countryside, warm-glowing towns.

---

## 6. Rebake triggers — when the field is re-baked

Two free functions in [main.cpp](src/app/main.cpp) own the trigger points:
`bake_macro_light_field(app, out)` (collect + bake, passing `&app.features` for
Increment B) and `rebake_macro_lights(app)` (bake + `upload_light_field`, guarded
by `macro.ready()`).

| When | Path | Why |
|---|---|---|
| **World generation** (`boot_world`) | inline bake → full `upload()` | first field for a fresh world |
| **Save load** (`boot_world_from_save`) | `rebake_macro_lights` after state install | **staleness fix** — glow must reflect *loaded* settlements/spires, not the ones generated before load |
| **Mid-game population drift** | dirty-flag → `rebake_macro_lights` | populations mutate as the daily economy ticks; the glow must follow |

**The drift trigger is precise and universal.** `WorldTickResult.dailyTicksProcessed
> 0` is the exact signal that a daily tick mutated populations (it fires in both
tick paths precisely when `tick_settlements_` / `tick_villages_` run). Both tick
branches set `app.macroLightsDirty` on that signal, but the flag is **flushed
only on the macro (non-subworld) branch** — so a player inside a subworld never
pays a GPU sync (`vkDeviceWaitIdle`) for a map that isn't being drawn; the field
is simply rebaked the next time the macro map is shown.

---

## 7. Mountains are a biome now — occlusion caveat

Mountains used to be a `FeatureType` (`FT_Mountain`) and therefore occluded light
through the cost table. They have since been refactored into a **biome**
(`biomes.h` `Biome::Mountain`, classified by elevation ≥ `kMountainBiomeLevel`),
so they no longer appear in the feature grid this bake reads. Consequences:

- **Forested mountains still occlude** — via their `FT_Tree` feature composed on
  top of the Mountain biome. This is the common, praised case.
- **Bare (treeless) rocky massifs are currently transparent to light** — they are
  no longer in the feature grid, and the bake does not yet sample elevation.

Restoring bare-massif occlusion is a **planned follow-up**: sample the elevation
(or biome) in the bake and treat `Biome::Mountain` as a high-cost cell, the same
way `FT_Tree` is treated today. Deliberately deferred until the mountains→biome
refactor settles. Until then, the occlusion table in §2 is complete and correct
for the *feature* grid.

---

## 8. Verification

**Unit test** — `macro_lighting_test` ([tests/macro_lighting_test.cpp](tests/macro_lighting_test.cpp),
CTest-registered) locks:
- radial + occluded falloff shape and torus wrap;
- **colour fidelity** — a red emitter stays red in the field;
- **the gain lock** — a single max-strength light core encodes to exactly the
  gain level and stays *well below saturation* (`< 128`), which is the regression
  guard for the "cities blow out to white" bug;
- **stacked clamp** — many overlapping lights clamp gracefully at the ceiling;
- **forest solid-block occlusion** — a wall of `FT_Tree` darkens the far side.

**Eyeball in-game** — enter the macro map, advance the clock to night (watch
`nightDarken` rise), and confirm: towns are warm pools (not white blowouts),
light spreads over open ground and along roads, forest stands throw the far side
into shadow. To check the drift trigger, let several in-game days pass on the
macro map and confirm a growing/shrinking town's glow tracks its population.

---

## 9. Status (2026-07-28) — honest

- **Bake + brightness knob + cost table** (`macro_lighting.{h,cpp}`): DONE,
  `macro_lighting_test` green, consistent with the new 4-value `FeatureType`.
- **GPU upload** (`upload_light_field`): DONE, compiles clean in isolation.
- **Rebake hooks** (`main.cpp`): written, but share a translation unit with the
  parallel agent's in-flight mountains→biome refactor, so they cannot compile
  until that lands. Low risk (straight calls into the unit-tested bake).
- **Not yet committed** — the tree does not build while the mountains refactor is
  mid-flight; committing waits for a green tree.
- **Follow-up:** bare-mountain occlusion (§7).

---

## Files

| File | Role |
|---|---|
| `src/macro/macro_lighting.h` | `MacroLight`, `kMacroGlowCeil`, **`kMacroGlowGain`**, API + docs |
| `src/macro/macro_lighting.cpp` | `collect_macro_lights`, `bake_light_field`, cost table, Dijkstra |
| `src/macro/vk_macro_renderer.{h,cpp}` | `upload()` + surgical `upload_light_field()` (binding 4) |
| `src/app/main.cpp` | `bake_macro_light_field`, `rebake_macro_lights`, dirty-flag triggers |
| `shaders/macro.frag` | samples `u_lightField`, decodes `· 1.5`, adds at night |
| `tests/macro_lighting_test.cpp` | unit coverage incl. the gain / anti-saturation lock |
