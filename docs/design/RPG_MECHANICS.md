# Universal RPG Mechanics System

## Summary

A performance-first RPG architecture designed to be **universal for all entities** (players, NPCs, enemies). Character progression is driven by:

- **Classes** — Determine starting skills; additional skills learned during playthrough
- **Levels** — Grant attribute points and skill points for character growth

The system is built on five core layers:

1. **Attributes** — Provide raw flat bonuses to stats (HP, MP, damage, etc.)
2. **Skills** — Provide percentage multipliers applied after attribute bonuses
3. **Spells** — Enable magic abilities and effects
4. **Items** — Weapons and artifacts with varied bonuses
5. **Perks** — Strong build-defining bonuses for specialization

## Overview

A minimal, universal RPG mechanics system that can be applied uniformly to all entities (players, NPCs, enemies) for consistency and easy extension.

---

## Core Components

### Resources

| Resource                  | Purpose                      | Critical State             |
|---------------------------|------------------------------|----------------------------|
| **HP** (Hit Points)       | Health                       | ≤ 0 → Dead                 |
| **MP** (Mana Points)      | Casting spells               | Can't cast when empty      |
| **SP** (Stamina Points)   | Actions & world movement     | ≤ 0 → Damage HP            |

### Resource Attributes

| Resource | Primary Attribute | Raw Bonus | Skill Multiplier |
|----------|-------------------|-----------|------------------|
| **HP**   | VIT               | +10 per point | Bodybuilding (+5%/rank) |
| **MP**   | WIL               | +10 per point | Meditation (+5%/rank) |
| **SP**   | END               | +10 per point | Endurance (+5%/rank) |

---

### 1. Attributes (`src/game/attributes.ts`)

Eight primary attributes, each providing **raw flat bonuses**:

| Attribute              | Code   | Raw Bonus per Point                                                 |
|------------------------|--------|---------------------------------------------------------------------|
| **STR** (Strength)     | `str`  | +1 physical damage                                                  |
| **VIT** (Vitality)     | `vit`  | +10 max HP, +0.1 HP regen                                           |
| **END** (Endurance)    | `end`  | +10 max SP, +0.1 SP regen                                           |
| **WIL** (Willpower)    | `wil`  | +10 max MP, +0.1 MP regen                                           |
| **INT** (Intelligence) | `int`  | +1 spell damage                                                     |
| **WIS** (Wisdom)       | `wis`  | +1% EXP bonus                                                       |
| **LCK** (Luck)         | `lck`  | Crit scaling (asymptotic), better loot                               |
| **CHA** (Charisma)     | `cha`  | +1 relation per point, -1% trade tariffs                             |
| **SPD** (Speed)        | `spd`  | Movement speed (asymptotic)                                          |
---

### 2. Level & Experience System

#### Level Data

- `level` — Current character level (starts at 1)
- `exp` — Current experience points
- `exp_to_next` — Experience required for next level
- `skill_points` — Available skill points for allocation

#### Level Rewards

**Per Level:**
- **+1 Skill Point** — Can be allocated to any known skill
- **+3 Attribute Points** — Can be allocated to any attribute
- **+1 Perk Point every 10 levels** — Can be used to select powerful perks

**Starting Points at Level 1:**
- **8 Attribute Points** — To distribute among the 8 core attributes
- **3 Skill Points** — To allocate to known skills
- **1 Perk Point** — To select your first perk

#### Experience Formulas

**Experience required for next level:**

$$EXP\_next(lvl) = 1000 \cdot lvl \cdot (0.1 \cdot lvl + 1)$$

**Experience from defeating enemies:**

$$EXP\_fight(lvl_m, k) = 10 \cdot lvl_m \cdot k$$

**Experience from completing quests:**

$$EXP\_quest(lvl_q, k) = 100 \cdot lvl_q \cdot k$$

**Where:**
- $k$ — Difficulty modifier (1.0 normal, 1.5 elite, 2.0 boss, etc.)
- $lvl_m$ — Enemy/monster level
- $lvl_q$ — Quest level

#### Point Allocation

**Attribute Points:**
- Start with 9 points at level 1
- Gain +3 per level thereafter
- Allocate to increase individual attributes one at a time
- Can be allocated at any time while points remain

