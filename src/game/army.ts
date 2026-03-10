// === Army System ===
// Modular army composition for global-world entities.
// Mount & Blade-inspired unit types with rock-paper-scissors counters.
// Designed for expansion: add new unit types by extending UnitType + UNIT_STATS.

export const enum UnitType {
	Swordsman = 0,
	Archer = 1,
	Spearman = 2,
	Horseman = 3,
}

/** All defined unit types — iterate this to stay future-proof. */
export const ALL_UNIT_TYPES: UnitType[] = [
	UnitType.Swordsman,
	UnitType.Archer,
	UnitType.Spearman,
	UnitType.Horseman,
];

/**
 * Army composition keyed by UnitType.
 * To add a new unit type: add the enum value, add to ALL_UNIT_TYPES,
 * add to UNIT_STATS, and add to ADVANTAGE. Everything else adapts.
 */
export type ArmyComposition = Record<UnitType, number>;

export function defaultArmy(): ArmyComposition {
	return Object.fromEntries(ALL_UNIT_TYPES.map(t => [t, 0])) as unknown as ArmyComposition;
}

export function totalUnits(army: ArmyComposition): number {
	let sum = 0;
	for (const t of ALL_UNIT_TYPES) {
		sum += army[t] ?? 0;
	}

	return sum;
}

// ── Per-type base stats (tuned for subworld grid coordinates) ───

export type UnitStats = {
	hp: number;
	damage: number;
	speed: number; // Grid units / s
	attackRange: number; // Grid units
	cooldown: number; // Seconds between attacks
	label: string;
};

export const UNIT_STATS: Record<UnitType, UnitStats> = {
	[UnitType.Swordsman]: {
		hp: 100, damage: 15, speed: 40, attackRange: 3, cooldown: 1, label: 'Swd',
	},
	[UnitType.Archer]: {
		hp: 50, damage: 12, speed: 35, attackRange: 30, cooldown: 1.5, label: 'Arc',
	},
	[UnitType.Spearman]: {
		hp: 80, damage: 12, speed: 35, attackRange: 4, cooldown: 1.2, label: 'Spr',
	},
	[UnitType.Horseman]: {
		hp: 90, damage: 18, speed: 80, attackRange: 3, cooldown: 0.8, label: 'Hrs',
	},
};

// ── Rock-paper-scissors damage matrix ───────────────────────────
//
// Swordsman  → 1.5× vs Archer   (closes gap, shield blocks arrows)
// Archer     → 1.5× vs Spearman (kite at range)
// Spearman   → 1.8× vs Horseman (brace against charge)
// Horseman   → 1.4× vs Swordsman (charge and mobility)

const ADVANTAGE: Record<UnitType, Partial<Record<UnitType, number>>> = {
	[UnitType.Swordsman]: {[UnitType.Archer]: 1.5},
	[UnitType.Archer]: {[UnitType.Spearman]: 1.5},
	[UnitType.Spearman]: {[UnitType.Horseman]: 1.8},
	[UnitType.Horseman]: {[UnitType.Swordsman]: 1.4},
};

export function getDamageMultiplier(attacker: UnitType, defender: UnitType): number {
	return ADVANTAGE[attacker]?.[defender] ?? 1;
}

/** Count surviving soldiers of each type for a given team. */
export function countSurvivors(
	entities: ReadonlyArray<{unitType?: number; hp?: number; team?: number; kind: string}>,
	team: number,
): ArmyComposition {
	const army = defaultArmy();
	for (const entity of entities) {
		if (entity.kind !== 'soldier' || entity.team !== team) {
			continue;
		}

		if (entity.hp === undefined || entity.hp <= 0) {
			continue;
		}

		const ut = entity.unitType as UnitType | undefined;
		if (ut !== undefined && ALL_UNIT_TYPES.includes(ut)) {
			army[ut]++;
		}
	}

	return army;
}

/** Ensure an NPC has at least a minimal army derived from their level. */
export function ensureArmy(army: ArmyComposition | undefined, level: number): ArmyComposition {
	if (army && totalUnits(army) > 0) {
		return army;
	}

	const a = defaultArmy();
	a[UnitType.Swordsman] = Math.max(1, Math.floor(level * 0.5));
	a[UnitType.Archer] = Math.max(0, Math.floor(level * 0.3));
	a[UnitType.Spearman] = Math.max(0, Math.floor(level * 0.2));
	return a;
}

// ── Garrison generation (M&B style: population → militia) ──────

/** Per-unit gold cost for hiring soldiers. */
export const HIRE_COST: Record<UnitType, number> = {
	[UnitType.Swordsman]: 10,
	[UnitType.Archer]: 15,
	[UnitType.Spearman]: 12,
	[UnitType.Horseman]: 25,
};

/**
 * Generate garrison from settlement population.
 * Each soldier costs 1 pop — returned count is subtracted from population externally.
 * Yields a small batch (≈ sqrt(pop) * 0.3, capped) each day cycle.
 */
export function generateGarrison(
	population: number,
	rng: () => number,
): {garrison: ArmyComposition; popCost: number} {
	const budget = Math.min(10, Math.max(0, Math.floor(Math.sqrt(population) * 0.3)));
	if (budget <= 0 || population < 20) {
		return {garrison: defaultArmy(), popCost: 0};
	}

	const garrison = defaultArmy();
	let spent = 0;
	for (let i = 0; i < budget; i++) {
		const roll = rng();
		let ut: UnitType;
		if (roll < 0.45) {
			ut = UnitType.Swordsman;
		} else if (roll < 0.70) {
			ut = UnitType.Archer;
		} else if (roll < 0.90) {
			ut = UnitType.Spearman;
		} else {
			ut = UnitType.Horseman;
		}

		garrison[ut]++;
		spent++;
	}

	return {garrison, popCost: spent};
}

/** Merge source army into target (additive). */
export function addArmy(target: ArmyComposition, source: ArmyComposition): void {
	for (const t of ALL_UNIT_TYPES) {
		target[t] += source[t] ?? 0;
	}
}

/**
 * Hire one unit from a settlement garrison into the player's army.
 * Returns gold cost or 0 if unavailable/unaffordable.
 * Population is NOT changed here — it was already paid when the garrison was generated.
 */
export function hireUnit(
	playerArmy: ArmyComposition,
	garrison: ArmyComposition,
	unitType: UnitType,
	playerGold: number,
): number {
	const cost = HIRE_COST[unitType];
	if ((garrison[unitType] ?? 0) <= 0 || playerGold < cost) {
		return 0;
	}

	garrison[unitType]--;
	playerArmy[unitType]++;
	return cost;
}

/**
 * Fire one unit from the player's army into the deserter pool.
 * Returns true if a unit was actually fired.
 */
export function fireUnit(
	playerArmy: ArmyComposition,
	deserterPool: ArmyComposition,
	unitType: UnitType,
): boolean {
	if ((playerArmy[unitType] ?? 0) <= 0) {
		return false;
	}

	playerArmy[unitType]--;
	deserterPool[unitType]++;
	return true;
}

/**
 * Drain the deserter pool and return total count for spawning.
 * Resets pool to zero.
 */
export function drainDeserterPool(pool: ArmyComposition): number {
	let total = 0;
	for (const t of ALL_UNIT_TYPES) {
		total += pool[t] ?? 0;
		pool[t] = 0;
	}

	return total;
}
