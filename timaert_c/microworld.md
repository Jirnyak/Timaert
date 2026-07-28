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
engagement set is CPU-embodied.
