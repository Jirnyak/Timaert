# Universal RPG Mechanics System

## Summary

A performance-first RPG architecture designed to be **universal for all entities** (players, NPCs, enemies). Character progression is driven by:

- **Classes** — Determine starting skills; additional skills learned during playthrough
- **Levels** — Grant attribute points and skill points for character growth

The system is built on five core layers:

1. **Attributes** — Provide percentage multipliers and determine event check outcomes
2. **Skills** — Grant flat base bonuses and unlock tactical options
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

| Resource | Primary Attributes |
|----------|--------------------|
| **HP**   | END                |
| **MP**   | WIL                |
| **SP**   | SPD                |

---

### 1. Attributes (`src/systems/attributes.h`)

Nine primary attributes, each providing specific bonuses:

| Attribute              | Code   | Effect                                                              |
|------------------------|--------|---------------------------------------------------------------------|
| **STR** (Strength)     | `str`  | Physical damage +1% per point, carry weight                        |
| **END** (Endurance)    | `end_` | HP regen +1% per point, HP                                          |
| **AGI** (Agility)      | `agi`  | Dodge, SP regen +1% per point                                       |
| **WIL** (Willpower)    | `wil`  | MP regen +1% per point, MP                                          |
| **INT** (Intelligence) | `int_` | Spell damage +1% per point, active spell slots +1 per point        |
| **WIS** (Wisdom)       | `wis`  | Experience bonus +1% per point, learned spell slots +1 per point   |
| **LCK** (Luck)         | `lck`  | Better loot, crit                                                    
| **CHA** (CHARISMA)     | `cha`  | +1 relation per point, -1% trade tarifs                             |
| **SPD** (Speed)        | `spd`  | Movement speed +1% (asymptotic), SP                                 |
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
- **+1 Attribute Point** — Can be allocated to any attribute
- **+1 Perk Point every 3 levels** — Can be used to select powerful perks

**Starting Points at Level 1:**
- **9 Attribute Points** — To distribute among the 9 core attributes
- **5 Skill Points** — To allocate to known skills
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
- Start with 5 points at level 1
- Gain +1 per level thereafter
- Can only allocate to skills the character knows
- New skills learned through gameplay, quests, or events

**Perk Points:**
- Start with 1 point at level 1
- Gain +1 perk point every 10 levels (levels 10, 20, 30, etc.)
- Used to select powerful, build-defining perks
- Each perk provides significant advantages and disadvantages

---

### 3. Combat Stats (Calculated from Attributes)

#### Hit Points (HP)

$$HP(HP_0, END) = HP_0 \cdot \left( 1 + 0.1 \cdot END \right)$$


- **Base HP:** 100 + perk bonuses
- **Primary contributors:** END

#### Mana Points (MP)

$$MP(MP_0, WIL) = MP_0 \cdot \left( 1 + 0.1 \cdot WIL \right)$$

- **Base MP:** 10 + perk bonuses
- **Primary contributors:** WIL

#### Stamina Points (SP)

$$SP(SP_0, SPD) = SP_0 \cdot \left( 1 + 0.1 \cdot SPD \right)$$

- **Base SP:** 100 + perk bonuses
- **Primary contributors:**\cdot \left(1 + 0.1 \cdot SPD)$$

**HP regeneration:**

$$HP\_regen = base\_hp\_regen \cdot (1 + 0.1 \cdot END)$$

**MP regeneration:**

$$MP\_regen = base\_mp\_regen \cdot (1 + 0.1 \cdot WIL)$$

**SP regeneration:**

$$SP\_regen = base\_sp\_regen \cdot (1 + 0.1 \cdot AGI)$$

---

### 4. Derived Bonuses

Automatic calculations derived from attributes:

```cpp
struct DerivedBonuses {
    float phys_damage_mult;     // 1.0 + STR * 0.01
    float carry_weight_mult;    // 1.0 + STR * 0.01
    float spell_damage_mult;    // 1.0 + INT * 0.01
    float hp_regen_mult;        // 1.0 + END * 0.01
    float mp_regen_mult;        // 1.0 + WIL * 0.01
    float sp_regen_mult;        // 1.0 + AGI * 0.01
    float exp_mult;             // 1.0 + WIS * 0.01
    float move_speed_mult;      // 1.0 + SPD / (SPD + 50) [asymptotic]
    float trade_discount;       // CHA * 0.01
    int32_t relation_bonus;    // CHA * 1
};
```

