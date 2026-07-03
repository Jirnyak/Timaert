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
- **Markers:** universal POI overlay — 4 styles (quest/poi/danger/waypoint),
  stored in `GameState::markers`, drawn via ImGui foreground.

## Data-driven extension

Add a kingdom/capital rule → one `kingdom_defs()` row. Add a marker style → one
entry in `kMarkerColors` / `kMarkerGlyphs`.

## Connections

Zones gate spires ([zones.md](zones.md)); quests place/remove markers
([quests.md](quests.md)); settlements host economy and recruitment
([economy.md](economy.md), [microcombat.md](microcombat.md)).
