# Spell System

Modular, oldschool (MM6/7/8 + D&D), dual-layer (macro + micro ARPG).

## Architecture

```
src/game/spells/
  spell-types.ts          — pure types (Spell, SpellBook, effects)
  spell-casting.ts        — scaling formulas, cast validation, cooldowns
  index.ts                — registry + re-exports
  definitions/
    magic-bolt.ts         — each file = one spell module
    fireball.ts
    ice-shard.ts
    energy-beam.ts
    lightning-chain.ts
    haste.ts
    flight.ts
    armageddon.ts

src/screens/
  SpellOverlay.svelte     — book UI, choose active spell
```

Layer placement: L1 (pure data + formulas). No event bus, no UI imports.

## Adding a Spell

1. Create `src/game/spells/definitions/my-spell.ts` exporting `mySpell: Spell`.
2. Import in `index.ts`, add to `SPELL_CATALOG`.
3. Done.

## Core Formula

$$S = M_{stat} \times M_{tier}$$

- $M_{stat} = 1 + INT \times 0.01$ (from `calculateDerived`)
- $M_{tier} = 1 + 0.08 \times (tier - 1)$

Outputs:
- `damage = baseDamage × S × scaling.power`
- `heal = baseHeal × S × scaling.power`
- `radius = baseRadius × (1 + (S-1) × scaling.radius)`
- `duration = baseDuration × (1 + (S-1) × scaling.duration)`

## Dual Layer

Every spell defines `micro: MicroEffect | null` and `macro: MacroEffect | null`.

**Micro** (ARPG combat): shape (projectile/beam/nova/chain/self/aura/summon/targeted),
damage, heal, radius, chain count, speed, duration, friendly fire, status effects.

**Macro** (world map): travel_speed, ignore_terrain, heal_party, damage_region,
reveal_map, buff_army.

## Design Rules

1. Each spell has unique pros AND cons — no "strictly better" spells.
2. Friendly fire on AoE creates tactical tension.
3. High-tier spells have severe drawbacks (mana, cooldown, reputation).
4. Spell discovery via books/quests/teachers — not level gates.
5. Tags (fire, ice, arcane...) for AI scoring and item synergy, not schools.
6. INT scales damage, WIL scales mana pool, WIS could gate max learned spells.

## Initial Spell Set (8 spells)

| Spell | Tier | Shape | Tags | Key Tradeoff |
|-------|------|-------|------|--------------|
| Magic Bolt | 1 | Projectile | arcane | Cheap but weak scaling |
| Fireball | 2 | Projectile+AoE | fire | Strong AoE but friendly fire |
| Ice Shard | 2 | Projectile | ice | High burst but single target |
| Energy Beam | 2 | Beam | arcane, light | Pierces line but needs aim |
| Lightning Chain | 3 | Chain | lightning | Multi-target but unpredictable |
| Haste | 2 | Self-buff | body, air | Speed boost but no damage |
| Flight | 3 | Self-buff | air, arcane | Terrain bypass but short + expensive |
| Armageddon | 5 | Nova | fire, dark | Destroys everything including reputation |

## UI — SpellOverlay

Book-style overlay (press B). Opens in both macro and micro world.
Left sidebar: learned spell list with active indicator.
Right page: spell details — icon, name, rarity, stats, scaled values, pros/cons,
layer availability badges, equip button.

Active spell indicator shown in HUD. Casting uses `canCast()` → `startCast()`.

## Integration Points

- `PlayerState.spellBook: SpellBook` — learned spells + active + cooldowns
- `effect-applicator.ts` — add spell effect types as needed
- `event-types.ts` — add SpellCast event tag if logging desired
- Subworld engine — call `spellDamage()` for combat resolution
