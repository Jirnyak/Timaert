# Landmarks — Ландмарки

The 4th cell layer (above biome, feature, zone): settlements, capitals, villages,
spires/dungeons, and runtime markers.

- **Code:** [macro/state.h](src/macro/state.h) (`Settlement`, `Village`,
  `Spire`), [macro/politik.h](src/macro/politik.h) (placement),
  [macro/settlement_score.h](src/macro/settlement_score.h) (site capacity),
  [macro/markers.h](src/macro/markers.h), subworld
  [gens/](src/sub/gens) (interiors)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Politics System, §Marker System

## Model

- **Placement:** SETTLEMENT IS DOWNSTREAM OF THE GROUND (R2, session 25).
  `settlement_site_score` is the one suitability door: terms 0..16 for
  arable (the wheat row of the resource registry), water, forest and
  deposits, vetoes on water / forest-massif / mountain rock, and weights
  as DATA — one row per settlement class. Capitals and cities: politics
  decides how many and whose, the score decides where among the valid
  candidates. Villages: the whole hinterland (half the city spacing) is
  scanned and the best sites win.
- **A village is not TYPED by its resource.** Peasants work whatever
  stands around them, so one weights row prices grain, water, wood and
  ore together; a village that grows up beside a vein is a mining village
  by neighbourhood, not by a field in the save.
- **Capacity answers three questions with one number:** where a place
  stands (argmax), how many souls live there (population = rate × score,
  floored), and how many villages a city's land feeds (summed hinterland
  capacity / quota, at least one wherever any ground is admissible).
  Villages divide that land — separation is half the hinterland — so they
  scatter around the town instead of clumping in its fattest corner.
- **Spires** ([macro/spires.cpp](src/macro/spires.cpp)): ONE spire per
  registered spell — a new spell registered is a new spire on the next
  world, no other change. The danger field IS the placement law: the
  gate is `landmark_def(Spire).minZone + tier − 1` capped at `maxZone`
  (tier 1 opens in "Untamed", tier 5 demands "Hellgate"), so a doom
  spell's spire stands where the world is at its worst and its guards
  are strong because strong things live there — no per-spire scaling.
  Even spread by best-candidate sampling (max-min distance to placed
  spires); named places veto their cell (resolve_context scan order
  would shadow a co-located spire); the gate relaxes zone-by-zone on
  worlds missing the band; deterministic from `worldSeed`. Placed AFTER
  `generate_zones` in boot (the one landmark outside
  `populate_landmarks_from_politik`, which only clears the list).
  `Spire.spellId` (the registry ordinal) is the ONE saved key; tier is
  asked from `kSpellDefs` at the moment of reading (v39 — the registry
  lives in the world layers, Rule 13). A CONSUMED spire (the player learned its
  spell, [dungeons.md](dungeons.md) §Spire tower) flips `depleted`:
  dark map sprite, no night glow, no orb.
- **Boot order is the law:** trees and deposits are derived BEFORE
  politics, because placement reads them.
- **Report card:** the `[worldgen]` boot line prints `cities`, `villages`,
  `villageless`, `vilSpacing` and the share of villages standing by water
  / ploughland / a vein — the causality, measured on every world. A second
  line prints `spires=placed/spells zones=[lo..hi] minPairDist` — every
  spell offered, every spire in the wild band, spread that reads
  "scattered".
- **Interiors:** entering a landmark cell runs its self-contained subworld
  generator (city streets/walls, village farms/houses, ruins).
- **Markers:** universal overlay — 4 styles (quest/poi/danger/waypoint), each a
  glyph + ARGB colour in `kMarkerGlyph` / `kMarkerColor`, stored in
  `GameState::markers` and drawn by style in `draw_macro_overlay`. Each style
  states its SURFACE as data (`kMarkerSurface`, [map.md](map.md)): waypoints
  are map-page ink only, quest/POI/danger signal in the world too. Quests
  auto-produce the gold "!" pins (`rebuild_quest_markers`, [quests.md](quests.md));
  the **QuestMarkers** UI element toggles/scales them ([ui-settings.md](ui-settings.md)).

## Data-driven extension

Add a kingdom/capital rule → one `kingdom_defs()` row. Add a marker style → one
`MarkerStyle` enumerator + one `kMarkerGlyph` / `kMarkerColor` entry. Change
what land a settlement class wants → one `kSettlementScoreRows` row (weights
only). Teach the score a new resource → one term reading its registry row.

## S9 — the life cycle: NOT BUILT

CANON S9 says a landmark is **an agent with a life cycle**: it grows, can be
destroyed (leaving a ruin, not an empty cell), can change owner through squads,
can BECOME another landmark (village → city as a universal contextual
transition, not a hardcode), and new ones can arise — the player founding his
own through the same mechanism. **None of that exists.** Landmarks are created
in exactly two places and never again: world generation (`state.cpp` —
`settlements.push_back` / `villages.push_back`; `spires.cpp` for spires) and
save-load (`save.cpp`, which clears and refills the same vectors). There is no
`erase`, no kind change, no runtime emergence anywhere in the tree. The canon's
own verdict applies: "a landmark list built at generation and immutable" is a
defect of the model, and everything that reads landmarks (map, knowledge, glow,
roads, spawn, paths) must eventually survive their birth, transformation and
death.

**Known structural debts:**

- **`Settlement` + `Village` are two structs for one canon row.** The split is
  why villages produce nothing (all `kRecipes` are `site = City`,
  [economy.md](economy.md)) and hand out no quests
  ([quests.md](quests.md)) — every "which list are you in" branch is a second
  vocabulary against S16.
- **`landmark_registry.cpp` is entirely dead** — its `collect_landmarks`
  aggregator has no caller — and `landmark_registry.h` is visibly two files
  merged into one (two `#pragma once` blocks: the dead TS-era aggregator on
  top, the live `kLandmarks` table below). The live table stays; the dead half
  is for the axe.

## Connections

Zones gate spires by spell tier ([zones.md](zones.md)); a spire cell's
subworld raises the tower and its climb ([dungeons.md](dungeons.md) §Spire
tower), and the orb on the crown teaches the spell
([spells.md](spells.md) §Learning); quests place/remove markers
([quests.md](quests.md)); settlements host economy and recruitment
([economy.md](economy.md), [microcombat.md](microcombat.md)).
