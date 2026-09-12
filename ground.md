# THE ground surface — the procedural skin of the subworld

> **This is THE document for what the ground LOOKS like.** The material
> ROUTING — which tile carries which material id, how the 3×3 biome ring
> dithers, why the id is sampled per-fragment — lives in
> [src/sub/material.h](src/sub/material.h) and [render.md](render.md). This
> file is the other half: given an id, what the surface is made of.

Three files hold it, and nothing else does:

| what | where |
|---|---|
| the NUMBERS | [data/ground_materials.csv](data/ground_materials.csv), [data/ground_cover.csv](data/ground_cover.csv) |
| the generator | [tools/gen_ground_table.py](tools/gen_ground_table.py) → [shaders/ground_surface.glsl](shaders/ground_surface.glsl) |
| the SHAPES | [shaders/mesh.frag](shaders/mesh.frag) (families + cover), on the primitives in [shaders/surface_lib.glsl](shaders/surface_lib.glsl) |

Tuning the look is an edit to a CSV cell and a re-run of the generator. It is
never a shader edit, and `ground_table_test` fails the build's ctest if the
generated table and the CSVs ever disagree.

## Why the ground used to read as plastic

Measured before the work, over 34 cells of the map (the `~/timaert_shotkit`
harness): contour energy 0.04–0.22 on a 0–1 scale, one colour bucket covering
20–46 % of the frame, and every bit of the detail in those frames coming from
tree crowns. Three causes, all structural:

1. **No middle band.** The synth jumped from a 28 m biome patch straight to a
   4.5 cm white-noise hash. Everything between — the 0.3–2 m scale the eye
   reads as *surface* — was missing, so past two metres the hash averaged into
   a flat colour (and shimmered while it did).
2. **No normal perturbation at all.** There was nothing for the sun, the
   relief march or the shadow map to play on. Dynamic light cannot show a
   surface that has no slope.
3. **No anti-aliasing rule.** The high-frequency term was point-sampled at any
   range, so it crawled when the camera moved. Amplitude was never the
   problem; the absence of a pixel-footprint fade was.

## The three bands

| band | scale | what it is | what it does |
|---|---|---|---|
| macro | ~28 m and a quarter of that | the terrain-scale patchwork (`macro_cv`) | the only band that reaches the horizon — it is what keeps a bare biome from being one flat plane |
| meso | 0.3–4.5 m (`meso_m`) | the family's STRUCTURE — tussocks, wind ripples, stone plates, plough ridges | the band the eye reads as "surface"; also the band the NORMAL is taken from |
| micro | 2–6 cm (`micro_m`) | the grain | the close-range texture, faded out by the pixel footprint before it can crawl |

The meso and micro bands are ONE field, read twice: its VALUE tints the albedo
and its SLOPE tilts the normal. That is why a crevice is dark *and* indented,
and why the shading needs no new pass, texture or descriptor — the sun and the
PCF shadow map already in the frame do it.

## Calibration, not taste

Every "how blotchy" number in the CSVs is a **coefficient of variation** of
luminance (`lum_std / mean`, linear) — the quantity you can measure off a
photograph. The shader applies the mean-preserving lognormal
`exp(σz − σ²/2)`, whose CV is `sqrt(exp(σ²) − 1)`; the generator inverts that
to `σ = sqrt(ln(1 + cv²))`. So the numbers are comparable across materials
instead of being one author's eye, and the surface's average brightness is
exactly the colour the table says — texture never darkens or lightens the
material it decorates. Anchors, from the reference project's *measured* set:
smooth concrete 0.08, bare soil 0.22, corroded metal 0.44.

`relief_m` is likewise physical: the peak height of the relief **in metres**.

## Families

A family is a SHAPE, not a material: several grounds share one and differ only
by their numbers.

