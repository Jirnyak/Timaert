# 2D Simulation: Fantasy Medieval RPG Economic System

## 1. World Model

### 1.1 World Grid

- Size: **1024 x 1024 cells**
- Each cell contains:
  - `terrain_type`
  - `resource_fields`
  - `movement_cost`
  - optional modifiers

### 1.2 Resource Fields (Modular System)

Each cell contains a dictionary:

```latex
R_cell = \{ r_1, r_2, ..., r_n \}
```

Where each resource:

```latex
r_i = (type, density, regeneration_rate)
```

### Base Resources (expandable)

- Iron
- Fertility (crop potential)
- Clay
- Wood
- Gold
- Water
- Coal
- Gems
- Fur
- (Modular: add new resource without system rewrite)

### Resource Density

```latex
0 \le density \le 1
```

Regeneration:

```latex
density_{t+1} = density_t + regen - extraction
```

---

## 2. Landmarks

### 2.1 Types

- Village
- City

Each landmark:

```latex
L = (population, inventory, location, production_capacity)
```

Population drives everything.

---

## 3. Population Mechanics

Population determines:

- Units spawned per tick
- Production rate
- Consumption rate
- Caravan count

### Population Growth Model

Population in each settlement grows according to a normalized logistic (sigmoid) function, with a maximum carrying capacity of 1,000,000:

```latex
P_{t+1} = P_t + r \cdot P_t \left(1 - \frac{P_t}{K}\right)
```

Where:
- $P_t$ — current population
- $r$ — growth rate coefficient (e.g. 0.01 per tick, tunable)
- $K$ — carrying capacity (set to 1,000,000)

This ensures rapid growth at low population, slowing as the population approaches the cap. Growth can be further modified by food supply, unrest, or other factors:

```latex
P_{t+1} = P_t + r \cdot P_t \left(1 - \frac{P_t}{K}\right) \cdot F \cdot S
```

Where:
- $F$ — food sufficiency factor ($0 < F \leq 1$)
- $S$ — stability/unrest factor ($0 < S \leq 1$)

If food or stability is low, growth slows or reverses (if $F$ or $S < 0$).

### Spawn Functions

```latex
Peasants = k_p \cdot Population
Caravans = k_c \cdot Population
ProductionRate = k_{prod} \cdot Population
ConsumptionRate = k_{cons} \cdot Population
```

Initial balance:

```latex
k_{cons} \approx \frac{1}{10} k_{prod}
```

(To be tuned by testing)

---

## 4. Villages

### Function

- Gather raw resources
- Store in local inventory
- Sell to caravans
- Deliver to nearest city

### Peasant Squads

Each squad:

- Assigned to nearest high-density cell
- Extracts per tick:

```latex
Extracted = BaseGatherRate \cdot SkillCoef \cdot CellDensity
```

Store in village inventory.

---

## 5. Cities

### Function

- Buy raw resources
- Produce goods
- Spawn caravans

### Taxation & Political System

- **Taxation:**
  - Each city collects gold based on its population and trade volume:
    ```latex
    Gold_{city} = \alpha \cdot Population + \beta \cdot TradeVolume
    ```
    Where $\alpha$ and $\beta$ are tunable coefficients.
  - A percentage of city gold is paid as tax to the controlling faction/kingdom:
    ```latex
    Gold_{tax} = Gold_{city} \cdot TaxPercentage
    ```
    The remainder stays in the city treasury for local use.

- **Political System:**
  - Every cell and landmark is controlled by a specific faction (e.g., kingdom, city-state, bandit clan).
  - Each city/landmark has an `owner_faction` property.
  - Factions receive tax income from all cities/landmarks they control:
    ```latex
    Gold_{faction} = \sum_{i \in cities} Gold_{tax,i}
    ```
  - Faction gold can be used for diplomacy, armies, development, etc. (future expansion).

This system allows for dynamic control shifts, economic competition, and emergent political gameplay.

### Production Chains (Minimal Model)

```latex
k_1 \cdot Resource_A + k_2 \cdot Resource_B \rightarrow k_3 \cdot Good_C
```

