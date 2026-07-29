# Microworld — Микромир

L2 subworld: **the macroworld, detailed.** Each macro cell becomes a
1024×1024 tile map; the player stands in a seamless 3×3 grid (3072×3072) of them.
Dual rendering: top-down 2D and first-person 3D.

- **Code:** [`src/sub/`](src/sub) —
  [engine.h](src/sub/engine.h),
  [seamless_manager.h](src/sub/seamless_manager.h),
  [base_generator.h](src/sub/base_generator.h),
  [renderer_2d.h](src/sub/renderer_2d.h),
  [renderer_3d.h](src/sub/renderer_3d.h)
- **TS origin:** `subworld/*`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L2 — Microworld (Subworld)

## Model

- **Seamless 9-cell grid:** player at centre; 8 neighbours generated around.
  Boundary crossing re-centres, installs deterministic placeholders, and
  generates exposed cells on `std::jthread` workers (no seam-path stall).
- **Neighbour-aware pipeline (per cell):** Layer 1 heightmap (macro blend +
  detail + coastal sculpting + mountain amplification) → Layer 2 features
  (roads connect toward road neighbours, forests blend) → Layer 3 landmarks
  (self-contained generators).
- **`CellContext`** carries macroHeight, biome, feature, landmark, seed — read,
  never re-derived.
- **Renderers:** 2D tile map; 3D sky → terrain → water → spell effects → tree
  billboards → NPC paper-doll billboards.

## Settlements — city street plan

A subworld **city** (`gens/dispatch.cpp` `gen_city`) is a walled disk of houses,
fields and a central plaza + keep. The interior street network is
**radial-concentric** rather than a centre-rooted starburst:

- **Avenues** — radial roads from the plaza to the rim, evenly spaced over the
  full circle from a seed-derived base rotation, so a city is symmetric in every
  direction *regardless of which neighbours carry roads*;
- **Ring roads** — concentric polygons (subdivided so they read as round) tying
  the avenues into blocks and spreading circumferential road density;
- **Frontage streets** — short tangential streets fanning from every
  avenue×ring node, so the road-gated house scatter finds frontage across the
  whole footprint instead of piling downtown.

The old design grew every street as a ray from the cell centre and, with no
road-bearing neighbour, defaulted its axis to angle 0 — so houses clumped into
the east/south-east corner with whole quadrants empty (the "one clump" report).
Measured on the parity-test city, the plan moves houses from **4 of 8 angular
sectors empty** (min/max 0.00) to **all 8 populated** (min/max ≈ 0.68) and lifts
the outer-half fraction from 0.44 to 0.63.

All layout tunables — avenue/ring/street counts vs population, ring radii, house
count curve — live in **[`src/sub/city_layout.h`](src/sub/city_layout.h)** as one
`CityLayout` config + pure `city_*()` response curves (the `seasons.h` /
`BiomeConfig` idiom), so retuning a city is data, not code. The street RNG is
seeded off `ctx.seed` so it never perturbs the r-stream that drives keep / house
/ field / wall placement (determinism + save-stability preserved). The spread is
locked by `tests/city_distribution_test.cpp` (no empty sector, angular balance,
radial spread across seeds/populations); counts stay locked by
`subworld_generator_parity_test`.

## Seamless crossing (no hitch)

A boundary crossing must re-centre the 3×3 window by one cell **without a visible
frame** — no hitch, no texture/lighting pop, no vanishing structures. The manager
does not regenerate the world; it **toroidally shifts** its CPU composite buffers
(`composite_tiles_`, `composite_height_`) by `(shiftX,shiftY) ∈ {−1,0,1}²`,
fills only the newly exposed cells with deterministic placeholders, and hands the
renderer a `CompositeDirty` describing exactly that shift + the fresh cells
(`mark_composite_shift`, `shift_composite_buffers`). Workers generate the real
exposed cells and stitch them in later as ordinary per-cell drains.

The kept overlap — 6/9 (axis) or 4/9 (diagonal) of the grid — is byte-identical,
so the renderer **relocates it on the GPU** and rebuilds only the 3–5 fresh
cells: a crossing is **O(new content)**, not O(3×3). The full design, the
GPU ping-pong, the shift math, the self-checks, and the hard-won gotchas
(`-ffast-math`, validation-layer timing) live in
**[seamless-crossing.md](seamless-crossing.md)**.

## Data-driven extension

Add a biome → one `BiomeConfig` + one ground texture. Add a landmark → one
self-contained generator TU in [gens/](src/sub/gens).

## Connections

Reads the macroworld as source of truth ([macroworld.md](macroworld.md)).
Hosts all combat ([microcombat.md](microcombat.md)) and spell visuals
([spells.md](spells.md)). The combatant crowd is GPU-driven; the player's
engagement set is CPU-embodied. Overworld NPCs within ±1 cell are projected into
the 3×3 as real combat bodies and can be *possessed*; leaving as a possessed body
remaps the macro player onto its overworld cell ([possession.md](possession.md)).
