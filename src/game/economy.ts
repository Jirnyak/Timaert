// === Economy System — modular resource/goods/trade engine ===
//
// Layer 1 (Macroworld). Mount & Blade inspired local markets with emergent
// price gradients. Adding a new resource auto-generates goods and plugs
// into prices, production, and trade.
//
// References: ECONOMICS.MD for design rationale.

// ── Resources (modular — append here to extend economy) ──

export const RESOURCE_NAMES = ['Grain', 'Wood', 'Iron', 'Clay', 'Silver', 'Gems'] as const;
export type ResourceName = typeof RESOURCE_NAMES[number];
export const NUM_RESOURCES = RESOURCE_NAMES.length;

/** Rarity 0-1 (lower = rarer). Affects base value of derived goods. */
export const RESOURCE_RARITIES: Record<ResourceName, number> = {
	Grain: 0.5,
	Wood: 0.4,
	Iron: 0.3,
	Clay: 0.3,
	Silver: 0.2,
	Gems: 0.1,
};

/** Base resource prices (inverse of rarity × 10). */
function buildBasePrice(): Record<ResourceName, number> {
	const p: Record<string, number> = {};
	for (const r of RESOURCE_NAMES) {
		p[r] = Math.round(1 / (RESOURCE_RARITIES[r] * 2));
	}

	return p as Record<ResourceName, number>;
}

export const RESOURCE_BASE_PRICE: Record<ResourceName, number> = buildBasePrice();

/** Which terrain favours each resource. terrainIndex from biomes.ts:
 *  0=water 1=sand 2=grass 3=dirt 4=mount 5=snow 6=jungle 7=swamp 8=tundra */
export const RESOURCE_TERRAIN: Record<ResourceName, number[]> = {
	Grain: [2, 3], // Grass, dirt (plains, fertile)
	Wood: [2, 6, 8], // Grass, jungle, tundra (forested)
	Iron: [4, 3], // Mountains, dirt
	Clay: [1, 7], // Sand (rivers/desert), swamp
	Silver: [4, 5], // Mountains, snow
	Gems: [4], // Mountains only
};

// ── Goods (auto-generated from resource pairs) ──

export type Good = {
	id: string;
	name: string;
	r1: ResourceName;
	r2: ResourceName;
	baseValue: number; // 1 / (rarity_r1 × rarity_r2 × 10)
	growthK: number;
	wealthK: number;
	happinessK: number;
};

/** Hand-tuned good names and coefficients for each pair. */
const GOOD_DEFS: Record<string, {name: string; growthK: number; wealthK: number; happinessK: number}> = {
	'Grain+Wood': {
		name: 'Bread', growthK: 0.8, wealthK: 0.1, happinessK: 0.1,
	},
	'Grain+Iron': {
		name: 'Tools', growthK: 0.6, wealthK: 0.3, happinessK: 0.1,
	},
	'Grain+Clay': {
		name: 'Bricks', growthK: 0.5, wealthK: 0.4, happinessK: 0.1,
	},
	'Grain+Silver': {
		name: 'Coins', growthK: 0.2, wealthK: 0.7, happinessK: 0.1,
	},
	'Grain+Gems': {
		name: 'Jewelry', growthK: 0.1, wealthK: 0.3, happinessK: 0.6,
	},
	'Wood+Iron': {
		name: 'Weapons', growthK: 0.1, wealthK: 0.6, happinessK: 0.3,
	},
	'Wood+Clay': {
		name: 'Furniture', growthK: 0.4, wealthK: 0.5, happinessK: 0.1,
	},
	'Wood+Silver': {
		name: 'Silverware', growthK: 0.1, wealthK: 0.5, happinessK: 0.4,
	},
	'Wood+Gems': {
		name: 'Ornaments', growthK: 0.1, wealthK: 0.4, happinessK: 0.5,
	},
	'Iron+Clay': {
		name: 'Tiles', growthK: 0.3, wealthK: 0.5, happinessK: 0.2,
	},
	'Iron+Silver': {
		name: 'Armor', growthK: 0.1, wealthK: 0.5, happinessK: 0.4,
	},
	'Iron+Gems': {
		name: 'Crowns', growthK: 0, wealthK: 0.3, happinessK: 0.7,
	},
	'Clay+Silver': {
		name: 'Pottery', growthK: 0.2, wealthK: 0.4, happinessK: 0.4,
	},
	'Clay+Gems': {
		name: 'Sculptures', growthK: 0.1, wealthK: 0.3, happinessK: 0.6,
	},
	'Silver+Gems': {
		name: 'Regalia', growthK: 0, wealthK: 0.2, happinessK: 0.8,
	},
};

