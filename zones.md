# Zones — Зоны

A universal per-cell **danger field** — like a terrain heightmap, but for
danger, and a **CONTINUUM**: one byte per cell, `0` = absolutely safe, `255` =
where the strongest demons stand (owner, 2026-08-24). The ten old steps were a
false discreteness; the `kZoneLabels` strings (Safe Haven → Hellgate) went to
the axe with the dead-code sweep of 2026-08-29 — nothing read them: **no
mechanic may branch on a band; mechanics read the byte.**
Pure data, never saved — baked by `generate_zones` and honestly RE-baked by
`rebake_world` (see below).

- **Code:** [macro/zones.h](src/macro/zones.h)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Difficulty Zones

## Model

`danger = clamp01( fbmNoise − civInfluence + mountainInfluence )`, composed
from: (1) BFS civilization potential (cities/villages/roads pull danger down),
(2) mountain-interior depth (pushes up), (3) 5-octave fBM base, (4) forest
danger scaling continuously with the cell's tree count (deep massifs get the
full boost, ambience little). Quantised once to the 0..255 byte.

**How strongly a FEATURE civilizes is a registry column** (2026-08-29):
`FeatureDef::civStrength` ([macro/features.h](src/macro/features.h) — the
feature table grew three physics columns, `{bedWeight, opticalCost,
civStrength}`: road 0.35, dirt road 0.22, field 0). The optics twin fell the
same day — `feature_optical_cost` reads the same row instead of a private
`kFeatureOpticalCost[]` array in `optics.h`. One row now answers what a road
IS to every field that cares: the step price, the sight price, the pull on
danger.

**Civ-baking: MEASURED, NOT MERGED (2026-08-29).** The canon-audit fix «civ
через единый запекатель» was tried: re-expressing the civ BFS as a
multi-source `optical_sweep` (uniform step cost, budget = strength/decay)
reproduces the FIELD but not the BYTES — on seed 12345, 103,464 of 1,048,576
cells differ by up to 2.46e-07 (the sweep accumulates octile distance and
multiplies by decay once; the BFS subtracts pre-scaled decays sequentially,
and float arithmetic is not associative). The QUANTISED danger bytes matched
100 % on that seed — the game-visible field is identical — but exact parity
was the bar, so the BFS stays, the measurement is recorded at the site
(`zones.cpp`), and a measured **re-tie** of the field is the owner's call.

The continuous value exists only **during** the bake: a test that wants its
precision asks `generate_zones` for the optional `continuousOut` capture; the
game never stores it. (`ZoneLayer::field` — the parallel `[0,1]` float grid,
4 MiB per 1024² world with no reader — fell to the axe 2026-08-24,
canon-audit D.)

## Data-driven extension

Reshape the whole world by editing the top-of-file `constexpr` tunables
(`kCiv*`, `kMountain*`, `kWaterBoost`, `kNoise*`). No engine changes.

## Derived thresholds — никаких ручных порогов

Every consumer that used to name a band now derives its byte:

- **Subworld exit gate** — `kSafeExitDanger = (3·256)/10 − 1 = 75`
  (`zones.h`): the "settled land" ceiling — everything the old quantiser
  called bands 0..2. Above it, hostiles near the player block the subworld
  exit (`exit_blocked_by_danger`, `sub/engine.cpp`). (Honest note: the
  code's own comment says `ceil(…) − 1` = 76, but the expression is integer
  division — the constant is 75, so the last byte of band 2 (76) already
  gates. One byte of drift between a comment and its expression; the
  mechanic reads the byte either way.)
- **Spires** — the registry row's band, in bytes (`kLandmarks`
  minZone/maxZone: Spire `128..255`, `landmark_registry.h`); the tier walks
  the row's own band in four **derived** steps —
  `tierStep = (maxZone − minZone) / 4` (`spires.cpp`) — tier 1 opens at the
  row's minZone, tier 5 demands its maxZone ([landmarks.md](landmarks.md)).
