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

`relief_m` is likewise physical: the peak height of the relief **in metres**,
and `edge_m` is the distance in metres that this ground's margin wanders into
its neighbour (see the joint section below).

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

## The joint between two materials

Raised by the owner 2026-09-12 (*«стыки разных материалов — они тайловые,
очень резкие»*), built the same day, and then **rebuilt the same day**, which
is the part worth writing down.

**Why it happened.** `u_material` is an R8 texture, **one texel per world tile
(1 m), sampled NEAREST** — deliberately, because that per-fragment lookup is
what keeps a 1-tile road connected instead of dissolving between the terrain
mesh's 16 m vertices (see [render.md](render.md)). The price is that a POINT
SAMPLE of it is a step function, so every joint was a 1 m axis-aligned
staircase. Ground↔ground biome boundaries were already softened —
`pick_ground_biome` dithers two climates across a ~250-tile band — but the
AUTHORED boundary (`material_is_authored`: road, field, shore, rock, water) is
crisp by design, and that is what the eye was catching.

**The first attempt, and why it was wrong in KIND.** It sampled the id twice —
here and a metre away along a noise field — and blended wherever the two
disagreed. It looked better than the staircase and it was still not a fix:
*"the two samples disagree"* is a BINARY region, and the silhouette of that
region is just the old hard edge in a new place. The owner found it in one
pass, on a beach, where it drew tongues and fingers of sand with knife edges.
**A discontinuity cannot be hidden by putting a smooth fill inside a
discontinuous outline.**

**What it does now — one idea, and it is continuous everywhere.** A fragment
does not sit on one tile; it sits inside a square metre that up to four tiles
share. ONE `textureGather` returns all four ids at once, and the bilinear
fractions are each tile's SHARE of this fragment:

* the ground here is the one with the largest share, and the runner-up's share
  of the pair is the blend weight. It reaches 0.5 exactly on the line between
  two tile centres and falls to 0 at either centre — the transition is a
  **continuous function of position**, with no region, no silhouette, and no
  number needed to set its width;
* `edge_m` sharpens those shares to the ground's own margin width, because raw
  bilinear spreads every transition across a full tile — right for a beach,
  wrong for a stone one tile across, which would then be inside its own ramp
  everywhere and read as a stain rather than a stone;
* the same `edge_m` jitters WHERE the gather is taken, along two noise octaves
  (bending every ~3 m, fraying four times finer). That is the one job the
  jitter is actually good at: moving a margin, not softening it;
* the runner-up lends its **colour**, not a second synth: it borrows the
  winner's already-computed shape and the place's already-computed patchwork,
  and its cover joins as the mean tint its density describes. Across a
  one-metre band a family's PATTERN is not legible; its hue and lightness are.
  Measured, that choice is the whole cost of being correct (below);
* the width is the **pair's** margin, the wider of the two — a margin belongs
  to the meeting, not to one side of it. Read from the point-sampled centre
  instead, as it was at first, the width itself jumped at the tile line: a
  discontinuity in how the discontinuity is hidden, which is the same mistake
  one level down. (The jitter's amplitude may still be read that way, because
  it scales a DISPLACEMENT, and a displacement stays continuous however
  abruptly its amplitude changes.);
* **the sward thins too.** Blending only the albedo left one hard thing at a
  joint: the winner's cover was drawn at full strength up to the line and then
  switched — strands, tilt and all. The cover's DENSITY now rides the same
  shares: where the neighbour grows the same thing the density crosses over,
  and where it grows something else this sward thins to nothing by the halfway
  line, with the neighbour's own cover arriving as the mean tint. It costs
  nothing — `cover_apply` already took the density as an argument.

`edge_m` is per material because it is one fact about the ground — how far its
margin reaches — read twice. A built thing keeps a small number (road 0.3 m)
and stays a road; sand and peat creep with a large one (1.6 m).

**Cost, measured** (`TIMAERT_GPU_STATS`, 3000 bodies, interleaved A/B of the
shader alone, minimum over eight one-second windows — the mean drifts with the
chassis, the minimum does not):

| | 162,148 — treeline confetti, the worst case | 776,776 — an ordinary cell |
|---|---|---|
| no joint at all | 6.97 ms | 7.45 ms |
| first attempt (jitter + binary blend) | 8.14 ms | 8.00 ms |
| coverage + a full second synth | 8.92 ms | 8.30 ms |
| coverage + borrowed colour | 8.00 ms | 8.13 ms |
| **+ pair's margin, thinning sward (shipped)** | **8.04 ms** | **8.15 ms** |

So the correct construction costs what the incorrect one did — but only
because the runner-up borrows the shape. Paying for its own synth was measured
at roughly double the joint's price for a difference no one can see inside a
metre.

