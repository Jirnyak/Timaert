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

export type ArmyComposition = {
	swordsmen: number;
	archers: number;
	spearmen: number;
	horsemen: number;
};

export function defaultArmy(): ArmyComposition {
	return {
		swordsmen: 0, archers: 0, spearmen: 0, horsemen: 0,
	};
}

export function totalUnits(army: ArmyComposition): number {
	return army.swordsmen + army.archers + army.spearmen + army.horsemen;
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

		switch (entity.unitType) {
			case UnitType.Swordsman: {
				army.swordsmen++;
				break;
			}

			case UnitType.Archer: {
				army.archers++;
				break;
			}

			case UnitType.Spearman: {
				army.spearmen++;
				break;
			}

			case UnitType.Horseman: {
				army.horsemen++;
				break;
			}

			case undefined: {
				break;
			}

			default: {
				break;
			}
		}
	}

	return army;
}

/** Ensure an NPC has at least a minimal army derived from their level. */
export function ensureArmy(army: ArmyComposition | undefined, level: number): ArmyComposition {
	if (army && totalUnits(army) > 0) {
		return army;
	}

	return {
		swordsmen: Math.max(1, Math.floor(level * 0.5)),
		archers: Math.max(0, Math.floor(level * 0.3)),
		spearmen: Math.max(0, Math.floor(level * 0.2)),
		horsemen: 0,
	};
}
