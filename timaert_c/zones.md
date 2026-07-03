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

## Connections

Spires require zone ≥ 5 ([landmarks.md](landmarks.md)); subworld spawns scale
with zone (+monster level, +18% hp/damage per level above 2,
[microcombat.md](microcombat.md)); the danger level gates subworld exit
(yellow/red → no exit); the map overlay renders the green→red field.
