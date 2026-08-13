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
one signed `int8` per cell, `−127` saturated magic … `0` … `+127` saturated
black energy — is the second, and it is designed to live in this family
(per-cell layer, diffused, regenerated rather than hand-authored; cf. the baked
night-glow spread in [macro-lighting.md](macro-lighting.md)). Sources are black
artifacts wherever they are kept plus a carrier's trail on the plus side, and
artifact-destroyers, great mage squads and dragons on the minus. **Annihilation
is the addition operator, not a routine to write.** Spec: [lore.md](lore.md)
§1.1 and §3.5. `NOT BUILT`.

## Connections

Spires require zone ≥ 5 ([landmarks.md](landmarks.md)); subworld spawns scale
with zone (+monster level, +18% hp/damage per level above 2,
[microcombat.md](microcombat.md)); the danger level gates subworld exit
(yellow/red → no exit); the map overlay renders the green→red field.

## Owner ruling 2026-08-13 — stat scaling is condemned

The zone stat boost (+level, +18% hp/damage) is a hidden auto-level and will
be REMOVED: a creature's strength is its table row; the zone must shape the
COMPOSITION of what spawns, via weight streams composed from every context
system (biome, forest, landmark, zone, later weather/black-energy field).
Zones themselves are unfinished (static after boot, never re-baked). The
session prompt is ready:
[proposals/session-prompts.md](proposals/session-prompts.md)
§ «Сессия — ЗОНЫ И КОНТЕКСТНЫЙ СПАВН».
