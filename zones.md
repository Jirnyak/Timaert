# Zones — Зоны

A universal per-cell **danger heightmap** (level 0–9) — like a terrain
heightmap, but for danger. Pure data, regenerated on load (never saved).

- **Code:** [macro/zones.h](src/macro/zones.h)
- **TS origin:** `game/zones.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Difficulty Zones

## Model

`danger = clamp01( fbmNoise − civInfluence + mountainInfluence )`, composed
from: (1) BFS civilization potential (cities/villages/roads pull danger down),
(2) mountain-interior depth (pushes up), (3) 5-octave fBM base. Quantised to
bytes 0–9 (Safe Haven → Hellgate).

## Data-driven extension

Reshape the whole world by editing the top-of-file `constexpr` tunables
(`kCiv*`, `kMountain*`, `kWaterBoost`, `kNoise*`). No engine changes.

## A second world field is coming

The danger heightmap is the first per-cell world field; **THE FIELD** —
one signed `int8` per cell, `−127` saturated BLACK ENERGY … `0` … `+127` saturated
MAGIC — is the second, and it is designed to live in this family
(per-cell layer, diffused, regenerated rather than hand-authored; cf. the baked
night-glow spread in [macro-lighting.md](macro-lighting.md)). Negative sources:
cults, cult landmarks, cultist squads and black artifacts wherever they are kept
(plus a carrier's trail). Positive sources: strong mages — magical landmarks above
all — and dragons. **Annihilation
is the addition operator, not a routine to write.** Spec: [lore.md](lore.md)
§1.1 and §3.5. `NOT BUILT`.

## Connections

Spires demand zone ≥ 4 + their spell's tier — tier 1 opens in "Untamed",
tier 5 in "Hellgate" ([landmarks.md](landmarks.md)); the danger level gates
subworld exit (yellow/red → no exit); the map overlay renders the green→red
field. **The zone does not touch a spawned body's numbers** — see below.

## Owner ruling 2026-08-13, EXECUTED 2026-08-20 — stat scaling is dead

The zone stat boost (+level, +18% hp/damage per level above 2) was a hidden
auto-level and has been **deleted** — together with a second one nobody had
named: the settlement's `√(pop/100)` level bonus, which made a capital's guard
stronger than a hamlet's for no reason but the size of the town. Both are gone
from `sub/spawn.cpp` and from the dungeon path in `sub/engine.cpp`; the
`zoneLevel` parameter left the spawn API entirely, so the markup cannot come
back by accident. A creature's strength is its table row (CANON.md S12); a
place decides WHO stands there and HOW MANY, never what they are worth.
`subworld_spawn_parity_test` holds the door with a negative control (re-inject
the √(pop/100) term and it fails).

What the zone must do INSTEAD is still unbuilt: shape the COMPOSITION of what
spawns, via weight streams composed from every context system (biome, forest,
landmark, zone, later weather / black-energy field) — CANON.md S6, the one
context door. Until that lands the zone genuinely has no say in a spawn, and
that is the honest state, not a regression.
Zones themselves are unfinished (static after boot, never re-baked). The
session prompt is ready:
[proposals/session-prompts.md](proposals/session-prompts.md)
§ «Сессия — ЗОНЫ И КОНТЕКСТНЫЙ СПАВН».