#### Combat Formulas

**Dodge Chance (Defender vs Attacker):**

```cpp
float dodge_chance = agi_defender / (agi_defender + agi_attacker + K);
```
---

**Crit Chance (Attacker vs Defender):**

```cpp
float crit_chance = lck_attacker / (lck_attacker + lck_defender + K);
```
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

Spells inherit attribute bonuses automatically:

```cpp
int spell_damage = base_damage * caster.derived_bonuses.spell_damage_mult;
```

## UI Display (stat_state.h)

The character sheet displays:

#### 1. Level & Experience

- Current level
- EXP bar: current / next threshold

#### 2. Attributes (with allocation)

- Shows all 9 attributes with current values
- Available attribute points displayed in green if > 0
- Clickable "+" buttons to increase attributes
- Hover highlighting on allocatable attributes

#### 3. Derived Stats (implicit in calculations)

- Physical damage multiplier
---

- Spell damage multiplier
- Critical strike chance
- Dodge chance
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

Skills provide flat base stat increases applied before attribute-based multipliers or specific mechanics bonuses and unlocks.

**Key Principles:**
- Skills do not modify attributes or derived percentage bonuses
- Skills affect base values only before multipliers are applied
- Players earn **+1 skill point per level**
- Initial skill list is determined by character class
- Additional skills can be learned through events, quests, and gameplay

**Final Stat Calculation Order:**

```
FinalStat = (BaseStat + Σ SkillBaseBonuses + Σ ItemBaseBonuses)
            × AttributeMultipliers
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

- **Base values only** — Skills affect base values only
- **No attribute modification** — Skills do not modify attributes
- **No percentage multipliers** — Skills do not apply percentage multipliers
- **Linear scaling** — Skills scale linearly
- **Conditional activation** — Skills may require contextual conditions (weapon, state)
- **Atrributes symmetry** - number of total skills should use each of 9 attributes uniformly (e.g. 90 skills 10 for each attributes)

---

### Skill Data Structure

Located in [src/systems/skills.h](src/systems/skills.h):

```cpp
struct Skills {
    int32_t bodybuilding;
    int32_t travel;
    int32_t fighter;
};
```

---

### Physical Skills

#### Bodybuilding

| Property   | Value          |
|------------|----------------|
| Category   | Physical       |
| Condition  | Always active  |

**Base stat effects (per rank):**
- Base HP: **+1**

**Formulas:**

$$BaseHP = BaseHP_0 + Bodybuilding$$

---

### Combat Skills

#### Swordsman

| Property   | Value                 |
|------------|-----------------------|
| Category   | Combat                |
| Condition  | Weapon type == Sword  |

**Base stat effects (per rank, while sword equipped):**
- Base weapon damage: **+1**
- Base stamina cost (attacks): **−1.0%**

**Formulas:**

$$BaseWeaponDamage = BaseWeaponDamage_0 + Swordsman$$

$$BaseSP\_cost = BaseSP\_cost_0 \times (1 - 0.005 \times Swordsman)$$

---

### Miscellaneous Skills

#### Travel

| Property   | Value  |
|------------|--------|
| Category   | Misc   |
| Condition  | Always active on world map |

**Base stat effects (per rank):**
- Terrain weight: **−1%** per point

**Formulas:**

$$BaseSP\_cost = BaseSP\_cost_0 \times (1 - 0.01 \times Travel)$$

---

### Skill Application Order

Skills are applied in the following sequence:

base → skills → items → perks → attributes → situational

```cpp
BaseStats base = base_stats;
apply_skills(base, skills);
apply_items(base, items);
apply_perks(base, perks);
FinalStats final = calculate(base, attributes, situational_mods);
```

---

### Universality & Extensibility

**Universal Application:**
- Skills apply to players, NPCs, enemies

**New Skill Requirements:**
- Modify only base stats
- Define activation conditions
- Stack additively with item base bonuses

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