# Spells — Заклинания

Modular spell framework: **adding a spell is one file, no engine changes.**

- **Code:** [content/spells/](src/content/spells) —
  [spell_types.h](src/content/spells/spell_types.h),
  [spell_book.h](src/content/spells/spell_book.h),
  [registry.cpp](src/content/spells/registry.cpp);
  effects in [sub/spell_effects.h](src/sub/spell_effects.h)
- **TS origin:** `game/spells/*`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Spell System

## Model

- **`SpellBook`** { learned, activeSpellId, cooldowns, sustainedActive,
  sustainedDrainCarry }; API `learn / set_active / can_cast / cast / tick`.
- **Registry** maps spell id → type metadata + effect. Modules: fireball,
  ice-shard, lightning-chain, energy-beam, magic-bolt, armageddon, flight, haste.
- Effects become ECS projectile/beam descriptors rendered as 3D billboards /
  ribbons in the subworld.

## Data-driven extension

Add a spell → register it in `registry.cpp` + (if it has a visual) one
`spell_effects` descriptor. Sustained spells drain mana via `tick`.

## Connections

Mana/costs from the RPG system ([rpg.md](rpg.md)); damage resolves in combat
([microcombat.md](microcombat.md)); unlocks are part of progression
([progression.md](progression.md)).
