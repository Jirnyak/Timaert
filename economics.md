
# Fantasy Medieval RPG Economic System

A modular, emergent economic simulation for a 2D world, designed for performance and extensibility. Inspired by systemic games like Mount & Blade, this system models local economies, trade, and political control, supporting dynamic gameplay and future expansion.

---

## 1. World Model

### 1.1 World Grid

- **Size:** 1024 x 1024 cells
- **Cell properties:**
  - `terrain_type`
  - `resource_fields` (see below)
  - `movement_cost`
  - Optional modifiers

### 1.2 Resource Fields (Modular)

Each cell contains:

```latex
R_{cell} = \{ r_1, r_2, ..., r_n \}
```

Where each resource:

```latex
r_i = (type, density, regeneration\_rate)
```

**Base Resources (extensible):**

- Iron
- Fertility (crop potential)
- Clay
- Wood
- Gold
- Water
- Coal
- Gems
- Fur

**Resource Density:**

```latex
0 \leq density \leq 1
```

**Regeneration:**

```latex
density_{t+1} = density_t + regen - extraction
```

---

## 2. Landmarks

### 2.1 Landmark Types

- Village
- City

Each landmark:

```latex
L = (population, inventory, location, production\_capacity)
```

Population is the primary driver of economic activity.

---

## 3. Population Mechanics

Population affects:

- Unit spawn rate
- Production and consumption
- Caravan count

### 3.1 Population Growth (Logistic Model)

Population in each settlement grows according to a normalized logistic (sigmoid) function, capped at 1,000,000:

```latex
P_{t+1} = P_t + r \cdot P_t \left(1 - \frac{P_t}{K}\right)
```

Where:
- $P_t$ — current population
- $r$ — growth rate coefficient (e.g. 0.01 per tick)
- $K$ — carrying capacity (1,000,000)

Modifiers for food and stability:

```latex
P_{t+1} = P_t + r \cdot P_t \left(1 - \frac{P_t}{K}\right) \cdot F \cdot S
```

- $F$ — food sufficiency ($0 < F \leq 1$)
- $S$ — stability/unrest ($0 < S \leq 1$)

If $F$ or $S$ is low, growth slows or reverses.

### 3.2 Spawn & Production Functions

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

---

## 4. Villages

**Role:**
- Gather raw resources
- Store in local inventory
- Sell to caravans
- Deliver to nearest city

**Peasant Squads:**
- Assigned to nearest high-density cell
- Extraction per tick:

```latex
Extracted = BaseGatherRate \cdot SkillCoef \cdot CellDensity
```

---

## 5. Cities

**Role:**
- Buy raw resources
- Produce goods
- Spawn caravans

### 5.1 Taxation & Political System

- **Taxation:**
  - City gold is a function of population and trade:
    ```latex
    Gold_{city} = \alpha \cdot Population + \beta \cdot TradeVolume
    ```
  - A percentage is paid as tax to the controlling faction/kingdom:
    ```latex
    Gold_{tax} = Gold_{city} \cdot TaxPercentage
    ```
  - The remainder stays in the city treasury.
- **Political System:**
  - Every cell and landmark is controlled by a specific faction (e.g., kingdom, city-state, bandit clan).
  - Each city/landmark has an `owner_faction` property.
  - Factions receive tax income from all cities/landmarks they control:
    ```latex
    Gold_{faction} = \sum_{i \in cities} Gold_{tax,i}
    ```
  - Faction gold can be used for diplomacy, armies, development, etc. (future expansion).

### 5.2 Production Chains (Minimal Model)

```latex
k_1 \cdot Resource_A + k_2 \cdot Resource_B \rightarrow k_3 \cdot Good_C
```

**Examples:**
- Iron + Coal → Tools
- Fertility + Water → Bread
- Fur + Cloth → Clothes
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

No global market. All trade is local and emergent.

---

## 7. Caravan System (Emergent Trade)

**Caravan Logic:**
1. Spawn at city
2. Load goods proportional to surplus:
   ```latex
   Load_i = \max(Inventory_i - LocalNeed_i, 0)
   ```
3. Choose destination city (expected profit estimation)
4. Travel (pathfinding)
5. Sell if profitable
6. Buy local surplus
7. Return or redirect

**Emergent global distribution** arises from:
- Price differences
- Distance cost
- Local scarcity

---

## 8. Market System

Each item has:

```latex
IntrinsicValue_i
```

### 8.1 Local Market Price

Let:
- $S_i$ = local supply
- $D_i$ = local demand

**Demand factor:**

```latex
DemandFactor_i = \frac{D_i}{S_i + \epsilon}
```

**Sell Price (NPC sells to player):**

```latex
P_{sell} = IV + Commission \cdot IV \cdot CHA_{seller} \cdot DemandFactor
```

**Buy Price (NPC buys from player):**

```latex
P_{buy} = IV \cdot (1 - Commission) \cdot \frac{1}{DemandFactor}
```

**Alternative symmetric form:**

```latex
Price_i = IV_i \cdot (1 + \alpha \cdot \ln(D_i / S_i))
```

No global prices. Everything is local.

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

**Gathering Skill:**

```latex
GatherRate = BaseRate \cdot (1 + SkillLevel \cdot \beta)
```

**Production Skill:**

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

```latex
DistanceCost = Distance \cdot TransportCoef
```

Prevents infinite arbitrage.

### 11.2 Resource Depletion Pressure

High density extraction lowers future density. Forces migration and dynamic economy.

### 11.3 Soft Price Stabilization

Use logarithmic function instead of linear. Prevents runaway inflation/deflation.

### 11.4 Production Limiter

Production must depend on input availability:

```latex
ActualProduction = \min\left(
\frac{Resource_A}{k_1},
\frac{Resource_B}{k_2}
\right)
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

**Per tick order:**
1. Resource regeneration
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
