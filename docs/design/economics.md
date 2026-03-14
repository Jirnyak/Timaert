# Universal Economic Simulation System

A performance-first economic simulation for a fantasy RPG world, designed to be **universal for all settlements and factions**. The system is modular, extensible, and supports emergent trade, local markets, and dynamic political control.

---

## Core Components

### World Grid

| Property         | Description                |
|------------------|---------------------------|
| Size             | 1024 x 1024 cells         |
| Cell Properties  | terrain, resources, cost  |
| Resource Fields  | Modular, see below        |

#### Resource Fields

Each cell:

$$R_{cell} = \{ r_1, r_2, ..., r_n \}$$

Each resource:

$$r_i = (type, amount)$$

| Resource   | Notes                |
|------------|----------------------|
| Iron       |                      |
| Fertility  | Crop potential       |
| Clay       |                      |
| Wood       | trees                |
| Gold       |                      |
| Water      |                      |
| Coal       |                      |
| Gems       |                      |
| Fur        |                      |

---

## Landmarks

| Type    | Description |
|---------|-------------|
| Village | Resource gathering, local trade |
| City    | Production, trade, politics     |

Each landmark:

$$L = (population, inventory, location, production\_capacity)$$

---

## Population System

Population drives:
- Unit spawn rate
- Production/consumption
- Caravan count

### Population Growth (Logistic)

$$P_{t+1} = P_t + r \cdot P_t \left(1 - \frac{P_t}{K}\right) \cdot F \cdot S$$

| Symbol | Meaning                |
|--------|------------------------|
| $P_t$  | Current population     |
| $r$    | Growth rate (e.g. 0.01)|
| $K$    | Carrying capacity (1M) |
| $F$    | Food sufficiency $0-1$ |
| $S$    | Stability $0-1$        |

If $F$ or $S$ is low, growth slows or reverses.

#### Spawn & Production

| Output         | Formula                        |
|---------------|---------------------------------|
| Peasants      | $k_p \cdot Population$          |
| Caravans      | $k_c \cdot Population$          |
| Production    | $k_{prod} \cdot Population$     |
| Consumption   | $k_{cons} \cdot Population$     |

Initial balance: $k_{cons} \approx \frac{1}{10} k_{prod}$

---

## Villages

| Function         | Description                  |
|------------------|-----------------------------|
| Gather resources | Peasant squads assigned      |
| Store inventory  | Local storage                |
| Sell/deliver     | To caravans/cities           |

**Extraction per tick:**
$$Extracted = BaseGatherRate \cdot SkillCoef \cdot CellDensity$$

---

## Cities

| Function         | Description                  |
|------------------|-----------------------------|
| Buy resources    | From villages/caravans/peasants |
| Produce goods    | See production chains        |
| Spawn caravans   | For trade                    |

City specialization - labor allocation production percantage of pop

### Taxation & Politics

| Concept         | Formula/Description |
|-----------------|--------------------|
| City gold       | $Gold_{city} = \alpha \cdot Population + \beta \cdot TradeVolume$ |
| Tax paid        | $Gold_{tax} = Gold_{city} \cdot TaxPercentage$ |
| Faction income  | $Gold_{faction} = \sum_{i \in cities} Gold_{tax,i}$ |
| Ownership       | Each cell/landmark: `owner_faction` |

Faction gold can be used for diplomacy, armies, development, etc.

### Production Chains

$$k_1 \cdot Resource_A + k_2 \cdot Resource_B \rightarrow k_3 \cdot Good_C$$

| Example                | Formula/Notes |
|------------------------|--------------|
| Iron + Coal → Tools    |              |
| Fertility + Water → Bread |           |
| Fur + Cloth → Clothes  |              |
| Clay + Coal → Pottery  |              |

Production per tick:
$$Produced = ProductionCapacity \cdot SkillCoef$$

---

## Inventory Model

Each landmark:
$$Inventory = \{ item_i : quantity \}$$

No global market. All trade is local and emergent.

---

## Caravan System

| Step | Description |
|------|-------------|
| 1    | Spawn at city |
| 2    | Load surplus: $Load_i = \max(Inventory_i - LocalNeed_i, 0)$ |
| 3    | Choose destination (profit estimate) |
| 4    | Travel (pathfinding) |
| 5    | Sell if profitable |
| 6    | Buy local surplus |
| 7    | Return/redirect |

Emergent global distribution from price, distance, scarcity.

transport cost for caravan naturally from it SP deplete (minimise SP to gold profit)

---

## Market System

Each item:
$$IntrinsicValue_i$$

### Local Market Price

| Symbol | Meaning |
|--------|---------|
| $S_i$  | Local supply |
| $D_i$  | Local demand |

**Demand factor:**
\[
\boxed{
\begin{aligned}
\text{Pressure}_i &= \ln\Bigl(\frac{D_i + \epsilon}{S_i + \epsilon}\Bigr),\\
P^{\text{target}}_i &= IV_i \cdot \bigl(1 + \alpha \cdot \text{Pressure}_i \bigr),\\
P_{i,t+1} &= P_{i,t} + \lambda \bigl(P^{\text{target}}_i - P_{i,t}\bigr)
\end{aligned}
}
\]

\text{Defaults: } \epsilon = 1,\quad \alpha = 0.15,\quad \lambda = 0.1


| Price Type | Formula |
|------------|---------|
| Sell (NPC→player) | $P_{sell} = IV + Commission \cdot IV \cdot CHA_{seller} \cdot DemandFactor$ |
| Buy (NPC←player)  | $P_{buy} = IV \cdot (1 - Commission) \cdot \frac{1}{DemandFactor}$ |
| Symmetric alt.    | $Price_i = IV_i \cdot (1 + \alpha \cdot \ln(D_i / S_i))$ |

No global prices. Everything is local.

---

## Consumption Model

| Consumed | Formula |
|----------|---------|
| Food, tools, goods | $Consumed_i = BaseNeed_i \cdot Population$ |

If shortage:
- Productivity decreases
- Population growth slows
- Risk of unrest (future)

---

## RPG Skill System

| Skill Type | Formula |
|------------|---------|
| Gathering  | $GatherRate = BaseRate \cdot (1 + SkillLevel \cdot \beta)$ |
| Production | $Output = BaseOutput \cdot (1 + SkillLevel \cdot \gamma)$ |

Higher skill: more output, less waste.

---


## Emergent Properties

- Trade routes naturally appear
- Mining towns grow near iron
- Fertile regions become food exporters
- Luxury goods cluster in wealthy cities
- Border towns fluctuate with caravan flow

---

## Modular Expansion

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

## Minimal Simulation Tick

| Step | Description |
|------|-------------|
| 2    | Peasants gather |
| 3    | Production executes |
| 4    | Consumption applies |
| 5    | Market Price recalculation |
| 6    | Caravan decision making |
| 7    | Movement resolution |
| 8    | Trade resolution |
| 9    | Population adjustment |

---

## Design Philosophy

- Fully local economy
- No global controller
- Emergent trade
- Deterministic but dynamic
- Minimal formulas, maximal emergence
- Mount & Blade inspired but systemic

---