- **Map embers** — the live view's rare-ember danger overlay fades in over
  `smoothstep(166, 255, zone)` (`macro.frag`) — the old bands 6.5..9, in
  bytes ([map.md](map.md)).

## The zone has its SAY in a spawn — the matching law (CANON S6/S12, 2026-08-24)

The 2026-08-20 promise ("shape the COMPOSITION of what spawns") is **built**.
The one spawn law ([macro/fauna.h](src/macro/fauna.h), `roll_spawns` /
`pick_town_row`) weighs every row of the one body table:

    weight(row, cell) = row.weight × habitat(row, cell)
                      × danger_match(spawn_strength(row), danger byte)

- **Strength is DERIVED, never hand-set** — `spawn_strength`: log₂ of the
  row's own combat power (hp × damage/cooldown), normalized over the table —
  the weakest row is 0, the strongest demon 255. Strengthen a row and it
  migrates to dangerous ground by itself.
- **The match is symmetric** around strength == danger (owner's word: hell is
  peopled by demons, not by demons plus rabbits), **halving per
  `kDangerHalfLife = 256/10` bytes** of mismatch, floor 1/1024 of the peak —
  a doom-tier horror in a safe meadow is «исчезающе малая вероятность»,
  literally, never zero. No cutoffs, no bands: composition flows across the
  map.
- **S12 stands untouched:** the zone changes WHO spawns — never a number on a
  body after the pick. `subworld_spawn_parity_test` pins both halves (danger
  shifts composition; rabbit < wolf < troll by derived strength; the
  autolevel negative controls still run).

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
the √(pop/100) term and it fails). What the zone does INSTEAD is the matching
law above — landed 2026-08-24 with the context door
([context.md](context.md)).

## Rebaked, honestly (S7 closed 2026-08-24)

The field used to be baked exactly once, at boot, before a save was applied —
a world where landmarks live and die (S9) would have walked over a stale
danger field, and loads DID walk the seed's virgin derivations (canon-audit
G2). Closed by **`rebake_world`** (`app/main.cpp`): zones are re-derived
whole, with every other derived field in dependency order, on every LOAD, on
the seasonal settle, and on EVERY macro↔micro transition (S7, the owner's
literal word). The GPU zone texture follows through the surgical
`upload_zone_field` (binding 2, `vk_macro_renderer`) at the macro path's
dirty flush — never a mid-frame drain (Session 19's law). The full rebaker
contract lives in [context.md](context.md).

## A second world field is coming

The danger field is the first per-cell world field; **THE FIELD** —
one signed `int8` per cell, `−127` saturated BLACK ENERGY … `0` … `+127` saturated
MAGIC — is the second, and it is designed to live in this family
(per-cell layer, diffused, regenerated rather than hand-authored; cf. the baked
night-glow spread in [macro-lighting.md](macro-lighting.md)). Negative sources:
cults, cult landmarks, cultist squads and black artifacts wherever they are kept
(plus a carrier's trail). Positive sources: strong mages — magical landmarks above
all — and dragons. **Annihilation
is the addition operator, not a routine to write.** Spec: [lore.md](lore.md)
§1.1 and §3.5. `NOT BUILT`. (`world_fields.h` is its ready socket: a saved
truth-field is one registry row.)

## The field used to be CUT at the world seam (fixed 2026-08-20)

`NOISE_BASE_CELLS` was 96, and the noise lattice closes only when its period
DIVIDES the world's width. None of the five octaves did: the base wrapped at
1056 cells, the next at 1008, and so on. Measured on the shipped field, the
jump across the seam was **10.9× a normal neighbouring step**, and the quantised
danger BAND changed on **39.9 % of the seam's rows** against 3.7 % anywhere
else — a player crossing x = 0 walked out of a safe haven into a war zone for
no reason in the world. 128 is the nearest power of two; every octave (128, 64,
32, 16, 8) now divides 1024, and the same measurement puts the seam at 0.17× a
normal step. Seamlessness is a property of construction (CANON.md S1).