/** All goods, auto-generated from resource pairs. */
export const ALL_GOODS: Good[] = [];

for (let i = 0; i < NUM_RESOURCES; i++) {
	for (let j = i + 1; j < NUM_RESOURCES; j++) {
		const r1 = RESOURCE_NAMES[i];
		const r2 = RESOURCE_NAMES[j];
		const key = `${r1}+${r2}`;
		const def = GOOD_DEFS[key] ?? {
			name: `${r1}-${r2} Blend`,
			growthK: 0.33, wealthK: 0.34, happinessK: 0.33,
		};
		ALL_GOODS.push({
			id: key,
			name: def.name,
			r1, r2,
			baseValue: Math.round(1 / (RESOURCE_RARITIES[r1] * RESOURCE_RARITIES[r2] * 10)),
			growthK: def.growthK,
			wealthK: def.wealthK,
			happinessK: def.happinessK,
		});
	}
}

export const NUM_GOODS = ALL_GOODS.length;

// ── Settlement Economy State ──

export type ResourceStock = Record<ResourceName, number>;
export type GoodStock = Record<string, number>; // Key = Good.id
export type PriceTable = Record<string, number>; // Key = resource name or good id

export type EconomyState = {
	resources: ResourceStock;
	goods: GoodStock;
	prices: PriceTable;
	wealth: number;
	happiness: number;
	/** Resources this settlement can gather (villages) or empty (cities). */
	localResources: ResourceName[];
};

export function createEconomyState(localResources: ResourceName[] = []): EconomyState {
	const resources: Record<string, number> = {};
	for (const r of RESOURCE_NAMES) {
		resources[r] = 0;
	}

	const goods: GoodStock = {};
	for (const g of ALL_GOODS) {
		goods[g.id] = 0;
	}

	const prices: PriceTable = {};
	for (const r of RESOURCE_NAMES) {
		prices[r] = RESOURCE_BASE_PRICE[r];
	}

	for (const g of ALL_GOODS) {
		prices[g.id] = g.baseValue;
	}

	return {
		resources: resources as ResourceStock,
		goods, prices, wealth: 0, happiness: 0.5, localResources,
	};
}

// ── Resource Gathering (villages) ──

const GATHER_BASE = 0.3;

export function gatherResources(eco: EconomyState, population: number): void {
	const workers = Math.sqrt(population) * eco.happiness;
	for (const r of eco.localResources) {
		const amount = workers * GATHER_BASE * RESOURCE_RARITIES[r];
		eco.resources[r] += amount;
	}
}

// ── Goods Production (cities) ──

const PRODUCTION_RATE = 0.15;

export function produceGoods(eco: EconomyState, population: number): void {
	const efficiency = Math.sqrt(population) * PRODUCTION_RATE;
	for (const g of ALL_GOODS) {
		const have1 = eco.resources[g.r1];
		const have2 = eco.resources[g.r2];
		if (have1 >= 1 && have2 >= 1) {
			const batch = Math.min(have1, have2, efficiency);
			eco.resources[g.r1] -= batch;
			eco.resources[g.r2] -= batch;
			eco.goods[g.id] += batch;
		}
	}
}

// ── Price Model (local supply & demand) ──

const PRICE_DECAY = 0.05; // How fast prices converge toward equilibrium
const PRICE_FLOOR = 1;
const PRICE_CEILING = 500;
const DEMAND_PER_POP = 0.01;

export function updatePrices(eco: EconomyState, population: number, isCity: boolean): void {
	const baseDemand = population * DEMAND_PER_POP;

	// Resource prices
	for (const r of RESOURCE_NAMES) {
		const supply = eco.resources[r];
		// Cities demand resources for production; villages have surplus
		const demand = isCity ? baseDemand * 2 : baseDemand * 0.3;
		const ratio = supply > 0.01 ? demand / supply : 10;
		const target = RESOURCE_BASE_PRICE[r] * ratio;
		eco.prices[r] += (target - eco.prices[r]) * PRICE_DECAY;
		eco.prices[r] = Math.max(PRICE_FLOOR, Math.min(PRICE_CEILING, eco.prices[r]));
	}

	// Good prices
	for (const g of ALL_GOODS) {
		const supply = eco.goods[g.id];
		const demand = baseDemand;
		const ratio = supply > 0.01 ? demand / supply : 10;
		const target = g.baseValue * ratio;
		eco.prices[g.id] += (target - eco.prices[g.id]) * PRICE_DECAY;
		eco.prices[g.id] = Math.max(PRICE_FLOOR, Math.min(PRICE_CEILING, eco.prices[g.id]));
	}
}

// ── Goods Satisfaction → Population Growth / Happiness / Wealth ──