**Skill Points:**
- Start with 3 points at level 1
- Gain +1 per level thereafter
- Can only allocate to skills the character knows
- New skills learned through gameplay, quests, or events

**Perk Points:**
- Start with 1 point at level 1
- Gain +1 perk point every 10 levels (levels 10, 20, 30, etc.)
- Used to select powerful, build-defining perks
- Each perk provides significant advantages and disadvantages

---

### 3. Combat Stats (Attributes = Raw, Skills = Multipliers)

The core design: **attributes provide raw flat growth**, **skills provide percentage multipliers**.

$$FinalStat = (Base + AttributeRaw) \times (1 + SkillRank \times SkillMult)$$

#### Hit Points (HP)

$$HP = (100 + VIT \times 10) \times (1 + Bodybuilding \times 0.05)$$

- **Base HP:** 100
- **Attribute:** VIT (+10 per point)
- **Skill:** Bodybuilding (+5% per rank)

#### Mana Points (MP)

$$MP = (100 + WIL \times 10) \times (1 + Meditation \times 0.05)$$

- **Base MP:** 100
- **Attribute:** WIL (+10 per point)
- **Skill:** Meditation (+5% per rank)

#### Stamina Points (SP)

$$SP = (100 + END \times 10) \times (1 + Endurance \times 0.05)$$

- **Base SP:** 100
- **Attribute:** END (+10 per point)
- **Skill:** Endurance (+5% per rank)

**HP regeneration:**

$$HP\_regen = 1 + VIT \times 0.1$$

**MP regeneration:**

$$MP\_regen = 0.5 + WIL \times 0.1$$

**SP regeneration:**

$$SP\_regen = 2 + END \times 0.1$$

---

### 4. Derived Bonuses

Derived from attributes (raw flat) and skills (multipliers):

```typescript
type DerivedBonuses = {
    rawPhysDamage: number;   // STR × (1 + fighter × 0.05)
    rawSpellDamage: number;  // INT × (1 + spellcraft × 0.05)
    expMult: number;         // 1 + WIS × 0.01
    moveSpeedMult: number;   // (1 + SPD / (SPD + 50)) × (1 + travel × 0.03)
    tradeDiscount: number;   // CHA × 0.01
    relationBonus: number;   // CHA
    critBase: number;        // LCK / (LCK + 50)  [asymptotic]
};
```

**Physical damage:** base weapon damage + rawPhysDamage

**Spell damage:** base spell damage + rawSpellDamage × tierMultiplier

#### Combat Formulas

**Crit Chance (asymptotic):**

$$crit = \frac{LCK}{LCK + 50}$$
---

### 5. Integration with Player

The `Player` struct now includes:

```cpp
Attributes attributes{};              // Primary attributes
LevelData level_data{};               // Level & experience tracking
CombatStats combat_stats{};           // HP, MP, regen (derived)
DerivedBonuses derived_bonuses{};     // Calculated bonuses
int32_t attribute_points_spent = 10;  // Tracks allocation
```

## Universality & Extensibility

### Design for NPCs/Enemies

The same structures can be used for any entity:

```cpp
struct NPC {
    Attributes attributes;
    LevelData level_data;
    CombatStats combat_stats;
    DerivedBonuses derived_bonuses;
    // ... other NPC data
};
```

### Easy Perk Integration

Perks modify base values before calculation:

```cpp
// Example: +100 BASE HP perk
int base_hp = 100 + perk_bonuses.base_hp;
combat_stats.recalculate(base_hp, base_mp, attributes);
```

### Item & Enchantment System

Items can temporarily modify attributes:

```cpp
// Apply item bonus
Attributes effective_attrs = player.attributes;
effective_attrs.str += item.str_bonus;

---

// Recalculate with effective attributes
player.derived_bonuses.recalculate(effective_attrs);
```

### Spell Damage Scaling

Active skills are also implemented as spells for universality minimalism.

Spells use raw spell damage as additive bonus:

```typescript
// Spell strength = rawSpellDamage × tierMultiplier
// Final = (baseDamage + spellStrength) × scalingPower
const s = spellStrength(spell, attributes, skills);
const damage = Math.floor((baseDamage + s) * scaling.power);
```

