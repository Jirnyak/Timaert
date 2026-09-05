# Spells — Заклинания

Modular spell framework: **adding a spell is one file, no engine changes.**

- **Code:** data rows in [macro/spells.h](src/macro/spells.h) (`kSpellDefs`);
  behaviour in [content/spells/](src/content/spells) —
  [casting.h](src/content/spells/casting.h) (the binding contract),
  [effects.cpp](src/content/spells/effects.cpp) (spawn fns, `kSpellEffects`),
  [spell_book.h](src/content/spells/spell_book.h);
  projectile ticking in [sub/spell_effects.h](src/sub/spell_effects.h)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Spell System

## Model

- **`SpellBook`** — FLAT ordinal-indexed rows over the append-only registry
  (v59): `learned[kSpellCount]`, `activeSpell` (ordinal, −1 = none),
  `cooldownSteps[kSpellCount]`, `sustained[kSpellCount]`,
  `sustainedDrainCarry`. The three string-keyed heap containers died (S26:
  flat data; S20.1: the ordinal IS the identity — strings resolve at the
  edges via `spell_ordinal`). API `learn / set_active / can_cast / cast /
  tick`, all by ordinal. Behavioural rules resolve by ROW too since
  2026-08-29: `spellbook_rule_active(book, SpellRuleId::Flight)` scans the
  sustained rows against the registry's own `rule` column — the last
  name-based check (a `"flight"` string compare) is dead.
  `cooldownSteps` holds **steps remaining, not seconds left** (core/time.h
  `kStepsPerSecond`): the registry authors seconds, the world counts the
  simulation's own integer quantum, and the only conversion back is the string
  a human reads ("Cooldown 1.4s"). `tick` takes a step COUNT. That is why a
  spell comes back after the same amount of FIGHT whether the clock above is
  racing on the map or crawling underground — where a world tick is 0.25 real
  seconds, so counting ticks instead would have made a one-second cooldown last
  sixteen. See [time.md](time.md).
- **Registry** = `kSpellDefs` (macro/spells.h): one constexpr DATA row per
  spell — fireball, ice-shard, magic-bolt, lightning-chain, energy-beam,
  armageddon, haste, flight — with APPEND-ONLY ordinals (the row index rides
  saves as `Spire.spellId`; `spell_registry_test` pins the order).
  **Lightning-chain's chain is UNREACHABLE today (canon-audit H5)** — do not
  read this row as a working mechanic: `apply_spell_chain`
  (sub/spell_effects.cpp) returns on its first line always, because the three
  `ecs::Projectile` chain fields (`chainRemaining/chainDecay/chainRadius`) are
  read but written nowhere, and `SpellDef::chainCount` is filled in 8 rows and
  read by nobody. `spell_casting_effects_test.cpp` describes this in prose and
  deliberately asserts nothing (S26: a test never guards a defect). The spell
  casts and flies as an ordinary bolt; only the chain hop is dead. Living in
  the world layers per ARCHITECTURE.md Rule 13 (owner 2026-08-14), it is
  askable by worldgen (`generate_spires`), the subworld (tier at resolve)
  and the event applicator alike. BEHAVIOUR binds above: `kSpellEffects`
  (content/spells/effects.cpp) holds one spawn fn per row, ordinal-parallel
  under a static_assert that refuses a drifted table.
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
`EventTag::SpireDepleted` (spire id + spell registry ordinal); the effect
applicator (events/effect_applicator.cpp) resolves the ordinal against
`kSpellDefs` — the registry lives below it now (Rule 13) — teaches the
book, writes "You have learned X!" to the log and hands a `SpellLearned`
follow-up back to the caller to emit for observers (quests watch spells,
not spires; the caller owns the bus and the emit-after-the-span-walk
order). Console `learn` and quest rewards remain the other two doors
into the same idempotent `spellbook_learn`.

## Data-driven extension

Add a spell → APPEND one data row to `kSpellDefs` (macro/spells.h; never
reorder — ordinals are forever) + one effect row to `kSpellEffects`
(content/spells/effects.cpp; the static_assert refuses a mismatch;
nullptr for self-buffs). Sustained spells drain mana via `tick`. The
next world offers the new spell's spire with no further change — count,
placement and the tower's storeys all derive from the registry row.

## Connections

Mana/costs from the RPG system ([rpg.md](rpg.md)). Damage is DICE of the
spell row since phase 3 (2026-09-05): `SpellDef.dice` rolled at cast through
THE strike assembly (`spell_strike` → roll_strike, caster's LCK at the crit
door), the wound + its tag's DamageType column + the crit verdict ride the
projectile to the one damage door — see [combat.md](combat.md). Unlocks are
part of progression ([progression.md](progression.md)).
