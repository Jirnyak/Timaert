# Landmarks — Ландмарки

The 4th cell layer (above biome, feature, zone): settlements, capitals, villages,
spires/dungeons, and runtime markers.

- **Code:** [macro/state.h](src/macro/state.h) (`Settlement`, `Village`,
  `Spire`), [macro/politik.h](src/macro/politik.h) (placement),
  [macro/settlement_score.h](src/macro/settlement_score.h) (site capacity),
  [macro/markers.h](src/macro/markers.h), subworld
  [gens/](src/sub/gens) (interiors)
- **TS origin:** `game/politik.ts`, `game/markers.ts`, `subworld/city-generator.ts`…
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
  `Spire.tier` rides the save (v38) because the subworld may not reach
  into the spell registry. A CONSUMED spire (the player learned its
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
  `GameState::markers` and drawn by style in `draw_macro_overlay`. Quests
  auto-produce the gold "!" pins (`rebuild_quest_markers`, [quests.md](quests.md));
  the **QuestMarkers** UI element toggles/scales them ([ui-settings.md](ui-settings.md)).

## Data-driven extension

Add a kingdom/capital rule → one `kingdom_defs()` row. Add a marker style → one
`MarkerStyle` enumerator + one `kMarkerGlyph` / `kMarkerColor` entry. Change
what land a settlement class wants → one `kSettlementScoreRows` row (weights
only). Teach the score a new resource → one term reading its registry row.

## Connections

Zones gate spires by spell tier ([zones.md](zones.md)); a spire cell's
subworld raises the tower and its climb ([dungeons.md](dungeons.md) §Spire
tower), and the orb on the crown teaches the spell
([spells.md](spells.md) §Learning); quests place/remove markers
([quests.md](quests.md)); settlements host economy and recruitment
([economy.md](economy.md), [microcombat.md](microcombat.md)).
