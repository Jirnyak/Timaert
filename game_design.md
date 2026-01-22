# Samosbor — Game Design

## Table of Contents

- [Overview](#overview)
- [Core Concepts](#core-concepts)
  - [Cell](#cell)
  - [Object (OBJ)](#object-obj)
  - [Landmark](#landmark)
- [Global Map Simulation](#global-map-simulation)
  - [World Generation](#world-generation)
  - [Kingdoms & Political Map](#kingdoms--political-map)
  - [Landmarks](#landmarks)
  - [Difficulty Zones](#difficulty-zones)
  - [Game Time](#game-time)
- [Fight System (RPG, Turn-Based)](#fight-system-rpg-turn-based)
  - [Attributes](#attributes)
  - [Spells & Skills](#spells--skills)
- [Notes & Future Ideas](#notes--future-ideas)
- [Glossary](#glossary)


## Overview

Samosbor simulates a large-scale open world with persistent landmarks and emergent behaviors (similar to Mount & Blade-style world simulation). The design separates the global strategic layer (map, kingdoms, resources, population) from turn-based tactical fights.


## Core Concepts

### Cell
- A cell is the smallest unit of the world grid.
- It has a position and may contain objects, terrain, resources, and zone data.

### Object (OBJ)
- Any interactive, unique entity in the world (NPCs, caravans, armies, spells/effects, landmarks when they need unique behavior).

### Landmark
- A persistent, usually non-movable object (cities, castles, ruins, monster dens).
- Landmarks can change state, be created, or be destroyed; such events are intended to be rare.


## Global Map Simulation

The global map models terrain, climate, resources, politics, and long-term simulation of populations and movement.

### World Generation
1. Seed large continents and islands on a field array.
2. Apply noise/diffusion to produce organic landforms.
3. Generate a climate map (temperature field).
4. Overlay resource maps relative to terrain (forests, iron, clay, land fertility).

Notes:
- Resource distribution should reflect terrain (iron biased toward mountains, clay on coasts/deserts, forests in temperate zones).

### Kingdoms & Political Map
- The world contains many factions (target ~128), including major kingdoms, minor factions, bandits, cults, and others.
- Each kingdom occupies territories on the political map similar to real-world political boundaries.
- Kingdoms can spawn units and interact via trade, warfare, migration, and events.

### Landmarks
- Types: cities, villages, castles/keeps, monster/bandit dens, ruins, sanctuaries.
- Roles:
  - Cities: accumulate wealth and population, home to NPCs/lords, quest hubs, spawn caravans/armies/pilgrims.
  - Villages: spawn peasants who gather local resources and bring goods to markets.
  - Castles/Keeps: seat of lords, quest locations, defensive structures.
  - Ruins & Sanctuaries: sources of artifacts, spellbooks, and high-value encounters.

### Difficulty Zones
- A separate layer classifies tiles by danger levels (used for encounter spawning and event difficulty):
  - Cities: 0–10
  - Roads/Paths: 5–15
  - Forests: 10–20
  - Landmarks / High-value sites: 15–30
- Difficulty should increase over game time to scale challenge.

### Game Time
- Primary world tick: 1 hour.
- Movement: roughly 1 tile ≈ 1 day (adjustable, depends on unit speed; fastest unit may use 1 hour to cross a tile if desired).
- Entities age with game time (players/NPCs gain years according to elapsed game days).
- Landmark populations update yearly using a simple growth model (e.g., logistic or sigmoid curve tuned for modest growth).


## Fight System (RPG, Turn-Based)

Tactical combat is separate from global simulation and uses turn-based mechanics.

### Attributes
- Primary stats: HP, MP, STR, INT, CHA, LCK
- These affect combat, casting, interactions, and random chances.

### Spells & Skills
- Spells and skills function across both the global and tactical layers but with different mechanics and scales.
- Examples of cross-layer effects:
  - A scouting skill provides better world map intel.
  - A strategic spell might alter a landmark or modify local difficulty temporarily.


## Notes & Future Ideas
- Expand resource types (more minerals, rare herbs, unique artifacts).
- Add dynamic caravan and trade simulations with supply/demand affecting prices in city markets.
- Implement seasonal effects on travel, farming, and spawn rates.
- Build quests that can change landmark states (sieges, founding new settlements, reclaiming ruins).
- Introduce political mechanics: vassalage, treaties, and large-scale wars.


## Glossary
- Cell: smallest world grid unit.
- OBJ: unique interactive entity.
- Landmark: persistent special location.
- Difficulty Zone: tile classification controlling spawn strength.
- World Tick: base time unit (1 hour).


---

If you want, I can:
- add diagrams or a visual map of layers,
- expand any section into design specs (data structures, serialization, algorithms), or
- generate a simple prototype of the world-gen pipeline.
