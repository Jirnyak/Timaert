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
- The smallest unit of the world grid.
- Contains position data and may hold objects, terrain, resources, and zone classifications.

### Object (OBJ)
- Any interactive, unique entity in the world: NPCs, caravans, armies, spell effects, or dynamically-behaving landmarks.

### Landmark
- A persistent, typically non-movable object such as cities, castles, ruins, or monster dens.
- Landmarks can change state, be created, or destroyed, but such events are rare.


## Global Map Simulation

The global map models terrain, climate, resources, politics, and long-term simulation of populations and movement.

### World Generation
1. Seed large continents and islands on a field array.
2. Apply noise/diffusion to produce organic landforms.
3. Generate a climate map (temperature field).
4. Overlay resource maps based on terrain (forests, iron, clay, land fertility).

**Resource Distribution:**
- Iron deposits favor mountainous regions.
- Clay concentrates on coasts and deserts.
- Forests occupy temperate zones.

### Kingdoms & Political Map
- The world contains approximately 128 distinct factions: major kingdoms, minor powers, bandit camps, cults, and others.
- Each kingdom occupies territories on the political map, much like real-world nation-states.
- Kingdoms spawn units and interact through trade, warfare, migration, and events.
- Each kingdom tracks: total population, leaders (lords), ownership status, treasury, resources, and relationships with all other factions (on a scale of −127 to +127).

### Landmarks
**Types:** Cities, villages, castles/keeps, monster/bandit dens, ruins, and sanctuaries.

**Functions:**
- **Cities:** accumulate wealth and population; host NPCs and lords; serve as quest hubs; spawn caravans, armies, and pilgrims.
- **Villages:** spawn peasants who harvest local resources and deliver goods to city markets.
- **Castles/Keeps:** seats of power for lords; contain quests; provide defensive structures.
- **Ruins & Sanctuaries:** repositories of artifacts, spellbooks, and high-value encounters.

### Difficulty Zones
A separate layer classifies tiles by danger level to control encounter spawning and event intensity:

| Zone | Difficulty Range |
|------|------------------|
| Cities | 0–10 |
| Roads & Paths | 5–15 |
| Forests | 10–20 |
| Landmarks & High-value Sites | 15–30 |

Difficulty scales upward over game time to maintain challenge progression.

### Game Time
- **Primary tick:** 1 hour of in-game time.
- **Movement:** Approximately 1 tile traversal ≈ 1 day (adjustable based on unit speed; the fastest units may cross a tile in 1 hour).
- **Aging:** Entities progress in age as game time elapses (players and NPCs gain one year per 365 game days).
- **Population dynamics:** Landmark populations update annually using a simple growth model (e.g., logistic or sigmoid curve).


## Fight System (RPG, Turn-Based)

Tactical combat is separate from global simulation and uses turn-based mechanics.

### Attributes
- **Primary stats:** HP, MP, STR, INT, CHA, LCK, SPD, AGI, END, WIL, WIS

- small synergy at high levels k<<1
- HP stats (STR, END, AGI) 
$$\text{HP}(\text{lvl}, \text{END}, \text{STR}, \text{AGI}) = \text{lvl} \cdot 10 \cdot \text{END} \cdot \left(1 + k_1 \sqrt{\text{STR}} + k_2 \sqrt{\sqrt{\text{STR} \cdot \text{AGI}}}\right)$$
- MP stats (WIL, INT WIS)
$$\text{MP}(\text{lvl}, \text{WIL}, \text{INT}, \text{WIS}) = \text{lvl} \cdot 10 \cdot \text{WIL} \cdot \left(1 + k_1 \sqrt{\text{INT}} + k_2 \sqrt{\sqrt{\text{INT} \cdot \text{WIS}}}\right)$$
- MP=lvl⋅10⋅WIL⋅(1+k1*sqrt(INT)+k2sqrt(sqrt(INT⋅WIS)) 

- Misc stats (LCK, SPD, CHA)
- LCK - better loot, favorable encounters, crit +1%
- SPD - map movement speed, combat initiative, asymptotic 
- CHA - trade prices, relation bonus 10 per CHA
- STR phys damage mult
- END hp regen
- AGI dodge
- WIL mana regen
- INT spell damage mult 
- WIS exp bonus +1$

- Spell mechanics - spell damage is increased by mana applied in spell 
- example - fireball 10 base damage 10 mana. each 10 mana increase damage +10%

**Experience System:**

Let $\text{lvl}_p$ = player level, $\text{lvl}_m$ = enemy level, $\text{lvl}_q$ = area level, $k$ = modifier (boss difficulty or quest length).

$$\text{EXP}_\text{next}(\text{lvl}_p) = 100 \cdot \text{lvl}_p^{1.5}$$
$$\text{EXP}_\text{fight}(\text{lvl}_m, k) = 10 \cdot \text{lvl}_m \cdot k$$
$$\text{EXP}_\text{quest}(\text{lvl}_q, k) = 20 \cdot \text{lvl}_q \cdot k$$

### Spells & Skills
Spells and skills operate across both global and tactical layers with mechanics scaled to each context.

**Cross-Layer Examples:**
- A scouting skill reveals greater detail on the world map.
- A strategic spell can alter a landmark's state or temporarily modify local difficulty.


## Future Ideas
- Expand resource types: new minerals, rare herbs, and unique artifacts.
- Dynamic caravan and trade systems with supply/demand affecting city market prices.
- Seasonal effects on travel speed, farming yields, and encounter spawn rates.
- Dynamic quests that alter landmark states (sieges, founding settlements, reclaiming ruins).
- Political depth: vassalage mechanics, treaties, and large-scale faction wars.


## Glossary
- **Cell:** smallest world grid unit.
- **OBJ:** unique interactive entity.
- **Landmark:** persistent special location.
- **Difficulty Zone:** tile classification controlling encounter and event intensity.
- **World Tick:** base time unit (1 hour).

