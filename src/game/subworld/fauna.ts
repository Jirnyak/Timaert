/**
 * Biome fauna distribution — data-driven animal/monster spawn tables.
 *
 * Each biome + feature + landmark combination defines a FaunaTable:
 * a list of creature entries with spawn weight, faction, AI, combat
 * stats, and visual properties. The universal populator in spawn.ts
 * reads these tables to fill subworld cells with local wildlife.
 *
 * To add a new creature to a biome:
 *   1. Add an entry to the relevant BIOME_FAUNA table below.
 *   2. Done. No engine changes needed.
 *
 * To add fauna to a new biome/feature:
 *   1. Add a new table constant.
 *   2. Register it in getFaunaTable().
 *   3. Done.
 */

import {Biome} from '../biomes';
import type {CombatTemplate} from '../army';
import type {AiKind} from './types';
import {type CellFeature} from './map-data';

// ── Fauna entry ─────────────────────────────────────────────────

export type FaunaEntry = {
	/** Display name. */
	label: string;
	/** Spawn weight (relative probability within the table). */
	weight: number;
	/** Faction id — determines hostility via universal faction table. */
	factionId: string;
	/** AI behavior. */
	ai: AiKind;
	/** Combat stats template. */
	combat: CombatTemplate;
	/** Base level for stat scaling. */
	baseLevel: number;
	/** Display color (CSS). */
	color: string;
	/** Entity radius. */
	radius: number;
};

export type FaunaTable = {
	/** Creatures that can spawn in this context. */
	entries: FaunaEntry[];
	/** Min creatures per cell. */
	minCount: number;
	/** Max creatures per cell. */
	maxCount: number;
};

// ── Combat templates for wildlife ───────────────────────────────

const RABBIT: FaunaEntry = {
	label: 'Rabbit', weight: 15, factionId: 'wildlife', ai: 'flee',
	combat: {
		hp: 5, damage: 0, speed: 55, attackRange: 0, cooldown: 9, label: 'Rbt',
	},
	baseLevel: 1, color: '#b8a080', radius: 0.4,
};

const DEER: FaunaEntry = {
	label: 'Deer', weight: 12, factionId: 'wildlife', ai: 'flee',
	combat: {
		hp: 15, damage: 2, speed: 50, attackRange: 2, cooldown: 2, label: 'Der',
	},
	baseLevel: 1, color: '#a08060', radius: 0.6,
};

const FOX: FaunaEntry = {
	label: 'Fox', weight: 8, factionId: 'wildlife', ai: 'wander',
	combat: {
		hp: 12, damage: 4, speed: 45, attackRange: 2, cooldown: 1.2, label: 'Fox',
	},
	baseLevel: 1, color: '#cc6633', radius: 0.5,
};

const WOLF: FaunaEntry = {
	label: 'Wolf', weight: 6, factionId: 'wildlife', ai: 'combat',
	combat: {
		hp: 30, damage: 10, speed: 50, attackRange: 3, cooldown: 1, label: 'Wlf',
	},
	baseLevel: 2, color: '#666666', radius: 0.7,
};

const BEAR: FaunaEntry = {
	label: 'Bear', weight: 3, factionId: 'wildlife', ai: 'combat',
	combat: {
		hp: 80, damage: 18, speed: 35, attackRange: 3, cooldown: 1.5, label: 'Ber',
	},
	baseLevel: 3, color: '#5a3a1a', radius: 1,
};

const BOAR: FaunaEntry = {
	label: 'Boar', weight: 5, factionId: 'wildlife', ai: 'combat',
	combat: {
		hp: 40, damage: 12, speed: 40, attackRange: 3, cooldown: 1.2, label: 'Bor',
	},
	baseLevel: 2, color: '#6b4e37', radius: 0.7,
};

const SNAKE: FaunaEntry = {
	label: 'Snake', weight: 4, factionId: 'wildlife', ai: 'combat',
	combat: {
		hp: 10, damage: 8, speed: 30, attackRange: 2, cooldown: 0.8, label: 'Snk',
	},
	baseLevel: 1, color: '#3a5a2a', radius: 0.3,
};

const HAWK: FaunaEntry = {
	label: 'Hawk', weight: 3, factionId: 'wildlife', ai: 'wander',
	combat: {
		hp: 8, damage: 5, speed: 60, attackRange: 3, cooldown: 1, label: 'Hwk',
	},
	baseLevel: 1, color: '#8b6b4b', radius: 0.4,
};

