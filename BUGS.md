# BUGS & DESIGN ISSUES

This document tracks known bugs, design conflicts, and architectural issues in the project.
Contributions welcome! All concerns and proposals are encouraged—no idea is too small.

**Reporting Format:**
- **Location**: Main project file or code line
- **Explanation**: Detailed description of the problem
- **Proposal**: Suggested solution or approach

---

## Critical Issues

### 1. random_events.cpp System Architecture
**Location**: [src/systems/random_events.cpp](src/systems/random_events.cpp)

**Explanation**: 
- ~1500 lines of scripted code with significant conflicts across game systems
- Not linked to any specific mechanics or simulation (pure randomness)
- Will conflict with future plot/quest system implementation
- Current architecture makes modular integration difficult

**Proposal**: 
Combine random events with the future plot/quest system using a script-based approach (analogy to RenPy):
- Design a unified narrative/event system before implementing quests
- Make events data-driven rather than hardcoded
- Allow dynamic event triggering based on game state
- **Priority**: HIGH - Must be addressed before quest/plot system design to ensure clean, modular integration

---

## Medium Priority Issues


### 2. Hardcoded Faction System
**Location**: [src/core/types.h](src/core/types.h) - `enum class FactionID`

**Explanation**:
- Limited, hardcoded factions prevent dynamic gameplay
- Conflicts with planned politics and economics design
- Cannot support dynamical factions, player-owned factions, or faction destruction/creation
- Blocks procedural generation of factions

**Proposal**:
Introduce a small faction class system (analogy to entity objects):
- Each faction as an object with properties: name, treasury, resources
- Support procedural generation of factions
- Allow runtime faction creation/destruction
- Support player-owned factions
- Integrate with ECS architecture (`ecs::World`)

**Reference**: See [prototype politics system](https://github.com/Jirnyak/politic_sim) for architectural inspiration

---

### 5. Trading and Fighting System
**Location**: [src/states/battle_state.h](src/states/battle_state.h)

**Explanation**:
- Combat mode auto-triggers when on same tile as most entities, making it impossible to leave cities without fighting
- City trading mode is separate from main combat system and doesn't use the new inventory system
- Talk and fight options are mixed together without proper state separation
- RPG mechanics and skill system are disconnected from interactions

**Proposal**:
- Create two separate systems: **Interaction State** (talk, trade, quests, choose to fight) and **Battle State**
- Player always enters an interaction screen with available options before entering relevant mode (fight/trade/plot)
- Ensure RPG attributes are linked uniformly:
  - Charisma for dialogue and trading
  - Strength/Agility/Speed/Level for combat, peace, flee probability
- Keep systems modular and extensible for future quest integration

---

## Minor Issues & Polish

### 3. Terrain Type Jungle vs. Forest Objects
**Location**: [src/core/types.h](src/core/types.h) - `TerrainType::Jungle`

**Explanation**:
- Visual representation is uninviting (renders as dark squares on the world map)
- Conflicts with design goals and player experience
- Duplication of concept: both terrain type Jungle and forest objects (trees) represent trees
- Unclear distinction between terrain-level vegetation and object-level vegetation
- Causes ambiguity in world representation

**Proposal**:
- Clarify separation of concerns: terrain represents broad biome type, objects represent specific entities
- Either:
  - Option A: Replace Jungle terrain with Grass/appropriate terrain + place forest objects
  - Option B: Keep Jungle as terrain marker but improve visuals and clearly define it as dense vegetation biome
- Ensure consistency with forest/tree object system

---

### 4. Resource and Politics Map Rendering
**Location**: [src/states/map_state.h](src/states/map_state.h)

**Explanation**:
- Black color (0,0,0) incorrectly renders as red (255,0,0) when switching to resource/politics map view
- Works correctly in relief map mode
- Resulting visuals are uninviting

**Proposal**:
- Debug color mapping in resource/politics map rendering pipeline
- Improve visual palette for resource and politics maps to make them more visually appealing and distinctive

## Guidelines for Contributors

When reporting issues:
1. Be specific about location (file path + line number when possible)
2. Explain the conflict or problem clearly
3. Propose a solution that aligns with the codebase philosophy
4. Consider ECS architecture and performance-first design principles
5. Link related issues and affected systems
6. Mark resolved issues with ~~strikethrough~~ and note the solution