The worst case is a cell whose treeline scatters single tiles of rock through
grass, so a large share of its pixels sit inside a joint. That is also the
remaining open item:

**And the crumb is gone — one law, both boundaries.** The owner asked the
question that changed the shape of the fix: *«у нас же это не единственный
переход между биомами? такой крупой она универсальная или у гор своя?»* It was
universal. Two call sites in `sub/material.{h,cpp}` each flipped their own copy
of the same per-tile coin — the biome pick across a cell border, and the
treeline's grass against stone — and the treeline's own comment said outright
that it used "the same style of hash the seam dither uses". One mechanism,
copied by hand rather than shared.

A coin flipped per square metre makes PEPPER: a metre of stone, a metre of
grass, a metre of stone, where nature puts patches. Only the mountain showed it
because only the mountain has the contrast — the biome seam mixes two green
grounds across a ~250-tile band, so its pepper is a metre of one green among
another.

So there is now ONE door, `ground_dither01` (sub/material.h), and every ground
boundary the micro world draws goes through it. It is a correlated field —
value noise over the same tile hash, one lattice cell per 24 m (`a stand of
scrub, a spill of scree`) plus an octave at a third of that for the fringe.
Measured by the test that pins the law: **a stretch of one answer runs 34 tiles
where the coin gave 2.** Mean stays 1/2, so the AMOUNT of stone in a band is
exactly what it was — only its arrangement changed. Still a pure function of
absolute tile coordinates, so the seam contract is untouched.

Two details that are easy to get wrong and are written into the code:

* the treeline is decorrelated from the seam by an **offset**, never by scaling
  the coordinate. Scaling was right for a coin (it only reshuffled it) and is
  wrong for a field: the old `absX * 7` would divide the patch down to three
  metres and hand the pepper straight back;
* `structure_shade` keeps the coin. It is a per-OBJECT wobble, not a boundary —
  correlating it would paint neighbouring houses the same brightness, which is
  the opposite of its job.

**The two layers now say the same thing.** The CPU decides WHICH ground owns a
tile, with patches instead of grains; the shader reads that tile field as a
COVERAGE and blends the two grounds that share a fragment. One idea — a
boundary is a competition between two claims, resolved continuously —
expressed once on each side of the bus.

### What it costs, and the rule it had to obey

**The seam is sacred.** A subworld crossing is where this game pays its only
load, and the owner's rule on it is absolute: that number moves in one
direction, down (2026-09-12). The first version of this law broke the rule and
I reported it as "+0.6 ms, off the frame" from a single noisy sample. It was
wrong by twenty times. Measured properly — `TIMAERT_SEAM_TRACE=1`, the
`matFill` stage of a full 9-cell build, 9.4 million tiles, minimum of four
runs:

| | matFill |
|---|---|
| the coin it replaced | **19.4 ms** |
| the law, first cut (what that report was about) | 32.6 ms |
| the law as it ships now | **20.7 ms** |

**Why a field can be nearly as cheap as a coin.** A coin must be flipped per
tile — a 64-bit mix, every tile, forever. A field is CONSTANT over its
lattice, so a row walker (`GroundDitherRow`) refreshes four corner hashes once
per 24 tiles, keeps the y smoothstep as a row constant, indexes a shared
table for the x one, and advances east by an increment. Three lerps and a
table read is what a tile pays.

**And the trap that cost the most, twice.** Hoisting turns CONDITIONAL work
unconditional. The coin was flipped LAZILY — deep inside a cell all four ring
corners agree and the pick returns before touching it; above and below the
treeline band the answer needs no field either. Handing the pick a ready
float, and pre-filling rows into buffers, both made that work eager: 39 ms,
double the coin. The fix is that the row goes IN and the field is drawn where
the coin was flipped — after the early-out, never before it.

Four more attempts are recorded in the header with their numbers, so nobody
spends the afternoon again: a second finer octave (25.5 — mesh.frag already
frays every boundary, so the fringe was paid for twice), the lattice hashed
once per cell (27.3 — hashes were never the cost; 8 KB in the hot loop is),
the material tabulated for every ring biome (22.3 — two switches are cheaper
than a cache line), and moving the pick's body into the header (no change —
LTO was already inlining it).

**Where that leaves it: +1.3 ms on a full build, ~+0.4 ms on a crossing** (a
crossing refills three cells of nine), off the frame, against a load the seam
smoke's own `gen` number cannot even distinguish from noise (6.83 → 6.73 ms
median). That is not "down", and the owner's rule says down. The remaining
1.3 ms is the difference between three lerps and one integer hash, so it will
not come out of this law — it has to come out of the fill around it, which
has never been optimised and still asks `terrain_material_for` per tile
through two switches.

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