| family | grounds | shape |
|---|---|---|
| `soil` | snow (frozen earth), valley | two octaves of plain organic mottle |
| `turf` | tundra, taiga, meadow, steppe, tropics | tussocks — a clump octave and its child |
| `sand` | desert, shore | wind ripples: **stretched** (crests five times longer across the drift than along it), **ridged** (sharp crest, broad trough — the asymmetry wind builds) and **wandering** (displaced by a wave a ripple or so long) |
| `furrow` | field, field_v | ridge-and-furrow: one ridge pair per `meso_m`, plough lines eight to the ridge |
| `stone` | rock | plates with their own tone, split by cracks, on a warped lattice (an unwarped one reads as paving) |
| `track` | road | packed dirt: shallow hollows where the feet fall, gravel between |
| `mud` | swamp, waterbed | wet flats — broad slicks, almost no relief |

## Cover — what lies ON the ground

The contract is three values: **(id, density, height)**. The ground row picks
the id and the density; the cover row carries colour, strand pitch, height and
wind response. ONE function in mesh.frag draws every one of them — there is no
per-cover branch, and there must never be one.

Shipped rows: `grass` (the first instance — upright blades that bend with the
same wind the clouds ride), `snow` (a blanket: the same function with wide flat
strands and no wind — which is why the snow biome's GROUND is honest frozen
earth and the white is cover at density 1), `moss` (islands in the joints of
stone). Ash, dust, fallen leaves, a film of water: **one row each**.

Height buys two things at once — the parallax shift that makes tall cover lean
away from the eye, and the strand slope (`height × strand_per_m`) that tilts
the normal so the sun shades the blades. Density fades through the pixel
footprint: at range the strands dissolve into the flat tint their mean colour
describes, rather than sparkling.

### No sine anywhere, ever

The sand family shipped once with `sin()` ripples, and the owner named it the
first time he looked at sand: *«писок волнистый периодический некрасиво»*
(2026-09-12). A sine is a perfectly repeating wave and the eye reads the repeat
instantly — no amount of phase warping hides a train of identical crests. The
replacement is `wridge` on a stretched, displaced lattice, and the rule
generalises: **a surface pattern that must look natural is built from noise,
never from a periodic function.** The one exception in this tree is the
`furrow` family, where the repeat is the point — a ploughed field IS periodic,
because a plough is.

## Two laws worth not re-learning

**The synth TILES at 1024 m, and must.** Two constraints meet: a window sits
anywhere in a map a million metres across, where a float32 has 6 cm of
resolution left — a 4 cm grain in absolute coordinates quantised into visible
rectangular blocks (it did, and it is what the near ground looked like until
this was fixed) — and the 3×3 window recentres by exactly one macro cell,
1024 m, which the synth must be invariant under or the whole surface
re-mottles the moment the player crosses a boundary. A field that tiles at
1024 m answers both: coordinates stay inside the window's own ±1536 m, and a
1024 m shift is the identity. Every octave therefore snaps its frequency to a
whole number of cycles per period and wraps its lattice there
(`wfreq`/`wnoise`/`wcell` in surface_lib.glsl). 1024 m is also the LARGEST
period that can do this — a bigger tile would not survive a one-cell shift.

The price, stated plainly: the surface **repeats every 1024 m**, so two cells
of the same material a kilometre apart wear the same pattern, and a 3×3 window
holds three copies of it. Noise has no landmarks, so this is not expected to be
legible — but if the owner ever sees it, the honest fix is a second, much
slower octave seeded per macro cell, not a longer period.

**The pixel footprint is the GEOMETRIC MEAN of the two screen axes.** Ground is
almost always seen at a grazing angle, where one pixel covers centimetres
across the view and half a metre along it. Taking the max — the longer axis —
erased every surface frequency past a few metres and put the whole middle
distance back to flat plastic. The geometric mean is the footprint of an
area-equivalent square, the same quantity a mip level is chosen by.

## OPEN DECISION — the joints between materials

Raised by the owner 2026-09-12, looking at a hillside: *«стыки разных
материалов — они тайловые, очень резкие»*. Not this file's synth; put here
because this is where the ground is documented and the fix would land next to
it. **Nothing below is built. It is a menu, and the owner picks.**

