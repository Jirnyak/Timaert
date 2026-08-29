# Landmarks — Ландмарки

The 4th cell layer (above biome, feature, zone): settlements, capitals, villages,
spires/dungeons, and runtime markers.

- **Code:** [macro/state.h](src/macro/state.h) (`Landmark` — the ONE record,
  `GameState::landmarks` — the ONE roster), [macro/politik.h](src/macro/politik.h) (placement),
  [macro/settlement_score.h](src/macro/settlement_score.h) (site capacity),
  [macro/markers.h](src/macro/markers.h), subworld
  [gens/](src/sub/gens) (interiors)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Politics System, §Marker System

## Model

- **ONE RECORD, ONE ROSTER (owner verdict, 2026-08-29, save v62):** a landmark
  is one `struct Landmark` (`macro/state.h`) with a **`type` column**
  (`LandmarkType`), and the world holds them in ONE
  `std::vector<Landmark> GameState::landmarks`. The structs `Settlement` /
  `Village` / `Spire` and their three vectors are DEAD, and with them every
  "which list are you in" branch — the second vocabulary against S16 that made
  villages produce nothing and hand out no quests. Columns a kind does not use
  ride at zero defaults (a village's `spellId` is 0, a spire's `garrison` is
  empty) — every kind writes every column, so the SERIALIZER is one pair too
  (`write_landmark` / `read_landmark`, `save.cpp`) instead of six functions.
  Kind-specific state is a column, not a struct: `spellId` + `depleted` are the
  spire's columns; `population` / `mood` / `inventory` / `garrison` /
  `famineActive` are the settlement's; `renown` is everyone's
  ([chronicle.md](chronicle.md)). Lookup is `landmark_by_id(gs, id)` — ids are
  world-unique ordinals (v54), so no kind argument exists to get wrong.
  **`for_each_landmark` carries the cell-ownership priority**: it yields in the
  constant `kLandmarkYieldOrder` (City → Village → Spire → Ruin → Lair →
  Shrine → Mine → Tower, `landmark_iter.h`), so "first yielded at a cell wins
  the cell" — the same order `build_landmark_grid` bakes into the u16 grid.
- **ONE vocabulary, one baked answer (2026-08-24):** `LandmarkType` — the
  registry enum ([macro/landmark_registry.h](src/macro/landmark_registry.h))
  — is the one landmark vocabulary on every layer. The subworld's
  `CellLandmarkKind` and the fauna router's `LandmarkKind` (two more
  five-value copies joined by a hand bridge, which physically could not name
  Lair/Shrine/Mine/Tower — canon-audit C1/C2) are dead. "Who stands on this
  cell" is answered by the baked u16 **`LandmarkGrid`**
  ([macro/landmark_grid.h](src/macro/landmark_grid.h)) in the iterator's one
  priority order; LIVE fields — population, tier, kingdom, depleted — are
  resolved from `GameState` by the `{type, id}` the grid returns, at the
  moment of asking. The grid is rebaked by `rebake_world` on every load,
  seasonal settle and macro↔micro transition ([context.md](context.md)) —
  the ready support for S9: a landmark that is born, dies or transmutes adds
  its transition at the rebake points and nowhere else.
- **The registry's spawn columns:** `minZone`/`maxZone` are now DANGER BYTES
  on the 0..255 continuum ([zones.md](zones.md); the old 0..9 rows translated
  as band edges — City 0..76, Spire 128..255, unchanged in meaning, finer in
  resolution), and the new **`spawnFaction`** column forces a faction onto
  every creature the place rolls (Ruin/Spire = `"demons"`: a ruin's wolves
  ARE demons — the spawn law reads it, [monsters.md](monsters.md)). Since
  2026-08-29 the row also answers the FAUNA questions as columns —
  `faunaHabitat` / `faunaMin` / `faunaMax` / `faunaCap` — cross-guarded by
  static_assert against `fauna.h`'s habitat masks, so a place's wildlife
  budget is registry data, not a switch in the spawner.
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
  world, no other change. The danger field IS the placement law: the tier
  walks the registry row's own byte band in four DERIVED steps —
  `gate = minZone + (tier − 1) · (maxZone − minZone) / 4` — so tier 1 opens
  at the Spire row's minZone (128, the old "Untamed") and tier 5 demands its
  maxZone (255, "Hellgate"); a doom spell's spire stands where the world is
  at its worst and its guards are strong because strong things live there —
  no per-spire scaling ([zones.md](zones.md)). Even spread by best-candidate
  sampling (max-min distance to placed spires); named places veto their cell
  (the landmark grid's one priority order would shadow a co-located spire);
  the gate relaxes step-by-step on worlds missing the band; deterministic
  from `worldSeed`. Placed AFTER
  `generate_zones` in boot (the one landmark outside
  `populate_landmarks_from_politik`, which only clears the list).
  `Landmark::spellId` (the registry ordinal) is the ONE saved key; tier is
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

## S9 — the life cycle: UNBLOCKED, machinery still not built

CANON S9 says a landmark is **an agent with a life cycle**: it grows, can be
destroyed (leaving a ruin, not an empty cell), can change owner through squads,
can BECOME another landmark (village → city as a universal contextual
transition, not a hardcode), and new ones can arise — the player founding his
own through the same mechanism. **The machinery does not exist yet**, but the
one-roster verdict (v62, above) removed the structural wall: a transmutation is
now **a write to the `type` column of an existing record** — the id, the name,
the inventory, the renown all survive it by construction, because they are
columns of the same row, not fields of a struct in the wrong vector. Before
v62, village → city meant destroying a `Village`, birthing a `Settlement`, and
teaching every reader and the save about the move; now it is one assignment
plus a `rebake_world` at the sanctioned rebake points (`landmark_grid.h` names
them). Today `type` is still assigned only at creation (`state.cpp` ×2,
`spires.cpp`) and load (`save.cpp`); there is no `erase`, no runtime
emergence; the one live state change is the spire's `depleted` bool flip. The
registry kinds Ruin / Lair / Shrine / Mine / Tower have rows but are never
instantiated — they wait as the transition targets (a razed city writes
`type = Ruin` and stays the same record). The transitions machinery is the
next track.

**Known structural debts:**

- ~~**`Settlement` + `Village` are two structs for one canon row.**~~ —
  **DEAD 2026-08-29** (the one-record verdict, v62). Its named fruit fell with
  it: the village production DOOR is open — `tick_villages_` calls
  `econ_produce_day(…, EconSite::Village, …)` daily, so a producing village is
  now one `site = Village` recipe row away (all nine `kRecipes` still say
  City — a balance decision, no longer a structural wall;
  [economy.md](economy.md)). Village quests are unblocked the same way.
- ~~`landmark_registry.cpp` is entirely dead~~ — **axed 2026-08-24** (H7):
  the caller-less `collect_landmarks` aggregator and its glued-on TS-era half
  are gone; `landmark_registry.h` is now only the live `kLandmarks` table.

## Connections

Zones gate spires by spell tier ([zones.md](zones.md)); a spire cell's
subworld raises the tower and its climb ([dungeons.md](dungeons.md) §Spire
tower), and the orb on the crown teaches the spell
([spells.md](spells.md) §Learning); quests place/remove markers
([quests.md](quests.md)); settlements host economy and recruitment
([economy.md](economy.md), [microcombat.md](microcombat.md)).