// ── Monsters ────────────────────────────────────────────────────

const GOBLIN: FaunaEntry = {
	label: 'Goblin', weight: 4, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 25, damage: 8, speed: 40, attackRange: 3, cooldown: 1, label: 'Gbl',
	},
	baseLevel: 2, color: '#4a8a2a', radius: 0.6,
};

const SKELETON: FaunaEntry = {
	label: 'Skeleton', weight: 3, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 35, damage: 10, speed: 30, attackRange: 3, cooldown: 1.2, label: 'Skl',
	},
	baseLevel: 3, color: '#d0c8b0', radius: 0.6,
};

const TROLL: FaunaEntry = {
	label: 'Troll', weight: 1, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 120, damage: 25, speed: 25, attackRange: 4, cooldown: 2, label: 'Trl',
	},
	baseLevel: 5, color: '#3a6a3a', radius: 1.2,
};

const SWAMP_THING: FaunaEntry = {
	label: 'Swamp Thing', weight: 3, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 60, damage: 14, speed: 20, attackRange: 4, cooldown: 1.5, label: 'Swt',
	},
	baseLevel: 3, color: '#2a4a1a', radius: 0.9,
};

const ICE_WRAITH: FaunaEntry = {
	label: 'Ice Wraith', weight: 2, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 45, damage: 16, speed: 35, attackRange: 5, cooldown: 1.3, label: 'Iwr',
	},
	baseLevel: 4, color: '#a0d0e0', radius: 0.7,
};

const SAND_SCORPION: FaunaEntry = {
	label: 'Sand Scorpion', weight: 5, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 35, damage: 12, speed: 35, attackRange: 3, cooldown: 1, label: 'Ssc',
	},
	baseLevel: 2, color: '#c0a050', radius: 0.6,
};

const MOUNTAIN_GOAT: FaunaEntry = {
	label: 'Mountain Goat', weight: 8, factionId: 'wildlife', ai: 'flee',
	combat: {
		hp: 20, damage: 5, speed: 40, attackRange: 2, cooldown: 1.5, label: 'Mgt',
	},
	baseLevel: 1, color: '#b0a090', radius: 0.6,
};

const EAGLE: FaunaEntry = {
	label: 'Eagle', weight: 4, factionId: 'wildlife', ai: 'wander',
	combat: {
		hp: 12, damage: 7, speed: 65, attackRange: 3, cooldown: 1, label: 'Egl',
	},
	baseLevel: 2, color: '#5a4030', radius: 0.5,
};

const STONE_GOLEM: FaunaEntry = {
	label: 'Stone Golem', weight: 1, factionId: 'monsters', ai: 'combat',
	combat: {
		hp: 150, damage: 20, speed: 15, attackRange: 4, cooldown: 2.5, label: 'Glm',
	},
	baseLevel: 5, color: '#7a7a7a', radius: 1.3,
};

const CROCODILE: FaunaEntry = {
	label: 'Crocodile', weight: 4, factionId: 'wildlife', ai: 'combat',
	combat: {
		hp: 50, damage: 15, speed: 25, attackRange: 3, cooldown: 1.5, label: 'Crc',
	},
	baseLevel: 3, color: '#4a6a3a', radius: 0.8,
};

// ── Per-biome fauna tables ──────────────────────────────────────

const MEADOW_FAUNA: FaunaTable = {
	entries: [RABBIT, DEER, FOX, WOLF, HAWK, BOAR],
	minCount: 2, maxCount: 6,
};

const FOREST_FAUNA: FaunaTable = {
	entries: [RABBIT, DEER, FOX, WOLF, BEAR, BOAR, GOBLIN],
	minCount: 3, maxCount: 8,
};

const TAIGA_FAUNA: FaunaTable = {
	entries: [RABBIT, DEER, WOLF, BEAR, FOX],
	minCount: 2, maxCount: 5,
};

const TUNDRA_FAUNA: FaunaTable = {
	entries: [RABBIT, WOLF, FOX, ICE_WRAITH],
	minCount: 1, maxCount: 4,
};

const SNOW_FAUNA: FaunaTable = {
	entries: [RABBIT, WOLF, ICE_WRAITH],
	minCount: 1, maxCount: 3,
};

const DESERT_FAUNA: FaunaTable = {
	entries: [SNAKE, HAWK, SAND_SCORPION],
	minCount: 1, maxCount: 4,
};