**Diagnosis.** `u_material` is an R8 texture, **one texel per world tile (1 m),
sampled NEAREST** — deliberately, because that per-fragment lookup is what
keeps a 1-tile road connected instead of dissolving between the terrain mesh's
16 m vertices (see [render.md](render.md)). The price is that a boundary
between two materials is a 1 m axis-aligned staircase with no blend anywhere.
Two different things then read as "tiled":

1. **Areas** — road, shore, rock, field meeting grass along straight
   rectangular edges. Ground↔ground biome boundaries are *already* softened:
   `pick_ground_biome` dithers the two climates across a ~250-tile band. What
   is NOT dithered is the AUTHORED boundary (`material_is_authored`), by
   design — an authored tile is supposed to be crisp.
2. **Confetti** — the treeline scatters SINGLE tiles of rock through grass
   (`treeline_is_rock`, a per-tile hash), and each one is a hard-edged 1 m
   rectangle. Close up that reads as grey litter dropped on a lawn rather than
   as stone showing through turf. This is the louder of the two artefacts in
   the owner's screenshot.

**Options.**

| | what | cost | risk |
|---|---|---|---|
| **A** | Jitter the lookup: sample `u_material` at `vUv` + a noise offset of a metre or so. Every joint becomes an organic curve at sub-tile scale. | ~2 noise samples/pixel (≈0.05 ms) | an amplitude wider than half a thin feature chews it — a 1-tile road would go dashed. Amplitude is data, safe around 0.5–1 m. |
| **B** | A, plus a real blend: where the jittered id differs from the centre id, synthesise BOTH materials and mix. A true interlock, not just a ragged line. | 2× the ground synth, but only in the boundary band (branchy) — est. +0.3–0.5 ms worst case | a branch that diverges inside a quad; needs the early-out to be honest |
| **C** | Fix it at the source: extend the existing dither in `sub/material.h` to authored↔ground boundaries, so the BAKED tile grid is ragged. | free at runtime; the 2D map inherits it | still quantised to 1 m squares (ragged squares, not a curve); it changes what the world IS — tiles are read by collision, roads and pathing — and must stay deterministic across a seam recentre |
| **D** | Make the treeline's rock appear in CLUMPS (threshold a noise field) instead of per-tile confetti. Touches only the scatter, not the boundary law. | one CPU function, free | changes the treeline's look everywhere at once (three consumers share the band: tree scatter, 3D material, 2D map) |

**Recommendation: D first, then A.** D removes the confetti, which is the
artefact that reads as a bug rather than as a style; A softens every remaining
joint for the price of two noise samples and touches no world data. B is the
upgrade if a ragged line still is not enough. C is the one option that changes
the world itself, and should only be taken if the owner wants the 2D map to
change too.

## Measured

Same seven cells, same seed 12345, same hour 11, same framing, before → after:

| cell | contour | one-bucket share |
|---|---|---|
| 776,776 autumn forest | 0.20 → 0.30 | 22 % → 26 % |
| 266,266 mixed | 0.20 → 0.25 | 17 % → 19 % |
| 436,266 hills | 0.14 → 0.21 | 38 % → 21 % |
| 96,776 plain | 0.07 → 0.12 | 27 % → 23 % |
| 528,400 grove + river | 0.06 → 0.13 | 46 % → 26 % |
| 656,016 bare massif | 0.05 → 0.14 | 37 % → 25 % |
| 320,896 sea bed | 0.04 → 0.07 | 34 % → 34 % |

Cost, measured with `TIMAERT_GPU_STATS=1` on the heaviest scene the harness
stages (`TIMAERT_SMOKE_BATTLE=3000`, reference cell, interleaved A/B of the
shader alone): scene pass **6.06 ms → 7.25 ms**, against a 15.6 ms budget at
64 fps. Attributed: the three bands +0.27 ms, the cover +0.45 ms, the normal's
two gradient taps +0.48 ms. Each is switchable from the CSV — `relief_m = 0`
skips the taps for that material, `cover_density = 0` skips the cover — and
both already early-out where the band is no longer resolvable, which is most
of a frame.

The sea bed is the one cell the work does not reach: it is read through the
water plane, which the water pass shades. That is the water shader's surface,
not this one's.