const TAX_RATE = 0.001;
const CONSUME_RATE = 0.02;

export function consumeAndGrow(eco: EconomyState, population: number): {popDelta: number} {
	let growthSignal = 0;
	let happinessSignal = 0;
	let wealthSignal = 0;

	// Consume goods for satisfaction
	for (const g of ALL_GOODS) {
		const stock = eco.goods[g.id];
		const consume = Math.min(stock, population * CONSUME_RATE);
		if (consume > 0) {
			eco.goods[g.id] -= consume;
			growthSignal += consume * g.growthK;
			wealthSignal += consume * g.wealthK * eco.prices[g.id];
			happinessSignal += consume * g.happinessK;
		}
	}

	// Also consume raw food (grain)
	const grainConsume = Math.min(eco.resources.Grain, population * CONSUME_RATE * 0.5);
	if (grainConsume > 0) {
		eco.resources.Grain -= grainConsume;
		growthSignal += grainConsume * 0.5;
	}

	// Wealth from trade surplus + local production
	eco.wealth += wealthSignal;
	eco.wealth *= (1 - TAX_RATE); // Tax drain (future: flows to faction)

	// Happiness — slow convergence based on goods satisfaction
	const targetHappiness = Math.min(1, 0.3 + happinessSignal / Math.max(1, population * 0.1));
	eco.happiness += (targetHappiness - eco.happiness) * 0.1;
	eco.happiness = Math.max(0.1, Math.min(1, eco.happiness));

	// Population growth — logistic model
	const popCap = 100 + eco.wealth * 0.5;
	const growthRate = 0.01 * growthSignal / Math.max(1, population * 0.1);
	const logistic = growthRate * (1 - population / popCap);
	const popDelta = Math.round(logistic * population);

	return {popDelta};
}

// ── Trade (universal caravan/peasant system) ──

export type TradeRoute = {
	originId: number;
	destId: number;
	cargo: Array<{key: string; qty: number; buyPrice: number}>;
	arrivalDay: number;
};

const DISTANCE_COST_FACTOR = 0.0001; // Quadratic distance cost
const CARAVAN_CAPACITY = 20;
const TRADE_COOLDOWN = 3; // Days between dispatches

export function findBestTradeRoute(
	origin: {id: number; x: number; y: number; eco: EconomyState},
	destinations: Array<{id: number; x: number; y: number; eco: EconomyState}>,
	isVillage: boolean,
	currentDay: number,
	lastTrade: number,
): TradeRoute | undefined {
	if (currentDay - lastTrade < TRADE_COOLDOWN) {
		return undefined;
	}

	let bestDest: typeof destinations[number] | undefined;
	let bestProfit = 0;
	let bestCargo: TradeRoute['cargo'] = [];

	for (const dest of destinations) {
		const dx = origin.x - dest.x;
		const dy = origin.y - dest.y;
		const distSq = dx * dx + dy * dy;
		const distanceCost = distSq * DISTANCE_COST_FACTOR;

		const cargo: TradeRoute['cargo'] = [];
		let totalProfit = 0;

		// Villages export resources; cities export goods
		if (isVillage) {
			for (const r of RESOURCE_NAMES) {
				const stock = origin.eco.resources[r];
				if (stock < 2) {
					continue;
				}

				const margin = dest.eco.prices[r] - origin.eco.prices[r] - distanceCost;
				if (margin > 0) {
					const qty = Math.min(stock * 0.5, CARAVAN_CAPACITY - cargo.reduce((s, c) => s + c.qty, 0));
					if (qty >= 1) {
						cargo.push({key: r, qty: Math.floor(qty), buyPrice: origin.eco.prices[r]});
						totalProfit += margin * Math.floor(qty);
					}
				}
			}
		} else {
			// City exports goods
			for (const g of ALL_GOODS) {
				const stock = origin.eco.goods[g.id];
				if (stock < 2) {
					continue;
				}

				const margin = dest.eco.prices[g.id] - origin.eco.prices[g.id] - distanceCost;
				if (margin > 0) {
					const qty = Math.min(stock * 0.5, CARAVAN_CAPACITY - cargo.reduce((s, c) => s + c.qty, 0));
					if (qty >= 1) {
						cargo.push({key: g.id, qty: Math.floor(qty), buyPrice: origin.eco.prices[g.id]});
						totalProfit += margin * Math.floor(qty);
					}
				}
			}

			// Cities can also send surplus resources
			for (const r of RESOURCE_NAMES) {
				const stock = origin.eco.resources[r];
				if (stock < 5) {
					continue;
				}

				const margin = dest.eco.prices[r] - origin.eco.prices[r] - distanceCost;
				if (margin > 0) {
					const remaining = CARAVAN_CAPACITY - cargo.reduce((s, c) => s + c.qty, 0);
					const qty = Math.min(stock * 0.3, remaining);
					if (qty >= 1) {
						cargo.push({key: r, qty: Math.floor(qty), buyPrice: origin.eco.prices[r]});
						totalProfit += margin * Math.floor(qty);
					}
				}
			}
		}

		// Sort cargo by margin (best first)
		cargo.sort((a, b) => {
			const mA = (dest.eco.prices[a.key] ?? 0) - a.buyPrice;
			const mB = (dest.eco.prices[b.key] ?? 0) - b.buyPrice;
			return mB - mA;
		});

		if (totalProfit > bestProfit && cargo.length > 0) {
			bestProfit = totalProfit;
			bestDest = dest;
			bestCargo = cargo;
		}
	}

	if (!bestDest || bestCargo.length === 0) {
		return undefined;
	}

	// Deduct cargo from origin
	for (const c of bestCargo) {
		if (RESOURCE_NAMES.includes(c.key as ResourceName)) {
			origin.eco.resources[c.key as ResourceName] -= c.qty;
		} else {
			origin.eco.goods[c.key] = (origin.eco.goods[c.key] ?? 0) - c.qty;
		}
	}

	const dx = origin.x - bestDest.x;
	const dy = origin.y - bestDest.y;
	const travelDays = Math.max(1, Math.ceil(Math.sqrt(dx * dx + dy * dy) / 50));

	return {
		originId: origin.id,
		destId: bestDest.id,
		cargo: bestCargo,
		arrivalDay: currentDay + travelDays,
	};
}

