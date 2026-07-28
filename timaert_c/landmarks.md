# Landmarks — Ландмарки

The 4th cell layer (above biome, feature, zone): settlements, capitals, villages,
spires/dungeons, and runtime markers.

- **Code:** [macro/state.h](src/macro/state.h) (`Settlement`, `Village`,
  `Spire`), [macro/politik.h](src/macro/politik.h) (placement),
  [macro/markers.h](src/macro/markers.h), subworld
  [gens/](src/sub/gens) (interiors)
- **TS origin:** `game/politik.ts`, `game/markers.ts`, `subworld/city-generator.ts`…
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Politics System, §Marker System

## Model

- **Placement:** capitals + cities from politik (kingdom-driven); villages
  scattered; spires from `generate_spires()` gated on zone ≥ 5.
- **Interiors:** entering a landmark cell runs its self-contained subworld
  generator (city streets/walls, village farms/houses, ruins).
- **Markers:** universal overlay — 4 styles (quest/poi/danger/waypoint), each a
  glyph + ARGB colour in `kMarkerGlyph` / `kMarkerColor`, stored in
  `GameState::markers` and drawn by style in `draw_macro_overlay`. Quests
  auto-produce the gold "!" pins (`rebuild_quest_markers`, [quests.md](quests.md));
  the **QuestMarkers** UI element toggles/scales them ([ui-settings.md](ui-settings.md)).

## Data-driven extension

Add a kingdom/capital rule → one `kingdom_defs()` row. Add a marker style → one
`MarkerStyle` enumerator + one `kMarkerGlyph` / `kMarkerColor` entry.

## Connections

Zones gate spires ([zones.md](zones.md)); quests place/remove markers
([quests.md](quests.md)); settlements host economy and recruitment
([economy.md](economy.md), [microcombat.md](microcombat.md)).