## UI Display (stat_state.h)

The character sheet displays:

#### 1. Level & Experience

- Current level
- EXP bar: current / next threshold

#### 2. Attributes (with allocation)

- Shows all 8 attributes with current values
- Available attribute points displayed in green if > 0
- Clickable "+" buttons to increase attributes
- Hover highlighting on allocatable attributes

#### 3. Derived Stats (implicit in calculations)

- Physical damage multiplier (raw + skill mult)
---

- Spell damage bonus (raw + skill mult)
- Critical strike chance
- Movement speed boost

#### 4. Vitals (calculated or display)

- Current/Max HP
- Current/Max Mana
- Current/Max Stamina

## Example: Attribute Allocation
---

## Performance Notes

- All calculations use `noexcept` for zero-cost guarantees
- Synergy terms (products of attributes) are minimal with coefficient 0.001
- Asymptotic functions prevent overflow at high levels
- Recalculation only occurs on attribute changes (not every frame)

---

## 5. Skills

### Overview

Skills provide **percentage multipliers** applied after attribute raw bonuses. This inverts the old model: attributes now give flat growth, skills amplify it.

**Key Principles:**
- Skills apply percentage multipliers to stats built from attribute raw bonuses
- Skills do not modify attributes directly
- Players earn **+1 skill point per level**
- Initial skill list is determined by character class
- Additional skills can be learned through events, quests, and gameplay

**Final Stat Calculation Order:**

```
FinalStat = (Base + Σ AttributeRawBonuses + Σ ItemBonuses)
            × (1 + SkillRank × SkillCoefficient)
            × SituationalMultipliers
```

---

### Skill Points & Progression

**Earning Skill Points:**
- Characters receive **1 skill point per level**
- Skill points can be allocated to any skill the character knows
- Skill points are separate from attribute points

**Learning New Skills:**
- **Starting skills** — Determined by character class at level 1
- **Event-based learning** — Random events may teach new skills
- **Quest rewards** — Completing quests can unlock new skills
- **Trainers** — NPCs may teach specific skills for a cost

---

### Classes

Classes determine the initial set of skills available to a character.

**Class Role:**
- Defines starting skill list at character creation
- Does not restrict attribute allocation
- Does not prevent learning additional skills later

**Examples:**
- **Warrior** — Starts with combat skills (Swordsman, Bodybuilding)
- **Traveler** — Starts with survival skills (Travel, Navigation)
- **Mage** — Starts with magic-related skills (Spellcasting, Meditation)

*Note: Specific class implementations are defined in [src/systems/character_templates.h](src/systems/character_templates.h)*

---

### Skill Rules

- **Percentage multipliers** — Skills multiply the raw stat built from attributes
- **No attribute modification** — Skills do not modify attributes
- **Linear coefficient scaling** — Each rank adds a fixed % (e.g. +5%)
- **Conditional activation** — Some skills may require contextual conditions (weapon, state)
- **Attributes symmetry** — Total skills should use each of 9 attributes uniformly

---

### Skill Data Structure

Located in `src/game/attributes.ts`:

```typescript
type Skills = {
    bodybuilding: number;  // +5% max HP per rank
    meditation: number;    // +5% max MP per rank
    travel: number;        // +3% move speed per rank
    fighter: number;       // +5% physical damage per rank
    endurance: number;     // +5% max SP per rank
    spellcraft: number;    // +5% spell damage per rank
};
```

---

### Physical Skills

#### Bodybuilding

| Property   | Value          |
|------------|----------------|
| Category   | Physical       |
| Condition  | Always active  |

**Multiplier effect (per rank):**
- Max HP: **+5%**

**Formula:**

$$MaxHP = (100 + END \times 10) \times (1 + Bodybuilding \times 0.05)$$

---

#### Meditation

| Property   | Value          |
|------------|----------------|
| Category   | Physical       |
| Condition  | Always active  |

**Multiplier effect (per rank):**
- Max MP: **+5%**

**Formula:**

$$MaxMP = (100 + WIL \times 10) \times (1 + Meditation \times 0.05)$$

---

#### Endurance

| Property   | Value          |
|------------|----------------|
| Category   | Physical       |
| Condition  | Always active  |