/** Deliver cargo at destination and return revenue to origin. */
export function settleTradeRoute(
	route: TradeRoute,
	dest: EconomyState,
	origin: EconomyState,
): number {
	let revenue = 0;
	for (const c of route.cargo) {
		const sellPrice = dest.prices[c.key] ?? c.buyPrice;
		revenue += sellPrice * c.qty;

		// Add stock to destination
		if (RESOURCE_NAMES.includes(c.key as ResourceName)) {
			dest.resources[c.key as ResourceName] += c.qty;
		} else {
			dest.goods[c.key] = (dest.goods[c.key] ?? 0) + c.qty;
		}
	}

	// Profit flows back to origin wealth
	const cost = route.cargo.reduce((s, c) => s + c.buyPrice * c.qty, 0);
	origin.wealth += Math.max(0, revenue - cost);
	return revenue;
}

// ── Player trade helpers ──

/**
 * Calculate buy price for player at a settlement.
 * Charisma and bargaining skill reduce the price.
 */
export function playerBuyPrice(
	basePrice: number,
	charisma: number,
	bargaining: number,
): number {
	const discount = 1 - (charisma * 0.01 + bargaining * 0.02);
	return Math.max(1, Math.round(basePrice * Math.max(0.5, discount)));
}

/**
 * Calculate sell price for player at a settlement.
 * Charisma and bargaining skill increase the price.
 */
export function playerSellPrice(
	basePrice: number,
	charisma: number,
	bargaining: number,
): number {
	const bonus = 1 + (charisma * 0.01 + bargaining * 0.02);
	return Math.max(1, Math.round(basePrice * 0.7 * Math.min(1.5, bonus)));
}

// ── Biome → resource mapping for village placement ──

/**
 * Determine which resources a village at (x,y) can gather based on terrain.
 * Uses height as primary signal (no GPU read needed).
 * heightNorm: 0-1 from TraversabilityData.heightData / 255.
 * terrainIndex: 0-8 biome index (optional; if unavailable, derived from height).
 */
export function resourcesForTerrain(
	heightNorm: number,
	terrainIndex?: number,
): ResourceName[] {
	const result: ResourceName[] = [];

	// Derive approximate terrain if index not available
	let ti: number;
	if (terrainIndex !== undefined) {
		ti = terrainIndex;
	} else if (heightNorm > 0.75) {
		ti = 4;
	} else if (heightNorm > 0.5) {
		ti = 3;
	} else if (heightNorm < 0.25) {
		ti = 1;
	} else {
		ti = 2;
	}

	for (const r of RESOURCE_NAMES) {
		const terrains = RESOURCE_TERRAIN[r];
		if (terrains.includes(ti)) {
			result.push(r);
		}
	}

	// Everyone can at least gather grain if on fertile land
	if (result.length === 0 && (ti === 2 || ti === 3)) {
		result.push('Grain');
	}

	// Mountains above 0.7 height get a bonus to rare resources
	if (heightNorm > 0.7 && !result.includes('Iron')) {
		result.push('Iron');
	}

	return result;
}
