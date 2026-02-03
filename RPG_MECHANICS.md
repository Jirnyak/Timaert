# Universal RPG Mechanics System

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
| **LCK** (Luck)         | `lck`  | Better loot, crit                                                   |
| **SPD** (Speed)        | `spd`  | Movement speed +1% (asymptotic), SP                                 |
---

### 2. Level & Experience System

#### Level Data

- `level` — Current character level (starts at 1)
- `exp` — Current experience points
- `exp_to_next` — Experience required for next level

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
- $lvl_q$ — Qculty modifier (1.0 normal, 1.5 elite, 2.0 boss, etc.)
- $lvl_m$ = enemy/:**
- Formula: `level + 9` total (10 at level 1)
- Allocate to increase individual attributes one at a time
- Can be allocated at any time while points remain
- Tracked via `attribute_points_spent`

**Perk Points:evel + 9` total (10 at level 1)
- Allocate to increase individual attributes one at a time
- Can be allocated at any time while points remain
- Tracked via `attribute_points_spent`

**Perk Points**
---

### 3. Combat Stats (Calculated from Attributes)

#### Hit Points (HP)

$$HP(HP_0, END) = HP_0 \cdot \left( 1 + 0.1 \cdot END \right)$$

<!-- HP(HP_0, END, STR, AGI) = HP_0 \cdot \left( 1 + 0.1 \cdot END + 0.05 \cdot STR + 0.03 \cdot AGI\right) -->
<!-- Synergy term: 0.001(END \cdot STR + END \cdot AGI + STR \cdot AGI) -->

- **Base HP:** 100 + perk bonuses
- **Primary contributors:** END

#### Mana Points (MP)

$$MP(MP_0, WIL) = MP_0 \cdot \left( 1 + 0.1 \cdot WIL \right)$$

<!-- Synergy term: 0.001(WIL \cdot INT + WIL \cdot WIS + WIS \cdot INT) -->

- **Base MP:** 10 + perk bonuses
- **Primary contributors:** WIL

#### Stamina Points (SP)

$$SP(SP_0, SPD) = SP_0 \cdot \left( 1 + 0.1 \cdot SPD \right)$$

<!-- Synergy term: 0.001(SPD \cdot AGI + SPD \cdot WIL + AGI \cdot WIL) -->

- **Base SP:** 100 + perk bonuses
- **Primary contributors:**\cdot \left(1 + 0.1 \cdot SPD)$$

<!-- Synergy term: 0.001(SPD \cdot AGI + SPD \cdot WIL + AGI \cdot WIL) -->

- Base SP: 100 + perk bonuses
- Primary contributors: SPD
**HP regeneration:**

$$HP\_regen = base\_hp\_regen \cdot (1 + 0.1 \cdot END)$$

**MP regeneration:**

$$MP\_regen = base\_mp\_regen \cdot (1 + 0.1 \cdot WIL)$$

**SP regeneration:**
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
    int32_t relation_bonus;     // 1.0 + END * 0.01
    float mp_regen_mult;       // 1.0 + WIL * 0.01
    float sp_regen_mult;       // 1.0 + AGI * 0.01
    float exp_mult;            // 1.0 + WIS * 0.01
    float move_speed_mult;     // 1.0 + SPD / (SPD + 50) [asymptotic]
    float trade_discount;      // CHA * 0.01
    int32_t relation_bonus;    // CHA * 1
};
```

#### Combat Formulas

**Dodge Chance (Defender vs Attacker):**

```cpp
// float dodge_chance = std::clamp((agi_defender - agi_attacker) * 0.01f, 0.0f, 1.0f);
float dodge_chance = agi_defender / (agi_defender + agi_attacker + K);
```
---

### 5. Integration with Player

The `Player` struct now includes:

```cpp
Attributes attributes{};               // Primary attributes
LevelData level_data{};                // Level & experience tracking
CombatStats combat_stats{};            // HP, MP, regen (derived)
DerivedBonuses derived_bonuses{};      // Calculated bonuses
int32_t attribute_points_spent = 10; 
---

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

## Future Extensions

The system supports:

- **Perks** — Add percentage or flat bonuses to attributes/stats
- **Enchantments** — Temporary attribute modifications
- **Classes/Archetypes** — Different stat growth curves
- **Leveling Branches** — Specialized progression paths
- **Items** —tions use `noexcept` for zero-cost guarantees
- Synergy terms (products of attributes) are minimal with coefficient 0.001
- Asymptotic functions prevent overflow at high levels
- Recalculation only occurs on attribute changes (not every frame)

## Future Extensions

The system supports:

- **Perks** - Add percentage or flat bonuses to attributes/stats
- **Enchantments** - Temporary attribute modifications
- **Classes/Archetypes** - Different stat growth curves
- **Leveling Branches** - Specialized progression paths
- **Items** - Attribute-based effect scaling
