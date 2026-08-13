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
- **Flight rules a new spell inherits for free** (`sub/spell_effects.cpp`, detail
  in [microcombat.md](microcombat.md)): the bolt leaves the caster's eye, sweeps
  the segment it crosses each tick and strikes the first body it reaches, hits
  everyone regardless of faction, and dies on terrain, masonry or any face of the
  3×3 window. Give a spell more speed and it does NOT become worse at hitting —
  that used to be true and was the bug behind point-blank shots passing through.

## THE ACTIVE-ABILITY LAW

**Every active ability in the game is a spell** — owner's ruling, 2026-08-06
([lore.md](lore.md) §4.1). Cleric *miracles*, magebane dispels, black-energy
powers and anything the fiction invents later are **rows in THIS registry**, not
a parallel system: one set of rows, one resource path, one cast path. At most a
spell carries **one extensible `kind` field** (`spell` / `miracle` / …) for what
it is called and who teaches it — never a second registry, never a hardcoded
branch.

This is what makes the setting's central joke free rather than expensive: the
Empire of Light's religion is a forgery, so a cleric's miracles literally *are*
the heresy his order burns villages for — the same rows in the same file
([lore.md](lore.md) §3.2, §4.1).

## Learning — the spires

The in-world way to a spell is its SPIRE ([landmarks.md](landmarks.md)):
one per registered spell, standing in the wild band its `tier` demands,
guarded by the demon family of its own cell's headcount. The player
climbs the tower ([dungeons.md](dungeons.md) §Spire tower), steps onto
the crown and touches the orb — the spire flips `depleted` forever (dark
sprite, no glow, no orb), and the spell lands in the book.

The seam between layers is an event: the engine emits
`EventTag::SpireDepleted` (spire id + spell registry ordinal); the app
layer resolves the ordinal — only content/ knows the registry — teaches
the book, writes "You have learned X!" to the log and emits
`SpellLearned` for observers (quests watch spells, not spires). Console
`learn` and quest rewards remain the other two doors into the same
idempotent `spellbook_learn`.

## Data-driven extension

Add a spell → register it in `registry.cpp` + (if it has a visual) one
`spell_effects` descriptor. Sustained spells drain mana via `tick`. The
next world offers the new spell's spire with no further change — count,
placement and the tower's storeys all derive from the registry row.

## Connections

Mana/costs from the RPG system ([rpg.md](rpg.md)); damage resolves in combat
([microcombat.md](microcombat.md)); unlocks are part of progression
([progression.md](progression.md)).