const STEPPE_FAUNA: FaunaTable = {
	entries: [RABBIT, DEER, FOX, HAWK, SNAKE, BOAR],
	minCount: 2, maxCount: 5,
};

const SWAMP_FAUNA: FaunaTable = {
	entries: [SNAKE, createFrog(), CROCODILE, SWAMP_THING],
	minCount: 2, maxCount: 6,
};

const TROPICS_FAUNA: FaunaTable = {
	entries: [SNAKE, BOAR, CROCODILE, DEER],
	minCount: 2, maxCount: 6,
};

const VALLEY_FAUNA: FaunaTable = {
	entries: [RABBIT, DEER, FOX, WOLF, HAWK, BOAR],
	minCount: 2, maxCount: 6,
};

const MOUNTAIN_FAUNA: FaunaTable = {
	entries: [MOUNTAIN_GOAT, EAGLE, WOLF, STONE_GOLEM],
	minCount: 1, maxCount: 4,
};

const WATER_FAUNA: FaunaTable = {
	entries: [],
	minCount: 0, maxCount: 0,
};

const RUIN_FAUNA: FaunaTable = {
	entries: [SKELETON, GOBLIN, TROLL, SNAKE],
	minCount: 2, maxCount: 6,
};

// Zero fauna for cities/villages — NPCs come from population
const EMPTY_FAUNA: FaunaTable = {
	entries: [],
	minCount: 0, maxCount: 0,
};

// ── Helpers ─────────────────────────────────────────────────────

function createFrog(): FaunaEntry {
	return {
		label: 'Frog', weight: 10, factionId: 'wildlife', ai: 'flee',
		combat: {
			hp: 3, damage: 0, speed: 30, attackRange: 0, cooldown: 9, label: 'Frg',
		},
		baseLevel: 1, color: '#2a8a2a', radius: 0.3,
	};
}

// ── Biome → feature → landmark fauna resolution ─────────────────

const BIOME_FAUNA: Record<number, FaunaTable> = {
	[Biome.Tundra]: TUNDRA_FAUNA,
	[Biome.Taiga]: TAIGA_FAUNA,
	[Biome.Snow]: SNOW_FAUNA,
	[Biome.Valley]: VALLEY_FAUNA,
	[Biome.Meadow]: MEADOW_FAUNA,
	[Biome.Swamp]: SWAMP_FAUNA,
	[Biome.Desert]: DESERT_FAUNA,
	[Biome.Steppe]: STEPPE_FAUNA,
	[Biome.Tropics]: TROPICS_FAUNA,
	[Biome.Water]: WATER_FAUNA,
};

// Feature overrides (when a cell has a specific macroworld feature)
const FEATURE_FAUNA: Partial<Record<number, FaunaTable>> = {
	// CellFeature.Tree = 2 → forest fauna
	2: FOREST_FAUNA,
	// CellFeature.Mountain = 3 → mountain fauna
	3: MOUNTAIN_FAUNA,
};

/**
 * Resolve the fauna table for a cell based on its biome, feature, and landmark.
 * Priority: landmark > feature > biome.
 */
export function getFaunaTable(
	biome: Biome,
	feature: CellFeature,
	landmark: string | undefined,
): FaunaTable {
	// Cities and villages have no wild fauna — NPCs from population
	if (landmark === 'city' || landmark === 'village') {
		return EMPTY_FAUNA;
	}

	// Ruins have their own monster table
	if (landmark === 'ruin') {
		return RUIN_FAUNA;
	}

	// Feature override (forest, mountain)
	const featureTable = FEATURE_FAUNA[feature as number];
	if (featureTable) {
		return featureTable;
	}

	// Biome default
	return BIOME_FAUNA[biome as number] ?? MEADOW_FAUNA;
}

/**
 * Roll fauna count and pick creatures from a table using weighted random.
 * Returns an array of picked FaunaEntry references.
 */
export function rollFauna(
	table: FaunaTable,
	rng: () => number,
): FaunaEntry[] {
	if (table.entries.length === 0 || table.maxCount === 0) {
		return [];
	}

	const count = table.minCount + Math.floor(rng() * (table.maxCount - table.minCount + 1));
	const totalWeight = table.entries.reduce((s, entry) => s + entry.weight, 0);
	const result: FaunaEntry[] = [];

	for (let i = 0; i < count; i++) {
		let roll = rng() * totalWeight;
		for (const entry of table.entries) {
			roll -= entry.weight;
			if (roll <= 0) {
				result.push(entry);
				break;
			}
		}
	}

	return result;
}
