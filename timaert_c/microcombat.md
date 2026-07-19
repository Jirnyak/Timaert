# Microcombat — Микробой (меч и магия ARPG)

**Unified, in-subworld combat — there is no separate battle mode.** Player,
NPCs, garrison soldiers, and bandits share **one stat block** and **one engine**.
Sword-and-magic ARPG resolution happens as normal subworld play.

- **Code:** [macro/army.h](src/macro/army.h) (`CombatTemplate`),
  [sub/ai.cpp](src/sub/ai.cpp), [sub/engine.h](src/sub/engine.h),
  [sub/spawn.h](src/sub/spawn.h)
- **TS origin:** `subworld/engine.ts`, `subworld/ai.ts`, `game/army.ts`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Combat System

## Model

- **One `CombatTemplate`** (hp, damage, speed, range, cooldown, melee/missile)
  on every NPC kind — no RPS table, no per-unit-type stats.
- **Hostility is faction-driven:** derived from `factions[...].relation`;
  `kHitRepPenalty` on attacking neutrals, `kHostileThreshold` flips a faction.
- **Engine:** `tick_combat_move` for melee + missile; `kCrowdPenalty` spreads
  gangs into natural formations; `kDetectionRadius` awareness.
- **Death:** killing blow → XP to the killer's owner; corpse holds data-driven
  loot ([rpg.md](rpg.md)); zone danger gates whether the player may leave.

## Data-driven extension

Make a kind hireable/soldier-capable → tag it + set one stat row + one upkeep
number in the registry. Add loot → one `kNpcLoot[]` row.

## Backend / GPU (the primary target)

This is the headline compute-simulation case: **thousands of combatants**. The
crowd is **GPU-resident** (compute shaders over packed SSBOs); an NPC is
**embodied** to the CPU/ECS the instant the player can act on it (reticle /
engagement radius), then de-embodied afterward — same entity, no discontinuity.
Kernels obey the four crowd rules (data packing, lookup buffers, branchless
damage math, cohort sorting) and the **no-stall** transfer rule (embody the few,
never read back the mass). See [ARCHITECTURE.md](ARCHITECTURE.md)
§GPU-Driven Simulation.