**Multiplier effect (per rank):**
- Max SP: **+5%**

**Formula:**

$$MaxSP = (100 + END \times 10) \times (1 + Endurance \times 0.05)$$

---

### Combat Skills

#### Fighter

| Property   | Value                 |
|------------|-----------------------|
| Category   | Combat                |
| Condition  | Always active         |

**Multiplier effect (per rank):**
- Physical damage: **+5%**

**Formula:**

$$PhysDmg = STR \times (1 + Fighter \times 0.05)$$

---

#### Spellcraft

| Property   | Value                 |
|------------|-----------------------|
| Category   | Combat                |
| Condition  | Always active         |

**Multiplier effect (per rank):**
- Spell damage: **+5%**

**Formula:**

$$SpellDmg = INT \times (1 + Spellcraft \times 0.05)$$

---

### Miscellaneous Skills

#### Travel

| Property   | Value  |
|------------|--------|
| Category   | Misc   |
| Condition  | Always active on world map |

**Multiplier effect (per rank):**
- Movement speed: **+3%**

**Formula:**

$$MoveSpeed = (1 + SPD / (SPD + 50)) \times (1 + Travel \times 0.03)$$

---

### Skill Application Order

Skills are applied in the following sequence:

base → attributes (raw) → items → skills (multipliers) → perks → situational

```typescript
const rawStat = base + attributeBonus + itemBonus;
const final = rawStat * (1 + skillRank * skillCoefficient) * situationalMods;
```

---

### Universality & Extensibility

**Universal Application:**
- Skills apply to players, NPCs, enemies

**New Skill Requirements:**
- Apply a percentage multiplier to a stat
- Define activation conditions
- Stack multiplicatively with other skill bonuses

---

## 6. Perks

### Overview

Perks are powerful, build-defining choices that provide both significant advantages and disadvantages. They fundamentally alter gameplay and character builds.

**Key Principles:**
- **Very strong and game-changing** — Each perk dramatically affects gameplay
- **Both advantage and disadvantage** — Every perk has a tradeoff
- **Limited selection** — Start with 1 perk point at level 1, gain 1 more every 10 levels
- **Permanent choices** — Once selected, perks cannot be removed

---

### Perk Points

**Earning Perk Points:**
- Start with **1 perk point at level 1**
- Gain **+1 perk point every 10 levels** (levels 10, 20, 30, 40, etc.)

**Using Perk Points:**
- Each perk costs 1 perk point to select
- Perks are selected from a defined list
- Some perks may have prerequisites or restrictions

---

### Available Perks

#### Immortal
**Advantage:** Never die from old age  
**Disadvantage:** 100% more EXP needed to level up

#### Short-Lived
**Advantage:** 100% more EXP gained  
**Disadvantage:** Die of old age at 33

#### Mechanical
**Advantage:** Start with +100 attribute points, 50 skill points & choose 10 skills  
**Disadvantage:** No level-up or EXP gain

#### Talented
**Advantage:** Instantly gain 1 level  
**Disadvantage:** Uses 1 perk point

#### Gifted
**Advantage:** Choose two attributes; one is multiplied by 2  
**Disadvantage:** Other chosen attribute is divided by 2

#### Natural
**Advantage:** +3 attribute point per level  
**Disadvantage:** No skill points gained

#### Educated
**Advantage:** +1 skill point per level  
**Disadvantage:** No attribute points gained

---

### Perk Implementation

Perks are stored as a `Set<PerkID>` in the player state and apply their effects through various game systems:

```typescript
export type Perks = Set<PerkID>;

// Check if player has a perk
if (hasPerk(player.perks, 'immortal')) {
    // Apply immortal effects
}
```

---

### Future Perks

Additional perks planned for implementation:

- **God's Mark** — All attributes doubled; die after 1 year
- **Saint** — Random attribute changes based on moral choices
- **Possess** — Take over other entities; no personal growth
- **Death Word** — Kill anyone with a word; permanently 1 HP
- **Antimagus** — Cannot cast magic; invincible to magic
- **Magic Body** — Mana serves as HP
- **Blood Magic** — Spend HP to cast spells
- **Leader** — Start with own faction; age increased by 10 years
- **Revenant** — Resurrect after death with penalties
- And many more...