Example:

- Iron + Coal → Tools
- Fertility + Water → Bread
- Fur + cloth → Clothes
- Clay + Coal → Pottery

Production per tick:

```latex
Produced = ProductionCapacity \cdot SkillCoef
```

---

## 6. Inventory Model

Each landmark:

```latex
Inventory = \{ item_i : quantity \}
```

No global market.
All trade is local and emergent.

---

## 7. Caravan System (Emergent Trade)

### Caravan Logic

1. Spawn at city
2. Load goods proportional to surplus:

```latex
Load_i = max(Inventory_i - LocalNeed_i, 0)
```

3. Choose destination city:
   - Expected profit estimation
4. Travel (pathfinding)
5. Sell if profitable
6. Buy local surplus
7. Return or redirect

Global distribution emerges from:

- Price differences
- Distance cost
- Local scarcity

---

## 8. Market System

Each item has:

```latex
IntrinsicValue_i
```

### Local Market Price

Let:

- `S_i` = local supply
- `D_i` = local demand

Define demand factor:

```latex
DemandFactor_i = \frac{D_i}{S_i + \epsilon}
```

### Sell Price (NPC sells to player)

```latex
P_{sell} = IV + Commission \cdot IV \cdot CHA_{seller} \cdot DemandFactor
```

### Buy Price (NPC buys from player)

```latex
P_{buy} = IV \cdot (1 - Commission) \cdot \frac{1}{DemandFactor}
```

Alternative simplified symmetric form:

```latex
Price_i = IV_i \cdot (1 + \alpha \cdot \ln(D_i / S_i))
```

(Stable and smooth)

No global prices.
Everything local.

---

## 9. Consumption Model

Cities and villages consume:

- Food
- Tools
- Basic goods

Per tick:

```latex
Consumed_i = BaseNeed_i \cdot Population
```

If shortage:

- Productivity decreases
- Population growth slows
- Risk of unrest (optional future feature)

---

## 10. RPG Skill System

### Gathering Skill

```latex
GatherRate = BaseRate \cdot (1 + SkillLevel \cdot \beta)
```

### Production Skill

```latex
Output = BaseOutput \cdot (1 + SkillLevel \cdot \gamma)
```

Higher skill:

- More output from same input
- Reduced waste

---

## 11. Critical Improvements (Necessary)

### 11.1 Transportation Cost

Without this, economy breaks.

```latex
EffectiveProfit = SellPrice - BuyPrice - DistanceCost
```

DistanceCost:

```latex
DistanceCost = Distance \cdot TransportCoef
```

Prevents infinite arbitrage.

---

### 11.2 Resource Depletion Pressure

High density extraction lowers future density.
Forces migration and dynamic economy.

---

### 11.3 Soft Price Stabilization

Use logarithmic function instead of linear.

Prevents runaway inflation/deflation.

---

### 11.4 Production Limiter

Production must depend on input availability:

```latex
ActualProduction = min(
\frac{Resource_A}{k_1},
\frac{Resource_B}{k_2}
)
```

Avoids infinite production.

---

## 12. Emergent Properties Expected

- Trade routes naturally appear
- Mining towns grow near iron
- Fertile regions become food exporters
- Luxury goods cluster in wealthy cities
- Border towns fluctuate with caravan flow
- Resource exhaustion creates economic shifts

---

## 13. Modular Expansion Points

Easy to add:

- New resource
- New production chain
- New landmark type (Castle, Port)
- Taxation system
- Factions controlling cities
- War disrupting trade
- Seasonal fertility changes
- Bandits attacking caravans
- Banking / credit system

No core rewrite required.

---

## 14. Minimal Simulation Tick

Per tick order:

2. Peasants gather
3. Production executes
4. Consumption applies
5. Price recalculation
6. Caravan decision making
7. Movement resolution
8. Trade resolution
9. Population adjustment

---

## Design Philosophy

- Fully local economy
- No global controller
- Emergent trade
- Deterministic but dynamic
- Minimal formulas, maximal emergence
- Mount & Blade inspired but systemic

---

END
